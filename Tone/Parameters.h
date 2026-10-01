#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ToneIDs
{
    inline constexpr auto inGain   = "inGain";
    inline constexpr auto outGain  = "outGain";
    inline constexpr auto mix      = "mix";

    inline constexpr auto trOn      = "trOn";
    inline constexpr auto trAttack  = "trAttack";
    inline constexpr auto trSustain = "trSustain";

    inline constexpr auto tubeOn    = "tubeOn";
    inline constexpr auto tubeDrive = "tubeDrive";
    inline constexpr auto tubeBias  = "tubeBias";
    inline constexpr auto tubeMix   = "tubeMix";

    inline constexpr auto tapeOn    = "tapeOn";
    inline constexpr auto tapeDrive = "tapeDrive";
    inline constexpr auto tapeSpeed = "tapeSpeed";
    inline constexpr auto tapeWow   = "tapeWow";
    inline constexpr auto tapeBias  = "tapeBias";

    inline constexpr auto excOn     = "excOn";
    inline constexpr auto excFreq   = "excFreq";
    inline constexpr auto excAmount = "excAmount";

    inline constexpr auto dsOn      = "dsOn";
    inline constexpr auto dsFreq    = "dsFreq";
    inline constexpr auto dsSens    = "dsSens";
    inline constexpr auto dsRange   = "dsRange";
    inline constexpr auto dsListen  = "dsListen";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createToneLayout()
{
    using namespace juce;
    using F = AudioParameterFloat;
    using B = AudioParameterBool;
    using A = AudioParameterFloatAttributes;
    using namespace ToneIDs;

    auto lin = [] (float lo, float hi) { return NormalisableRange<float> (lo, hi); };
    auto skew = [] (float lo, float hi, float centre) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (centre); return r; };

    const auto dB  = A().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; }).withLabel ("dB");
    const auto pct = A().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; }).withLabel ("%");
    const auto sgn = A().withStringFromValueFunction ([] (float v, int)
    {
        const int i = roundToInt (v);
        return (i > 0 ? "+" : "") + String (i) + " %";
    }).withLabel ("%");
    const auto hz = A().withStringFromValueFunction ([] (float v, int)
    {
        return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz";
    }).withLabel ("Hz");

    AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* s) { return ParameterID { s, 1 }; };

    l.add (std::make_unique<F> (id (inGain),  "Input",  lin (-24.0f, 24.0f), 0.0f, dB));
    l.add (std::make_unique<F> (id (outGain), "Output", lin (-24.0f, 12.0f), 0.0f, dB));
    l.add (std::make_unique<F> (id (mix),     "Mix",    lin (0.0f, 100.0f), 100.0f, pct));

    l.add (std::make_unique<B> (id (trOn), "Transient On", false));
    l.add (std::make_unique<F> (id (trAttack),  "Attack",  lin (-100.0f, 100.0f), 0.0f, sgn));
    l.add (std::make_unique<F> (id (trSustain), "Sustain", lin (-100.0f, 100.0f), 0.0f, sgn));

    l.add (std::make_unique<B> (id (tubeOn), "Tube On", true));
    l.add (std::make_unique<F> (id (tubeDrive), "Tube Drive", lin (0.0f, 100.0f), 30.0f, pct));
    l.add (std::make_unique<F> (id (tubeBias),  "Tube Bias",  lin (0.0f, 100.0f), 40.0f, pct));
    l.add (std::make_unique<F> (id (tubeMix),   "Tube Mix",   lin (0.0f, 100.0f), 100.0f, pct));

    l.add (std::make_unique<B> (id (tapeOn), "Tape On", true));
    l.add (std::make_unique<F> (id (tapeDrive), "Tape Drive", lin (0.0f, 100.0f), 30.0f, pct));
    l.add (std::make_unique<AudioParameterChoice> (id (tapeSpeed), "Tape Speed", StringArray { "7.5", "15", "30" }, 1));
    l.add (std::make_unique<F> (id (tapeWow),   "Wow & Flutter", lin (0.0f, 100.0f), 0.0f, pct));
    l.add (std::make_unique<F> (id (tapeBias),  "Tape Bias", lin (0.0f, 100.0f), 50.0f, pct));

    l.add (std::make_unique<B> (id (excOn), "Exciter On", false));
    l.add (std::make_unique<F> (id (excFreq),   "Exciter Freq",   skew (1500.0f, 12000.0f, 5000.0f), 5000.0f, hz));
    l.add (std::make_unique<F> (id (excAmount), "Exciter Amount", lin (0.0f, 100.0f), 25.0f, pct));

    l.add (std::make_unique<B> (id (dsOn), "De-esser On", false));
    l.add (std::make_unique<F> (id (dsFreq),  "De-ess Freq", skew (3000.0f, 12000.0f, 6000.0f), 5500.0f, hz));
    l.add (std::make_unique<F> (id (dsSens),  "Sensitivity", lin (0.0f, 100.0f), 50.0f, pct));
    l.add (std::make_unique<F> (id (dsRange), "Range",       lin (0.0f, 12.0f), 6.0f, dB));
    l.add (std::make_unique<B> (id (dsListen), "De-ess Listen", false));

    return l;
}
