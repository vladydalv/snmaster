#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ParamIDs
{
    // Вхід / вихід
    inline constexpr auto inGain    = "inGain";
    inline constexpr auto outGain   = "outGain";

    // EQ
    inline constexpr auto eqOn      = "eqOn";
    inline constexpr auto hpfFreq   = "hpfFreq";
    inline constexpr auto lowGain   = "lowGain";
    inline constexpr auto lowFreq   = "lowFreq";
    inline constexpr auto midGain   = "midGain";
    inline constexpr auto midFreq   = "midFreq";
    inline constexpr auto midQ      = "midQ";
    inline constexpr auto highGain  = "highGain";
    inline constexpr auto highFreq  = "highFreq";

    // Компресор
    inline constexpr auto compOn    = "compOn";
    inline constexpr auto threshold = "threshold";
    inline constexpr auto ratio     = "ratio";
    inline constexpr auto attack    = "attack";
    inline constexpr auto release   = "release";
    inline constexpr auto knee      = "knee";
    inline constexpr auto scHpf     = "scHpf";
    inline constexpr auto makeup    = "makeup";
    inline constexpr auto compMix   = "compMix";
    inline constexpr auto compAuto  = "compAuto";

    // Сатурація
    inline constexpr auto satOn     = "satOn";
    inline constexpr auto drive     = "drive";
    inline constexpr auto satMix    = "satMix";
    inline constexpr auto satType   = "satType";

    // Стерео
    inline constexpr auto widthOn   = "widthOn";
    inline constexpr auto width     = "width";
    inline constexpr auto monoBass  = "monoBass";

    // Лімітер
    inline constexpr auto limOn     = "limOn";
    inline constexpr auto limGain   = "limGain";
    inline constexpr auto ceiling   = "ceiling";
    inline constexpr auto limRel    = "limRel";

    // Порівняння на однаковій гучності
    inline constexpr auto gainMatch = "gainMatch";
}

namespace ParamText
{
    inline juce::String db (float v, int)   { return juce::String (v, 1) + " dB"; }
    inline juce::String hz (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                            : juce::String (juce::roundToInt (v)) + " Hz";
    }
    inline juce::String ms (float v, int)   { return v < 10.0f ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; }
    inline juce::String pct (float v, int)  { return juce::String (juce::roundToInt (v)) + " %"; }
    inline juce::String ratio (float v, int){ return juce::String (v, v < 10.0f ? 1 : 0) + ":1"; }
    inline juce::String q (float v, int)    { return juce::String (v, 2); }
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    using F = AudioParameterFloat;
    using B = AudioParameterBool;
    using A = AudioParameterFloatAttributes;

    auto range = [] (float lo, float hi, float centre)
    {
        NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    };
    auto lin = [] (float lo, float hi, float step = 0.0f) { return NormalisableRange<float> (lo, hi, step); };

    const auto dB  = A().withStringFromValueFunction (ParamText::db).withLabel ("dB");
    const auto Hz  = A().withStringFromValueFunction (ParamText::hz).withLabel ("Hz");
    const auto ms  = A().withStringFromValueFunction (ParamText::ms).withLabel ("ms");
    const auto pct = A().withStringFromValueFunction (ParamText::pct).withLabel ("%");

    const auto monoText = A().withStringFromValueFunction ([] (float v, int)
    {
        return v < 20.0f ? String ("Off") : ParamText::hz (v, 0);
    }).withLabel ("Hz");

    const auto hpfText = A().withStringFromValueFunction ([] (float v, int)
    {
        return v < 15.0f ? String ("Off") : ParamText::hz (v, 0);
    }).withLabel ("Hz");

    AudioProcessorValueTreeState::ParameterLayout layout;
    auto id = [] (const char* s) { return ParameterID { s, 1 }; };

    layout.add (std::make_unique<F> (id (ParamIDs::inGain),  "Input",  lin (-24.0f, 24.0f), 0.0f, dB));
    layout.add (std::make_unique<F> (id (ParamIDs::outGain), "Output", lin (-24.0f, 0.0f),  0.0f, dB));

    layout.add (std::make_unique<B> (id (ParamIDs::eqOn), "EQ On", true));
    layout.add (std::make_unique<F> (id (ParamIDs::hpfFreq),  "Low Cut",    range (10.0f, 300.0f, 40.0f),     10.0f,   hpfText));
    layout.add (std::make_unique<F> (id (ParamIDs::lowGain),  "Low Gain",   lin (-12.0f, 12.0f), 0.0f, dB));
    layout.add (std::make_unique<F> (id (ParamIDs::lowFreq),  "Low Freq",   range (30.0f, 500.0f, 110.0f),    100.0f,  Hz));
    layout.add (std::make_unique<F> (id (ParamIDs::midGain),  "Mid Gain",   lin (-12.0f, 12.0f), 0.0f, dB));
    layout.add (std::make_unique<F> (id (ParamIDs::midFreq),  "Mid Freq",   range (200.0f, 8000.0f, 1200.0f), 1000.0f, Hz));
    layout.add (std::make_unique<F> (id (ParamIDs::midQ),     "Mid Q",      range (0.3f, 4.0f, 1.0f),         0.7f,
                                     A().withStringFromValueFunction (ParamText::q)));
    layout.add (std::make_unique<F> (id (ParamIDs::highGain), "High Gain",  lin (-12.0f, 12.0f), 0.0f, dB));
    layout.add (std::make_unique<F> (id (ParamIDs::highFreq), "High Freq",  range (2000.0f, 20000.0f, 8000.0f), 10000.0f, Hz));

    layout.add (std::make_unique<B> (id (ParamIDs::compOn), "Comp On", true));
    layout.add (std::make_unique<F> (id (ParamIDs::threshold), "Threshold", lin (-40.0f, 0.0f), -12.0f, dB));
    layout.add (std::make_unique<F> (id (ParamIDs::ratio),     "Ratio",     range (1.0f, 10.0f, 2.5f), 2.0f,
                                     A().withStringFromValueFunction (ParamText::ratio)));
    layout.add (std::make_unique<F> (id (ParamIDs::attack),    "Attack",    range (0.1f, 100.0f, 10.0f),  10.0f,  ms));
    layout.add (std::make_unique<F> (id (ParamIDs::release),   "Release",   range (10.0f, 1000.0f, 150.0f), 150.0f, ms));
    layout.add (std::make_unique<F> (id (ParamIDs::knee),      "Knee",      lin (0.0f, 12.0f), 6.0f, dB));
    layout.add (std::make_unique<F> (id (ParamIDs::scHpf),     "SC Filter", range (20.0f, 300.0f, 90.0f), 90.0f, Hz));
    layout.add (std::make_unique<F> (id (ParamIDs::makeup),    "Makeup",    lin (0.0f, 18.0f), 0.0f, dB));
    layout.add (std::make_unique<F> (id (ParamIDs::compMix),   "Comp Mix",  lin (0.0f, 100.0f), 100.0f, pct));
    layout.add (std::make_unique<B> (id (ParamIDs::compAuto),  "Auto Release", false));

    layout.add (std::make_unique<B> (id (ParamIDs::satOn), "Sat On", false));
    layout.add (std::make_unique<F> (id (ParamIDs::drive),  "Drive",   lin (0.0f, 100.0f), 25.0f, pct));
    layout.add (std::make_unique<F> (id (ParamIDs::satMix), "Sat Mix", lin (0.0f, 100.0f), 100.0f, pct));
    layout.add (std::make_unique<AudioParameterChoice> (id (ParamIDs::satType), "Sat Type", StringArray { "Tube", "Tape", "Soft" }, 0));

    layout.add (std::make_unique<B> (id (ParamIDs::widthOn), "Width On", true));
    layout.add (std::make_unique<F> (id (ParamIDs::width),    "Width",     lin (0.0f, 200.0f), 100.0f, pct));
    layout.add (std::make_unique<F> (id (ParamIDs::monoBass), "Mono Bass", lin (0.0f, 300.0f), 0.0f, monoText));

    layout.add (std::make_unique<B> (id (ParamIDs::limOn), "Limiter On", true));
    layout.add (std::make_unique<F> (id (ParamIDs::limGain), "Gain",    lin (0.0f, 18.0f), 0.0f, dB));
    layout.add (std::make_unique<F> (id (ParamIDs::ceiling), "Ceiling", lin (-3.0f, 0.0f), -1.0f, dB));
    layout.add (std::make_unique<F> (id (ParamIDs::limRel),  "Lim Release", range (1.0f, 500.0f, 60.0f), 60.0f, ms));

    layout.add (std::make_unique<B> (id (ParamIDs::gainMatch), "Gain Match", false));

    return layout;
}
