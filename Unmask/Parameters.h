#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace UnmaskIDs
{
    inline constexpr auto mode    = "mode";      // Auto / Manual
    inline constexpr auto range   = "range";     // де шукати конфлікт: Lows / Low-mids / Mids / Highs / Full
    inline constexpr auto freq    = "freq";      // частота вручну
    inline constexpr auto depth   = "depth";     // наскільки глибоко різати, коли звучить ключ
    inline constexpr auto width   = "width";     // ширина (Q)
    inline constexpr auto attack  = "attack";
    inline constexpr auto release = "release";
    inline constexpr auto sens    = "sens";      // чутливість до ключа
    inline constexpr auto delta   = "delta";     // слухати лише те, що вирізається
}

/** Межі пошуку конфлікту для кожного діапазону, Гц. */
inline std::pair<float, float> unmaskRange (int r)
{
    static constexpr std::pair<float, float> v[] { { 40.0f, 160.0f }, { 160.0f, 800.0f }, { 800.0f, 5000.0f }, { 5000.0f, 12000.0f }, { 40.0f, 8000.0f } };
    return v[juce::jlimit (0, 4, r)];
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createUnmaskLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* s) { return ParameterID { s, 1 }; };
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (roundToInt (v)) + " Hz"; }).withLabel ("Hz");
    auto ms = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, v < 10.0f ? 1 : 0) + " ms"; });
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; });
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; });

    l.add (std::make_unique<AudioParameterChoice> (id (UnmaskIDs::mode), "Mode", StringArray { "Auto", "Manual" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (id (UnmaskIDs::range), "Search Range", StringArray { "Lows", "Low-mids", "Mids", "Highs", "Full" }, 4));
    NormalisableRange<float> fr (40.0f, 12000.0f); fr.setSkewForCentre (600.0f);
    l.add (std::make_unique<AudioParameterFloat> (id (UnmaskIDs::freq), "Frequency", fr, 300.0f, hz));
    l.add (std::make_unique<AudioParameterFloat> (id (UnmaskIDs::depth), "Depth", NormalisableRange<float> (0.0f, 12.0f, 0.1f), 4.0f, db));
    l.add (std::make_unique<AudioParameterFloat> (id (UnmaskIDs::width), "Width (Q)", NormalisableRange<float> (0.5f, 4.0f, 0.01f), 1.4f));
    NormalisableRange<float> at (0.5f, 50.0f); at.setSkewForCentre (5.0f);
    l.add (std::make_unique<AudioParameterFloat> (id (UnmaskIDs::attack), "Attack", at, 5.0f, ms));
    NormalisableRange<float> rl (20.0f, 600.0f); rl.setSkewForCentre (120.0f);
    l.add (std::make_unique<AudioParameterFloat> (id (UnmaskIDs::release), "Release", rl, 120.0f, ms));
    l.add (std::make_unique<AudioParameterFloat> (id (UnmaskIDs::sens), "Sensitivity", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 60.0f, pct));
    l.add (std::make_unique<AudioParameterBool> (id (UnmaskIDs::delta), "Delta", false));
    return l;
}
