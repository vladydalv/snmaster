// Тест аналізатора і Assist у Master: «поганий» мікс → вердикти → Learn → Assist → ближче до цілі.
#include "../Master/PluginProcessor.h"
#include "../Master/PluginEditor.h"
#include "../Common/Analysis.h"
#include <iostream>
#include <random>

using namespace juce;
constexpr double fs = 48000.0;
static int failures = 0;
static void check (bool ok, const String& what) { std::cout << (ok ? "[ OK ] " : "[FAIL] ") << what << std::endl; if (! ok) ++failures; }

/** Реалістичний за спектром «мікс»: шум із балансом типового рок-мастера + навмисні вади
    (+7 дБ «вати» на 250 Гц, широкий бас, дуже тихо), ритмічна модуляція як від барабанів. */
struct BadMix
{
    static constexpr int N = 1 << 19;
    AudioBuffer<float> data { 2, N };
    int pos = 0;

    BadMix()
    {
        std::mt19937 rng (5);
        std::uniform_real_distribution<float> ph (0.0f, MathConstants<float>::twoPi);
        dsp::FFT fft (19);
        sn::Biquad mud; mud.setPeak (48000.0, 250.0, 1.0, 7.0);
        const auto base = an::targetCurve (1995.0f, 1);   // баланс стоунера 90-х
        std::vector<float> spec[2];
        std::vector<float> phase0 ((size_t) N, 0.0f);
        for (int ch = 0; ch < 2; ++ch)
        {
            spec[ch].assign ((size_t) N * 2, 0.0f);
            for (int k = 1; k < N / 2; ++k)
            {
                const float f = (float) (k * fs / N);
                if (f < 25.0f || f > 20000.0f) continue;
                // інтерполяція кривої (дБ на третину октави) у log-частоті
                int b = 0; while (b < an::kBands - 2 && an::bandHz[(size_t) b + 1] < f) ++b;
                const float t = jlimit (0.0f, 1.0f, std::log (f / an::bandHz[(size_t) b]) / std::log (an::bandHz[(size_t) b + 1] / an::bandHz[(size_t) b]));
                float db = base[(size_t) b] + t * (base[(size_t) b + 1] - base[(size_t) b]);
                db += (float) mud.magnitudeDb (fs, f);
                const float mag = std::pow (10.0f, db / 20.0f) / std::sqrt (f);
                // Широкий бас: нижче 150 Гц канали мають незалежні фази
                const float phase = (ch == 1 && f > 150.0f) ? phase0[(size_t) k] : ph (rng);
                if (ch == 0) phase0[(size_t) k] = phase;
                spec[ch][(size_t) (2 * k)] = mag;          // тимчасово: модуль
                spec[ch][(size_t) (2 * k + 1)] = phase;    // і фаза
            }
            for (int k = 0; k < N / 2; ++k)
            {
                const float m = spec[ch][(size_t) (2 * k)], a = spec[ch][(size_t) (2 * k + 1)];
                spec[ch][(size_t) (2 * k)] = m * std::cos (a);
                spec[ch][(size_t) (2 * k + 1)] = m * std::sin (a);
            }
        }
        for (int ch = 0; ch < 2; ++ch)
        {
            fft.performRealOnlyInverseTransform (spec[ch].data());
            for (int i = 0; i < N; ++i)
            {
                const double s = i / fs, beat = std::fmod (s, 0.25);
                const float env = 0.6f + 0.8f * (float) std::exp (-beat * 18.0);   // «барабани»
                data.setSample (ch, i, spec[ch][(size_t) i] * env);
            }
        }
        // Дуже тихо: -30 LUFS
        sn::LoudnessMeter m; m.prepare (fs, 2); m.process (data);
        data.applyGain (sn::dbToGain (-30.0f - m.integrated.load()));
        sn::LoudnessMeter m2; m2.prepare (fs, 2); m2.process (data);
        int argmax = 0; float pk = 0.0f;
        for (int i = 0; i < N; ++i) if (std::abs (data.getSample (0, i)) > pk) { pk = std::abs (data.getSample (0, i)); argmax = i; }
        std::cout << "Test mix: " << m2.integrated.load() << " LUFS, peak " << sn::gainToDb (pk) << " dBFS at sample " << argmax << std::endl;
    }

    void fill (AudioBuffer<float>& b)
    {
        for (int i = 0; i < b.getNumSamples(); ++i, pos = (pos + 1) % N)
            for (int ch = 0; ch < 2; ++ch) b.setSample (ch, i, data.getSample (ch, pos));
    }
};

int main()
{
    ScopedJuceInitialiser_GUI init;
    SpacenerdMasterProcessor p;
    for (auto id : { ParamIDs::eqOn, ParamIDs::compOn, ParamIDs::widthOn })
        p.apvts.getParameter (id)->setValueNotifyingHost (0.0f);   // спершу «сирий» мікс
    p.apvts.getParameter (ParamIDs::limOn)->setValueNotifyingHost (0.0f);
    p.setRateAndBufferSizeDetails (fs, 512);
    p.prepareToPlay (fs, 512);

    std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
    MainContent* mc = nullptr;
    for (auto* c : ed->getChildren()) if (auto* m = dynamic_cast<MainContent*> (c)) mc = m;
    auto& an = mc->analyzer;

    BadMix mix;
    MidiBuffer midi;
    auto play = [&] (double seconds)
    {
        const int blocks = (int) (seconds * fs / 512);
        for (int k = 0; k < blocks; ++k)
        {
            AudioBuffer<float> b (2, 512);
            mix.fill (b);
            p.processBlock (b, midi);
            if (k % 3 == 0) { p.analysis.tick(); an.tick(); }    // ≈ 31 Гц, як таймер процесора
        }
    };

    play (12.0);
    std::cout << "Master sees: " << p.loudness.integrated.load() << " LUFS, TP " << sn::gainToDb (p.truePeakMax.load()) << std::endl;
    const auto before = an.getReport();
    std::cout << "Before (match " << before.matchPercent << "%):" << std::endl;
    bool mudFound = false, quietFound = false, monoFound = false;
    for (auto& v : before.verdicts)
    {
        std::cout << "   - " << v.text << std::endl;
        mudFound |= v.text.contains ("Muddy");
        quietFound |= v.text.contains ("Quiet");
        monoFound |= v.text.contains ("mono");
    }
    check (mudFound && quietFound && monoFound, "Analyzer finds mud, low loudness and wide bass");

    // Стабільність: оцінка всього треку не «стрибає» (два виміри з інтервалом 3 с)
    {
        const int m1 = an.getReport().matchPercent;
        play (3.0);
        const int m2 = an.getReport().matchPercent;
        check (std::abs (m1 - m2) <= 2, "Whole-track verdict is stable: " + String (m1) + "% -> " + String (m2) + "%");
        check (an.listenedSeconds() > 14.0, "Listened time accumulates: " + String (an.listenedSeconds(), 1) + " s");
    }
    an.applyAssist();
    p.resetMeters();
    play (24.0);
    const auto after = an.getReport();
    std::cout << "After Assist (match " << after.matchPercent << "%):" << std::endl;
    for (auto& v : after.verdicts) std::cout << "   - " << v.text << std::endl;
    const float midGain = p.apvts.getRawParameterValue (ParamIDs::midGain)->load();
    const float midFreq = p.apvts.getRawParameterValue (ParamIDs::midFreq)->load();
    std::cout << "   EQ mid " << midGain << " dB @ " << midFreq << " Hz, limiter gain "
              << p.apvts.getRawParameterValue (ParamIDs::limGain)->load() << " dB, mono bass "
              << p.apvts.getRawParameterValue (ParamIDs::monoBass)->load() << " Hz" << std::endl;
    std::cout << "   loudness after Assist: " << p.analysis.outLufs() << " LUFS (target " << p.analysis.loudTargetLufs() << ")" << std::endl;
    check (std::abs (p.analysis.outLufs() - p.analysis.loudTargetLufs()) < 1.0f, "Assist lands on the streaming loudness target");
    check (after.matchPercent > before.matchPercent + 15, "Assist improves match: " + String (before.matchPercent) + "% -> " + String (after.matchPercent) + "%");
    check (midGain < -2.0f && midFreq > 150.0f && midFreq < 450.0f, "Assist cuts the mud region");

    // Чи рухається крива YOUR MIX від ручок (а не намальована): LOW GAIN +12 і HIGH GAIN -12
    {
        auto band = [] (const an::Spectrum& sp, float f) { for (int b = 0; b < an::kBands; ++b) if (an::bandHz[(size_t) b] >= f) return sp[(size_t) b]; return 0.0f; };
        const auto before2 = an.mixSpectrum();
        p.apvts.getParameter (ParamIDs::limOn)->setValueNotifyingHost (0.0f);
        play (3.0);
        const auto ref = an::normalise (an.mixSpectrum());
        auto* lg = p.apvts.getParameter (ParamIDs::lowGain);  lg->setValueNotifyingHost (lg->convertTo0to1 (12.0f));
        auto* lf = p.apvts.getParameter (ParamIDs::lowFreq);  lf->setValueNotifyingHost (lf->convertTo0to1 (100.0f));
        auto* hg = p.apvts.getParameter (ParamIDs::highGain); hg->setValueNotifyingHost (hg->convertTo0to1 (-12.0f));
        play (1.0);
        const auto after1s = an::normalise (an.mixSpectrum());
        play (2.0);
        const auto after3s = an::normalise (an.mixSpectrum());
        std::cout << "   63 Hz: " << band (ref, 63) << " -> 1s " << band (after1s, 63) << " -> 3s " << band (after3s, 63) << " dB" << std::endl;
        std::cout << "   12.5 kHz: " << band (ref, 12500) << " -> 1s " << band (after1s, 12500) << " -> 3s " << band (after3s, 12500) << " dB" << std::endl;
        juce::ignoreUnused (before2);
        check (band (after1s, 63) - band (ref, 63) > 6.0f && band (ref, 12500) - band (after1s, 12500) > 6.0f,
               "Mix curve follows EQ within 1 s");
    }

    // Знімок
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
    File file ("/home/claude/master.png"); file.deleteFile();
    FileOutputStream os (file);
    PNGImageFormat().writeImageToStream (img, os);

    std::cout << (failures == 0 ? "ALL PASSED" : String (failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
