#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace FbIDs
{
    inline constexpr auto trigger  = "trigger";   // Auto / Hold / Auto + Hold
    inline constexpr auto hold     = "hold";
    inline constexpr auto delay    = "delay";     // скільки нота має тягнутися до зриву (Auto)
    inline constexpr auto harmonic = "harmonic";  // Tone / Octave / Fifth / Auto
    inline constexpr auto distance = "distance";
    inline constexpr auto amount   = "amount";
    inline constexpr auto morph    = "morph";     // наскільки струна «перетікає» у фідбек
    inline constexpr auto tone     = "tone";
    inline constexpr auto drift    = "drift";
    inline constexpr auto outGain  = "outGain";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createFeedbackLayout()
{
    using namespace juce;
    using namespace FbIDs;
    using F = AudioParameterFloat;
    using A = AudioParameterFloatAttributes;

    auto lin  = [] (float lo, float hi) { return NormalisableRange<float> (lo, hi); };
    auto skew = [] (float lo, float hi, float centre) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (centre); return r; };

    const auto pct = A().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; }).withLabel ("%");
    const auto sec = A().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " s"; }).withLabel ("s");
    const auto dB  = A().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; }).withLabel ("dB");
    const auto hz  = A().withStringFromValueFunction ([] (float v, int)
    {
        return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz";
    }).withLabel ("Hz");
    const auto dist = A().withStringFromValueFunction ([] (float v, int)
    {
        // Умовна відстань до кабінету: 0 % ≈ впритул, 100 % ≈ 4 м
        const float m = 0.05f + 3.95f * v * 0.01f;
        return m < 1.0f ? String (roundToInt (m * 100.0f)) + " cm" : String (m, 1) + " m";
    }).withLabel ("m");

    AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* s) { return ParameterID { s, 1 }; };

    l.add (std::make_unique<AudioParameterChoice> (id (trigger), "Trigger", StringArray { "Auto", "Hold", "Both" }, 0));
    l.add (std::make_unique<AudioParameterBool> (id (hold), "Hold", false));
    l.add (std::make_unique<F> (id (delay),    "Delay",    skew (0.2f, 5.0f, 1.2f), 1.2f, sec));
    l.add (std::make_unique<AudioParameterChoice> (id (harmonic), "Harmonic", StringArray { "Tone", "Octave", "Fifth", "Auto" }, 3));
    l.add (std::make_unique<F> (id (distance), "Distance", lin (0.0f, 100.0f), 40.0f, dist));
    l.add (std::make_unique<F> (id (amount),   "Amount",   lin (0.0f, 100.0f), 70.0f, pct));
    l.add (std::make_unique<F> (id (morph),    "Morph",    lin (0.0f, 100.0f), 50.0f, pct));
    l.add (std::make_unique<F> (id (tone),     "Tone",     skew (800.0f, 8000.0f, 2500.0f), 2500.0f, hz));
    l.add (std::make_unique<F> (id (drift),    "Drift",    lin (0.0f, 100.0f), 25.0f, pct));
    l.add (std::make_unique<F> (id (outGain),  "Output",   lin (-24.0f, 12.0f), 0.0f, dB));
    return l;
}
