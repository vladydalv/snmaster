#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace StompIDs
{
    // Drive
    inline constexpr auto driveOn   = "driveOn";
    inline constexpr auto circuit   = "circuit";    // 0…4: '65 Germanium … Modern (плавно)
    inline constexpr auto gain      = "gain";
    inline constexpr auto tone      = "tone";
    inline constexpr auto battery   = "battery";
    inline constexpr auto cleanBass = "cleanBass";  // Hz, мін = Off
    inline constexpr auto level     = "level";
    // Octave
    inline constexpr auto octOn     = "octOn";
    inline constexpr auto octPos    = "octPos";     // Pre / Post (до чи після драйву)
    inline constexpr auto sub1      = "sub1";       // −1 октава
    inline constexpr auto sub2      = "sub2";       // −2 октави
    inline constexpr auto octUp     = "octUp";      // +1 октава
    inline constexpr auto octUp2    = "octUp2";     // +2 октави
    inline constexpr auto detune    = "detune";     // розстроювання +1/+2 (Spectral)
    inline constexpr auto octDry    = "octDry";
    inline constexpr auto octEngine = "octEngine";  // Poly / Vintage / Mono HQ
    inline constexpr auto octTone   = "octTone";
    inline constexpr auto bloom     = "bloom";      // октави наростають після атаки
    inline constexpr auto wobRate   = "wobRate";    // темп Wobble (ділення такту)
    inline constexpr auto wobble    = "wobble";     // фільтр на октавах, що «гуляє» з LFO модуляції
    // Modulation
    inline constexpr auto modOn     = "modOn";
    inline constexpr auto modMode   = "modMode";    // Tremolo / Harmonic / Pan / Vibrato
    inline constexpr auto rate      = "rate";
    inline constexpr auto modSync      = "sync";       // Free / 1/4 / 1/8 / 1/8T / 1/16
    inline constexpr auto depth     = "depth";
    inline constexpr auto shape     = "shape";      // синус → «рубане» тремоло
    inline constexpr auto rise      = "rise";       // модуляція наростає після кожної атаки
    // Echo
    inline constexpr auto echoOn    = "echoOn";
    inline constexpr auto echoTime      = "time";
    inline constexpr auto echoSync  = "echoSync";   // Free / 1/4 / 1/8. / 1/8 / 1/16
    inline constexpr auto feedback  = "feedback";
    inline constexpr auto echoTone  = "echoTone";
    inline constexpr auto echoMix   = "echoMix";
    inline constexpr auto wear      = "wear";       // «зношена плівка»: детонація + насичення в петлі
    // Output
    inline constexpr auto outGain   = "outGain";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createStompLayout()
{
    using namespace juce;
    using namespace StompIDs;
    using F = AudioParameterFloat;
    using B = AudioParameterBool;
    using C = AudioParameterChoice;
    using A = AudioParameterFloatAttributes;

    auto lin  = [] (float lo, float hi) { return NormalisableRange<float> (lo, hi); };
    auto skew = [] (float lo, float hi, float c) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (c); return r; };
    const auto pct = A().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; }).withLabel ("%");
    const auto dB  = A().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; }).withLabel ("dB");
    const auto hz  = A().withStringFromValueFunction ([] (float v, int)
    {
        return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : v < 1.0f ? String (v, 2) + " Hz" : v < 100.0f ? String (v, 1) + " Hz" : String (roundToInt (v)) + " Hz";
    }).withLabel ("Hz");
    const auto sgn = A().withStringFromValueFunction ([] (float v, int) { return (v > 0.5f ? "+" : "") + String (roundToInt (v)) + " %"; }).withLabel ("%");
    const auto ms  = A().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; }).withLabel ("ms");
    const auto circuitText = A().withStringFromValueFunction ([] (float v, int)
    {
        static const char* n[] { "Germanium", "British", "Triangle", "Op-amp", "Modern" };
        static const char* sh[] { "Germ", "Brit", "Tri", "Op-amp", "Modern" };
        const int i = jlimit (0, 4, roundToInt (v));
        return std::abs (v - (float) i) < 0.08f ? String (n[i]) : String (sh[(int) v]) + " > " + String (sh[std::min (4, (int) v + 1)]);
    });
    const auto bassText = A().withStringFromValueFunction ([] (float v, int) { return v < 45.0f ? String ("Off") : String (roundToInt (v)) + " Hz"; }).withLabel ("Hz");

    AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* s) { return ParameterID { s, 1 }; };

    l.add (std::make_unique<B> (id (driveOn), "Drive On", true));
    l.add (std::make_unique<F> (id (circuit), "Circuit", lin (0.0f, 4.0f), 2.0f, circuitText));
    l.add (std::make_unique<F> (id (gain),    "Gain",    lin (0.0f, 100.0f), 60.0f, pct));
    l.add (std::make_unique<F> (id (tone),    "Tone",    lin (-100.0f, 100.0f), 0.0f, sgn));
    l.add (std::make_unique<F> (id (battery), "Battery", lin (0.0f, 100.0f), 0.0f, pct));
    l.add (std::make_unique<F> (id (cleanBass), "Clean Bass", skew (40.0f, 400.0f, 120.0f), 40.0f, bassText));
    l.add (std::make_unique<F> (id (level),   "Level",   lin (-24.0f, 12.0f), 0.0f, dB));

    l.add (std::make_unique<B> (id (octOn), "Octave On", false));
    l.add (std::make_unique<C> (id (octPos), "Octave Position", StringArray { "Pre", "Post" }, 0));
    // 100 % = октава так само гучна, як вхід; до 200 % (+6 дБ) — октава домінує
    l.add (std::make_unique<F> (id (sub1),   "Sub -1",   lin (0.0f, 200.0f), 100.0f, pct));
    l.add (std::make_unique<F> (id (sub2),   "Sub -2",   lin (0.0f, 200.0f), 0.0f, pct));
    l.add (std::make_unique<F> (id (octUp),  "Up +1",    lin (0.0f, 200.0f), 0.0f, pct));
    l.add (std::make_unique<F> (id (octUp2), "Up +2",    lin (0.0f, 200.0f), 0.0f, pct));
    l.add (std::make_unique<F> (id (detune), "Detune",   lin (0.0f, 25.0f), 0.0f,
                                A().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " ct"; }).withLabel ("ct")));
    l.add (std::make_unique<F> (id (octDry), "Octave Dry", lin (0.0f, 100.0f), 100.0f, pct));
    l.add (std::make_unique<C> (id (octEngine), "Octave Engine", StringArray { "Poly", "Vintage", "Mono HQ", "Spectral" }, 1));
    l.add (std::make_unique<F> (id (octTone), "Octave Tone", skew (150.0f, 8000.0f, 1500.0f), 2500.0f, hz));
    l.add (std::make_unique<F> (id (bloom),  "Bloom",    skew (0.0f, 2000.0f, 300.0f), 0.0f, ms));
    l.add (std::make_unique<F> (id (wobble), "Wobble",   lin (0.0f, 100.0f), 0.0f, pct));
    l.add (std::make_unique<C> (id (wobRate), "Wobble Rate", StringArray { "1/2", "1/4", "1/8", "1/8T", "1/16", "1/32" }, 2));

    l.add (std::make_unique<B> (id (modOn), "Mod On", false));
    l.add (std::make_unique<C> (id (modMode), "Mod Mode", StringArray { "Tremolo", "Harmonic", "Pan", "Vibrato", "Uni-Vibe" }, 0));
    l.add (std::make_unique<F> (id (rate),  "Rate",  skew (0.3f, 15.0f, 4.0f), 5.0f, hz));
    l.add (std::make_unique<C> (id (modSync),  "Mod Sync", StringArray { "Free", "1/4", "1/8", "1/8T", "1/16" }, 0));
    l.add (std::make_unique<F> (id (depth), "Depth", lin (0.0f, 100.0f), 60.0f, pct));
    l.add (std::make_unique<F> (id (shape), "Shape", lin (0.0f, 100.0f), 20.0f, pct));
    l.add (std::make_unique<F> (id (rise),  "Rise",  skew (0.0f, 3000.0f, 400.0f), 0.0f, ms));

    l.add (std::make_unique<B> (id (echoOn), "Echo On", false));
    l.add (std::make_unique<F> (id (echoTime),     "Time",     skew (40.0f, 1200.0f, 350.0f), 380.0f, ms));
    l.add (std::make_unique<C> (id (echoSync), "Echo Sync", StringArray { "Free", "1/4", "1/8.", "1/8", "1/16" }, 0));
    l.add (std::make_unique<F> (id (feedback), "Feedback", lin (0.0f, 95.0f), 35.0f, pct));
    l.add (std::make_unique<F> (id (echoTone), "Echo Tone", skew (800.0f, 12000.0f, 3000.0f), 3000.0f, hz));
    l.add (std::make_unique<F> (id (echoMix),  "Echo Mix", lin (0.0f, 100.0f), 30.0f, pct));
    l.add (std::make_unique<F> (id (wear),     "Wear",     lin (0.0f, 100.0f), 30.0f, pct));

    l.add (std::make_unique<F> (id (outGain), "Output", lin (-24.0f, 12.0f), 0.0f, dB));
    return l;
}
