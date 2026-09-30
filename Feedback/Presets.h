#pragma once

#include <vector>
#include <utility>
#include "Parameters.h"

struct FbPreset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;
};

inline const std::vector<FbPreset>& getFeedbackPresets()
{
    using namespace FbIDs;
    static const std::vector<FbPreset> presets
    {
        { "Init", {} },
        // Повільне наростання на октаву — класика стоунер-рифу, що «висить»
        { "Stoner Drone", { { trigger, 0 }, { delay, 1.5f }, { harmonic, 1 }, { distance, 70 },
                            { amount, 75 }, { morph, 60 }, { tone, 2000 }, { drift, 20 } } },
        // Близько до кабінету: швидкий зрив на квінту, яскраво
        { "Close Squeal",  { { trigger, 2 }, { delay, 0.5f }, { harmonic, 2 }, { distance, 10 },
                            { amount, 70 }, { morph, 40 }, { tone, 4500 }, { drift, 15 } } },
        // Вокальне «виття» на основному тоні, керування кнопкою Hold
        { "Wailing Lead",  { { trigger, 2 }, { harmonic, 0 }, { distance, 35 },
                            { amount, 80 }, { morph, 50 }, { tone, 3000 }, { drift, 30 } } },
        // Далеко, м'яко, повільно: струна розчиняється у фідбеку
        { "Space Swell",   { { trigger, 0 }, { delay, 1.0f }, { harmonic, 3 }, { distance, 95 },
                            { amount, 70 }, { morph, 85 }, { tone, 1600 }, { drift, 45 } } },
        // Психоделічний «живий» фідбек з непередбачуваною гармонікою
        { "Psych Chaos",   { { trigger, 2 }, { delay, 0.8f }, { harmonic, 3 }, { distance, 45 },
                            { amount, 85 }, { morph, 60 }, { tone, 3500 }, { drift, 70 } } },
    };
    return presets;
}
