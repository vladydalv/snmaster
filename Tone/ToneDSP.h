#pragma once

#include "../Common/Saturation.h"

namespace sn
{
//==============================================================================
/** Транзієнт-шейпер. Працює з різницями обвідних у дБ, тому не залежить від рівня
    (не треба підганяти поріг, як у більшості шейперів). Стерео зв'язане. */
class TransientShaper
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        auto c = [this] (double ms) { return (float) std::exp (-1.0 / (0.001 * ms * fs)); };
        fastA = c (0.5); slowA = c (25.0); relA = c (60.0);
        fastR = c (30.0); slowR = c (300.0); atkR = c (0.5);
        smooth = c (0.3);
        reset();
    }

    void reset() { eFA = eSA = eFR = eSR = 0.0f; gDb = 0.0f; }

    void process (float* const* data, int numCh, int n, float attackPct, float sustainPct)
    {
        const float a = attackPct * 0.01f, s = sustainPct * 0.01f;
        for (int i = 0; i < n; ++i)
        {
            float x = 0.0f;
            for (int c = 0; c < numCh; ++c) x = std::max (x, std::abs (data[c][i]));

            follow (eFA, x, fastA, relA);
            follow (eSA, x, slowA, relA);
            follow (eFR, x, atkR, fastR);
            follow (eSR, x, atkR, slowR);

            float target = 0.0f;
            if (eFR > 3.0e-4f)   // нижче -70 dBFS не чіпаємо
            {
                const float attackDiff  = juce::jlimit (0.0f, 18.0f, gainToDb (eFA) - gainToDb (eSA));
                const float sustainDiff = juce::jlimit (0.0f, 18.0f, gainToDb (eSR) - gainToDb (eFR));
                target = juce::jlimit (-15.0f, 15.0f, 0.8f * (a * attackDiff + s * sustainDiff));
            }
            gDb = target + smooth * (gDb - target);
            const float g = dbToGain (gDb);
            for (int c = 0; c < numCh; ++c) data[c][i] *= g;
        }
    }

private:
    static void follow (float& e, float x, float atk, float rel) noexcept
    {
        e = x > e ? x + atk * (e - x) : x + rel * (e - x);
    }

    double fs = 48000.0;
    float fastA = 0, slowA = 0, relA = 0, fastR = 0, slowR = 0, atkR = 0, smooth = 0;
    float eFA = 0, eSA = 0, eFR = 0, eSR = 0, gDb = 0;
};

//==============================================================================
/** Ексайтер: генерує гармоніки лише з верхньої смуги (без бруду на середині),
    парні + непарні, і додає їх паралельно. Працює в оверсемплованому домені. */
class Exciter
{
public:
    void prepare (double osRate, int numChannels)
    {
        fs = osRate;
        ch.assign ((size_t) numChannels, {});
        lastFreq = -1.0f;
    }

    void reset() { for (auto& c : ch) { c.pre.reset(); c.post.reset(); } }

    void process (juce::dsp::AudioBlock<float>& block, float freq, float amountPct)
    {
        if (freq != lastFreq)
        {
            Biquad hp; hp.setHighPass (fs, freq);
            for (auto& c : ch) { c.pre.copyCoeffs (hp); c.post.copyCoeffs (hp); }
            lastFreq = freq;
        }

        constexpr float drive = 4.0f, bias = 0.2f;
        const float tb = std::tanh (bias);
        const float lin = 1.0f / (drive * (1.0f - tb * tb));
        const float amt = 2.0f * amountPct * 0.01f;

        for (size_t c = 0; c < block.getNumChannels() && c < ch.size(); ++c)
        {
            auto& st = ch[c];
            auto* d = block.getChannelPointer (c);
            for (size_t i = 0; i < block.getNumSamples(); ++i)
            {
                const float h = st.pre.process (d[i]);
                const float shaped = (std::tanh (drive * h + bias) - tb) * lin;
                const float harmonics = st.post.process (shaped - h);   // лише продукти нелінійності
                d[i] += amt * harmonics;
            }
        }
    }

private:
    struct Channel { Biquad pre, post; };
    std::vector<Channel> ch;
    double fs = 176400.0;
    float lastFreq = -1.0f;
};

//==============================================================================
/** Де-есер з відносним детектором: порівнює рівень смуги сибілянтів з рівнем
    усього сигналу. Тому однаково працює на тихих і гучних фразах — поріг не «пливе».
    Ослаблює лише верхню смугу (split-band), у спокої сигнал проходить без змін. */
class DeEsser
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        fs = sampleRate;
        band.assign ((size_t) numChannels, {});
        det.reset();
        auto c = [this] (double ms) { return (float) std::exp (-1.0 / (0.001 * ms * fs)); };
        envAtk = c (0.5); envRel = c (40.0); gAtk = c (1.0); gRel = c (60.0);
        lastFreq = -1.0f;
        reset();
    }

    void reset()
    {
        for (auto& b : band) b.reset();
        det.reset();
        eBand = eFull = 0.0f; grDb = 0.0f;
    }

    /** Повертає максимальне ослаблення (дБ) за блок. */
    float process (float* const* data, int numCh, int n, float freq, float sensPct, float rangeDb, bool listen)
    {
        if (freq != lastFreq)
        {
            Biquad hp; hp.setHighPass (fs, freq);
            for (auto& b : band) b.copyCoeffs (hp);
            det.copyCoeffs (hp);
            lastFreq = freq;
        }

        const float thr = -2.0f - 0.18f * sensPct;      // відносний поріг: -2 … -20 дБ
        float maxGr = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            float full = 0.0f, hi = 0.0f;
            float hb[2] = { 0.0f, 0.0f };
            for (int c = 0; c < numCh && c < 2; ++c)
            {
                hb[c] = band[(size_t) c].process (data[c][i]);
                full = std::max (full, std::abs (data[c][i]));
                hi = std::max (hi, std::abs (hb[c]));
            }

            eBand = hi > eBand ? hi + envAtk * (eBand - hi) : hi + envRel * (eBand - hi);
            eFull = full > eFull ? full + envAtk * (eFull - full) : full + envRel * (eFull - full);

            float target = 0.0f;
            if (eBand > 1.8e-3f)   // > -55 dBFS
            {
                const float rel = gainToDb (eBand) - gainToDb (eFull);
                target = juce::jlimit (0.0f, rangeDb, (rel - thr) * 1.5f);
            }
            grDb = target > grDb ? target + gAtk * (grDb - target) : target + gRel * (grDb - target);
            maxGr = std::max (maxGr, grDb);

            const float keep = 1.0f - dbToGain (-grDb);
            for (int c = 0; c < numCh && c < 2; ++c)
                data[c][i] = listen ? hb[c] : data[c][i] - hb[c] * keep;
        }
        return maxGr;
    }

private:
    double fs = 48000.0;
    std::vector<Biquad> band;
    Biquad det;
    float envAtk = 0, envRel = 0, gAtk = 0, gRel = 0;
    float eBand = 0, eFull = 0, grDb = 0, lastFreq = -1.0f;
};
} // namespace sn
