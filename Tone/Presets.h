#pragma once

#include <vector>
#include <utility>
#include "Parameters.h"

/** Заводські пресети Tone. Незгадані параметри — за замовчуванням.
    Стадії відкалібровані на номінальний рівень -18 dBFS (типовий запис у звукову карту). */
struct TonePreset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;
};

inline const std::vector<TonePreset>& getTonePresets()
{
    using namespace ToneIDs;
    static const std::vector<TonePreset> presets
    {
        { "Init", {} },

        { "Vocal: Warm", {
            { tubeOn, 1 }, { tubeDrive, 35 }, { tubeBias, 45 },
            { tapeOn, 1 }, { tapeDrive, 20 }, { tapeSpeed, 1 },
            { dsOn, 1 }, { dsFreq, 5500 }, { dsSens, 50 }, { dsRange, 6 } } },

        { "Vocal: Air & Presence", {
            { tubeOn, 1 }, { tubeDrive, 25 }, { tubeBias, 35 },
            { tapeOn, 1 }, { tapeDrive, 15 }, { tapeSpeed, 2 },
            { excOn, 1 }, { excFreq, 6000 }, { excAmount, 30 },
            { dsOn, 1 }, { dsFreq, 6000 }, { dsSens, 55 }, { dsRange, 8 } } },

        { "DI Guitar Polish (after amp sim)", {
            { trOn, 1 }, { trAttack, 15 },
            { tubeOn, 1 }, { tubeDrive, 30 }, { tubeBias, 40 },
            { tapeOn, 1 }, { tapeDrive, 35 }, { tapeSpeed, 1 },
            { excOn, 1 }, { excFreq, 3500 }, { excAmount, 15 } } },

        { "Fuzz Guitar Thick", {
            { trOn, 1 }, { trSustain, 20 },
            { tubeOn, 1 }, { tubeDrive, 20 }, { tubeBias, 50 },
            { tapeOn, 1 }, { tapeDrive, 45 }, { tapeSpeed, 0 } } },

        { "Bass: Round & Solid", {
            { trOn, 1 }, { trAttack, 20 },
            { tubeOn, 1 }, { tubeDrive, 40 }, { tubeBias, 60 },
            { tapeOn, 1 }, { tapeDrive, 40 }, { tapeSpeed, 1 } } },

        { "Drums: Punch", {
            { trOn, 1 }, { trAttack, 40 }, { trSustain, -10 },
            { tubeOn, 0 },
            { tapeOn, 1 }, { tapeDrive, 25 }, { tapeSpeed, 2 } } },

        { "Drum Bus: Tape Glue", {
            { trOn, 1 }, { trAttack, 15 },
            { tubeOn, 1 }, { tubeDrive, 15 },
            { tapeOn, 1 }, { tapeDrive, 45 }, { tapeSpeed, 1 },
            { mix, 80 } } },

        { "Mix Bus: Tape 30 ips", {
            { tubeOn, 1 }, { tubeDrive, 15 }, { tubeBias, 30 },
            { tapeOn, 1 }, { tapeDrive, 20 }, { tapeSpeed, 2 } } },

        { "Lo-Fi Psych", {
            { tubeOn, 1 }, { tubeDrive, 45 }, { tubeBias, 70 },
            { tapeOn, 1 }, { tapeDrive, 55 }, { tapeSpeed, 0 }, { tapeWow, 60 } } },
    };
    return presets;
}
