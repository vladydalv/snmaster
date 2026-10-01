#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "../Common/Analysis.h"

/** Референсний трек: улюблена платівка, з якою порівнюєш свій мастер.
    Декодується у фоні, переводиться на частоту проєкту, міряються гучність (LUFS) і спектр усього треку. */
struct RefTrack
{
    juce::AudioBuffer<float> audio;     // стерео, на частоті хоста
    float lufs = -100.0f;
    an::Spectrum spectrum {};
    juce::String name;
    juce::File file;

    static std::unique_ptr<RefTrack> load (const juce::File& f, double targetRate)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
        if (reader == nullptr || reader->lengthInSamples <= 0 || targetRate <= 0.0) return nullptr;

        // До 12 хвилин
        const auto len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (720.0 * reader->sampleRate));
        juce::AudioBuffer<float> src (2, len);
        reader->read (&src, 0, len, 0, true, true);
        if (reader->numChannels == 1) src.copyFrom (1, 0, src, 0, 0, len);

        auto r = std::make_unique<RefTrack>();
        r->name = f.getFileNameWithoutExtension();
        r->file = f;
        const double ratio = reader->sampleRate / targetRate;
        const int outLen = (int) std::floor ((double) len / ratio) - 4;
        if (outLen <= 0) return nullptr;
        r->audio.setSize (2, outLen);
        for (int ch = 0; ch < 2; ++ch)
        {
            if (std::abs (ratio - 1.0) < 1.0e-9) { r->audio.copyFrom (ch, 0, src, ch, 0, outLen); continue; }
            juce::LagrangeInterpolator interp;
            interp.process (ratio, src.getReadPointer (ch), r->audio.getWritePointer (ch), outLen);
        }

        // Гучність і спектр усього треку
        sn::LoudnessMeter lm; lm.prepare (targetRate, 2);
        an::Analyzer a; a.prepare (targetRate);
        constexpr int block = 4096;
        for (int pos = 0; pos < outLen; pos += block)
        {
            const int n = std::min (block, outLen - pos);
            juce::AudioBuffer<float> view (r->audio.getArrayOfWritePointers(), 2, pos, n);
            lm.process (view);
            a.push (view.getReadPointer (0), view.getReadPointer (1), n);
        }
        r->lufs = lm.integrated.load();
        r->spectrum = a.song();
        return r;
    }
};
