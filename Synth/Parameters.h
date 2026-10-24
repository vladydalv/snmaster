#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace SynIDs
{
    // Голос
    inline constexpr auto engine = "engine", mode = "mode", glide = "glide", bend = "bend", volume = "volume";
    inline constexpr auto vibRate = "vibRate", vibWheel = "vibWheel", vibAT = "vibAT", vibAuto = "vibAuto", drift = "drift";
    // Осцилятори
    inline constexpr auto aWave = "aWave", aOct = "aOct", aSemi = "aSemi", aFine = "aFine", aLevel = "aLevel", aShape = "aShape", aUni = "aUni", aDetune = "aDetune", aSpread = "aSpread";
    inline constexpr auto bWave = "bWave", bOct = "bOct", bSemi = "bSemi", bFine = "bFine", bLevel = "bLevel", bShape = "bShape", bUni = "bUni", bDetune = "bDetune", bSpread = "bSpread";
    inline constexpr auto bSync = "bSync", fm = "fm";
    inline constexpr auto subLevel = "subLevel", subOct = "subOct", subWave = "subWave", noise = "noise";
    // Саксофон (фізична модель)
    inline constexpr auto saxBreath = "saxBreath", saxReed = "saxReed", saxGrowl = "saxGrowl", saxAir = "saxAir", saxBody = "saxBody", saxScoop = "saxScoop";
    // Чаші й дзвони (модальний синтез)
    inline constexpr auto bowlType = "bowlType", bowlDecay = "bowlDecay", bowlHard = "bowlHard", bowlRub = "bowlRub", bowlBeat = "bowlBeat", bowlBright = "bowlBright";
    // Фільтр
    inline constexpr auto fType = "fType", cutoff = "cutoff", reso = "reso", fDrive = "fDrive", fEnv = "fEnv", fKey = "fKey", fVel = "fVel";
    inline constexpr auto fA = "fA", fD = "fD", fS = "fS", fR = "fR";
    // Драйв
    inline constexpr auto dOn = "dOn", dType = "dType", dAmount = "dAmount", dTone = "dTone", dLow = "dLow", dMix = "dMix";
    // Підсилювач
    inline constexpr auto aA = "aA", aD = "aD", aS = "aS", aR = "aR", ampVel = "ampVel";
    // Модуляція
    inline constexpr auto l1Shape = "l1Shape", l1Rate = "l1Rate", l1Sync = "l1Sync", l1Div = "l1Div";
    inline constexpr auto l2Shape = "l2Shape", l2Rate = "l2Rate", l2Sync = "l2Sync", l2Div = "l2Div";
    inline constexpr auto mA = "mA", mD = "mD";
    inline const char* const mSrc[4] { "m1Src", "m2Src", "m3Src", "m4Src" };
    inline const char* const mDst[4] { "m1Dst", "m2Dst", "m3Dst", "m4Dst" };
    inline const char* const mAmt[4] { "m1Amt", "m2Amt", "m3Amt", "m4Amt" };
    // Арпеджіатор
    inline constexpr auto arpOn = "arpOn", arpMode = "arpMode", arpRate = "arpRate", arpOct = "arpOct", arpGate = "arpGate";
    // Ефекти
    inline constexpr auto chOn = "chOn", chRate = "chRate", chDepth = "chDepth", chMix = "chMix";
    inline constexpr auto dlOn = "dlOn", dlTime = "dlTime", dlFb = "dlFb", dlMix = "dlMix", dlPing = "dlPing";
    inline constexpr auto rvOn = "rvOn", rvSize = "rvSize", rvMix = "rvMix", rvShimmer = "rvShimmer", rvFreeze = "rvFreeze";
}

namespace syn
{
enum Engine : int { EngAnalog = 0, EngSax, EngBowls };
enum Mode : int { ModePoly = 0, ModeMono, ModeLegato };
enum Wave : int { WSaw = 0, WSquare, WTri, WSine, WSuper };
enum FType : int { FLadder24 = 0, FLadder12, FMS20, FBand, FHigh };
enum DType : int { DWarm = 0, DFuzz, DFold, DRectify };
enum LShape : int { LSine = 0, LTri, LSaw, LSquare, LSH };
enum Src : int { SNone = 0, SLfo1, SLfo2, SModEnv, SVel, SWheel, SAT, SKey, SRandom, kNumSrc };
enum Dst : int { DNone = 0, DPitch, DPitchA, DPitchB, DShapeA, DShapeB, DFm, DCutoff, DReso, DDrive, DAmp, DPan, DDetune, DNoise, DBreath, DGrowl, kNumDst };

inline const juce::StringArray& srcNames() { static const juce::StringArray s { "-", "LFO 1", "LFO 2", "Mod Env", "Velocity", "Mod Wheel", "Aftertouch", "Key", "Random" }; return s; }
inline const juce::StringArray& dstNames() { static const juce::StringArray s { "-", "Pitch", "Osc A Pitch", "Osc B Pitch", "Osc A Shape", "Osc B Shape", "FM", "Cutoff", "Resonance", "Drive", "Volume", "Pan", "Detune", "Noise", "Sax Breath", "Sax Growl" }; return s; }
inline const juce::StringArray& divNames() { static const juce::StringArray s { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/4 T", "1/8", "1/8 T", "1/16", "1/16 T", "1/32" }; return s; }
inline double divBeats (int i) { static const double b[] { 16, 8, 4, 2, 1, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 }; return b[juce::jlimit (0, 10, i)]; }
inline const juce::StringArray& delayNames() { static const juce::StringArray s { "1/16", "1/8 T", "1/8", "1/8 .", "1/4 T", "1/4", "1/4 .", "1/2" }; return s; }
inline double delayBeats (int i) { static const double b[] { 0.25, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0 }; return b[juce::jlimit (0, 7, i)]; }
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createSynthLayout()
{
    using namespace juce;
    using namespace SynIDs;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* s) { return ParameterID { s, 1 }; };
    using F = AudioParameterFloat;
    using C = AudioParameterChoice;
    using B = AudioParameterBool;
    auto attr = [] (std::function<String (float)> f) { return AudioParameterFloatAttributes().withStringFromValueFunction ([f] (float v, int) { return f (v); }); };
    auto pct = attr ([] (float v) { return String (roundToInt (v)) + " %"; });
    auto spct = attr ([] (float v) { return (v > 0.5f ? "+" : "") + String (roundToInt (v)) + " %"; });
    auto db = attr ([] (float v) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; });
    auto st = attr ([] (float v) { return (v > 0.5f ? "+" : "") + String (roundToInt (v)) + " st"; });
    auto ct = attr ([] (float v) { return (v > 0.5f ? "+" : "") + String (roundToInt (v)) + " ct"; });
    auto oct = attr ([] (float v) { return (v > 0.5f ? "+" : "") + String (roundToInt (v)) + " oct"; });
    auto hz = attr ([] (float v) { return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : String (roundToInt (v)) + " Hz"; });
    auto lfoHz = attr ([] (float v) { return String (v, v < 10.0f ? 2 : 1) + " Hz"; });
    auto ms = attr ([] (float v) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " s" : String (v, v < 10.0f ? 1 : 0) + " ms"; });
    auto uni = attr ([] (float v) { return String (roundToInt (v)) + (roundToInt (v) == 1 ? " voice" : " voices"); });
    auto lowKeep = attr ([] (float v) { return v < 25.0f ? String ("off") : String (roundToInt (v)) + " Hz"; });
    auto time = [] (float lo, float hi, float def) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (std::sqrt (lo * hi) * 0.6f); return std::pair { r, def }; };

    l.add (std::make_unique<C> (id (engine), "Engine", StringArray { "Analog", "Sax", "Bowls" }, 0));
    l.add (std::make_unique<C> (id (mode), "Voice Mode", StringArray { "Poly", "Mono", "Legato" }, 0));
    { NormalisableRange<float> r (0.0f, 2000.0f); r.setSkewForCentre (150.0f); l.add (std::make_unique<F> (id (glide), "Glide", r, 0.0f, ms)); }
    l.add (std::make_unique<F> (id (bend), "Bend Range", NormalisableRange<float> (1.0f, 12.0f, 1.0f), 2.0f, st));
    l.add (std::make_unique<F> (id (volume), "Volume", NormalisableRange<float> (-36.0f, 6.0f, 0.1f), -6.0f, db));
    l.add (std::make_unique<F> (id (vibRate), "Vibrato Rate", NormalisableRange<float> (2.0f, 9.0f, 0.01f), 5.2f, lfoHz));
    l.add (std::make_unique<F> (id (vibWheel), "Vibrato by Wheel", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 30.0f, pct));
    l.add (std::make_unique<F> (id (vibAT), "Vibrato by Aftertouch", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 20.0f, pct));
    l.add (std::make_unique<F> (id (vibAuto), "Auto Vibrato", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, pct));
    l.add (std::make_unique<F> (id (drift), "Drift", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 10.0f, pct));

    const StringArray waves { "Saw", "Square", "Triangle", "Sine", "Super" };
    for (int o = 0; o < 2; ++o)
    {
        const bool a = o == 0;
        const String n = a ? "Osc A " : "Osc B ";
        l.add (std::make_unique<C> (id (a ? aWave : bWave), n + "Wave", waves, a ? 0 : 1));
        l.add (std::make_unique<F> (id (a ? aOct : bOct), n + "Octave", NormalisableRange<float> (-3.0f, 3.0f, 1.0f), 0.0f, oct));
        l.add (std::make_unique<F> (id (a ? aSemi : bSemi), n + "Semitone", NormalisableRange<float> (-12.0f, 12.0f, 1.0f), 0.0f, st));
        l.add (std::make_unique<F> (id (a ? aFine : bFine), n + "Fine", NormalisableRange<float> (-50.0f, 50.0f, 0.1f), a ? 0.0f : 7.0f, ct));
        l.add (std::make_unique<F> (id (a ? aLevel : bLevel), n + "Level", NormalisableRange<float> (0.0f, 100.0f, 0.1f), a ? 80.0f : 0.0f, pct));
        l.add (std::make_unique<F> (id (a ? aShape : bShape), n + "Shape", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, pct));
        l.add (std::make_unique<F> (id (a ? aUni : bUni), n + "Unison", NormalisableRange<float> (1.0f, 7.0f, 1.0f), 1.0f, uni));
        l.add (std::make_unique<F> (id (a ? aDetune : bDetune), n + "Detune", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 25.0f, pct));
        l.add (std::make_unique<F> (id (a ? aSpread : bSpread), n + "Stereo", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 60.0f, pct));
    }
    l.add (std::make_unique<B> (id (bSync), "Osc B Hard Sync", false));
    l.add (std::make_unique<F> (id (fm), "FM B > A", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, pct));
    l.add (std::make_unique<F> (id (subLevel), "Sub Level", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, pct));
    l.add (std::make_unique<C> (id (subOct), "Sub Octave", StringArray { "-1 oct", "-2 oct" }, 0));
    l.add (std::make_unique<C> (id (subWave), "Sub Wave", StringArray { "Sine", "Square" }, 0));
    l.add (std::make_unique<F> (id (noise), "Noise", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, pct));

    l.add (std::make_unique<F> (id (saxBreath), "Sax Breath", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 60.0f, pct));
    l.add (std::make_unique<F> (id (saxReed), "Sax Reed", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, pct));
    l.add (std::make_unique<F> (id (saxGrowl), "Sax Growl", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, pct));
    l.add (std::make_unique<F> (id (saxAir), "Sax Air", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f, pct));
    l.add (std::make_unique<F> (id (saxBody), "Sax Body", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 60.0f, pct));
    l.add (std::make_unique<F> (id (saxScoop), "Sax Scoop", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 25.0f, pct));

    l.add (std::make_unique<C> (id (bowlType), "Bowl Type", StringArray { "Singing bowl", "Crystal bowl", "Bell", "Chimes", "Kalimba", "Glass" }, 0));
    l.add (std::make_unique<F> (id (bowlDecay), "Bowl Decay", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 60.0f, pct));
    l.add (std::make_unique<F> (id (bowlHard), "Bowl Mallet", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 40.0f, pct));
    l.add (std::make_unique<F> (id (bowlRub), "Bowl Rub", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, pct));
    l.add (std::make_unique<F> (id (bowlBeat), "Bowl Beating", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 40.0f, pct));
    l.add (std::make_unique<F> (id (bowlBright), "Bowl Brightness", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, pct));

    l.add (std::make_unique<C> (id (fType), "Filter Type", StringArray { "Ladder 24", "Ladder 12", "MS-20", "Band", "High" }, 0));
    { NormalisableRange<float> r (20.0f, 20000.0f); r.setSkewForCentre (800.0f); l.add (std::make_unique<F> (id (cutoff), "Cutoff", r, 2500.0f, hz)); }
    l.add (std::make_unique<F> (id (reso), "Resonance", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 15.0f, pct));
    l.add (std::make_unique<F> (id (fDrive), "Filter Drive", NormalisableRange<float> (0.0f, 24.0f, 0.1f), 0.0f, db));
    l.add (std::make_unique<F> (id (fEnv), "Filter Env", NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 30.0f, spct));
    l.add (std::make_unique<F> (id (fKey), "Key Track", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f, pct));
    l.add (std::make_unique<F> (id (fVel), "Filter Velocity", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 30.0f, pct));
    for (auto [aid, did, sid, rid, n, a0, d0, s0, r0] : { std::tuple { fA, fD, fS, fR, "Filter", 2.0f, 350.0f, 30.0f, 300.0f }, { aA, aD, aS, aR, "Amp", 2.0f, 400.0f, 80.0f, 180.0f } })
    {
        { auto [r, d] = time (0.5f, 10000.0f, a0); l.add (std::make_unique<F> (id (aid), String (n) + " Attack", r, a0, ms)); }
        { auto [r, d] = time (1.0f, 20000.0f, d0); l.add (std::make_unique<F> (id (did), String (n) + " Decay", r, d0, ms)); }
        l.add (std::make_unique<F> (id (sid), String (n) + " Sustain", NormalisableRange<float> (0.0f, 100.0f, 0.1f), s0, pct));
        { auto [r, d] = time (1.0f, 20000.0f, r0); l.add (std::make_unique<F> (id (rid), String (n) + " Release", r, r0, ms)); }
    }
    l.add (std::make_unique<F> (id (ampVel), "Amp Velocity", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 40.0f, pct));

    l.add (std::make_unique<B> (id (dOn), "Drive On", true));
    l.add (std::make_unique<C> (id (dType), "Drive Type", StringArray { "Warm", "Fuzz", "Fold", "Rectify" }, 0));
    l.add (std::make_unique<F> (id (dAmount), "Drive", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, pct));
    { NormalisableRange<float> r (800.0f, 20000.0f); r.setSkewForCentre (5000.0f); l.add (std::make_unique<F> (id (dTone), "Drive Tone", r, 9000.0f, hz)); }
    { NormalisableRange<float> r (20.0f, 400.0f); r.setSkewForCentre (100.0f); l.add (std::make_unique<F> (id (dLow), "Clean Low", r, 20.0f, lowKeep)); }
    l.add (std::make_unique<F> (id (dMix), "Drive Mix", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, pct));

    const StringArray shapes { "Sine", "Triangle", "Saw", "Square", "S&H" };
    for (int k = 0; k < 2; ++k)
    {
        const String n = k == 0 ? "LFO 1 " : "LFO 2 ";
        { NormalisableRange<float> r (0.02f, 40.0f); r.setSkewForCentre (2.0f); l.add (std::make_unique<F> (id (k == 0 ? l1Rate : l2Rate), n + "Rate", r, k == 0 ? 4.0f : 0.3f, lfoHz)); }
        l.add (std::make_unique<C> (id (k == 0 ? l1Shape : l2Shape), n + "Shape", shapes, k == 0 ? 1 : 0));
        l.add (std::make_unique<B> (id (k == 0 ? l1Sync : l2Sync), n + "Tempo Sync", false));
        l.add (std::make_unique<C> (id (k == 0 ? l1Div : l2Div), n + "Division", syn::divNames(), k == 0 ? 6 : 2));
    }
    { auto [r, d] = time (0.5f, 10000.0f, 1.0f); l.add (std::make_unique<F> (id (mA), "Mod Env Attack", r, 1.0f, ms)); }
    { auto [r, d] = time (1.0f, 20000.0f, 300.0f); l.add (std::make_unique<F> (id (mD), "Mod Env Decay", r, 300.0f, ms)); }
    for (int k = 0; k < 4; ++k)
    {
        l.add (std::make_unique<C> (id (mSrc[k]), "Mod " + String (k + 1) + " Source", syn::srcNames(), 0));
        l.add (std::make_unique<C> (id (mDst[k]), "Mod " + String (k + 1) + " Target", syn::dstNames(), 0));
        l.add (std::make_unique<F> (id (mAmt[k]), "Mod " + String (k + 1) + " Amount", NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, spct));
    }

    l.add (std::make_unique<B> (id (arpOn), "Arp On", false));
    l.add (std::make_unique<C> (id (arpMode), "Arp Mode", StringArray { "Up", "Down", "Up-Down", "Random", "As played" }, 0));
    l.add (std::make_unique<C> (id (arpRate), "Arp Rate", syn::divNames(), 8));
    l.add (std::make_unique<F> (id (arpOct), "Arp Octaves", NormalisableRange<float> (1.0f, 4.0f, 1.0f), 1.0f, oct));
    l.add (std::make_unique<F> (id (arpGate), "Arp Gate", NormalisableRange<float> (5.0f, 100.0f, 1.0f), 60.0f, pct));

    l.add (std::make_unique<B> (id (chOn), "Chorus On", false));
    l.add (std::make_unique<F> (id (chRate), "Chorus Rate", NormalisableRange<float> (0.1f, 5.0f, 0.01f), 0.6f, lfoHz));
    l.add (std::make_unique<F> (id (chDepth), "Chorus Depth", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f, pct));
    l.add (std::make_unique<F> (id (chMix), "Chorus Mix", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f, pct));
    l.add (std::make_unique<B> (id (dlOn), "Delay On", false));
    l.add (std::make_unique<C> (id (dlTime), "Delay Time", syn::delayNames(), 3));
    l.add (std::make_unique<F> (id (dlFb), "Delay Feedback", NormalisableRange<float> (0.0f, 95.0f, 1.0f), 35.0f, pct));
    l.add (std::make_unique<F> (id (dlMix), "Delay Mix", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 25.0f, pct));
    l.add (std::make_unique<B> (id (dlPing), "Delay Ping-Pong", true));
    l.add (std::make_unique<B> (id (rvOn), "Reverb On", false));
    l.add (std::make_unique<F> (id (rvSize), "Reverb Size", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f, pct));
    l.add (std::make_unique<F> (id (rvMix), "Reverb Mix", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 20.0f, pct));
    l.add (std::make_unique<F> (id (rvShimmer), "Shimmer", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, pct));
    l.add (std::make_unique<B> (id (rvFreeze), "Reverb Freeze", false));
    return l;
}
