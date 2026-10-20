#pragma once

#include "Parameters.h"

/** Заводські пресети: кожен стоїть на карті (рік × кімната), тож епоху видно до прослуховування. */
struct DecadePreset { const char* name; float year, room; std::vector<std::pair<const char*, float>> values; };

inline const std::vector<DecadePreset>& getDecadePresets()
{
    using namespace DecIDs;
    static const std::vector<DecadePreset> p {
        { "Init",                    1972, 40, {} },
        // 1950-ті
        { "'52 Radio Session",       1952, 60, {} },
        { "'56 Rockabilly Room",     1956, 45, { { gritDrive, 30 } } },
        { "'59 Echo Chamber Brushes", 1959, 75, { { hitAttack, -30 } } },
        // 1960-ті
        { "'63 Surf Shack",          1963, 55, { { roomType, 5 }, { toneTop, 1.5f } } },
        { "'65 Four-Track Pop",      1965, 35, {} },
        { "'67 Psych Plate",         1967, 50, { { roomType, 3 }, { squashAmt, 40 } } },
        { "'69 Damped Toms",         1969, 15, { { squashAmt, 50 }, { toneTop, -1.5f } } },
        // 1970-ті
        { "'71 Stairwell Thunder",   1971, 92, { { roomType, 2 }, { roomSize, 40 }, { squashAmt, 30 } } },
        { "'72 Big Room Smash",      1972, 75, { { squashAmt, 40 } } },
        { "'74 Desert Garage",       1974, 55, { { roomType, 5 }, { gritDrive, 35 }, { toneLow, 1.5f } } },
        { "'76 Funk Dry",            1976,  5, { { hitAttack, 30 } } },
        { "'77 Towels On",           1977,  0, {} },
        { "'79 Disco Tight",         1979, 15, { { toneTop, 2 }, { hitAttack, 20 } } },
        // 1980-ті
        { "'81 New Wave Snap",       1981, 40, { { hitAttack, 25 } } },
        { "'84 Gated Snare",         1984, 70, {} },
        { "'86 Arena Hall",          1986, 90, { { roomType, 4 } } },
        { "'88 Drum Machine Grit",   1988, 30, { { gritChar, 3 }, { gritDrive, 40 } } },
        { "'89 Hair Metal Huge",     1989, 85, { { squashAmt, 30 }, { toneTop, 1 } } },
        // 1990-ті
        { "'91 Grunge Live Room",    1991, 70, {} },
        { "'93 Garage Punk",         1993, 50, { { roomType, 5 }, { gritDrive, 40 } } },
        { "'94 Heavy Stoner Bus",    1994, 60, { { gritDrive, 50 }, { toneLow, 2 }, { filterLp, -0.6f } } },
        { "'97 Britpop Shine",       1997, 40, { { toneTop, 1.5f } } },
        { "'99 Nu-Metal Tight",      1999, 15, { { squashAmt, 40 }, { hitAttack, 30 } } },
        // 2000-ні
        { "'02 Loud And Proud",      2002, 20, {} },
        { "'05 Indie Garage",        2005, 45, { { roomType, 5 }, { gritDrive, 20 } } },
        { "'08 Sample Punch",        2008, 10, { { hitAttack, 40 } } },
        // 2010-ті і зараз
        { "'12 Modern Rock",         2012, 30, {} },
        { "'15 Parallel Smash",      2015, 35, { { squashAmt, 60 } } },
        { "'18 Lo-Fi Bedroom",       2018, 20, { { gritChar, 3 }, { gritDrive, 30 }, { format, 3 }, { fmtAmount, 70 } } },
        { "'21 Desert Fuzz Kit",     2021, 55, { { roomType, 5 }, { gritDrive, 40 }, { toneLow, 1.5f } } },
        { "'24 Natural Room",        2024, 60, {} },
        { "'25 Modern Stoner",       2025, 50, { { gritDrive, 30 }, { toneLow, 2 }, { squashAmt, 20 } } },
        // Експерименти
        { "X Vinyl Breakbeat",       1971, 30, { { format, 2 }, { fmtAmount, 80 }, { fmtAge, 70 }, { hitAttack, 20 } } },
        { "X Tape Dub Room",         1975, 80, { { roomType, 3 }, { format, 3 }, { fmtAmount, 60 } } },
        { "X Radio From Mars",       1950, 70, { { format, 1 }, { fmtAmount, 100 }, { fmtAge, 80 } } },
        { "X MP3 Nostalgia",         2003, 20, { { format, 5 }, { fmtAmount, 75 } } },
    };
    return p;
}
