#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace EraIDs
{
    inline constexpr auto year      = "year";       // 1960 … 2025
    inline constexpr auto intensity = "intensity";  // 0 … 100 %
    inline constexpr auto genre     = "genre";
    inline constexpr auto mix       = "mix";
    inline constexpr auto outGain   = "outGain";
    inline constexpr auto gainMatch = "gainMatch";
    inline constexpr auto split     = "split";      // окремі епохи для низу і верху
    inline constexpr auto yearLow   = "yearLow";
    inline constexpr auto yearHigh  = "yearHigh";
    inline constexpr auto lowAmt    = "lowAmt";     // інтенсивність низу (вертикаль ручки LOW)
    inline constexpr auto highAmt   = "highAmt";    // інтенсивність верху (вертикаль ручки HIGH)
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createEraLayout()
{
    using namespace juce;
    using namespace EraIDs;
    using F = AudioParameterFloat;
    using A = AudioParameterFloatAttributes;

    AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* s) { return ParameterID { s, 1 }; };

    l.add (std::make_unique<F> (id (year), "Year", NormalisableRange<float> (1960.0f, 2025.0f), 1975.0f,
                                A().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)); })));
    l.add (std::make_unique<F> (id (intensity), "Intensity", NormalisableRange<float> (0.0f, 100.0f), 60.0f,
                                A().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; }).withLabel ("%")));
    l.add (std::make_unique<AudioParameterChoice> (id (genre), "Genre",
                                StringArray { "Neutral", "Stoner", "Psych", "Space Rock", "Grunge" }, 0));
    l.add (std::make_unique<F> (id (mix), "Mix", NormalisableRange<float> (0.0f, 100.0f), 100.0f,
                                A().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; }).withLabel ("%")));
    l.add (std::make_unique<F> (id (outGain), "Output", NormalisableRange<float> (-24.0f, 12.0f), 0.0f,
                                A().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; }).withLabel ("dB")));
    l.add (std::make_unique<AudioParameterBool> (id (gainMatch), "Gain Match", false));
    const auto yearText = A().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)); });
    l.add (std::make_unique<AudioParameterBool> (id (split), "Split Bands", false));
    l.add (std::make_unique<F> (id (yearLow),  "Low Year",  NormalisableRange<float> (1960.0f, 2025.0f), 1972.0f, yearText));
    l.add (std::make_unique<F> (id (yearHigh), "High Year", NormalisableRange<float> (1960.0f, 2025.0f), 2015.0f, yearText));
    const auto bandPct = A().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; }).withLabel ("%");
    l.add (std::make_unique<F> (id (lowAmt),  "Low Intensity",  NormalisableRange<float> (0.0f, 100.0f), 70.0f, bandPct));
    l.add (std::make_unique<F> (id (highAmt), "High Intensity", NormalisableRange<float> (0.0f, 100.0f), 70.0f, bandPct));
    return l;
}
