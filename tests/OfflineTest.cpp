// Офлайн-тест DSP: гучність, лімітер, затримка, стабільність. Опційно — знімок інтерфейсу.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <juce_events/juce_events.h>
#include <iostream>
#include <chrono>
#include <random>

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

    // Знімок інтерфейсу (потрібен X-сервер)
    if (argc > 1)
    {
        SpacenerdMasterProcessor p;
        setParam (p, ParamIDs::satOn, 1.0f);
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
            static_cast<MainContent*> (e->getChildComponent (0))->meters.update();
        }
        auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 1.5f);
        File f (argv[1]);
        f.deleteFile();
        FileOutputStream os (f);
        PNGImageFormat().writeImageToStream (img, os);
        std::cout << "Snapshot: " << f.getFullPathName() << std::endl;
    }

    std::cout << (failures == 0 ? "ALL PASSED" : String (failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
