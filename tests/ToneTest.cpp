// Офлайн-тести Spacenerd Tone.
#include "../Tone/PluginProcessor.h"
#include "../Tone/PluginEditor.h"
#include <iostream>
#include <chrono>
#include <random>
#include <complex>

using namespace juce;
using namespace ToneIDs;

static int failures = 0;
static void check (bool ok, const String& what)
{
    std::cout << (ok ? "[ OK ] " : "[FAIL] ") << what << std::endl;
    if (! ok) ++failures;
}

static void setParam (SpacenerdToneProcessor& p, const char* id, float v)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (v));
}

static void allOff (SpacenerdToneProcessor& p)
{
    for (auto id : { trOn, tubeOn, tapeOn, excOn, dsOn }) setParam (p, id, 0.0f);
}

using Gen = std::function<void (AudioBuffer<float>&, int64)>;

static AudioBuffer<float> run (SpacenerdToneProcessor& p, double fs, double seconds, Gen gen, int maxBlock = 512)
{
    p.setRateAndBufferSizeDetails (fs, maxBlock);
    p.prepareToPlay (fs, maxBlock);
    const int total = (int) (fs * seconds);
    AudioBuffer<float> out (2, total);
    MidiBuffer midi;
    const int sizes[] = { maxBlock, 37, 1, 256, maxBlock, 129, 7 };
    int pos = 0, k = 0;
    while (pos < total)
    {
        const int n = std::min (std::min (sizes[k++ % 7], maxBlock), total - pos);
        AudioBuffer<float> b (2, n);
        gen (b, pos);
        p.processBlock (b, midi);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, b, ch, 0, n);
        pos += n;
    }
    return out;
}

static Gen sine (double f, float amp, double fs)
{
    return [=] (AudioBuffer<float>& b, int64 start)
    {
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = amp * (float) std::sin (2.0 * MathConstants<double>::pi * f * (double) (start + i) / fs);
            b.setSample (0, i, v); b.setSample (1, i, v);
        }
    };
}

/** Амплітуда складової частоти f (Гьорцель з вікном Ганна). */
static float toneLevel (const AudioBuffer<float>& b, int from, int len, double f, double fs)
{
    std::complex<double> acc = 0.0;
    double wsum = 0.0;
    for (int i = 0; i < len; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * MathConstants<double>::pi * i / (len - 1));
        acc += w * (double) b.getSample (0, from + i) * std::exp (std::complex<double> (0.0, -2.0 * MathConstants<double>::pi * f * i / fs));
        wsum += w;
    }
    return (float) (2.0 * std::abs (acc) / wsum);
}

static float rms (const AudioBuffer<float>& b, int from, int len)
{
    return b.getRMSLevel (0, from, len);
}

int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI init;
    const double fs = 48000.0;
    const float nominal = sn::dbToGain (-18.0f);

    // 1. Калібрування: на -18 dBFS лампа і плівка не змінюють гучність за будь-якого Drive
    for (float drive : { 0.0f, 50.0f, 100.0f })
    {
        for (int stage = 0; stage < 2; ++stage)
        {
            SpacenerdToneProcessor p; allOff (p);
            setParam (p, stage == 0 ? tubeOn : tapeOn, 1.0f);
            setParam (p, stage == 0 ? tubeDrive : tapeDrive, drive);
            auto out = run (p, fs, 1.0, sine (1000.0, nominal, fs));
            const float lvl = sn::gainToDb (rms (out, 24000, 24000) / (nominal * 0.70710678f));
            check (std::abs (lvl) < 1.0f, String (stage == 0 ? "Tube" : "Tape") + " drive " + String ((int) drive)
                   + ": level change at -18 dBFS " + String (lvl, 2) + " dB");
        }
    }

    // 2. Лампа: Bias дає парні гармоніки
    {
        SpacenerdToneProcessor p; allOff (p);
        setParam (p, tubeOn, 1.0f); setParam (p, tubeDrive, 70.0f); setParam (p, tubeBias, 80.0f);
        auto out = run (p, fs, 1.0, sine (200.0, 0.3f, fs));
        const float h1 = toneLevel (out, 24000, 19200, 200.0, fs);
        const float h2 = toneLevel (out, 24000, 19200, 400.0, fs);
        const float h3 = toneLevel (out, 24000, 19200, 600.0, fs);
        check (h2 / h1 > 0.01f, "Tube bias: H2 " + String (sn::gainToDb (h2 / h1), 1) + " dB, H3 "
               + String (sn::gainToDb (h3 / h1), 1) + " dB rel. fundamental");
    }

    // 3. Плівка: гістерезис стабільний на гучному шумі, стрибках і DC
    {
        SpacenerdToneProcessor p; allOff (p);
        setParam (p, tapeOn, 1.0f); setParam (p, tapeDrive, 100.0f); setParam (p, inGain, 24.0f);
        std::mt19937 rng (5);
        std::uniform_real_distribution<float> ud (-1.0f, 1.0f);
        auto out = run (p, 44100.0, 5.0, [&] (AudioBuffer<float>& b, int64 start)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const int64 t = start + i;
                const float v = (t / 20000) % 3 == 0 ? ud (rng) : ((t / 20000) % 3 == 1 ? 1.0f : 0.0f);
                b.setSample (0, i, v); b.setSample (1, i, -v);
            }
        });
        bool finite = true;
        for (int i = 0; i < out.getNumSamples(); ++i) finite &= std::isfinite (out.getSample (0, i)) && std::isfinite (out.getSample (1, i));
        const float pk = out.getMagnitude (0, 0, out.getNumSamples());
        check (finite && pk < 8.0f, "Tape hysteresis stable (noise, steps, DC, +24 dB): peak " + String (sn::gainToDb (pk), 1) + " dBFS");
    }

    // 4. Плівка: гістерезис дає компресію на гучному сигналі (насичення)
    {
        SpacenerdToneProcessor p; allOff (p);
        setParam (p, tapeOn, 1.0f); setParam (p, tapeDrive, 60.0f); setParam (p, tapeSpeed, 2.0f);
        auto quiet = run (p, fs, 0.5, sine (1000.0, nominal, fs));
        SpacenerdToneProcessor p2; allOff (p2);
        setParam (p2, tapeOn, 1.0f); setParam (p2, tapeDrive, 60.0f); setParam (p2, tapeSpeed, 2.0f);
        auto loud = run (p2, fs, 0.5, sine (1000.0, nominal * 8.0f, fs));   // +18 dB
        const float gainLoud = sn::gainToDb (rms (loud, 12000, 9600) / rms (quiet, 12000, 9600));
        check (gainLoud < 17.0f && gainLoud > 6.0f, "Tape saturation: +18 dB input gives +" + String (gainLoud, 1) + " dB output");
    }

    // 5. Швидкість стрічки: горб на басу і завал ВЧ
    for (int speed = 0; speed < 3; ++speed)
    {
        float lv[3] {};
        const double freqs[3] { 1000.0, speed == 0 ? 45.0 : (speed == 1 ? 65.0 : 110.0), 14000.0 };
        for (int k = 0; k < 3; ++k)
        {
            SpacenerdToneProcessor p; allOff (p);
            setParam (p, tapeOn, 1.0f); setParam (p, tapeDrive, 0.0f); setParam (p, tapeSpeed, (float) speed);
            auto out = run (p, fs, 1.0, sine (freqs[k], nominal * 0.25f, fs));
            lv[k] = sn::gainToDb (toneLevel (out, 24000, 19200, freqs[k], fs));
        }
        const float bump = lv[1] - lv[0], hf = lv[2] - lv[0];
        check (bump > 0.8f && hf < 0.5f, "Tape " + String (speed == 0 ? "7.5" : speed == 1 ? "15" : "30")
               + " ips: bump +" + String (bump, 1) + " dB @ " + String ((int) freqs[1]) + " Hz, 14 kHz " + String (hf, 1) + " dB");
    }

    // 6. Аліасинг: лампа на максимумі, синус 5 кГц @ 44.1 — дзеркальні складові придушені
    {
        const double f44 = 44100.0;
        SpacenerdToneProcessor p; allOff (p);
        setParam (p, tubeOn, 1.0f); setParam (p, tubeDrive, 100.0f); setParam (p, tubeBias, 50.0f);
        auto out = run (p, f44, 1.0, sine (5000.0, 0.5f, f44));
        const float fund = toneLevel (out, 22050, 17640, 5000.0, f44);
        // 5-та гармоніка 25 кГц → дзеркало 19.1 кГц; 6-та 30 кГц → 14.1 кГц
        const float a1 = toneLevel (out, 22050, 17640, 19100.0, f44);
        const float a2 = toneLevel (out, 22050, 17640, 14100.0, f44);
        const float worst = sn::gainToDb (std::max (a1, a2) / fund);
        check (worst < -50.0f, "Aliasing (tube max, 5 kHz @ 44.1): worst alias " + String (worst, 1) + " dB");
    }

    // 7. Mix 50 % з усіма модулями вимкненими: сухий і мокрий вирівняні (рівна АЧХ, без гребінки)
    {
        float worst = 0.0f;
        for (double f : { 100.0, 1000.0, 5000.0, 12000.0 })
        {
            SpacenerdToneProcessor p; allOff (p);
            setParam (p, mix, 50.0f);
            auto out = run (p, fs, 0.5, sine (f, 0.25f, fs));
            worst = std::max (worst, std::abs (sn::gainToDb (toneLevel (out, 9600, 9600, f, fs) / 0.25f)));
        }
        check (worst < 0.2f, "Dry/wet alignment (mix 50%): worst deviation " + String (worst, 3) + " dB");
    }

    // 8. Затримка: заявлена = реальна
    {
        SpacenerdToneProcessor p;
        auto out = run (p, fs, 0.5, [] (AudioBuffer<float>& b, int64 start)
        {
            b.clear();
            if (start <= 1000 && start + b.getNumSamples() > 1000)
                for (int ch = 0; ch < 2; ++ch) b.setSample (ch, (int) (1000 - start), 0.05f);
        });
        int argmax = 0; float best = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i)
            if (std::abs (out.getSample (0, i)) > best) { best = std::abs (out.getSample (0, i)); argmax = i; }
        check (std::abs (argmax - 1000 - p.getLatencySamples()) <= 1,
               "Latency reported " + String (p.getLatencySamples()) + ", measured " + String (argmax - 1000));
    }

    // 9. Де-есер: сибілянти ослаблені, голосні — ні, незалежно від гучності
    for (float levelDb : { -12.0f, -32.0f })
    {
        const float a = sn::dbToGain (levelDb);
        // «Голосна»: 220 Гц + гармоніки; «с»: шум вище 5 кГц
        auto vowel = [&] (AudioBuffer<float>& b, int64 start)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double t = (double) (start + i) / fs;
                float v = 0.0f;
                for (int h = 1; h <= 6; ++h) v += (float) std::sin (2.0 * MathConstants<double>::pi * 220.0 * h * t) / (float) (h * h);
                b.setSample (0, i, a * v); b.setSample (1, i, a * v);
            }
        };
        std::mt19937 rng (11);
        std::normal_distribution<float> nd (0.0f, 1.0f);
        sn::Biquad hp; hp.setHighPass (fs, 5000.0);
        auto sss = [&] (AudioBuffer<float>& b, int64)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float v = a * 0.5f * hp.process (nd (rng));
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
        };

        auto measure = [&] (Gen g, bool dsEnabled)
        {
            SpacenerdToneProcessor p; allOff (p);
            setParam (p, dsOn, dsEnabled ? 1.0f : 0.0f);
            auto out = run (p, fs, 1.0, g);
            return rms (out, 24000, 24000);
        };
        const float vowelChange = sn::gainToDb (measure (vowel, true) / measure (vowel, false));
        hp.reset(); rng.seed (11);
        const float sOff = measure (sss, false);
        hp.reset(); rng.seed (11);
        const float sOn = measure (sss, true);
        const float sChange = sn::gainToDb (sOn / sOff);
        check (std::abs (vowelChange) < 0.5f && sChange < -3.0f,
               "De-esser @ " + String ((int) levelDb) + " dBFS: vowel " + String (vowelChange, 2) + " dB, sibilance " + String (sChange, 1) + " dB");
    }

    // 10. Транзієнт-шейпер: Attack+ піднімає пік удару, Sustain- прибирає хвіст
    {
        auto hits = [&] (AudioBuffer<float>& b, int64 start)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const int64 t = (start + i) % 24000;       // удар кожні 0.5 с
                const float env = std::exp (-(float) t / 4000.0f);
                const float v = 0.3f * env * (float) std::sin (2.0 * MathConstants<double>::pi * 120.0 * (double) t / fs);
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
        };
        auto shape = [&] (float atk, float sus)
        {
            SpacenerdToneProcessor p; allOff (p);
            setParam (p, trOn, 1.0f); setParam (p, trAttack, atk); setParam (p, trSustain, sus);
            return run (p, fs, 2.0, hits);
        };
        const int lat = [&] { SpacenerdToneProcessor q; q.prepareToPlay (fs, 512); return q.getLatencySamples(); }();
        auto ref = shape (0, 0), atk = shape (100, 0), sus = shape (0, -100);
        const int hit = 48000 + lat;
        const float peakRef = ref.getMagnitude (0, hit, 480), peakAtk = atk.getMagnitude (0, hit, 480);
        const float tailRef = rms (ref, hit + 9600, 4800), tailSus = rms (sus, hit + 9600, 4800);
        check (sn::gainToDb (peakAtk / peakRef) > 2.0f && sn::gainToDb (tailSus / tailRef) < -3.0f,
               "Transient: attack +100 → peak " + String (sn::gainToDb (peakAtk / peakRef), 1) + " dB, sustain -100 → tail "
               + String (sn::gainToDb (tailSus / tailRef), 1) + " dB");
    }

    // 11. Усі пресети: без NaN, рівень адекватний; CPU
    {
        SpacenerdToneProcessor probe;
        for (int i = 0; i < probe.getNumPrograms(); ++i)
        {
            SpacenerdToneProcessor p;
            p.setCurrentProgram (i);
            std::mt19937 rng ((unsigned) i);
            std::normal_distribution<float> nd (0.0f, 0.05f);
            const auto t0 = std::chrono::steady_clock::now();
            auto out = run (p, fs, 5.0, [&] (AudioBuffer<float>& b, int64 start)
            {
                for (int k = 0; k < b.getNumSamples(); ++k)
                {
                    const double t = (double) (start + k) / fs;
                    const float v = 0.15f * (float) std::sin (2.0 * MathConstants<double>::pi * 110.0 * t) + nd (rng);
                    b.setSample (0, k, v); b.setSample (1, k, v);
                }
            });
            const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            bool finite = true;
            for (int k = 0; k < out.getNumSamples(); ++k) finite &= std::isfinite (out.getSample (0, k));
            const float pk = out.getMagnitude (0, 0, out.getNumSamples());
            check (finite && pk < 4.0f, "Preset '" + p.getProgramName (i) + "': peak " + String (sn::gainToDb (pk), 1)
                   + " dBFS, " + String (5.0 / secs, 0) + "x realtime");
        }
    }

    // 12. Стан
    {
        SpacenerdToneProcessor a, b;
        setParam (a, tapeSpeed, 0.0f); setParam (a, tubeBias, 77.0f);
        MemoryBlock mb; a.getStateInformation (mb);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        check ((int) b.apvts.getRawParameterValue (tapeSpeed)->load() == 0
               && std::abs (b.apvts.getRawParameterValue (tubeBias)->load() - 77.0f) < 0.01f, "State save/restore");
    }

    if (argc > 1)
    {
        SpacenerdToneProcessor p;
        p.setCurrentProgram (2);
        run (p, fs, 2.0, sine (220.0, 0.3f, fs));
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        for (int f = 0; f < 10; ++f)
        {
            p.inPeak[0].push (0.3f); p.inPeak[1].push (0.28f);
            p.outPeak[0].push (0.35f); p.outPeak[1].push (0.33f);
            p.deEssGr.push (4.5f);
            for (auto* child : ed->getChildren())
                if (auto* tc = dynamic_cast<ToneContent*> (child)) tc->tick();
        }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
        File file (argv[1]);
        file.deleteFile();
        FileOutputStream os (file);
        PNGImageFormat().writeImageToStream (img, os);
        std::cout << "Snapshot: " << file.getFullPathName() << std::endl;
    }

    std::cout << (failures == 0 ? "ALL PASSED" : String (failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
