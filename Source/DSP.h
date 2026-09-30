#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>
#include <vector>
#include <cmath>

namespace sn
{
inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)  noexcept { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

/** Атомарний максимум: аудіопотік пише, UI забирає і обнуляє (peak hold за кадр). */
struct AtomicMax
{
    std::atomic<float> v { 0.0f };
    void push (float x) noexcept
    {
        auto cur = v.load (std::memory_order_relaxed);
        while (x > cur && ! v.compare_exchange_weak (cur, x, std::memory_order_relaxed)) {}
    }
    float take() noexcept { return v.exchange (0.0f, std::memory_order_relaxed); }
};

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
        envDb = 0.0f;
        for (auto& f : scFilters) f.reset();
    }

    struct Settings { float thresholdDb, ratio, attackMs, releaseMs, kneeDb, scHpfHz, makeupDb, mix; };

    /** Повертає максимальне зменшення підсилення (дБ, додатне) за блок. */
    float process (juce::AudioBuffer<float>& buffer, const Settings& s)
    {
        const int numCh = std::min (2, buffer.getNumChannels());
        const int n = buffer.getNumSamples();

        if (std::abs (s.scHpfHz - lastScFreq) > 0.01f)
        {
            auto c = juce::dsp::IIR::Coefficients<float>::makeHighPass (fs, s.scHpfHz);
            for (auto& f : scFilters) f.coefficients = c;
            lastScFreq = s.scHpfHz;
        }

        const float aA = std::exp (-1.0f / (0.001f * s.attackMs  * (float) fs));
        const float aR = std::exp (-1.0f / (0.001f * s.releaseMs * (float) fs));
        const float slope = 1.0f / s.ratio - 1.0f;
        const float W = s.kneeDb;
        const float makeup = dbToGain (s.makeupDb);
        const float wet = s.mix, dry = 1.0f - s.mix;

        float maxGr = 0.0f;
        float* const* data = buffer.getArrayOfWritePointers();

        for (int i = 0; i < n; ++i)
        {
            float sc = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                sc = std::max (sc, std::abs (scFilters[(size_t) ch].processSample (data[ch][i])));

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

            maxGr = std::max (maxGr, envDb);
            const float g = dbToGain (-envDb) * makeup;

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
    float envDb = 0.0f;
    float lastScFreq = -1.0f;
    std::array<juce::dsp::IIR::Filter<float>, 2> scFilters;
};

//==============================================================================
/** Ковзний мінімум за вікно (монотонна черга, O(1) у середньому). */
class SlidingMin
{
public:
    void prepare (int windowSize)
    {
        window = std::max (1, windowSize);
        cap = window + 1;
        vals.assign ((size_t) cap, 1.0f);
        idx.assign ((size_t) cap, 0);
        reset();
    }
    void reset() { head = tail = 0; count = 0; n = 0; }

    float push (float v) noexcept
    {
        while (count > 0 && vals[(size_t) back()] >= v) { tail = (tail - 1 + cap) % cap; --count; }
        vals[(size_t) tail] = v; idx[(size_t) tail] = n; tail = (tail + 1) % cap; ++count;
        while (idx[(size_t) head] <= n - window) { head = (head + 1) % cap; --count; }
        ++n;
        return vals[(size_t) head];
    }

private:
    int back() const noexcept { return (tail - 1 + cap) % cap; }
    std::vector<float> vals;
    std::vector<long long> idx;
    int window = 1, cap = 2, head = 0, tail = 0, count = 0;
    long long n = 0;
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
        g = 1.0f;
    }

    int getLatency() const noexcept { return totalDelay; }

    /** peaks[i] — пік (true peak) для семпла i, затриманий на detectorDelay.
        Повертає мінімальне підсилення (лінійне) за блок. */
    float process (float* const* data, int numCh, int n, const float* peaks,
                   float ceilingLin, float releaseMs, bool active)
    {
        const float relCoef = std::exp (-1.0f / (0.001f * releaseMs * (float) fs));
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
            minG = std::min (minG, g);

            const int readPos = (pos + 1) % size;
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& d = delay[(size_t) ch];
                d[(size_t) pos] = data[ch][i];
                const float delayed = d[(size_t) readPos];
                data[ch][i] = active ? delayed * g : delayed;
            }
            pos = (pos + 1) % size;
        }
        return minG;
    }

private:
    double fs = 44100.0;
    int L = 1, totalDelay = 1, pos = 0, boxPos = 0;
    float g = 1.0f, p1 = 0.0f, p2 = 0.0f;
    SlidingMin minFilter;
    std::vector<double> box;
    double boxSum = 1.0;
    std::vector<std::vector<float>> delay;
};

//==============================================================================
/** М'яка сатурація (tanh) з одиничним підсиленням на рівні близько -12 dBFS. */
struct Saturator
{
    static void process (juce::dsp::AudioBlock<float>& block, float drivePct, float mix)
    {
        const float k = 1.0f + 0.04f * drivePct;           // 1…5
        const float norm = 0.25f / std::tanh (k * 0.25f);  // компенсація рівня
        const float dry = 1.0f - mix;

        for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
        {
            auto* d = block.getChannelPointer (ch);
            for (size_t i = 0; i < block.getNumSamples(); ++i)
            {
                const float x = d[i];
                d[i] = dry * x + mix * std::tanh (k * x) * norm;
            }
        }
    }
};

//==============================================================================
/** Гучність за ITU-R BS.1770 / EBU R128: momentary, short-term, integrated. */
class LoudnessMeter
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        fs = sampleRate;
        numCh = numChannels;
        designKWeighting();
        state.assign ((size_t) numCh, {});
        subBlockLen = std::max (1, (int) std::round (fs * 0.1));
        resetAll();
    }

    void requestReset() noexcept { resetFlag.store (true); }

    void process (const juce::AudioBuffer<float>& buffer)
    {
        if (resetFlag.exchange (false)) resetAll();

        const int n = buffer.getNumSamples();
        const int chs = std::min (numCh, buffer.getNumChannels());

        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < chs; ++ch)
            {
                double x = buffer.getSample (ch, i);
                auto& st = state[(size_t) ch];
                x = biquad (x, pre, st.s1);
                x = biquad (x, rlb, st.s2);
                acc += x * x;
            }

            if (++accCount >= subBlockLen)
                finishSubBlock();
        }
    }

    std::atomic<float> momentary { -100.0f }, shortTerm { -100.0f }, integrated { -100.0f };

    // Для тесту: коефіцієнти K-зважування
    struct Coeffs { double b0, b1, b2, a1, a2; };
    Coeffs pre {}, rlb {};

private:
    struct BiquadState { double z1 = 0, z2 = 0; };
    struct ChannelState { BiquadState s1, s2; };

    static double biquad (double x, const Coeffs& c, BiquadState& s) noexcept
    {
        const double y = c.b0 * x + s.z1;
        s.z1 = c.b1 * x - c.a1 * y + s.z2;
        s.z2 = c.b2 * x - c.a2 * y;
        return y;
    }

    void designKWeighting()
    {
        // Формули з libebur128 (працюють для будь-якої частоти дискретизації)
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            pre.b0 = (Vh + Vb * K / Q + K * K) / a0;
            pre.b1 = 2.0 * (K * K - Vh) / a0;
            pre.b2 = (Vh - Vb * K / Q + K * K) / a0;
            pre.a1 = 2.0 * (K * K - 1.0) / a0;
            pre.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            rlb.b0 = 1.0; rlb.b1 = -2.0; rlb.b2 = 1.0;
            rlb.a1 = 2.0 * (K * K - 1.0) / a0;
            rlb.a2 = (1.0 - K / Q + K * K) / a0;
        }
    }

    static float toLufs (double energy) noexcept
    {
        return energy > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (energy)) : -100.0f;
    }

    void finishSubBlock()
    {
        ring[(size_t) ringPos] = acc / (double) accCount;
        ringPos = (ringPos + 1) % kRing;
        ringFilled = std::min (ringFilled + 1, kRing);
        acc = 0.0; accCount = 0;

        auto meanOf = [this] (int count)
        {
            double sum = 0.0;
            for (int k = 1; k <= count; ++k)
                sum += ring[(size_t) ((ringPos - k + kRing) % kRing)];
            return sum / (double) count;
        };

        if (ringFilled >= 4)
        {
            const double mEnergy = meanOf (4);
            const float m = toLufs (mEnergy);
            momentary.store (m);

            // Integrated: блоки 400 мс з 75 % перекриттям, абсолютний гейт -70 LUFS
            if (m >= -70.0f)
            {
                const int bin = juce::jlimit (0, kBins - 1, (int) ((m + 70.0f) * 10.0f));
                binCount[(size_t) bin] += 1;
                binEnergy[(size_t) bin] += mEnergy;
                computeIntegrated();
            }
        }
        shortTerm.store (ringFilled >= 30 ? toLufs (meanOf (30)) : -100.0f);
    }

    void computeIntegrated()
    {
        double e = 0.0; long long c = 0;
        for (int b = 0; b < kBins; ++b) { e += binEnergy[(size_t) b]; c += binCount[(size_t) b]; }
        if (c == 0) return;

        const float relGate = toLufs (e / (double) c) - 10.0f;
        const int startBin = juce::jlimit (0, kBins, (int) std::ceil ((relGate + 70.0f) * 10.0f));

        e = 0.0; c = 0;
        for (int b = startBin; b < kBins; ++b) { e += binEnergy[(size_t) b]; c += binCount[(size_t) b]; }
        integrated.store (c > 0 ? toLufs (e / (double) c) : -100.0f);
    }

    void resetAll()
    {
        for (auto& s : state) s = {};
        ring.fill (0.0);
        ringPos = ringFilled = 0;
        acc = 0.0; accCount = 0;
        binCount.fill (0);
        binEnergy.fill (0.0);
        momentary.store (-100.0f); shortTerm.store (-100.0f); integrated.store (-100.0f);
    }

    static constexpr int kRing = 30;
    static constexpr int kBins = 800; // -70…+10 LUFS з кроком 0.1

    double fs = 48000.0;
    int numCh = 2, subBlockLen = 4800;
    std::vector<ChannelState> state;
    std::array<double, kRing> ring {};
    int ringPos = 0, ringFilled = 0;
    double acc = 0.0; int accCount = 0;
    std::array<long long, kBins> binCount {};
    std::array<double, kBins> binEnergy {};
    std::atomic<bool> resetFlag { false };
};
} // namespace sn
