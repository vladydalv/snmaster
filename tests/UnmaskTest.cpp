#include <juce_audio_processors/juce_audio_processors.h>
#include "../Unmask/PluginProcessor.h"
#include "../Unmask/PluginEditor.h"
#include <iostream>

using namespace juce;

static int failures = 0;
static void check (bool ok, const String& what) { std::cout << (ok ? "[ OK ] " : "[FAIL] ") << what << std::endl; if (! ok) ++failures; }

static constexpr double fs = 48000.0, twoPi = MathConstants<double>::twoPi;

static void set (SpacenerdUnmaskProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

/** Бас (основний) і бочка (ключ) кожні 0.5 с; повертає вихід. */
static AudioBuffer<float> run (SpacenerdUnmaskProcessor& p, double sec, bool withKey, AudioBuffer<float>* inCopy = nullptr)
{
    const int total = (int) (sec * fs);
    AudioBuffer<float> out (2, total);
    if (inCopy) inCopy->setSize (2, total);
    p.setRateAndBufferSizeDetails (fs, 512);
    p.prepareToPlay (fs, 512);
    MidiBuffer midi;
    Random rnd (3);
    double ph = 0.0;
    for (int pos = 0; pos < total; pos += 512)
    {
        const int n = std::min (512, total - pos);
        AudioBuffer<float> b (4, n);
        for (int i = 0; i < n; ++i)
        {
            const int s = pos + i;
            const double t = s / fs;
            const float bass = (float) (0.3 * std::sin (twoPi * 63.0 * t) + 0.15 * std::sin (twoPi * 126.0 * t) + 0.02 * (rnd.nextFloat() * 2 - 1));
            const double tk = std::fmod (t, 0.5);
            if (tk < 1.0 / fs) ph = 0.0;
            ph += twoPi * (55.0 + 60.0 * std::exp (-tk * 30.0)) / fs;
            const float kick = withKey ? (float) (0.7 * std::exp (-tk * 10.0) * std::sin (ph)) : 0.0f;
            b.setSample (0, i, bass); b.setSample (1, i, bass);
            b.setSample (2, i, kick); b.setSample (3, i, kick);
        }
        if (inCopy) for (int ch = 0; ch < 2; ++ch) inCopy->copyFrom (ch, pos, b, ch, 0, n);
        p.processBlock (b, midi);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, b, ch, 0, n);
    }
    return out;
}

/** Рівень 63 Гц у вікні [t0, t1) кожного півсекундного циклу (останні 4 с). */
static float level63 (const AudioBuffer<float>& b, double t0, double t1)
{
    sn::Biquad bp; bp.setBandPass (fs, 63.0, 6.0);
    double acc = 0.0; int cnt = 0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float y = bp.process (b.getSample (0, i));
        const double t = i / fs, tk = std::fmod (t, 0.5);
        if (t > b.getNumSamples() / fs - 4.0 && tk >= t0 && tk < t1) { acc += y * y; ++cnt; }
    }
    return 10.0f * std::log10 ((float) (acc / std::max (1, cnt)) + 1.0e-20f);
}

int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI init;

    {
        SpacenerdUnmaskProcessor p;
        set (p, UnmaskIDs::range, 0);
        set (p, UnmaskIDs::depth, 6.0f);
        AudioBuffer<float> in;
        auto out = run (p, 12.0, true, &in);
        const float found = p.dsp.foundHz;
        check (found >= 50.0f && found <= 80.0f, "Auto finds the bass/kick clash: " + String (found, 0) + " Hz");
        const float hit = level63 (out, 0.03, 0.12) - level63 (in, 0.03, 0.12);
        const float gap = level63 (out, 0.42, 0.49) - level63 (in, 0.42, 0.49);
        check (hit < -3.0f && gap > -1.0f, "Bass ducks at 63 Hz only on kick hits: " + String (hit, 1) + " dB on hits, " + String (gap, 1) + " dB between");

        if (argc > 1)
        {
            SpacenerdUnmaskEditor ed (p);
            for (int i = 0; i < 20; ++i) ed.refresh();
            auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.5f);
            FileOutputStream os (File (argv[1]).getChildFile ("unmask.png")); os.setPosition (0); os.truncate();
            PNGImageFormat().writeImageToStream (img, os);
        }
    }
    {
        SpacenerdUnmaskProcessor p;
        AudioBuffer<float> in;
        auto out = run (p, 3.0, false, &in);
        float diff = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i) diff = std::max (diff, std::abs (out.getSample (0, i) - in.getSample (0, i)));
        check (diff < 1.0e-4f, "No key = untouched (max diff " + String (diff, 6) + ")");
    }
    {
        SpacenerdUnmaskProcessor p;
        set (p, UnmaskIDs::delta, 1.0f);
        set (p, UnmaskIDs::range, 0);
        AudioBuffer<float> in;
        auto out = run (p, 8.0, true, &in);
        const float gap = level63 (out, 0.38, 0.48) - level63 (in, 0.38, 0.48);
        check (gap < -20.0f, "Listen to cut: almost silent between hits (" + String (gap, 1) + " dB)");
    }
    for (int i = 0; i < SpacenerdUnmaskProcessor().getNumPrograms(); ++i)
    {
        SpacenerdUnmaskProcessor p;
        p.setCurrentProgram (i);
        auto out = run (p, 1.0, true);
        check (std::isfinite (out.getMagnitude (0, 0, out.getNumSamples())), "Preset '" + p.getProgramName (i) + "' runs");
    }

    {
        // Пресет з інтерфейсу (випадний список) реально застосовується
        SpacenerdUnmaskProcessor p;
        SpacenerdUnmaskEditor ed (p);
        snui::PresetBox* box = nullptr;
        std::function<void (Component&)> find = [&] (Component& c) { for (auto* ch : c.getChildren()) { if (auto* b = dynamic_cast<snui::PresetBox*> (ch)) box = b; find (*ch); } };
        find (ed);
        if (box != nullptr) box->setSelectedId (2, sendNotificationSync);
        check (box != nullptr && p.getCurrentProgram() == 1 && std::abs (p.apvts.getRawParameterValue (UnmaskIDs::depth)->load() - 5.0f) < 0.01f,
               "Preset from the editor list applies (program " + String (p.getCurrentProgram()) + ", depth " + String (p.apvts.getRawParameterValue (UnmaskIDs::depth)->load(), 1) + ")");
    }

    std::cout << (failures == 0 ? "ALL PASSED" : String (failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
