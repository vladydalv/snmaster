// Офлайн-тести Spacenerd Feedback.
#include "../Feedback/PluginProcessor.h"
#include "../Feedback/PluginEditor.h"
#include <iostream>
#include <random>
#include <complex>

using namespace juce;
using namespace FbIDs;

static int failures = 0;
static void check (bool ok, const String& what)
{
    std::cout << (ok ? "[ OK ] " : "[FAIL] ") << what << std::endl;
    if (! ok) ++failures;
}

static void setParam (SpacenerdFeedbackProcessor& p, const char* id, float v)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (v));
}

constexpr double fs = 48000.0;

/** Синтетична струна: гармоніки з різним згасанням. */
static float stringSample (double f, double t, float amp = 0.3f)
{
    if (t < 0.0) return 0.0f;
    double v = 0.0;
    for (int h = 1; h <= 8; ++h)
        v += std::sin (2.0 * MathConstants<double>::pi * f * h * t) / h * std::exp (-t / (2.5 / std::sqrt ((double) h)));
    return amp * (float) (v * std::min (1.0, t / 0.002));
}

using Sig = std::function<float (double t)>;

/** Повертає пару: вхід і вихід. */
static std::pair<AudioBuffer<float>, AudioBuffer<float>> run (SpacenerdFeedbackProcessor& p, double seconds, Sig sig,
                                                                std::function<void (double)> automation = nullptr)
{
    p.setRateAndBufferSizeDetails (fs, 256);
    p.prepareToPlay (fs, 256);
    const int total = (int) (fs * seconds);
    AudioBuffer<float> in (1, total), out (1, total);
    MidiBuffer midi;
    for (int pos = 0; pos < total; pos += 256)
    {
        const int n = std::min (256, total - pos);
        if (automation) automation ((double) pos / fs);
        AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i)
        {
            const float v = sig ((double) (pos + i) / fs);
            b.setSample (0, i, v); b.setSample (1, i, v);
            in.setSample (0, pos + i, v);
        }
        p.processBlock (b, midi);
        out.copyFrom (0, pos, b, 0, 0, n);
    }
    return { std::move (in), std::move (out) };
}

static float rmsAt (const AudioBuffer<float>& b, double t0, double t1)
{
    const int a = (int) (t0 * fs), n = (int) ((t1 - t0) * fs);
    return b.getRMSLevel (0, a, n);
}

/** Частота найсильнішої складової поблизу f (Гьорцель, пошук ±3 %). */
static double peakFreq (const AudioBuffer<float>& b, double t0, double t1, double f)
{
    const int a = (int) (t0 * fs), n = (int) ((t1 - t0) * fs);
    double bestF = f, bestM = -1.0;
    for (double ff = f * 0.97; ff <= f * 1.03; ff += f * 0.0005)
    {
        std::complex<double> acc = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * MathConstants<double>::pi * i / (n - 1));
            acc += w * b.getSample (0, a + i) * std::exp (std::complex<double> (0.0, -2.0 * MathConstants<double>::pi * ff * i / fs));
        }
        if (std::abs (acc) > bestM) { bestM = std::abs (acc); bestF = ff; }
    }
    return bestF;
}

static AudioBuffer<float> diff (const AudioBuffer<float>& out, const AudioBuffer<float>& in)
{
    AudioBuffer<float> d (out);
    d.addFrom (0, 0, in, 0, 0, in.getNumSamples(), -1.0f);
    return d;
}

static float cents (double a, double b) { return (float) (1200.0 * std::log2 (a / b)); }

int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI init;

    // 1. Точність визначення висоти (струни від низького E до високих ладів)
    {
        float worst = 0.0f;
        for (double f : { 82.41, 110.0, 196.0, 329.63, 659.26, 1046.5 })
        {
            sn::PitchDetector pd; pd.prepare (fs);
            float last = 0.0f;
            for (int i = 0; i < (int) (0.5 * fs); ++i)
                if (pd.push (stringSample (f, i / fs)) && pd.clarity > 0.8f) last = pd.hz;
            worst = std::max (worst, std::abs (cents (last, f)));
        }
        check (worst < 3.0f, "Pitch detection worst error: " + String (worst, 2) + " cents");
    }

    auto base = [] (SpacenerdFeedbackProcessor& p, int harm, float dist, float del)
    {
        setParam (p, trigger, 0); setParam (p, harmonic, (float) harm);
        setParam (p, distance, dist); setParam (p, delay, del);
        setParam (p, morph, 0.0f); setParam (p, drift, 0.0f); setParam (p, amount, 70.0f);
    };

    // 2. Строй фідбеку: Tone / Octave / Fifth точно на гармоніці струни
    {
        const char* names[] { "Tone", "Octave", "Fifth" };
        for (int h = 0; h < 3; ++h)
        {
            SpacenerdFeedbackProcessor p; base (p, h, 0.0f, 0.5f);
            auto [in, out] = run (p, 4.0, [] (double t) { return stringSample (110.0, t); });
            auto fb = diff (out, in);
            const double target = 110.0 * (h + 1);
            const double f = peakFreq (fb, 2.5, 3.5, target);
            const float lvl = sn::gainToDb (rmsAt (fb, 2.5, 3.5) / rmsAt (in, 0.0, 0.5));
            check (std::abs (cents (f, target)) < 5.0f && lvl > -12.0f,
                   String (names[h]) + ": feedback at " + String (f, 2) + " Hz (target " + String (target, 1) + ", "
                   + String (cents (f, target), 1) + " cents), level " + String (lvl, 1) + " dB");
        }
    }

    // 3. Відстань: близько — миттєво, далеко — повільно й плавно
    {
        auto fbCurve = [&] (float dist)
        {
            SpacenerdFeedbackProcessor p; base (p, 0, dist, 0.4f);
            auto [in, out] = run (p, 10.0, [] (double t) { return stringSample (110.0, t); });
            auto fb = diff (out, in);
            std::vector<float> c;
            for (double t = 0.0; t < 10.0; t += 0.5) c.push_back (sn::gainToDb (rmsAt (fb, t, t + 0.5) + 1.0e-9f));
            return c;
        };
        auto close = fbCurve (0.0f), far = fbCurve (100.0f);
        // Близько: вже за 1 с; далеко: через 1 с ще тихо, через 8 с — повний
        check (close[2] > -25.0f && far[2] < close[2] - 15.0f && far[17] > close[17] - 5.0f,
               "Distance: close @1s " + String (close[2], 1) + " dB, far @1s " + String (far[2], 1)
               + " dB, far @8.5s " + String (far[17], 1) + " dB");
        // Плавність: на далекій відстані наростання без стрибків > 6 дБ за 0.5 с після старту
        float maxJump = 0.0f;
        for (size_t i = 3; i < far.size(); ++i) if (far[i] > -60.0f) maxJump = std::max (maxJump, far[i] - far[i - 1]);
        check (maxJump < 12.0f, "Far swell smoothness: max rise " + String (maxJump, 1) + " dB per 0.5 s");
    }

    // 4. Глушіння струни зупиняє фідбек
    {
        SpacenerdFeedbackProcessor p; base (p, 0, 0.0f, 0.5f);
        auto [in, out] = run (p, 4.0, [] (double t) { return t < 2.5 ? stringSample (110.0, t) : 0.0f; });
        const float before = rmsAt (out, 2.0, 2.4), after = rmsAt (out, 2.8, 3.2);
        check (sn::gainToDb (after / before) < -40.0f, "Mute stops feedback: " + String (sn::gainToDb (after / before), 1) + " dB");
    }

    // 5. Режим Hold: без кнопки — тиша, з кнопкою — фідбек
    {
        SpacenerdFeedbackProcessor p1; base (p1, 0, 0.0f, 0.5f); setParam (p1, trigger, 1);
        auto [in1, out1] = run (p1, 3.0, [] (double t) { return stringSample (110.0, t); });
        const float without = sn::gainToDb (rmsAt (diff (out1, in1), 2.0, 3.0) + 1.0e-9f);

        SpacenerdFeedbackProcessor p2; base (p2, 0, 0.0f, 0.5f); setParam (p2, trigger, 1);
        auto [in2, out2] = run (p2, 3.0, [] (double t) { return stringSample (110.0, t); },
                                [&] (double t) { setParam (p2, hold, t > 1.0 ? 1.0f : 0.0f); });
        const float with = sn::gainToDb (rmsAt (diff (out2, in2), 2.0, 3.0));
        check (without < -80.0f && with > -30.0f, "Hold mode: without " + String (without, 1) + " dB, with " + String (with, 1) + " dB");
    }

    // 6. Нова нота: фідбек переходить на неї
    {
        SpacenerdFeedbackProcessor p; base (p, 0, 0.0f, 0.4f);
        auto [in, out] = run (p, 6.0, [] (double t) { return t < 3.0 ? stringSample (110.0, t) : stringSample (146.83, t - 3.0); });
        const double f = peakFreq (diff (out, in), 5.0, 6.0, 146.83);
        check (std::abs (cents (f, 146.83)) < 5.0f, "New note retune: " + String (f, 2) + " Hz (D3 146.83)");
    }

    // 6b. Фідбек у паузі (без ноти): Hold, D Standard, 6-та струна, основний тон → D2 73.42 Гц
    {
        SpacenerdFeedbackProcessor p; base (p, 0, 20.0f, 0.5f);
        setParam (p, trigger, 1); setParam (p, tuning, 2); setParam (p, openStr, 1);
        auto [in, out] = run (p, 5.0, [] (double) { return 0.0f; },
                              [&] (double t) { setParam (p, hold, t > 0.5 && t < 3.5 ? 1.0f : 0.0f); });
        const double f = peakFreq (out, 2.0, 3.0, 73.42);
        const float lvl = sn::gainToDb (rmsAt (out, 2.0, 3.0)), after = sn::gainToDb (rmsAt (out, 4.7, 5.0) + 1e-9f);
        check (std::abs (cents (f, 73.42)) < 5.0f && lvl > -30.0f && after < lvl - 15.0f,
               "Open-string feedback (no note): " + String (f, 2) + " Hz, " + String (lvl, 1) + " dBFS, after HOLD off " + String (after, 1) + " dB");
    }
    // 6c. Під час фідбеку у паузі зіграли ноту → фідбек переходить на неї
    {
        SpacenerdFeedbackProcessor p; base (p, 0, 0.0f, 0.5f);
        setParam (p, trigger, 2); setParam (p, openStr, 1);
        auto [in, out] = run (p, 5.0, [] (double t) { return t < 2.0 ? 0.0f : stringSample (110.0, t - 2.0); },
                              [&] (double t) { setParam (p, hold, t > 0.5 ? 1.0f : 0.0f); });
        const double f = peakFreq (diff (out, in), 4.0, 5.0, 110.0);
        check (std::abs (cents (f, 110.0)) < 5.0f, "Open-string → played note A2: " + String (f, 2) + " Hz");
    }

    // 7. Стабільність: шум, акорд, клацання, тиша
    {
        SpacenerdFeedbackProcessor p; p.setCurrentProgram (5);
        std::mt19937 rng (9);
        std::normal_distribution<float> nd (0.0f, 0.2f);
        auto [in, out] = run (p, 12.0, [&] (double t)
        {
            if (t < 3.0) return nd (rng);
            if (t < 7.0) return stringSample (82.41, t - 3.0) + stringSample (123.47, t - 3.0) + stringSample (164.81, t - 3.0);
            if (t < 7.5) return (float) ((int) (t * 200) % 2) * 0.9f;
            return 0.0f;
        });
        bool finite = true;
        for (int i = 0; i < out.getNumSamples(); ++i) finite &= std::isfinite (out.getSample (0, i));
        const float pk = out.getMagnitude (0, 0, out.getNumSamples());
        check (finite && pk < 4.0f, "Stability (noise, chord, clicks): peak " + String (sn::gainToDb (pk), 1) + " dBFS");
    }

    // 8. Пресети
    {
        SpacenerdFeedbackProcessor probe;
        for (int i = 0; i < probe.getNumPrograms(); ++i)
        {
            SpacenerdFeedbackProcessor p; p.setCurrentProgram (i);
            auto [in, out] = run (p, 6.0, [] (double t) { return stringSample (98.0, t); },
                                  [&] (double t) { setParam (p, hold, t > 1.0 ? 1.0f : 0.0f); });
            const float fbl = sn::gainToDb (rmsAt (out, 5.0, 6.0) / rmsAt (in, 5.0, 6.0));
            bool finite = true;
            for (int k = 0; k < out.getNumSamples(); ++k) finite &= std::isfinite (out.getSample (0, k));
            check (finite, "Preset '" + p.getProgramName (i) + "': out/in at 5-6 s " + String (fbl, 1) + " dB");
        }
    }

    if (argc > 1)
    {
        SpacenerdFeedbackProcessor p; p.setCurrentProgram (1);
        run (p, 3.0, [] (double t) { return stringSample (110.0, t); });
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        for (int f = 0; f < 5; ++f)
        {
            p.outPeak.push (0.4f);
            for (auto* child : ed->getChildren())
                if (auto* fc = dynamic_cast<FeedbackContent*> (child)) fc->tick();
        }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
        File file (argv[1]); file.deleteFile();
        FileOutputStream os (file);
        PNGImageFormat().writeImageToStream (img, os);
        std::cout << "Snapshot: " << file.getFullPathName() << std::endl;
    }

    std::cout << (failures == 0 ? "ALL PASSED" : String (failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
