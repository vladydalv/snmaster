#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ListenIDs
{
    inline constexpr auto instrument = "instrument";   // 0 = Auto, далі mix::Inst + 1
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createListenLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ListenIDs::instrument, 1 }, "Instrument",
               juce::StringArray { "Auto", "Kick", "Snare", "Drums", "Bass", "Guitar", "Vocal", "Keys", "Other" }, 0));
    return l;
}
