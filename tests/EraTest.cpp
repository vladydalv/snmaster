// Офлайн-тести Spacenerd Era.
#include "../Era/PluginProcessor.h"
#include "../Era/PluginEditor.h"
#include "../Common/Analysis.h"
#include <iostream>
#include <random>

using namespace juce;
using namespace EraIDs;

constexpr double fs = 48000.0;
static int failures = 0;
static void check (bool ok, const String& what) { std::cout << (ok ? "[ OK ] " : "[FAIL] ") << what << std::endl; if (! ok) ++failures; }

static AudioBuffer<float> makeMix (float gain, double seconds = 12.0)
{
    const int n = (int) (fs * seconds);
    AudioBuffer<float> b (2, n);
    std::mt19937 rng (1);
    std::normal_distribution<float> nd (0.0f, 1.0f);
    sn::Biquad hatL, hatR; hatL.setHighPass (fs, 7000.0); hatR.setHighPass (fs, 7000.0);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs, beat = std::fmod (t, 0.5), sb = std::fmod (t + 0.25, 0.5);
        const float kick = 0.5f * (float) (std::sin (2.0 * MathConstants<double>::pi * 55.0 * beat) * std::exp (-beat * 12.0));
        const float snare = 0.25f * nd (rng) * (float) std::exp (-sb * 25.0);
        const float bass = 0.18f * (float) std::sin (2.0 * MathConstants<double>::pi * 82.4 * t);
        float gtr = 0.0f;
        for (int h = 1; h <= 10; ++h)
            gtr += 0.05f / (float) h * (float) (std::sin (2.0 * MathConstants<double>::pi * 164.8 * h * t) + std::sin (2.0 * MathConstants<double>::pi * 246.9 * h * t));
        b.setSample (0, i, gain * (kick + snare + bass + gtr * 1.2f + 0.05f * hatL.process (nd (rng))));
        b.setSample (1, i, gain * (kick + snare + bass + gtr * 0.8f + 0.05f * hatR.process (nd (rng))));
    }
    return b;
}

static void set (SpacenerdEraProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

static AudioBuffer<float> process (const AudioBuffer<float>& in, std::function<void (SpacenerdEraProcessor&)> setup, SpacenerdEraProcessor* keep = nullptr)
{
    SpacenerdEraProcessor local;
    auto& p = keep ? *keep : local;
    setup (p);
    p.setRateAndBufferSizeDetails (fs, 512);
    p.prepareToPlay (fs, 512);
    AudioBuffer<float> out (in);
    MidiBuffer m;
    for (int pos = 0; pos < out.getNumSamples(); pos += 512)
    {
        AudioBuffer<float> chunk (out.getArrayOfWritePointers(), 2, pos, std::min (512, out.getNumSamples() - pos));
        p.processBlock (chunk, m);
    }
    return out;
}

static float diffDb (const AudioBuffer<float>& a, const AudioBuffer<float>& b, int shift = 0)
{
    const int from = (int) (fs * 2.0), n = a.getNumSamples() - from - shift;
    double e = 0.0, r = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = from; i < from + n; ++i)
        {
            const double d = a.getSample (ch, i + shift) - b.getSample (ch, i);
            e += d * d; r += (double) b.getSample (ch, i) * b.getSample (ch, i);
        }
    return (float) (10.0 * std::log10 (std::max (e, 1e-30) / std::max (r, 1e-30)));
}

static float lufsOf (const AudioBuffer<float>& b, double from)
{
    sn::LoudnessMeter m; m.prepare (fs, 2);
    AudioBuffer<float> tail (2, b.getNumSamples() - (int) (from * fs));
    for (int ch = 0; ch < 2; ++ch) tail.copyFrom (ch, 0, b, ch, (int) (from * fs), tail.getNumSamples());
    m.process (tail);
    return m.integrated.load();
}

static float truePeakDb (const AudioBuffer<float>& b)
{
    dsp::Oversampling<float> os (2, 2, dsp::Oversampling<float>::filterHalfBandFIREquiripple, true);
    os.initProcessing ((size_t) b.getNumSamples());
    AudioBuffer<float> c (b);
    dsp::AudioBlock<float> blk (c);
    auto up = os.processSamplesUp (blk);
    float pk = 0.0f;
    for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
        for (size_t i = 0; i < up.getNumSamples(); ++i) pk = std::max (pk, std::abs (up.getSample ((int) ch, (int) i)));
    return sn::gainToDb (pk);
}

int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI init;
    const auto music = makeMix (0.35f);    // ≈ сирий мікс, пікові ≈ -9 dBFS
    std::cout << "Input: " << String (lufsOf (music, 2.0), 1) << " LUFS" << std::endl;

    const bool quick = SystemStats::getEnvironmentVariable ("ERA_QUICK", {}).isNotEmpty();
    if (! quick) {
    // 1. Інтенсивність 0 — прозоро: та сама гучність і баланс смуг
    {
        auto out = process (music, [] (SpacenerdEraProcessor& q) { set (q, intensity, 0.0f); });
        auto bandDb = [] (const AudioBuffer<float>& b, double f)
        {
            sn::Biquad bp; bp.setBandPass (fs, f, 1.0);
            double e = 0.0;
            for (int i = 0; i < b.getNumSamples(); ++i) { const float y = bp.process (b.getSample (0, i)); if (i > 96000) e += (double) y * y; }
            return 10.0 * std::log10 (e + 1e-30);
        };
        float worst = std::abs (lufsOf (out, 2.0) - lufsOf (music, 2.0));
        for (double f : { 60.0, 250.0, 1000.0, 4000.0, 12000.0 })
            worst = std::max (worst, (float) std::abs (bandDb (out, f) - bandDb (music, f)));
        check (worst < 0.3f, "Intensity 0 is transparent: worst loudness/band deviation " + String (worst, 2) + " dB");
    }

    // 2. Кожна епоха: гучність близька до цільової, true peak ≤ -1 dBTP, без збоїв
    for (float yr : { 1960.0f, 1970.0f, 1980.0f, 1990.0f, 2000.0f, 2010.0f, 2025.0f })
    {
        auto out = process (music, [&] (SpacenerdEraProcessor& q) { set (q, year, yr); set (q, intensity, 100.0f); });
        const float L = lufsOf (out, 8.0), tp = truePeakDb (out);
        const float target = era::forYear (yr, 0).targetLufs;
        bool finite = true;
        for (int i = 0; i < out.getNumSamples(); ++i) finite &= std::isfinite (out.getSample (0, i));
        check (finite && tp <= -0.9f && std::abs (L - target) < 2.5f,
               String (roundToInt (yr)) + ": " + String (L, 1) + " LUFS (era target " + String (target, 1) + "), TP " + String (tp, 2) + " dBTP");
    }

    // 3. Епохи звучать по-різному
    {
        auto a = process (music, [] (SpacenerdEraProcessor& q) { set (q, year, 1965.0f); set (q, intensity, 80.0f); });
        auto b = process (music, [] (SpacenerdEraProcessor& q) { set (q, year, 1995.0f); set (q, intensity, 80.0f); });
        // Порівнюємо без різниці в гучності: нормуємо RMS
        const float ga = a.getRMSLevel (0, 96000, a.getNumSamples() - 96000), gb = b.getRMSLevel (0, 96000, b.getNumSamples() - 96000);
        b.applyGain (ga / gb);
        check (diffDb (b, a) > -15.0f, "1965 vs 1995 differ (loudness-matched): " + String (diffDb (b, a), 1) + " dB");
    }

    // 4. Жанри змінюють звук
    for (int gi = 1; gi <= 4; ++gi)
    {
        auto a = process (music, [] (SpacenerdEraProcessor& q) { set (q, year, 1972.0f); set (q, intensity, 80.0f); });
        auto b = process (music, [&] (SpacenerdEraProcessor& q) { set (q, year, 1972.0f); set (q, intensity, 80.0f); set (q, genre, (float) gi); });
        check (diffDb (b, a) > -30.0f, "Genre " + String (gi) + " changes sound: " + String (diffDb (b, a), 1) + " dB");
    }

    // 5. Затримка = реальна (сухий Mix 0 % вирівняний з мокрим)
    {
        SpacenerdEraProcessor p;
        auto out = process (music, [] (SpacenerdEraProcessor& q) { set (q, mix, 0.0f); }, &p);
        check (diffDb (out, music, p.getLatencySamples()) < -100.0f, "Mix 0% = delayed dry, latency " + String (p.getLatencySamples()));
    }

    // 6. Плавний рух курсора під час відтворення: без клацань (стрибків семпла)
    {
        SpacenerdEraProcessor p;
        p.setRateAndBufferSizeDetails (fs, 256); p.prepareToPlay (fs, 256);
        set (p, intensity, 90.0f);
        AudioBuffer<float> out (music); MidiBuffer m;
        float maxStep = 0.0f, prev = 0.0f;
        for (int pos = 0; pos < out.getNumSamples(); pos += 256)
        {
            set (p, year, 1960.0f + 65.0f * (float) pos / (float) out.getNumSamples());
            set (p, genre, (float) ((pos / 48000) % 5));
            AudioBuffer<float> chunk (out.getArrayOfWritePointers(), 2, pos, std::min (256, out.getNumSamples() - pos));
            p.processBlock (chunk, m);
        }
        // Порівнюємо з «гладкістю» входу: максимальний перепад між семплами
        float inStep = 0.0f;
        for (int i = 96001; i < out.getNumSamples(); ++i)
        {
            maxStep = std::max (maxStep, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
            inStep = std::max (inStep, std::abs (music.getSample (0, i) - music.getSample (0, i - 1)));
            prev = out.getSample (0, i);
        }
        juce::ignoreUnused (prev);
        check (maxStep < 1.0f, "Sweep year + switch genre while playing: max sample step " + String (maxStep, 3) + " (input " + String (inStep, 3) + ")");
    }

    }
    // 6b. Split: ручка LOW реально змінює низ, HIGH — верх (відносно 1 кГц, щоб авто-гучність не маскувала)
    {
        auto bandRel = [] (const AudioBuffer<float>& b, double f)
        {
            auto e = [&] (double fc)
            {
                sn::Biquad bp; bp.setBandPass (fs, fc, 1.4);
                double s = 0.0;
                for (int i = 0; i < b.getNumSamples(); ++i) { const float y = bp.process (b.getSample (0, i)); if (i > 96000) s += (double) y * y; }
                return 10.0 * std::log10 (s + 1e-30);
            };
            return (float) (e (f) - e (1000.0));
        };
        // Широкосмуговий сигнал (рожевий шум): у тестовому міксі майже немає енергії нижче 55 Гц
        AudioBuffer<float> pink (2, (int) (fs * 6.0));
        {
            std::mt19937 rng (5);
            std::normal_distribution<float> nd (0.0f, 1.0f);
            float b0 = 0, b1 = 0, b2 = 0;
            for (int i = 0; i < pink.getNumSamples(); ++i)
            {
                const float w = nd (rng);
                b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
                const float v = 0.03f * (b0 + b1 + b2 + w * 0.1848f);
                pink.setSample (0, i, v); pink.setSample (1, i, v);
            }
        }
        auto run = [&] (float lowY, float highY, float amt)
        {
            return process (pink, [=] (SpacenerdEraProcessor& q)
            {
                set (q, year, 1990.0f); set (q, intensity, 60.0f); set (q, split, 1.0f);
                set (q, yearLow, lowY); set (q, yearHigh, highY); set (q, lowAmt, amt); set (q, highAmt, amt);
            });
        };
        auto a = run (1960.0f, 1990.0f, 100.0f), b = run (2005.0f, 1990.0f, 100.0f);
        const float dLow = std::max (std::abs (bandRel (a, 45.0) - bandRel (b, 45.0)), std::abs (bandRel (a, 80.0) - bandRel (b, 80.0)));
        auto c = run (1990.0f, 1960.0f, 100.0f), d = run (1990.0f, 2005.0f, 100.0f);
        const float dHigh = std::abs (bandRel (c, 10000.0) - bandRel (d, 10000.0));
        auto e0 = run (1960.0f, 1960.0f, 0.0f), e1 = run (1960.0f, 1960.0f, 100.0f);
        const float dAmt = std::abs (bandRel (e0, 10000.0) - bandRel (e1, 10000.0));
        check (dLow > 4.0f, "Split LOW 1960 vs 2005 changes bass: " + String (dLow, 1) + " dB");
        check (dHigh > 5.0f, "Split HIGH 1960 vs 2005 changes top: " + String (dHigh, 1) + " dB");
        check (dAmt > 4.0f, "Split band intensity (vertical) changes sound: " + String (dAmt, 1) + " dB");
    }

    // 6b2. Пресет скидає Split
    {
        SpacenerdEraProcessor q;
        set (q, split, 1.0f); set (q, yearLow, 2020.0f);
        q.setCurrentProgram (1);
        check (q.apvts.getRawParameterValue (split)->load() < 0.5f, "Preset resets Split");
    }

    // 6c. Reference Match: «платівка» зі спектром і гучністю стоунера 1972 → рік поруч з 1972
    for (float refYear : { 1972.0f, 1998.0f, 2018.0f })
    {
        constexpr int N = 1 << 20;
        std::mt19937 rng (9);
        std::uniform_real_distribution<float> ph (0.0f, MathConstants<float>::twoPi);
        dsp::FFT fft (20);
        const auto curve = an::targetCurve (refYear, 1);
        AudioBuffer<float> rec (2, N);
        for (int ch = 0; ch < 2; ++ch)
        {
            std::vector<float> spec ((size_t) N * 2, 0.0f);
            for (int k = 1; k < N / 2; ++k)
            {
                const float f = (float) (k * fs / N);
                if (f < 20.0f || f > 20000.0f) continue;
                int b = 0; while (b < an::kBands - 2 && an::bandHz[(size_t) b + 1] < f) ++b;
                const float t = jlimit (0.0f, 1.0f, std::log (f / an::bandHz[(size_t) b]) / std::log (an::bandHz[(size_t) b + 1] / an::bandHz[(size_t) b]));
                const float db = curve[(size_t) b] + t * (curve[(size_t) b + 1] - curve[(size_t) b]);
                const float mag = std::pow (10.0f, db / 20.0f) / std::sqrt (f), a = ph (rng);
                spec[(size_t) (2 * k)] = mag * std::cos (a); spec[(size_t) (2 * k + 1)] = mag * std::sin (a);
            }
            fft.performRealOnlyInverseTransform (spec.data());
            rec.copyFrom (ch, 0, spec.data(), N);
        }
        rec.applyGain (0.3f / rec.getMagnitude (0, N));   // реальний діапазон цифрового звуку
        sn::LoudnessMeter lm; lm.prepare (fs, 2); lm.process (rec);
        rec.applyGain (sn::dbToGain (era::forYear (refYear, 1).targetLufs - lm.integrated.load()));
        const auto res = an::matchReference (rec, fs, 1);
        check (std::abs (res.year - refYear) <= 6.0f, "Reference Match: record like " + String (roundToInt (refYear)) + " -> "
               + String (roundToInt (res.year)) + " (" + String (res.match) + "%, " + String (res.lufs, 1) + " LUFS)");
    }

    // 7. Пресети
    if (! quick) {
        SpacenerdEraProcessor probe;
        for (int i = 0; i < probe.getNumPrograms(); ++i)
        {
            auto out = process (music, [&] (SpacenerdEraProcessor& q) { q.setCurrentProgram (i); });
            check (truePeakDb (out) <= -0.9f, "Preset '" + probe.getProgramName (i) + "': " + String (lufsOf (out, 8.0), 1) + " LUFS");
        }
    }

    if (argc > 1)
    {
        SpacenerdEraProcessor p; p.setCurrentProgram (1);
        process (music, [] (SpacenerdEraProcessor& q) { set (q, split, 1.0f); set (q, yearLow, 1970.0f); set (q, yearHigh, 2012.0f); set (q, lowAmt, 35.0f); set (q, highAmt, 85.0f); }, &p);
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        for (int f = 0; f < 8; ++f)
        {
            p.outPeak[0].push (0.8f); p.outPeak[1].push (0.78f); p.compGr.push (2.5f); p.limGr.push (1.5f);
            for (auto* child : ed->getChildren()) if (auto* c = dynamic_cast<EraContent*> (child)) c->tick();
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
