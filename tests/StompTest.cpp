// Офлайн-тести Spacenerd Stomp.
#include "../Stomp/PluginProcessor.h"
#include "../Stomp/PluginEditor.h"
#include <iostream>
#include <random>

using namespace juce;
using namespace StompIDs;

constexpr double fs = 48000.0;
static int failures = 0;
static void check (bool ok, const String& what) { std::cout << (ok ? "[ OK ] " : "[FAIL] ") << what << std::endl; if (! ok) ++failures; }

struct TestPlayHead final : AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0;
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i; i.setBpm (bpm); i.setIsPlaying (true); i.setPpqPosition (ppq); return i;
    }
};

static void set (SpacenerdStompProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

static AudioBuffer<float> sine (std::initializer_list<std::pair<double, float>> parts, double seconds)
{
    const int n = (int) (fs * seconds);
    AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i)
    {
        float v = 0.0f;
        for (auto [f, a] : parts) v += a * (float) std::sin (MathConstants<double>::twoPi * f * i / fs);
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
    return b;
}

static AudioBuffer<float> run (const AudioBuffer<float>& in, std::function<void (SpacenerdStompProcessor&)> setup,
                               TestPlayHead* ph = nullptr, int* latencyOut = nullptr)
{
    SpacenerdStompProcessor p;
    set (p, driveOn, 0.0f);   // за замовчуванням тест вмикає лише потрібне
    setup (p);
    if (ph) p.setPlayHead (ph);
    p.setRateAndBufferSizeDetails (fs, 512);
    p.prepareToPlay (fs, 512);
    if (latencyOut) *latencyOut = p.getLatency();
    AudioBuffer<float> out (in);
    MidiBuffer m;
    for (int pos = 0; pos < out.getNumSamples(); pos += 512)
    {
        const int len = std::min (512, out.getNumSamples() - pos);
        AudioBuffer<float> chunk (out.getArrayOfWritePointers(), 2, pos, len);
        p.processBlock (chunk, m);
        if (ph) ph->ppq += len / fs * ph->bpm / 60.0;
    }
    return out;
}

static float rmsDb (const AudioBuffer<float>& b, int ch, double from, double to)
{
    const int s = (int) (from * fs), e = std::min (b.getNumSamples(), (int) (to * fs));
    return sn::gainToDb (b.getRMSLevel (ch, s, e - s));
}

/** Амплітуда складової частоти f (кореляція з sin/cos) на відрізку. */
static float amp (const AudioBuffer<float>& b, int ch, double f, double from, double to)
{
    const int s = (int) (from * fs), e = std::min (b.getNumSamples(), (int) (to * fs));
    double re = 0, im = 0;
    for (int i = s; i < e; ++i)
    {
        const double ph = MathConstants<double>::twoPi * f * i / fs;
        re += b.getSample (ch, i) * std::cos (ph);
        im += b.getSample (ch, i) * std::sin (ph);
    }
    return (float) (2.0 * std::sqrt (re * re + im * im) / (e - s));
}

static bool finite (const AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (c, i))) return false;
    return true;
}

int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI init;
    const float a18 = 0.125892541f;                    // -18 dBFS
    const auto gtr = sine ({ { 220.0, a18 } }, 1.0);
    const float inRms = rmsDb (gtr, 0, 0.5, 1.0);

    // 1. Кожна схема: гучність ≈ вхід (±1.5 дБ), реальне спотворення
    {
        String levels;
        for (int c = 0; c <= 4; ++c)
        {
            auto out = run (gtr, [c] (SpacenerdStompProcessor& p) { set (p, driveOn, 1); set (p, circuit, (float) c); set (p, gain, 60); });
            const float lv = rmsDb (out, 0, 0.5, 1.0) - inRms;
            const float fund = amp (out, 0, 220.0, 0.5, 1.0);
            const float totalRms = out.getRMSLevel (0, (int) (0.5 * fs), (int) (0.5 * fs));
            const float thd = std::sqrt (std::max (0.0f, totalRms * totalRms - 0.5f * fund * fund)) / (fund * 0.70710678f);
            levels << String (lv, 1) << " dB/THD " << String (thd * 100.0f, 0) << "%  ";
            check (std::abs (lv) < 1.5f, "circuit " + String (c) + " level " + String (lv, 2) + " dB");
            check (thd > 0.15f, "circuit " + String (c) + " distorts (THD " + String (thd * 100.0f, 0) + "%)");
        }
        std::cout << "  " << levels << std::endl;
    }

    // 2. Морф: без стрибків гучності між сусідніми точками
    {
        float prev = 0.0f, worst = 0.0f;
        for (int k = 0; k <= 40; ++k)
        {
            const float x = (float) k * 0.1f;
            auto out = run (gtr, [x] (SpacenerdStompProcessor& p) { set (p, driveOn, 1); set (p, circuit, x); set (p, gain, 60); });
            const float lv = rmsDb (out, 0, 0.5, 1.0);
            if (k > 0) worst = std::max (worst, std::abs (lv - prev));
            prev = lv;
        }
        check (worst < 1.0f, "morph continuity: worst step " + String (worst, 2) + " dB");
    }

    // 3. Battery: «голодне» живлення — тихі хвости нот глушаться сильніше, ніж гучні атаки
    {
        const auto quiet = sine ({ { 220.0, 0.0056f } }, 1.0);     // -45 dBFS
        auto drop = [&] (float bat)
        {
            auto setup = [bat] (SpacenerdStompProcessor& p) { set (p, driveOn, 1); set (p, circuit, 0); set (p, gain, 50); set (p, battery, bat); };
            return rmsDb (run (gtr, setup), 0, 0.5, 1.0) - rmsDb (run (quiet, setup), 0, 0.5, 1.0);
        };
        const float d0 = drop (0.0f), d80 = drop (80.0f);
        check (d80 > d0 + 4.0f, "battery starves quiet tails: loud-quiet gap " + String (d0, 1) + " -> " + String (d80, 1) + " dB");
    }

    // 4. Clean Bass: низ проходить чистим
    {
        const auto bass = sine ({ { 50.0, 0.1f }, { 330.0, 0.1f } }, 1.5);
        auto withCb = run (bass, [] (SpacenerdStompProcessor& p) { set (p, driveOn, 1); set (p, circuit, 2); set (p, gain, 80); set (p, cleanBass, 150); });
        auto noCb   = run (bass, [] (SpacenerdStompProcessor& p) { set (p, driveOn, 1); set (p, circuit, 2); set (p, gain, 80); });
        const float lowIn = sn::gainToDb (0.1f);
        const float lowCb = sn::gainToDb (amp (withCb, 0, 50.0, 0.5, 1.5)), lowNo = sn::gainToDb (amp (noCb, 0, 50.0, 0.5, 1.5));
        check (std::abs (lowCb - lowIn) < 1.5f, "clean bass keeps 50 Hz: " + String (lowCb - lowIn, 2) + " dB (off: " + String (lowNo - lowIn, 1) + " dB)");
    }

    // 5. Прозорість і латентність при вимкненій педалі
    {
        int lat = 0;
        auto out = run (gtr, [] (SpacenerdStompProcessor&) {}, nullptr, &lat);
        double e = 0, d = 0;
        for (int i = 4800; i < gtr.getNumSamples() - lat; ++i)
        {
            const float diff = out.getSample (0, i + lat) - gtr.getSample (0, i);
            e += diff * diff; d += gtr.getSample (0, i) * gtr.getSample (0, i);
        }
        const float resid = (float) (10.0 * std::log10 (e / d + 1e-20));
        check (resid < -50.0f, "bypass-ish path transparent, latency " + String (lat) + " smp, residual " + String (resid, 1) + " dB");
    }

    // 6. Тремоло: глибина і період (Free 5 Гц і Sync 1/8 @120 = 4 Гц)
    {
        const auto tone = sine ({ { 1000.0, 0.25f } }, 2.0);
        auto period = [&] (const AudioBuffer<float>& out, float& depthDb)
        {
            // Огинаюча по вікнах 2 мс
            std::vector<float> env;
            const int w = 96;
            for (int i = (int) (0.5 * fs); i + w < out.getNumSamples(); i += w) env.push_back (out.getMagnitude (0, i, w));
            const float mx = *std::max_element (env.begin(), env.end()), mn = *std::min_element (env.begin(), env.end());
            depthDb = sn::gainToDb (mn / mx);
            std::vector<int> ups;
            const float mid = 0.5f * (mx + mn);
            for (size_t k = 1; k < env.size(); ++k) if (env[k - 1] < mid && env[k] >= mid) ups.push_back ((int) k);
            return ups.size() > 2 ? (double) (ups.back() - ups.front()) * w / fs / (double) (ups.size() - 1) : 0.0;
        };
        float dDb = 0.0f;
        auto free = run (tone, [] (SpacenerdStompProcessor& p) { set (p, modOn, 1); set (p, modMode, 0); set (p, rate, 5.0f); set (p, depth, 100); set (p, shape, 0); });
        const double t1 = period (free, dDb);
        check (std::abs (t1 - 0.2) < 0.01 && dDb < -20.0f, "tremolo 5 Hz: period " + String (t1 * 1000.0, 1) + " ms, depth " + String (dDb, 1) + " dB");
        TestPlayHead ph;
        auto synced = run (tone, [] (SpacenerdStompProcessor& p) { set (p, modOn, 1); set (p, modSync, 2); set (p, depth, 100); }, &ph);
        const double t2 = period (synced, dDb);
        check (std::abs (t2 - 0.25) < 0.01, "tremolo sync 1/8 @120: period " + String (t2 * 1000.0, 1) + " ms");
    }

    // 7. Пан: сигнал ходить між каналами; гармонічне і вібрато: рівень збережено
    {
        const auto tone = sine ({ { 440.0, 0.25f } }, 2.0);
        auto pan = run (tone, [] (SpacenerdStompProcessor& p) { set (p, modOn, 1); set (p, modMode, 2); set (p, rate, 2.0f); set (p, depth, 100); });
        float maxDiff = 0.0f;
        for (double t = 0.5; t < 1.9; t += 0.05)
            maxDiff = std::max (maxDiff, std::abs (rmsDb (pan, 0, t, t + 0.02) - rmsDb (pan, 1, t, t + 0.02)));
        check (maxDiff > 20.0f, "pan swings L/R: " + String (maxDiff, 1) + " dB");

        for (int mode : { 1, 3 })
        {
            auto out = run (tone, [mode] (SpacenerdStompProcessor& p) { set (p, modOn, 1); set (p, modMode, (float) mode); set (p, depth, 60); });
            const float d = rmsDb (out, 0, 0.5, 2.0) - rmsDb (tone, 0, 0.5, 2.0);
            check (finite (out) && d > -4.0f && d < 1.0f, String (mode == 1 ? "harmonic" : "vibrato") + " level " + String (d, 2) + " dB");
        }
        // Вібрато змінює висоту: складова 440 Гц «розмазана»
        auto vib = run (tone, [] (SpacenerdStompProcessor& p) { set (p, modOn, 1); set (p, modMode, 3); set (p, depth, 100); set (p, rate, 5.0f); });
        const float spread = sn::gainToDb (amp (vib, 0, 440.0, 0.5, 2.0) / 0.25f);
        check (spread < -1.0f, "vibrato modulates pitch (440 Hz line " + String (spread, 1) + " dB)");
    }

    // 8. Ехо: час повтору
    {
        AudioBuffer<float> imp (2, (int) fs);
        imp.clear();
        imp.setSample (0, 1000, 1.0f); imp.setSample (1, 1000, 1.0f);
        int lat = 0;
        auto out = run (imp, [] (SpacenerdStompProcessor& p) { set (p, echoOn, 1); set (p, echoTime, 300); set (p, feedback, 0); set (p, echoMix, 100); set (p, wear, 0); }, nullptr, &lat);
        int best = 0; float bv = 0.0f;
        for (int i = 1000 + lat + 2000; i < out.getNumSamples(); ++i)
            if (std::abs (out.getSample (0, i)) > bv) { bv = std::abs (out.getSample (0, i)); best = i; }
        const double ms = (best - 1000 - lat) / fs * 1000.0;
        check (std::abs (ms - 300.0) < 3.0, "echo repeat at " + String (ms, 1) + " ms");
    }

    // 9. Стабільність: усе на максимумі, шум і тиша
    {
        const int n = (int) (fs * 8.0);
        AudioBuffer<float> noise (2, n);
        std::mt19937 rng (3);
        std::normal_distribution<float> nd (0.0f, 0.3f);
        for (int i = 0; i < n; ++i) { const float v = i < n / 2 ? nd (rng) : 0.0f; noise.setSample (0, i, v); noise.setSample (1, i, v); }
        auto out = run (noise, [] (SpacenerdStompProcessor& p)
        {
            set (p, driveOn, 1); set (p, circuit, 2.7f); set (p, gain, 100); set (p, battery, 100); set (p, cleanBass, 300);
            set (p, octOn, 1); set (p, sub1, 100); set (p, sub2, 100); set (p, octUp, 100); set (p, octEngine, 2); set (p, wobble, 100); set (p, bloom, 300);
            set (p, modOn, 1); set (p, modMode, 1); set (p, rate, 15); set (p, depth, 100); set (p, shape, 100);
            set (p, echoOn, 1); set (p, feedback, 95); set (p, wear, 100); set (p, echoMix, 100); set (p, echoTime, 60);
        });
        check (finite (out) && out.getMagnitude (0, n) < 4.0f, "stable at extremes, peak " + String (sn::gainToDb (out.getMagnitude (0, n)), 1) + " dBFS");
    }

    // 10. Пресети застосовуються
    {
        SpacenerdStompProcessor p;
        bool ok = true;
        for (int i = 0; i < p.getNumPrograms(); ++i) { p.setCurrentProgram (i); ok &= p.getCurrentProgram() == i && p.getProgramName (i).isNotEmpty(); }
        p.setCurrentProgram (8);
        ok &= std::abs (p.apvts.getRawParameterValue (cleanBass)->load() - 150.0f) < 1.0f;
        check (ok, String (p.getNumPrograms()) + " presets load");
    }

    // 11. Октавер: висота кожного голосу, обидва двигуни
    {
        // Гітарна нота з гармоніками, 110 Гц (A2)
        const int n = (int) (fs * 1.5);
        AudioBuffer<float> note (2, n);
        for (int i = 0; i < n; ++i)
        {
            float v = 0.0f;
            for (int h = 1; h <= 6; ++h) v += 0.15f / (float) h * (float) std::sin (MathConstants<double>::twoPi * 110.0 * h * i / fs);
            note.setSample (0, i, v); note.setSample (1, i, v);
        }
        const float fund = amp (note, 0, 110.0, 0.5, 1.5);
        struct Case { const char* name; float s1, s2, up, chr; double f; };
        for (auto cs : { Case { "vintage -1", 100, 0, 0, 1, 55.0 }, Case { "vintage -2", 0, 100, 0, 1, 27.5 }, Case { "vintage +1", 0, 0, 100, 1, 220.0 },
                         Case { "mono HQ -1", 100, 0, 0, 2, 55.0 }, Case { "mono HQ -2", 0, 100, 0, 2, 27.5 }, Case { "mono HQ +1", 0, 0, 100, 2, 220.0 },
                         Case { "poly -1", 100, 0, 0, 0, 55.0 },    Case { "poly -2", 0, 100, 0, 0, 27.5 },    Case { "poly +1", 0, 0, 100, 0, 220.0 } })
        {
            auto out = run (note, [cs] (SpacenerdStompProcessor& p)
            {
                set (p, octOn, 1); set (p, octDry, 0); set (p, sub1, cs.s1); set (p, sub2, cs.s2); set (p, octUp, cs.up);
                set (p, octEngine, cs.chr); set (p, octTone, 8000);
            });
            const float target = amp (out, 0, cs.f, 0.5, 1.5);
            const float rel = sn::gainToDb (target / fund);
            check (finite (out) && rel > -9.0f, String (cs.name) + ": " + String (cs.f, 1) + " Hz at " + String (rel, 1) + " dB re input fundamental");
        }

        // Чистота (без «муті»): вихід періодичний з періодом нової ноти
        auto nacf = [] (const AudioBuffer<float>& b, double L)
        {
            const int s0 = (int) (0.4 * fs), e = (int) (1.4 * fs);
            const int Li = (int) L; const float t = (float) (L - Li);
            double xy = 0, xx = 0, yy = 0;
            for (int i = s0; i < e; ++i)
            {
                const float x = b.getSample (0, i), y = (1.0f - t) * b.getSample (0, i + Li) + t * b.getSample (0, i + Li + 1);
                xy += x * y; xx += x * x; yy += y * y;
            }
            return (float) (xy / std::sqrt (xx * yy + 1e-30));
        };
        for (auto [eng, voice, ratio] : { std::tuple<int, int, double> { 1, 0, 0.5 }, { 2, 0, 0.5 }, { 2, 2, 2.0 }, { 1, 2, 2.0 } })
        {
            auto out = run (note, [eng = eng, voice = voice] (SpacenerdStompProcessor& p)
            {
                set (p, octOn, 1); set (p, octDry, 0); set (p, octEngine, (float) eng); set (p, octTone, 8000);
                set (p, sub1, voice == 0 ? 100.0f : 0.0f); set (p, octUp, voice == 2 ? 100.0f : 0.0f);
            });
            const float c = nacf (out, fs / (110.0 * ratio));
            check (c > 0.9f, String (eng == 1 ? "vintage " : "mono HQ ") + (voice == 0 ? "-1" : "+1") + " clarity " + String (c, 3));
        }

        // Poly тримає акорд: A2 + E3 → суб обох нот
        const auto chord = sine ({ { 110.0, 0.12f }, { 164.81, 0.12f } }, 1.5);
        auto poly = run (chord, [] (SpacenerdStompProcessor& p) { set (p, octOn, 1); set (p, octDry, 0); set (p, sub1, 100); set (p, octEngine, 0); set (p, octTone, 8000); });
        const float a55 = sn::gainToDb (amp (poly, 0, 55.0, 0.5, 1.5) / 0.12f), a82 = sn::gainToDb (amp (poly, 0, 82.4, 0.5, 1.5) / 0.12f);
        check (a55 > -9.0f && a82 > -9.0f, "poly chord sub: 55 Hz " + String (a55, 1) + " dB, 82.4 Hz " + String (a82, 1) + " dB");

        // Аналоговий суб мовчить у тиші (без «бубніння»)
        AudioBuffer<float> silence (2, (int) fs); silence.clear();
        auto quiet = run (silence, [] (SpacenerdStompProcessor& p) { set (p, octOn, 1); set (p, octEngine, 1); set (p, sub1, 100); set (p, sub2, 100); });
        check (quiet.getMagnitude (0, quiet.getNumSamples()) < 1.0e-4f, "analog sub silent on silence");

        // Bloom: октава наростає після атаки
        auto bl = run (note, [] (SpacenerdStompProcessor& p) { set (p, octOn, 1); set (p, octDry, 0); set (p, sub1, 100); set (p, octEngine, 0); set (p, bloom, 500); });
        const float early = rmsDb (bl, 0, 0.0, 0.05), late = rmsDb (bl, 0, 1.2, 1.5);
        check (late > early + 8.0f, "bloom swells: " + String (early, 1) + " -> " + String (late, 1) + " dB");

        // Wobble: рівень октави «дихає» з частотою LFO
        auto wb = run (note, [] (SpacenerdStompProcessor& p)
        {
            set (p, octOn, 1); set (p, octDry, 0); set (p, octUp, 100); set (p, octEngine, 1); set (p, octTone, 6000);
            set (p, wobble, 100); set (p, rate, 4.0f);
        });
        float mn = 100.0f, mx = -100.0f;
        for (double t = 0.3; t < 1.45; t += 0.01) { const float v = rmsDb (wb, 0, t, t + 0.01); mn = std::min (mn, v); mx = std::max (mx, v); }
        check (finite (wb) && mx - mn > 8.0f, "wobble sweeps: " + String (mx - mn, 1) + " dB swing");

        // Вимкнений октавер прозорий
        int lat = 0;
        auto off = run (note, [] (SpacenerdStompProcessor& p) { set (p, octOn, 0); set (p, sub1, 100); }, nullptr, &lat);
        double e = 0, d = 0;
        for (int i = 4800; i < n - lat; ++i) { const float df = off.getSample (0, i + lat) - note.getSample (0, i); e += df * df; d += note.getSample (0, i) * note.getSample (0, i); }
        check (10.0 * std::log10 (e / d + 1e-20) < -50.0, "octave off: transparent");
    }

    // 12. Знімок інтерфейсу
    if (argc > 1)
    {
        SpacenerdStompProcessor p;
        p.setCurrentProgram (18);
        p.setRateAndBufferSizeDetails (fs, 512);
        p.prepareToPlay (fs, 512);
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        auto buf = sine ({ { 110.0, 0.3f } }, 0.2);
        MidiBuffer m;
        for (int k = 0; k < 10; ++k)
        {
            AudioBuffer<float> chunk (buf.getArrayOfWritePointers(), 2, k * 512, 512);
            p.processBlock (chunk, m);
            dynamic_cast<SpacenerdStompEditor*> (ed.get())->tickForTest();
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
