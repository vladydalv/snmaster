#pragma once

#include <vector>
#include <utility>
#include "Parameters.h"

/** Заводські пресети. Параметри, яких немає в списку, беруть значення за замовчуванням.
    Це стартові точки: Threshold і Gain лімітера підганяються під рівень конкретного міксу. */
struct FactoryPreset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;
};

inline const std::vector<FactoryPreset>& getFactoryPresets()
{
    using namespace ParamIDs;
    static const std::vector<FactoryPreset> presets
    {
        { "Init", {} },

        // Класичний стоунер: тепло, щільний низ, прибраний бубнів, живі динаміки
        { "Stoner", {
            { eqOn, 1 }, { hpfFreq, 25 },
            { lowGain, 2.0f }, { lowFreq, 80 },
            { midGain, -1.5f }, { midFreq, 350 }, { midQ, 0.9f },
            { highGain, 1.0f }, { highFreq, 8000 },
            { compOn, 1 }, { threshold, -14 }, { ratio, 2.0f }, { attack, 30 }, { release, 200 },
            { knee, 8 }, { scHpf, 100 }, { makeup, 1.5f }, { compMix, 70 },
            { satOn, 1 }, { drive, 35 }, { satMix, 60 },
            { widthOn, 1 }, { width, 100 }, { monoBass, 120 },
            { limOn, 1 }, { limGain, 3 }, { ceiling, -1 }, { limRel, 120 } } },

        // Сучасний стоунер: гучніше, тугіший низ, чистіші низькі середні, більше повітря
        { "Modern Stoner", {
            { eqOn, 1 }, { hpfFreq, 30 },
            { lowGain, 1.5f }, { lowFreq, 60 },
            { midGain, -2.0f }, { midFreq, 300 }, { midQ, 1.0f },
            { highGain, 2.0f }, { highFreq, 10000 },
            { compOn, 1 }, { threshold, -16 }, { ratio, 3.0f }, { attack, 20 }, { release, 120 },
            { knee, 6 }, { scHpf, 120 }, { makeup, 2.5f }, { compMix, 100 },
            { satOn, 1 }, { drive, 25 }, { satMix, 50 },
            { widthOn, 1 }, { width, 110 }, { monoBass, 150 },
            { limOn, 1 }, { limGain, 6 }, { ceiling, -1 }, { limRel, 50 } } },

        // Психоделік: м'який клей, стрічкове тепло, ширина і повітря
        { "Psychedelic", {
            { eqOn, 1 }, { hpfFreq, 20 },
            { lowGain, 1.0f }, { lowFreq, 100 },
            { midGain, -1.0f }, { midFreq, 500 }, { midQ, 0.7f },
            { highGain, 2.5f }, { highFreq, 12000 },
            { compOn, 1 }, { threshold, -12 }, { ratio, 1.8f }, { attack, 40 }, { release, 300 },
            { knee, 10 }, { scHpf, 80 }, { makeup, 1.0f }, { compMix, 60 },
            { satOn, 1 }, { drive, 30 }, { satMix, 50 },
            { widthOn, 1 }, { width, 125 }, { monoBass, 100 },
            { limOn, 1 }, { limGain, 2 }, { ceiling, -1 }, { limRel, 150 } } },

        // Спейс-рок: широка стереобаза, багато повітря, повільний реліз для «хвостів»
        { "Space Rock", {
            { eqOn, 1 }, { hpfFreq, 22 },
            { lowGain, 1.0f }, { lowFreq, 70 },
            { midGain, -1.5f }, { midFreq, 600 }, { midQ, 0.7f },
            { highGain, 3.0f }, { highFreq, 14000 },
            { compOn, 1 }, { threshold, -14 }, { ratio, 2.0f }, { attack, 50 }, { release, 400 },
            { knee, 10 }, { scHpf, 90 }, { makeup, 1.5f }, { compMix, 80 },
            { satOn, 1 }, { drive, 15 }, { satMix, 40 },
            { widthOn, 1 }, { width, 140 }, { monoBass, 120 },
            { limOn, 1 }, { limGain, 3 }, { ceiling, -1 }, { limRel, 200 } } },

        // Альтернатива / гранж: присутність гітар і вокалу, панч, помірна гучність
        { "Alternative / Grunge", {
            { eqOn, 1 }, { hpfFreq, 30 },
            { lowGain, 1.0f }, { lowFreq, 100 },
            { midGain, 1.5f }, { midFreq, 1500 }, { midQ, 0.8f },
            { highGain, 0.5f }, { highFreq, 8000 },
            { compOn, 1 }, { threshold, -15 }, { ratio, 3.0f }, { attack, 15 }, { release, 100 },
            { knee, 4 }, { scHpf, 110 }, { makeup, 2.5f }, { compMix, 85 },
            { satOn, 1 }, { drive, 40 }, { satMix, 50 },
            { widthOn, 1 }, { width, 105 }, { monoBass, 130 },
            { limOn, 1 }, { limGain, 5 }, { ceiling, -1 }, { limRel, 60 } } },
    };
    return presets;
}
