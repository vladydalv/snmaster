// Аудит: чи кожна ручка Master реально змінює звук (на міксоподібному стерео-сигналі).
#include "../Master/PluginProcessor.h"
#include <iostream>
#include <random>

using namespace juce;

constexpr double fs = 48000.0;
constexpr double seconds = 4.0;

static AudioBuffer<float> makeMix()
{
    const int n = (int) (fs * seconds);
    AudioBuffer<float> b (2, n);
    std::mt19937 rng (1);
    std::normal_distribution<float> nd (0.0f, 1.0f);
    sn::Biquad hatL, hatR; hatL.setHighPass (fs, 7000.0); hatR.setHighPass (fs, 7000.0);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        const double beat = std::fmod (t, 0.5);
        const float kick = 0.5f * (float) (std::sin (2.0 * MathConstants<double>::pi * 55.0 * beat) * std::exp (-beat * 12.0));
        const double sb = std::fmod (t + 0.25, 0.5);
        const float snare = 0.25f * nd (rng) * (float) std::exp (-sb * 25.0);
        const float bass = 0.18f * (float) std::sin (2.0 * MathConstants<double>::pi * 82.4 * t);
        float gtr = 0.0f;
        for (int h = 1; h <= 10; ++h)
            gtr += 0.05f / (float) h * (float) (std::sin (2.0 * MathConstants<double>::pi * 164.8 * h * t)
                                              + std::sin (2.0 * MathConstants<double>::pi * 246.9 * h * t));
        const float hl = 0.05f * hatL.process (nd (rng)), hr = 0.05f * hatR.process (nd (rng));
        b.setSample (0, i, kick + snare + bass + gtr * 1.2f + hl);
        b.setSample (1, i, kick + snare + bass + gtr * 0.8f + hr);
    }
    return b;
}

static AudioBuffer<float> process (const AudioBuffer<float>& in, std::function<void (SpacenerdMasterProcessor&)> setup)
{
    SpacenerdMasterProcessor p;
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

static void set (SpacenerdMasterProcessor& p, const String& id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

static float diffDb (const AudioBuffer<float>& a, const AudioBuffer<float>& b)
{
    const int from = (int) (fs * 1.0), n = a.getNumSamples() - from;
    double e = 0.0, r = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = from; i < from + n; ++i)
        {
            const double d = a.getSample (ch, i) - b.getSample (ch, i);
            e += d * d; r += (double) b.getSample (ch, i) * b.getSample (ch, i);
        }
    return (float) (10.0 * std::log10 (std::max (e, 1e-30) / std::max (r, 1e-30)));
}

int main()
{
    ScopedJuceInitialiser_GUI init;
    const auto mix = makeMix();

    // Кожну ручку перевіряємо в контексті, де вона має працювати
    auto context = [] (SpacenerdMasterProcessor& p, const String& id)
    {
        if (id == ParamIDs::lowFreq)                              set (p, ParamIDs::lowGain, 6.0f);
        if (id == ParamIDs::midFreq || id == ParamIDs::midQ)      set (p, ParamIDs::midGain, 6.0f);
        if (id == ParamIDs::highFreq)                             set (p, ParamIDs::highGain, 6.0f);
        if (id == ParamIDs::eqOn)                                 set (p, ParamIDs::midGain, 6.0f);
        if (id.startsWith ("sat") || id == ParamIDs::drive)       set (p, ParamIDs::satOn, id == ParamIDs::satOn ? 0.0f : 1.0f);
        if (id == ParamIDs::widthOn)                              set (p, ParamIDs::width, 150.0f);
        if (id == ParamIDs::compAuto)                             { set (p, ParamIDs::threshold, -24.0f); set (p, ParamIDs::ratio, 4.0f); }
        if (id == ParamIDs::limOn || id == ParamIDs::ceiling || id == ParamIDs::limRel) set (p, ParamIDs::limGain, 8.0f);
    };

    SpacenerdMasterProcessor probe;
    int dead = 0;
    for (auto* prm : probe.getParameters())
    {
        auto* r = dynamic_cast<RangedAudioParameter*> (prm);
        if (r == nullptr || r->getParameterID() == ParamIDs::gainMatch) continue;
        const auto id = r->getParameterID();
        const auto ref = process (mix, [&] (SpacenerdMasterProcessor& p) { context (p, id); });
        const float cur = [&] { SpacenerdMasterProcessor q; context (q, id); return q.apvts.getRawParameterValue (id)->load(); }();
        const float lo = r->convertFrom0to1 (0.0f), hi = r->convertFrom0to1 (1.0f);

        float worst = -300.0f;
        String detail;
        for (float v : { lo, hi })
        {
            if (std::abs (v - cur) < 1e-6f) continue;
            const auto out = process (mix, [&] (SpacenerdMasterProcessor& p) { context (p, id); set (p, id, v); });
            const float d = diffDb (out, ref);
            worst = std::max (worst, d);
            detail << " [" << String (v, 2) << ": " << String (d, 1) << " dB]";
        }
        const bool ok = worst > -40.0f;
        if (! ok) ++dead;
        std::cout << (ok ? "[ OK ] " : "[DEAD] ") << id << detail << std::endl;
    }
    const auto ref = process (mix, [] (SpacenerdMasterProcessor& p) { set (p, ParamIDs::satOn, 1.0f); });
    auto baseSetup = [] (SpacenerdMasterProcessor& p) { set (p, ParamIDs::satOn, 1.0f); };

    // Тип сатурації окремо (кожен тип проти Tube)
    for (int t : { 1, 2 })
    {
        const auto out = process (mix, [&] (SpacenerdMasterProcessor& p) { baseSetup (p); set (p, ParamIDs::satType, (float) t); });
        std::cout << "satType " << t << " vs Tube: " << diffDb (out, ref) << " dB" << std::endl;
    }
    // Компресор: скільки GR на цьому міксі за замовчуванням
    {
        SpacenerdMasterProcessor p; p.setRateAndBufferSizeDetails (fs, 512); p.prepareToPlay (fs, 512);
        AudioBuffer<float> out (mix); MidiBuffer m; float gr = 0.0f, lim = 0.0f;
        for (int pos = 0; pos < out.getNumSamples(); pos += 512)
        {
            AudioBuffer<float> chunk (out.getArrayOfWritePointers(), 2, pos, std::min (512, out.getNumSamples() - pos));
            p.processBlock (chunk, m);
            gr = std::max (gr, p.compGr.take()); lim = std::max (lim, p.limGr.take());
        }
        std::cout << "Default: comp GR max " << gr << " dB, limiter GR max " << lim << " dB, input peak "
                  << sn::gainToDb (mix.getMagnitude (0, mix.getNumSamples())) << " dBFS" << std::endl;
    }
    std::cout << (dead == 0 ? "NO DEAD CONTROLS" : String (dead) + " DEAD CONTROLS") << std::endl;
    return 0;
}
