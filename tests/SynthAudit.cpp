// Аудит SN Synth: кожен контрол чутно змінює звук у своєму контексті.
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Synth/PluginProcessor.h"
#include <iostream>

using namespace juce;
using namespace SynIDs;
static constexpr double fs = 48000.0;
static int failures = 0;

static void set (SpacenerdSynthProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

struct Ev { double t; MidiMessage m; };
using Setup = std::function<void (SpacenerdSynthProcessor&)>;

static std::vector<Ev> chord (std::initializer_list<int> notes, double on, double off, float vel = 0.8f)
{
    std::vector<Ev> e;
    for (int n : notes) { e.push_back ({ on, MidiMessage::noteOn (1, n, vel) }); e.push_back ({ off, MidiMessage::noteOff (1, n) }); }
    return e;
}

static AudioBuffer<float> render (const std::vector<Ev>& ev, double seconds, Setup setup)
{
    SpacenerdSynthProcessor p;
    p.setRateAndBufferSizeDetails (fs, 256);
    p.prepareToPlay (fs, 256);
    set (p, drift, 0.0f);
    if (setup) setup (p);
    AudioBuffer<float> out (2, (int) (seconds * fs));
    out.clear();
    for (int pos = 0; pos < out.getNumSamples(); pos += 256)
    {
        const int len = std::min (256, out.getNumSamples() - pos);
        MidiBuffer midi;
        for (auto& e : ev) { const int s = (int) (e.t * fs); if (s >= pos && s < pos + len) midi.addEvent (e.m, s - pos); }
        AudioBuffer<float> b (out.getArrayOfWritePointers(), 2, pos, len);
        p.processBlock (b, midi);
    }
    return out;
}

static float diffDb (const AudioBuffer<float>& a, const AudioBuffer<float>& b)
{
    double e = 0, r = 0;
    for (int c = 0; c < 2; ++c) for (int i = 0; i < a.getNumSamples(); ++i)
    { const double d = a.getSample (c, i) - b.getSample (c, i); e += d * d; r += 0.5 * ((double) a.getSample (c, i) * a.getSample (c, i) + (double) b.getSample (c, i) * b.getSample (c, i)); }
    return (float) (10.0 * std::log10 (e / (r + 1e-20) + 1e-20));
}

int main()
{
    ScopedJuceInitialiser_GUI init;
    {
        const auto t0 = Time::getMillisecondCounter();
        while (! syn::SaxLibrary::get().ready() && Time::getMillisecondCounter() - t0 < 30000) Thread::sleep (50);
        std::cout << (syn::SaxLibrary::get().ready() ? "sax: samples" : "sax: model") << std::endl;
    }
    const auto note1 = chord ({ 45 }, 0.0, 1.0);
    const auto triad = chord ({ 45, 52, 57 }, 0.0, 1.0);
    std::vector<Ev> legato { { 0.0, MidiMessage::noteOn (1, 45, 0.8f) }, { 0.5, MidiMessage::noteOn (1, 52, 0.8f) }, { 0.6, MidiMessage::noteOff (1, 45) }, { 1.2, MidiMessage::noteOff (1, 52) } };
    auto withCtl = [] (std::vector<Ev> e, MidiMessage m) { e.insert (e.begin(), { 0.0, m }); return e; };

    struct Case { const char* id; float lo, hi; Setup ctx; std::vector<Ev> ev; double len; };
    auto C = [] (const char* id, float lo, float hi, Setup ctx, std::vector<Ev> ev, double len = 1.5) { return Case { id, lo, hi, std::move (ctx), std::move (ev), len }; };
    const Setup none = {};
    const Setup oscB = [] (auto& p) { set (p, bLevel, 80.0f); };
    const Setup uniA = [] (auto& p) { set (p, aUni, 3.0f); };
    const Setup uniB = [] (auto& p) { set (p, bLevel, 80.0f); set (p, bUni, 3.0f); };
    const Setup saxE = [] (auto& p) { set (p, engine, 1.0f); set (p, mode, 2.0f); set (p, cutoff, 20000.0f); set (p, fEnv, 0.0f); set (p, aS, 100.0f); };
    const Setup bowlE = [] (auto& p) { set (p, engine, 2.0f); set (p, cutoff, 20000.0f); set (p, fEnv, 0.0f); set (p, aS, 100.0f); set (p, aR, 3000.0f); };
    const Setup drv = [] (auto& p) { set (p, dAmount, 60.0f); };
    const Setup env = [] (auto& p) { set (p, fEnv, 100.0f); set (p, cutoff, 200.0f); };
    const Setup lfo1 = [] (auto& p) { set (p, mSrc[0], 1.0f); set (p, mDst[0], 7.0f); set (p, mAmt[0], 60.0f); };
    const Setup lfo2 = [] (auto& p) { set (p, mSrc[0], 2.0f); set (p, mDst[0], 7.0f); set (p, mAmt[0], 60.0f); set (p, l2Rate, 3.0f); };
    const Setup menv = [] (auto& p) { set (p, mSrc[0], 3.0f); set (p, mDst[0], 7.0f); set (p, mAmt[0], 80.0f); set (p, cutoff, 300.0f); };
    const Setup arp = [] (auto& p) { set (p, arpOn, 1.0f); };
    const Setup ch = [] (auto& p) { set (p, chOn, 1.0f); };
    const Setup dl = [] (auto& p) { set (p, dlOn, 1.0f); };
    const Setup rv = [] (auto& p) { set (p, rvOn, 1.0f); };

    std::vector<Case> cases {
        C (engine, 0, 2, none, note1), C (mode, 0, 1, none, triad), C (glide, 0, 600, [] (auto& p) { set (p, mode, 1.0f); }, legato),
        C (bend, 1, 12, none, withCtl (note1, MidiMessage::pitchWheel (1, 16383))), C (volume, -24, 0, none, note1),
        C (vibWheel, 0, 100, none, withCtl (note1, MidiMessage::controllerEvent (1, 1, 127))),
        C (vibAT, 0, 100, [] (auto& p) { set (p, vibWheel, 0.0f); }, withCtl (note1, MidiMessage::channelPressureChange (1, 127))),
        C (vibRate, 2, 9, [] (auto& p) { set (p, vibWheel, 100.0f); }, withCtl (note1, MidiMessage::controllerEvent (1, 1, 127))),
        C (vibAuto, 0, 100, none, note1), C (drift, 0, 100, none, note1),
        C (aWave, 0, 3, none, note1), C (aOct, -1, 1, none, note1), C (aSemi, 0, 7, none, note1), C (aFine, -50, 50, none, note1), C (aLevel, 20, 100, none, note1),
        C (aShape, 0, 100, none, note1), C (aUni, 1, 5, none, note1), C (aDetune, 0, 100, uniA, note1), C (aSpread, 0, 100, uniA, note1),
        C (bWave, 0, 3, oscB, note1), C (bOct, -1, 1, oscB, note1), C (bSemi, 0, 7, oscB, note1), C (bFine, -50, 50, oscB, note1), C (bLevel, 0, 100, none, note1),
        C (bShape, 0, 100, oscB, note1), C (bUni, 1, 5, oscB, note1), C (bDetune, 0, 100, uniB, note1), C (bSpread, 0, 100, uniB, note1),
        C (bSync, 0, 1, [] (auto& p) { set (p, bLevel, 80.0f); set (p, bSemi, 7.0f); set (p, bWave, 0.0f); }, note1),
        C (fm, 0, 100, [] (auto& p) { set (p, bWave, 3.0f); }, note1),
        C (subLevel, 0, 100, none, note1), C (subOct, 0, 1, [] (auto& p) { set (p, subLevel, 80.0f); }, note1), C (subWave, 0, 1, [] (auto& p) { set (p, subLevel, 80.0f); }, note1),
        C (noise, 0, 100, none, note1),
        C (saxBreath, 0, 100, saxE, note1), C (saxReed, 0, 100, saxE, note1), C (saxGrowl, 0, 100, saxE, note1), C (saxAir, 0, 100, saxE, note1),
        C (saxBody, 0, 100, saxE, note1), C (saxScoop, 0, 100, saxE, note1),
        C (bowlType, 0, 5, bowlE, note1), C (bowlDecay, 0, 100, bowlE, note1, 3.0), C (bowlHard, 0, 100, bowlE, note1), C (bowlRub, 0, 100, bowlE, note1),
        C (bowlBeat, 0, 100, bowlE, note1, 3.0), C (bowlBright, 0, 100, bowlE, note1),
        C (fType, 0, 4, none, note1), C (cutoff, 200, 8000, none, note1), C (reso, 0, 100, none, note1), C (fDrive, 0, 24, none, note1),
        C (fEnv, -100, 100, none, note1), C (fKey, 0, 100, none, chord ({ 81 }, 0.0, 1.0)), C (fVel, 0, 100, none, chord ({ 45 }, 0.0, 1.0, 0.3f)),
        C (fA, 1, 1500, env, note1), C (fD, 20, 2000, env, note1), C (fS, 0, 100, env, note1), C (fR, 20, 2000, env, note1),
        C (dOn, 0, 1, drv, note1), C (dType, 0, 3, drv, note1), C (dAmount, 0, 100, none, note1), C (dTone, 1000, 20000, drv, note1),
        C (dLow, 20, 300, drv, note1), C (dMix, 0, 100, drv, note1),
        C (aA, 1, 800, none, note1), C (aD, 20, 2000, [] (auto& p) { set (p, aS, 20.0f); }, note1), C (aS, 10, 100, none, note1), C (aR, 20, 2000, none, note1),
        C (ampVel, 0, 100, none, chord ({ 45 }, 0.0, 1.0, 0.3f)),
        C (l1Shape, 0, 4, lfo1, note1), C (l1Rate, 0.5f, 10, lfo1, note1), C (l1Sync, 0, 1, [=] (auto& p) { lfo1 (p); set (p, l1Rate, 1.0f); }, note1), C (l1Div, 2, 8, [=] (auto& p) { lfo1 (p); set (p, l1Sync, 1.0f); }, note1),
        C (l2Shape, 0, 4, lfo2, note1), C (l2Rate, 0.5f, 10, lfo2, note1), C (l2Sync, 0, 1, [=] (auto& p) { lfo2 (p); set (p, l2Rate, 1.0f); }, note1), C (l2Div, 2, 8, [=] (auto& p) { lfo2 (p); set (p, l2Sync, 1.0f); }, note1),
        C (l1Dst, 0, 7, [] (auto& p) { set (p, l1Amt, 60.0f); }, note1), C (l1Amt, 0, 60, [] (auto& p) { set (p, l1Dst, 7.0f); }, note1),
        C (l2Dst, 0, 7, [] (auto& p) { set (p, l2Amt, 60.0f); set (p, l2Rate, 3.0f); }, note1), C (l2Amt, 0, 60, [] (auto& p) { set (p, l2Dst, 7.0f); set (p, l2Rate, 3.0f); }, note1),
        C (mA, 1, 800, menv, note1), C (mD, 20, 2000, menv, note1),
        C (arpOn, 0, 1, none, triad, 2.0), C (arpMode, 0, 3, arp, triad, 2.0), C (arpRate, 4, 10, arp, triad, 2.0), C (arpOct, 1, 3, arp, triad, 2.0), C (arpGate, 10, 100, arp, triad, 2.0),
        C (chOn, 0, 1, none, note1), C (chRate, 0.2f, 4, ch, note1), C (chDepth, 0, 100, ch, note1), C (chMix, 0, 100, ch, note1),
        C (dlOn, 0, 1, none, note1), C (dlTime, 0, 7, dl, note1, 2.5), C (dlFb, 0, 90, dl, note1, 2.5), C (dlMix, 0, 100, dl, note1), C (dlPing, 0, 1, dl, note1),
        C (rvOn, 0, 1, none, note1), C (rvSize, 0, 100, rv, note1, 3.0), C (rvMix, 0, 100, rv, note1), C (rvShimmer, 0, 100, rv, note1, 3.0), C (rvFreeze, 0, 1, rv, note1, 3.0),
    };
    for (int k = 0; k < 4; ++k)
    {
        cases.push_back (C (mSrc[k], 0, 1, [k] (auto& p) { set (p, mDst[k], 7.0f); set (p, mAmt[k], 80.0f); }, note1));
        cases.push_back (C (mDst[k], 0, 7, [k] (auto& p) { set (p, mSrc[k], 1.0f); set (p, mAmt[k], 80.0f); }, note1));
        cases.push_back (C (mAmt[k], 0, 80, [k] (auto& p) { set (p, mSrc[k], 1.0f); set (p, mDst[k], 7.0f); }, note1));
    }

    // Перевірити, що жоден параметр не пропущено
    {
        SpacenerdSynthProcessor p;
        for (auto* prm : p.getParameters())
            if (auto* r = dynamic_cast<RangedAudioParameter*> (prm))
            {
                bool found = false;
                for (auto& c : cases) if (r->getParameterID() == c.id) found = true;
                if (! found) { std::cout << "[MISS] no audit case for " << r->getParameterID() << std::endl; ++failures; }
            }
    }

    for (auto& c : cases)
    {
        const auto a = render (c.ev, c.len, [&] (auto& p) { if (c.ctx) c.ctx (p); set (p, c.id, c.lo); });
        const auto b = render (c.ev, c.len, [&] (auto& p) { if (c.ctx) c.ctx (p); set (p, c.id, c.hi); });
        const float d = diffDb (a, b);
        const bool ok = d > -25.0f;
        std::cout << (ok ? "[ OK ] " : "[FAIL] ") << String (c.id).paddedRight (' ', 11) << String (c.lo) << " -> " << String (c.hi) << ": change " << String (d, 1) << " dB" << std::endl;
        if (! ok) ++failures;
    }
    std::cout << (failures == 0 ? "ALL CONTROLS WORK" : String (failures) + " PROBLEMS") << std::endl;
    return 0;
}
