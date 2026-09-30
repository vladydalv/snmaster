#pragma once

#include <vector>
#include <utility>
#include "Parameters.h"

/** Заводські пресети Stomp. Незгадані параметри — за замовчуванням. */
struct StompPreset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;
};

inline const std::vector<StompPreset>& getStompPresets()
{
    using namespace StompIDs;
    static const std::vector<StompPreset> presets
    {
        { "Init", {} },
        { "Stoner Wall (Triangle)",      { { circuit, 2.0f }, { gain, 78 }, { tone, -15 } } },
        { "Desert Germanium",            { { circuit, 0.0f }, { gain, 72 }, { tone, -10 }, { battery, 15 } } },
        { "British Crunch (Orange-ish)", { { circuit, 1.0f }, { gain, 42 }, { tone, 10 } } },
        { "British Lead",                { { circuit, 1.3f }, { gain, 80 }, { tone, 5 } } },
        { "Grunge Op-amp",               { { circuit, 3.0f }, { gain, 65 }, { tone, -5 } } },
        { "Modern Tight",                { { circuit, 4.0f }, { gain, 70 } } },
        { "Dying Battery Sputter",       { { circuit, 0.0f }, { gain, 90 }, { battery, 80 } } },
        { "Bass Fuzz: Clean Low End",    { { circuit, 2.0f }, { gain, 70 }, { tone, 10 }, { cleanBass, 150 } } },
        { "Bass Drive: Growl",           { { circuit, 1.0f }, { gain, 50 }, { cleanBass, 100 } } },
        { "Rose Trem (Pan)",             { { circuit, 1.0f }, { gain, 40 },
                                           { modOn, 1 }, { modMode, 2 }, { rate, 6.0f }, { depth, 85 }, { shape, 45 } } },
        { "Choppy Trem 1/8",             { { driveOn, 0 }, { modOn, 1 }, { modMode, 0 }, { modSync, 2 }, { depth, 90 }, { shape, 80 } } },
        { "Psych Harmonic Vibe",         { { circuit, 0.5f }, { gain, 50 },
                                           { modOn, 1 }, { modMode, 1 }, { rate, 4.0f }, { depth, 75 }, { shape, 10 },
                                           { echoOn, 1 }, { echoTime, 420 }, { feedback, 40 }, { echoMix, 25 }, { wear, 50 } } },
        { "Singer Vibrato (Rise)",       { { driveOn, 0 }, { modOn, 1 }, { modMode, 3 }, { rate, 5.5f }, { depth, 45 }, { rise, 600 } } },
        { "Space Echo Fuzz",             { { circuit, 2.5f }, { gain, 70 },
                                           { echoOn, 1 }, { echoSync, 2 }, { feedback, 60 }, { echoTone, 2500 }, { echoMix, 35 }, { wear, 45 } } },
        { "Slapback Crunch",             { { circuit, 1.0f }, { gain, 35 },
                                           { echoOn, 1 }, { echoTime, 110 }, { feedback, 10 }, { echoMix, 30 }, { wear, 20 } } },
    };
    return presets;
}
