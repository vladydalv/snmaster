// Офлайн-тест DSP: гучність, лімітер, затримка, стабільність. Опційно — знімок інтерфейсу.
#include "../Master/PluginProcessor.h"
#include "../Master/PluginEditor.h"
#include "../Master/AlbumView.h"
#include <juce_events/juce_events.h>
#include <iostream>
#include <chrono>
#include <random>
#include <complex>

using namespace juce;

static int failures = 0;
static void check (bool ok, const String& what)
{
    std::cout << (ok ? "[ OK ] " : "[FAIL] ") << what << std::endl;
    if (! ok) ++failures;
}

static void setParam (SpacenerdMasterProcessor& p, const char* id, float v)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (v));
}

static void allOff (SpacenerdMasterProcessor& p)
{
    for (auto id : { ParamIDs::eqOn, ParamIDs::compOn, ParamIDs::satOn, ParamIDs::widthOn, ParamIDs::limOn })
        setParam (p, id, 0.0f);
}

using Gen = std::function<void (AudioBuffer<float>&, int64)>;

/** Проганяє сигнал блоками змінного розміру; повертає вихід. */
static AudioBuffer<float> run (SpacenerdMasterProcessor& p, double fs, int seconds, Gen gen, int maxBlock = 512)
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

int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI init;

    // 1. K-зважування при 48 кГц = еталон BS.1770
    {
        sn::LoudnessMeter m; m.prepare (48000.0, 2);
        const bool ok = std::abs (m.pre.b0 - 1.53512485958697) < 1e-6 && std::abs (m.pre.a1 + 1.69065929318241) < 1e-6
                     && std::abs (m.pre.a2 - 0.73248077421585) < 1e-6 && std::abs (m.rlb.a1 + 1.99004745483398) < 1e-6
                     && std::abs (m.rlb.a2 - 0.99007225036621) < 1e-6;
        check (ok, "K-weighting coefficients match BS.1770 @ 48 kHz");
    }

    // 2. EBU Tech 3341: стерео 1 кГц синус -23 dBFS → -23.0 LUFS
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        SpacenerdMasterProcessor p; allOff (p);
        const float a = sn::dbToGain (-23.0f);
        auto out = run (p, fs, 20, [&] (AudioBuffer<float>& b, int64 start)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float v = a * (float) std::sin (2.0 * MathConstants<double>::pi * 1000.0 * (double) (start + i) / fs);
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
        });
        const float I = p.loudness.integrated.load(), S = p.loudness.shortTerm.load();
        check (std::abs (I + 23.0f) < 0.15f && std::abs (S + 23.0f) < 0.15f,
               "Loudness @ " + String (fs) + " Hz: I=" + String (I, 2) + " S=" + String (S, 2) + " (expect -23.0)");
    }

    // 3. Лімітер: гучний шум +12 dB у лімітер, стеля -1 dBFS → жодного семпла вище стелі
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        SpacenerdMasterProcessor p; allOff (p);
        setParam (p, ParamIDs::limOn, 1.0f);
        setParam (p, ParamIDs::limGain, 12.0f);
        setParam (p, ParamIDs::ceiling, -1.0f);
        std::mt19937 rng (1);
        std::normal_distribution<float> nd (0.0f, 0.25f);
        auto out = run (p, fs, 10, [&] (AudioBuffer<float>& b, int64)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (ch, i, nd (rng));
        });
        const float peak = std::max (out.getMagnitude (0, 0, out.getNumSamples()), out.getMagnitude (1, 0, out.getNumSamples()));

        // Оцінка true peak: 4x лінійна... використовуємо JUCE oversampling FIR для перевірки
        dsp::Oversampling<float> tp (2, 2, dsp::Oversampling<float>::filterHalfBandFIREquiripple, true);
        tp.initProcessing ((size_t) out.getNumSamples());
        dsp::AudioBlock<float> blk (out);
        auto up = tp.processSamplesUp (blk);
        float tpk = 0.0f;
        for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
            for (size_t i = 0; i < up.getNumSamples(); ++i) tpk = std::max (tpk, std::abs (up.getSample ((int) ch, (int) i)));

        check (peak <= sn::dbToGain (-1.0f) + 1e-6f && tpk <= sn::dbToGain (-0.9f),
               "Limiter @ " + String (fs) + ": sample peak " + String (sn::gainToDb (peak), 3)
               + " dBFS, true peak ~" + String (sn::gainToDb (tpk), 2) + " dBTP (ceiling -1.0, tolerance 0.1)");
    }

    // 4. Затримка, заявлена хосту, = реальна (імпульс, налаштування за замовчуванням)
    {
        SpacenerdMasterProcessor p;
        auto out = run (p, 48000.0, 1, [&] (AudioBuffer<float>& b, int64 start)
        {
            b.clear();
            if (start <= 1000 && start + b.getNumSamples() > 1000)
                for (int ch = 0; ch < 2; ++ch) b.setSample (ch, (int) (1000 - start), 0.1f);
        });
        int argmax = 0; float best = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i)
            if (std::abs (out.getSample (0, i)) > best) { best = std::abs (out.getSample (0, i)); argmax = i; }
        const int measured = argmax - 1000;
        check (std::abs (measured - p.getLatencySamples()) <= 1,
               "Latency reported " + String (p.getLatencySamples()) + ", measured " + String (measured)
               + " samples; impulse gain " + String (sn::gainToDb (best / 0.1f), 2) + " dB");
    }

    // 5. Компресор: синус -6 dBFS, поріг -12, 2:1, knee 0 → GR близько 3 dB
    {
        SpacenerdMasterProcessor p; allOff (p);
        setParam (p, ParamIDs::compOn, 1.0f);
        setParam (p, ParamIDs::threshold, -12.0f);
        setParam (p, ParamIDs::ratio, 2.0f);
        setParam (p, ParamIDs::knee, 0.0f);
        setParam (p, ParamIDs::scHpf, 20.0f);
        const float a = sn::dbToGain (-6.0f);
        auto out = run (p, 48000.0, 3, [&] (AudioBuffer<float>& b, int64 start)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float v = a * (float) std::sin (2.0 * MathConstants<double>::pi * 1000.0 * (double) (start + i) / 48000.0);
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
        });
        const int n = out.getNumSamples();
        const float outPk = out.getMagnitude (0, n - 48000, 48000);
        const float gr = -6.0f - sn::gainToDb (outPk);
        check (gr > 1.0f && gr < 3.5f, "Compressor steady GR on -6 dBFS sine: " + String (gr, 2) + " dB (static max 3.0)");
    }

    // 6. Усе ввімкнено, екстремальні налаштування: без NaN/Inf, вихід під стелею
    {
        SpacenerdMasterProcessor p;
        for (auto id : { ParamIDs::eqOn, ParamIDs::compOn, ParamIDs::satOn, ParamIDs::widthOn, ParamIDs::limOn })
            setParam (p, id, 1.0f);
        setParam (p, ParamIDs::inGain, 24.0f);
        setParam (p, ParamIDs::lowGain, 12.0f);   setParam (p, ParamIDs::highGain, 12.0f);
        setParam (p, ParamIDs::hpfFreq, 300.0f);  setParam (p, ParamIDs::highFreq, 20000.0f);
        setParam (p, ParamIDs::ratio, 10.0f);     setParam (p, ParamIDs::attack, 0.1f);
        setParam (p, ParamIDs::drive, 100.0f);    setParam (p, ParamIDs::width, 200.0f);
        setParam (p, ParamIDs::monoBass, 300.0f); setParam (p, ParamIDs::limGain, 18.0f);
        setParam (p, ParamIDs::ceiling, -0.3f);   setParam (p, ParamIDs::limRel, 1.0f);
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> ud (-1.0f, 1.0f);

        const auto t0 = std::chrono::steady_clock::now();
        auto out = run (p, 44100.0, 30, [&] (AudioBuffer<float>& b, int64)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (ch, i, ud (rng));
        });
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();

        bool finite = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < out.getNumSamples(); ++i)
                finite &= std::isfinite (out.getSample (ch, i));
        const float pk = std::max (out.getMagnitude (0, 0, out.getNumSamples()), out.getMagnitude (1, 0, out.getNumSamples()));
        check (finite && pk <= sn::dbToGain (-0.3f) + 1e-6f,
               "Extreme settings: finite=" + String (finite ? "yes" : "no") + ", peak " + String (sn::gainToDb (pk), 2) + " dBFS");
        std::cout << "       CPU: 30 s of audio in " << secs << " s (" << (30.0 / secs) << "x realtime, this machine)\n";
    }

    // 6b. Хост дає блок більший за оголошений
    {
        SpacenerdMasterProcessor p;
        p.prepareToPlay (48000.0, 64);
        AudioBuffer<float> b (2, 1000); MidiBuffer m;
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 1000; ++i) b.setSample (ch, i, 0.3f * std::sin (0.05f * (float) i));
        p.processBlock (b, m);
        check (std::isfinite (b.getMagnitude (0, 0, 1000)), "Oversized host block handled");
    }

    // 8. Частотна характеристика тракту (усе вимкнено): 10 кГц і 18 кГц @ 44.1 кГц
    for (double f : { 100.0, 10000.0, 18000.0 })
    {
        SpacenerdMasterProcessor p; allOff (p);
        auto out = run (p, 44100.0, 1, [&] (AudioBuffer<float>& b, int64 start)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float v = 0.5f * (float) std::sin (2.0 * MathConstants<double>::pi * f * (double) (start + i) / 44100.0);
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
        });
        const float lvl = sn::gainToDb (out.getMagnitude (0, 22050, 22050) / 0.5f);
        check (std::abs (lvl) < 0.2f, "Flat response @ " + String (f) + " Hz: " + String (lvl, 3) + " dB");
    }

    // 9. Пресети: застосовуються, звучать без збоїв, тримають стелю
    {
        SpacenerdMasterProcessor probe;
        for (int i = 0; i < probe.getNumPrograms(); ++i)
        {
            SpacenerdMasterProcessor p;
            p.setCurrentProgram (i);
            std::mt19937 rng ((unsigned) i);
            std::normal_distribution<float> nd (0.0f, 0.2f);
            auto out = run (p, 48000.0, 20, [&] (AudioBuffer<float>& b, int64 start)
            {
                for (int k = 0; k < b.getNumSamples(); ++k)
                {
                    const double t = (double) (start + k) / 48000.0;
                    const float bass = 0.3f * (float) std::sin (2.0 * MathConstants<double>::pi * 55.0 * t);
                    b.setSample (0, k, bass + nd (rng)); b.setSample (1, k, bass + nd (rng));
                }
            });
            const int n = out.getNumSamples();
            const float pk = std::max (out.getMagnitude (0, 0, n), out.getMagnitude (1, 0, n));
            const float thr = p.apvts.getRawParameterValue (ParamIDs::threshold)->load();
            const float expectThr = i == 0 ? -12.0f : thr;
            check (pk <= sn::dbToGain (-1.0f) + 1e-6f && std::abs (thr - expectThr) < 0.01f && p.getCurrentProgram() == i,
                   "Preset '" + p.getProgramName (i) + "': peak " + String (sn::gainToDb (pk), 2)
                   + " dBFS, " + String (p.loudness.integrated.load(), 1) + " LUFS");
        }
    }


    // 10. EQ без cramping: high shelf +6 dB @ 14 кГц, порівняння з аналоговим прототипом RBJ @ 18 кГц (fs 44.1)
    {
        SpacenerdMasterProcessor p; allOff (p);
        setParam (p, ParamIDs::eqOn, 1.0f);
        setParam (p, ParamIDs::hpfFreq, 10.0f);
        setParam (p, ParamIDs::highFreq, 14000.0f);
        setParam (p, ParamIDs::highGain, 6.0f);
        const double f = 18000.0, fs = 44100.0;
        auto out = run (p, fs, 1, [&] (AudioBuffer<float>& b, int64 start)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float v = 0.25f * (float) std::sin (2.0 * MathConstants<double>::pi * f * (double) (start + i) / fs);
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
        });
        const float measured = sn::gainToDb (out.getMagnitude (0, 22050, 22050) / 0.25f);
        // Аналоговий high shelf (RBJ): H(s) = A * (A s^2 + sqrt(A)/Q s + 1) / (s^2 + sqrt(A)/Q s + A)
        const double A = std::pow (10.0, 6.0 / 40.0), Q = 0.707, w = f / 14000.0;
        const std::complex<double> sj (0.0, w);
        const auto H = A * (A * sj * sj + std::sqrt (A) / Q * sj + 1.0) / (sj * sj + std::sqrt (A) / Q * sj + A);
        const float analog = (float) (20.0 * std::log10 (std::abs (H)));
        check (std::abs (measured - analog) < 0.5f,
               "High shelf @ 18 kHz: " + String (measured, 2) + " dB vs analog " + String (analog, 2) + " dB (no cramping)");
    }

    // 11. Gain Match: гучність виходу вирівнюється з входом
    {
        SpacenerdMasterProcessor p;
        p.setCurrentProgram (2);                   // Modern Stoner: гучніше за вхід
        setParam (p, ParamIDs::gainMatch, 1.0f);
        sn::LoudnessMeter inRef; inRef.prepare (48000.0, 2);
        std::mt19937 rng (3);
        std::normal_distribution<float> nd (0.0f, 0.08f);
        auto out = run (p, 48000.0, 15, [&] (AudioBuffer<float>& b, int64 start)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double t = (double) (start + i) / 48000.0;
                const float v = 0.2f * (float) std::sin (2.0 * MathConstants<double>::pi * 82.0 * t) + nd (rng);
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
            inRef.process (b);
        });
        sn::LoudnessMeter outM; outM.prepare (48000.0, 2);
        AudioBuffer<float> tail (2, 48000 * 4);
        for (int ch = 0; ch < 2; ++ch) tail.copyFrom (ch, 0, out, ch, out.getNumSamples() - 48000 * 4, 48000 * 4);
        outM.process (tail);
        const float diff = outM.shortTerm.load() - inRef.shortTerm.load();
        check (std::abs (diff) < 1.0f, "Gain Match: out-in loudness " + String (diff, 2) + " LU (match "
               + String (p.matchDb.load(), 1) + " dB, meters see " + String (p.loudness.shortTerm.load(), 1) + " LUFS)");
    }

    // 12. Типи сатурації: на опорному рівні -6 dBFS гучність однакова, на гучному — без збоїв
    {
        float lv[3] {};
        for (int type = 0; type < 3; ++type)
        {
            for (float amp : { 0.5f, 0.95f })
            {
                SpacenerdMasterProcessor p; allOff (p);
                setParam (p, ParamIDs::satOn, 1.0f);
                setParam (p, ParamIDs::satType, (float) type);
                setParam (p, ParamIDs::drive, 60.0f);
                auto out = run (p, 44100.0, 1, [&] (AudioBuffer<float>& b, int64 start)
                {
                    for (int i = 0; i < b.getNumSamples(); ++i)
                    {
                        const float v = amp * (float) std::sin (2.0 * MathConstants<double>::pi * 1000.0 * (double) (start + i) / 44100.0);
                        b.setSample (0, i, v); b.setSample (1, i, v);
                    }
                });
                const float r = out.getRMSLevel (0, 22050, 22050) / (amp * 0.70710678f);
                if (amp == 0.5f) lv[type] = sn::gainToDb (r);
                else check (std::isfinite (r) && r > 0.3f && r < 1.5f, "Sat type " + String (type) + " @ -0.4 dBFS: gain " + String (sn::gainToDb (r), 2) + " dB");
            }
        }
        const float spread = std::max ({ lv[0], lv[1], lv[2] }) - std::min ({ lv[0], lv[1], lv[2] });
        check (spread < 1.0f, "Sat types level-matched @ -6 dBFS: tube " + String (lv[0], 2) + ", tape " + String (lv[1], 2)
               + ", soft " + String (lv[2], 2) + " dB");
    }

    // 7. Стан зберігається і відновлюється
    {
        SpacenerdMasterProcessor a, b;
        setParam (a, ParamIDs::threshold, -20.5f);
        setParam (a, ParamIDs::satOn, 1.0f);
        MemoryBlock mb; a.getStateInformation (mb);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        const float t = b.apvts.getRawParameterValue (ParamIDs::threshold)->load();
        const float s = b.apvts.getRawParameterValue (ParamIDs::satOn)->load();
        check (std::abs (t + 20.5f) < 0.01f && s > 0.5f, "State save/restore");
    }

    // Референс A/B: вирівнювання гучності і синхронізація з позицією хоста
    {
        struct Head final : AudioPlayHead
        {
            int64 pos = 0;
            Optional<PositionInfo> getPosition() const override
            {
                PositionInfo i; i.setIsPlaying (true); i.setTimeInSamples (pos); return i;
            }
        } head;

        auto file = File::getSpecialLocation (File::tempDirectory).getChildFile ("sn_ref_test.wav");
        {
            AudioBuffer<float> r (2, 48000 * 6);
            Random rnd (7);
            for (int i = 0; i < r.getNumSamples(); ++i)
            {
                const float v = 0.1f * (float) std::sin (2.0 * MathConstants<double>::pi * 220.0 * i / 48000.0) + 0.02f * (rnd.nextFloat() - 0.5f);
                r.setSample (0, i, v); r.setSample (1, i, v);
            }
            file.deleteFile();
            WavAudioFormat wav;
            std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (new FileOutputStream (file), 48000.0, 2, 24, {}, 0));
            w->writeFromAudioSampleBuffer (r, 0, r.getNumSamples());
        }
        auto track = RefTrack::load (file, 48000.0);
        check (track != nullptr && std::abs (track->lufs + 23.0f) < 3.0f,
               "Reference loads, loudness " + String (track ? track->lufs : 0.0f, 1) + " LUFS");

        SpacenerdMasterProcessor p;
        allOff (p);
        p.setPlayHead (&head);
        p.setReferenceTrack (std::move (track));
        const auto mixGen = [] (AudioBuffer<float>& b, int64 start)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float v = 0.4f * (float) std::sin (2.0 * MathConstants<double>::pi * 110.0 * (double) (start + i) / 48000.0);
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
        };
        p.setRateAndBufferSizeDetails (48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        MidiBuffer midi;
        sn::LoudnessMeter lm; lm.prepare (48000.0, 2);
        for (int pass = 0; pass < 2; ++pass)
        {
            setParam (p, ParamIDs::refAB, pass == 1 ? 1.0f : 0.0f);
            lm.requestReset();
            for (int k = 0; k < 48000 * 4 / 512; ++k)
            {
                AudioBuffer<float> b (2, 512);
                mixGen (b, head.pos);
                p.processBlock (b, midi);
                head.pos += 512;
                if (k > 40) lm.process (b);
            }
            static float mixL = 0.0f;
            if (pass == 0) mixL = lm.integrated.load();
            else check (std::abs (lm.integrated.load() - mixL) < 1.0f,
                        "Reference A/B loudness-matched: mix " + String (mixL, 1) + ", ref " + String (lm.integrated.load(), 1) + " LUFS");
        }
        file.deleteFile();
    }

    // Listen On: Phone ріже бас, Mono зводить канали
    {
        const auto level = [] (int mode, double hz, bool antiphase)
        {
            SpacenerdMasterProcessor p;
            allOff (p);
            setParam (p, ParamIDs::monitor, (float) mode);
            auto out = run (p, 48000.0, 2, [hz, antiphase] (AudioBuffer<float>& b, int64 start)
            {
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const float v = 0.3f * (float) std::sin (2.0 * MathConstants<double>::pi * hz * (double) (start + i) / 48000.0);
                    b.setSample (0, i, v); b.setSample (1, i, antiphase ? -v : v);
                }
            });
            return sn::gainToDb (out.getRMSLevel (0, 48000, 48000) / (0.3f / std::sqrt (2.0f)));
        };
        const float studio = level (0, 60.0, false), phone = level (1, 60.0, false), mono = level (4, 1000.0, true);
        check (std::abs (studio) < 0.5f && phone < -20.0f && mono < -40.0f,
               "Listen On: studio " + String (studio, 1) + " dB, phone @60 Hz " + String (phone, 1) + " dB, mono antiphase " + String (mono, 1) + " dB");
    }

    // Stream: гучний майстер стишується до цілі, тихий піднімається лише до -1 dBTP
    {
        const auto streamed = [] (float amp, bool spiky)
        {
            SpacenerdMasterProcessor p;
            allOff (p);
            setParam (p, ParamIDs::monitor, 5.0f);
            setParam (p, ParamIDs::loudTarget, 0.0f);                  // Spotify -14
            Random rnd (5);
            auto out = run (p, 48000.0, 12, [amp, spiky, &rnd] (AudioBuffer<float>& b, int64 start)
            {
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const double t = (double) (start + i) / 48000.0;
                    float v = amp * (float) (std::sin (2.0 * MathConstants<double>::pi * 220.0 * t) + 0.3 * (rnd.nextFloat() * 2 - 1));
                    if (spiky && std::fmod (t, 0.5) < 0.004) v += 0.25f * (float) std::sin (2.0 * MathConstants<double>::pi * 1000.0 * t);   // гострі піки
                    b.setSample (0, i, v); b.setSample (1, i, v);
                }
            });
            sn::LoudnessMeter lm; lm.prepare (48000.0, 2);
            AudioBuffer<float> tail (out.getArrayOfWritePointers(), 2, 48000 * 6, 48000 * 6);
            lm.process (tail);
            return std::make_pair (lm.integrated.load(), sn::gainToDb (tail.getMagnitude (0, 0, tail.getNumSamples())));
        };
        const auto loud = streamed (0.7f, false), quiet = streamed (0.02f, true);
        check (std::abs (loud.first + 14.0f) < 1.0f && quiet.first < -14.5f && quiet.second <= -0.9f,
               "Stream: loud master -> " + String (loud.first, 1) + " LUFS; quiet master lifted only to peak " + String (quiet.second, 1) + " dB (" + String (quiet.first, 1) + " LUFS)");
    }

    // Альбом: збереження, порівняння, поради
    {
        Album::dirOverride() = File::getSpecialLocation (File::tempDirectory).getChildFile ("sn_album_test");
        Album::dirOverride().deleteRecursively();
        Album a; a.name = "Test Record";
        AlbumSong s1 { "One", -9.0f, -1.0f, {} }, s2 { "Two", -9.4f, -1.2f, {} }, s3 { "Three", -11.5f, -1.0f, {} };
        for (int b = 0; b < an::kBands; ++b) s3.tone[(size_t) b] = an::bandHz[(size_t) b] < 160.0f ? 3.0f : 0.0f;    // важчий низ
        a.put (s1); a.put (s2); a.put (s3);
        a.save();
        Album b; b.load ("Test Record");
        const auto d = b.compare (s3);
        const auto tips = Album::advice (d);
        check (b.songs.size() == 3 && std::abs (d.lufs + 2.3f) < 0.2f && d.lows > 2.5f && tips.size() >= 2,
               "Album: saved/loaded, song 3 is " + String (d.lufs, 1) + " LU and lows +" + String (d.lows, 1) + " dB vs others; tips: " + tips.joinIntoString (" | "));
        Album::dirOverride().deleteRecursively();
    }

    // Знімок інтерфейсу (потрібен X-сервер)
    if (argc > 1)
    {
        SpacenerdMasterProcessor p;
        p.setCurrentProgram (2);
        run (p, 48000.0, 4, [] (AudioBuffer<float>& b, int64 start)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float v = 0.5f * (float) std::sin (2.0 * MathConstants<double>::pi * 110.0 * (double) (start + i) / 48000.0);
                b.setSample (0, i, v); b.setSample (1, i, v * 0.8f);
            }
        });
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        auto* e = dynamic_cast<SpacenerdMasterEditor*> (ed.get());
        // Кілька кадрів метра з реальними рівнями
        for (int f = 0; f < 10; ++f)
        {
            p.inPeak[0].push (0.5f); p.inPeak[1].push (0.4f);
            p.outPeak[0].push (0.85f); p.outPeak[1].push (0.8f);
            p.compGr.push (3.2f); p.limGr.push (1.4f);
            for (auto* child : e->getChildren())
                if (auto* mc = dynamic_cast<MainContent*> (child)) mc->tick();
        }
        auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 1.5f);
        File f (argv[1]);
        f.deleteFile();
        FileOutputStream os (f);
        PNGImageFormat().writeImageToStream (img, os);
        std::cout << "Snapshot: " << f.getFullPathName() << std::endl;

        // Альбом
        Album::dirOverride() = File::getSpecialLocation (File::tempDirectory).getChildFile ("sn_album_snap");
        Album::dirOverride().deleteRecursively();
        Album a; a.name = "Desert Sessions";
        AlbumSong s1 { "Sandstorm", -9.0f, -1.0f, {} }, s2 { "Mirage", -9.6f, -1.3f, {} };
        s2.tone[2] = 2.0f;
        a.put (s1); a.put (s2); a.save();
        p.apvts.state.setProperty ("album", "Desert Sessions", nullptr);
        p.apvts.state.setProperty ("albumSong", "Dune Rider", nullptr);
        for (int i = 0; i < 200; ++i) p.analysis.tick();
        AlbumView av (p);
        auto aimg = av.createComponentSnapshot (av.getLocalBounds(), true, 1.5f);
        FileOutputStream aos (f.getSiblingFile ("album.png")); aos.setPosition (0); aos.truncate();
        PNGImageFormat().writeImageToStream (aimg, aos);
        Album::dirOverride().deleteRecursively();
    }

    std::cout << (failures == 0 ? "ALL PASSED" : String (failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
