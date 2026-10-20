#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace DecIDs
{
    inline constexpr auto year      = "year";
    inline constexpr auto room      = "room";        // вертикаль карти: сухо → простір
    inline constexpr auto auth      = "auth";        // Authenticity: 0 — стійка нейтральна, 100 — повністю епоха
    inline constexpr auto mix       = "mix";
    inline constexpr auto inGain    = "inGain";
    inline constexpr auto outGain   = "outGain";
    inline constexpr auto roomType  = "roomType";    // Auto (за роком) або тип простору
    inline constexpr auto format    = "format";      // Auto (за роком) або носій
    inline constexpr auto fmtAmount = "fmtAmount";   // Clean → Worn
    inline constexpr auto fmtAge    = "fmtAge";
    // Модулі (вимикачі) і поправки до значень епохи (0 = як в епосі)
    inline constexpr auto toneOn = "toneOn", gritOn = "gritOn", filterOn = "filterOn", squashOn = "squashOn", hitOn = "hitOn", roomOn = "roomOn";
    inline constexpr auto toneLow = "toneLow", toneLowMid = "toneLowMid", tonePres = "tonePres", toneTop = "toneTop";
    inline constexpr auto gritDrive = "gritDrive", gritChar = "gritChar";
    inline constexpr auto filterHp = "filterHp", filterLp = "filterLp", filterReso = "filterReso";
    inline constexpr auto squashAmt = "squashAmt", squashSpeed = "squashSpeed";
    inline constexpr auto hitAttack = "hitAttack", hitSustain = "hitSustain";
    inline constexpr auto roomSize = "roomSize", roomDecay = "roomDecay";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createDecadeLayout()
{
    using namespace juce;
    using namespace DecIDs;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* s) { return ParameterID { s, 1 }; };
    using F = AudioParameterFloat;
    auto attr = [] (std::function<String (float)> f) { return AudioParameterFloatAttributes().withStringFromValueFunction ([f] (float v, int) { return f (v); }); };
    auto pct = attr ([] (float v) { return String (roundToInt (v)) + " %"; });
    auto spct = attr ([] (float v) { return (v > 0.5f ? "+" : "") + String (roundToInt (v)) + " %"; });
    auto db = attr ([] (float v) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; });
    auto oct = attr ([] (float v) { return std::abs (v) < 0.05f ? String ("era") : (v > 0 ? "+" : "") + String (v, 1) + " oct"; });
    auto yr = attr ([] (float v) { return String (roundToInt (v)); });

    l.add (std::make_unique<F> (id (year), "Year", NormalisableRange<float> (1950.0f, 2025.0f, 0.1f), 1972.0f, yr));
    l.add (std::make_unique<F> (id (room), "Room", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 40.0f, pct));
    l.add (std::make_unique<F> (id (auth), "Authenticity", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, pct));
    l.add (std::make_unique<F> (id (mix), "Mix", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, pct));
    l.add (std::make_unique<F> (id (inGain), "Input", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, db));
    l.add (std::make_unique<F> (id (outGain), "Output", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, db));
    l.add (std::make_unique<AudioParameterChoice> (id (roomType), "Room Type", StringArray { "Auto", "Studio", "Live room", "Chamber", "Hall", "Garage", "Gated" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (id (format), "Format", StringArray { "Auto", "Acetate", "Vinyl", "Cassette", "CD", "MP3", "Stream" }, 0));
    l.add (std::make_unique<F> (id (fmtAmount), "Format Amount", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 35.0f, pct));
    l.add (std::make_unique<F> (id (fmtAge), "Format Age", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 30.0f, pct));

    for (auto m : { toneOn, gritOn, filterOn, squashOn, hitOn, roomOn })
        l.add (std::make_unique<AudioParameterBool> (id (m), String (m).dropLastCharacters (2).toUpperCase() + " On", true));

    for (auto [p, n] : { std::pair { toneLow, "Tone Low" }, { toneLowMid, "Tone Low-Mid" }, { tonePres, "Tone Presence" }, { toneTop, "Tone Top" } })
        l.add (std::make_unique<F> (id (p), n, NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, db));
    l.add (std::make_unique<F> (id (gritDrive), "Grit Drive", NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f, spct));
    l.add (std::make_unique<AudioParameterChoice> (id (gritChar), "Grit Character", StringArray { "Auto", "Tape", "Tube", "Sampler", "Clip" }, 0));
    l.add (std::make_unique<F> (id (filterHp), "Filter High-pass", NormalisableRange<float> (-3.0f, 3.0f, 0.01f), 0.0f, oct));
    l.add (std::make_unique<F> (id (filterLp), "Filter Low-pass", NormalisableRange<float> (-3.0f, 3.0f, 0.01f), 0.0f, oct));
    l.add (std::make_unique<F> (id (filterReso), "Filter Resonance", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, pct));
    l.add (std::make_unique<F> (id (squashAmt), "Squash Amount", NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f, spct));
    l.add (std::make_unique<F> (id (squashSpeed), "Squash Speed", NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f, spct));
    l.add (std::make_unique<F> (id (hitAttack), "Hit Attack", NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f, spct));
    l.add (std::make_unique<F> (id (hitSustain), "Hit Sustain", NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f, spct));
    l.add (std::make_unique<F> (id (roomSize), "Room Size", NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f, spct));
    l.add (std::make_unique<F> (id (roomDecay), "Room Decay", NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f, spct));
    return l;
}
