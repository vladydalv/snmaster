#pragma once

#include "SNCommon.h"
#include "Saturation.h"

namespace sn
{
//==============================================================================
/** Stereo-linked feed-forward компресор, soft knee, HPF у сайдчейні, паралельний мікс. */
class Compressor
{
public:
    void prepare (double sampleRate, int /*numChannels ≤ 2*/)
    {
        fs = sampleRate;
        lastScFreq = -1.0f;
        reset();
    }

    void reset()
    {
        envDb = slowDb = 0.0f;
        for (auto& f : scFilters) f.reset();
    }

    struct Settings { float thresholdDb, ratio, attackMs, releaseMs, kneeDb, scHpfHz, makeupDb, mix; bool autoRelease; };

    /** Повертає максимальне зменшення підсилення (дБ, додатне) за блок. */
    float process (float* const* data, int numCh, int n, const Settings& s)
    {
        numCh = std::min (2, numCh);

        if (std::abs (s.scHpfHz - lastScFreq) > 0.01f)
        {
            for (auto& f : scFilters) f.setHighPass (fs, s.scHpfHz);
            lastScFreq = s.scHpfHz;
        }

        const float aA = std::exp (-1.0f / (0.001f * s.attackMs  * (float) fs));
        const float aR = std::exp (-1.0f / (0.001f * s.releaseMs * (float) fs));
        // Auto release: повільна обвідна з повільною атакою утримує лише тривалу компресію
        const float sA = std::exp (-1.0f / (0.001f * (10.0f * s.attackMs + 60.0f) * (float) fs));
        const float sR = std::exp (-1.0f / (0.001f * (5.0f * s.releaseMs) * (float) fs));
        const float slope = 1.0f / s.ratio - 1.0f;
        const float W = s.kneeDb;
        const float makeup = dbToGain (s.makeupDb);
        const float wet = s.mix, dry = 1.0f - s.mix;

        float maxGr = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            float sc = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                sc = std::max (sc, std::abs (scFilters[(size_t) ch].process (data[ch][i])));

            const float x = gainToDb (sc);
            const float over = x - s.thresholdDb;
            float gc; // ≤ 0

            if (2.0f * over < -W)             gc = 0.0f;
            else if (2.0f * std::abs (over) <= W && W > 0.0f)
            {
                const float t = over + 0.5f * W;
                gc = slope * t * t / (2.0f * W);
            }
            else                              gc = slope * over;

            const float target = -gc;
            const float a = target > envDb ? aA : aR;
            envDb = a * envDb + (1.0f - a) * target;

            float gr = envDb;
            if (s.autoRelease)
            {
                const float b = target > slowDb ? sA : sR;
                slowDb = b * slowDb + (1.0f - b) * target;
                gr = std::max (envDb, slowDb);
            }

            maxGr = std::max (maxGr, gr);
            const float g = dbToGain (-gr) * makeup;

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float in = data[ch][i];
                data[ch][i] = dry * in + wet * in * g;
            }
        }

        return maxGr;
    }

private:
    double fs = 44100.0;
    float envDb = 0.0f, slowDb = 0.0f;
    float lastScFreq = -1.0f;
    std::array<Biquad, 2> scFilters;
};

//==============================================================================
/** True-peak lookahead лімітер.
    Детекція йде по 4x-оверсемплованому сигналу (міжсемплові піки), а підсилення
    застосовується на базовій частоті: плавна обвідна майже не додає нових ISP.
    Ковзний мінімум + коробковий фільтр гарантують, що підсилення вже впало
    до потрібного рівня на момент приходу піку. */
class Limiter
{
public:
    /** detectorDelay: затримка детектора (у семплах базової частоти). */
    void prepare (double sampleRate, int numChannels, int lookaheadSamples, int detectorDelay)
    {
        fs = sampleRate;
        L = std::max (1, lookaheadSamples);
        totalDelay = std::max (0, detectorDelay) + L;
        minFilter.prepare (L);
        box.assign ((size_t) L, 1.0);
        delay.assign ((size_t) numChannels, std::vector<float> ((size_t) totalDelay + 1, 0.0f));
        reset();
    }

    void reset()
    {
        minFilter.reset();
        std::fill (box.begin(), box.end(), 1.0);
        boxSum = (double) L;
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        pos = boxPos = 0;
        p1 = p2 = 0.0f;
        g = gs = 1.0f;
    }

    int getLatency() const noexcept { return totalDelay; }

    /** peaks[i] — пік (true peak) для семпла i, затриманий на detectorDelay.
        Повертає мінімальне підсилення (лінійне) за блок. */
    float process (float* const* data, int numCh, int n, const float* peaks,
                   float ceilingLin, float releaseMs, bool active)
    {
        const float relCoef = std::exp (-1.0f / (0.001f * releaseMs * (float) fs));
        const float slowAtk = std::exp (-1.0f / (0.040f * (float) fs));
        const float slowRel = std::exp (-1.0f / (0.001f * 8.0f * releaseMs * (float) fs));
        const int size = totalDelay + 1;
        float minG = 1.0f;

        for (int i = 0; i < n; ++i)
        {
            // Розширення піку на ±1 семпл: запас на неточність вирівнювання детектора
            const float pk = std::max ({ peaks[i], p1, p2 });
            p2 = p1; p1 = peaks[i];

            const float req = (active && pk > ceilingLin) ? ceilingLin / pk : 1.0f;
            const float h = minFilter.push (req);
            boxSum += (double) h - box[(size_t) boxPos];
            box[(size_t) boxPos] = h;
            boxPos = (boxPos + 1) % L;
            const float s = std::min (1.0f, (float) (boxSum / (double) L));

            g = (s < g) ? s : s + (g - s) * relCoef;
            // Повільна стадія: тримає середнє обмеження на щільному матеріалі (менше пампінгу й спотворень басу)
            gs = (s < gs) ? s + (gs - s) * slowAtk : s + (gs - s) * slowRel;
            const float gOut = std::min (g, gs);
            minG = std::min (minG, gOut);

            const int readPos = (pos + 1) % size;
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& d = delay[(size_t) ch];
                d[(size_t) pos] = data[ch][i];
                const float delayed = d[(size_t) readPos];
                data[ch][i] = active ? delayed * gOut : delayed;
            }
            pos = (pos + 1) % size;
        }
        return minG;
    }

private:
    double fs = 44100.0;
    int L = 1, totalDelay = 1, pos = 0, boxPos = 0;
    float g = 1.0f, gs = 1.0f, p1 = 0.0f, p2 = 0.0f;
    SlidingMin minFilter;
    std::vector<double> box;
    double boxSum = 1.0;
    std::vector<std::vector<float>> delay;
};

} // namespace sn
