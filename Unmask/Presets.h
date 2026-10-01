#pragma once

#include "Parameters.h"

struct UnmaskPreset { const char* name; std::vector<std::pair<const char*, float>> values; };

inline const std::vector<UnmaskPreset>& getUnmaskPresets()
{
    using namespace UnmaskIDs;
    static const std::vector<UnmaskPreset> p {
        { "Init",                       {} },
        { "Bass ducks under Kick",      { { range, 0 }, { depth, 5.0f }, { width, 1.2f }, { attack, 1.5f }, { release, 90.0f }, { sens, 60 } } },
        { "Guitars make room for Vocal", { { range, 2 }, { depth, 3.0f }, { width, 1.0f }, { attack, 10.0f }, { release, 180.0f }, { sens, 55 } } },
        { "Keys make room for Vocal",   { { range, 2 }, { depth, 3.5f }, { width, 1.0f }, { attack, 10.0f }, { release, 200.0f }, { sens, 55 } } },
        { "Guitars under Snare crack",  { { range, 2 }, { depth, 2.5f }, { width, 1.6f }, { attack, 1.0f }, { release, 70.0f }, { sens, 65 } } },
        { "Synth pad ducks under Bass", { { range, 1 }, { depth, 4.0f }, { width, 0.9f }, { attack, 8.0f }, { release, 200.0f }, { sens, 50 } } },
    };
    return p;
}
