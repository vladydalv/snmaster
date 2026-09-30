#pragma once

#include <map>
#include <string>
#include <string_view>

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

namespace sn
{
/** Плавне вмикання/вимикання модуля (без клацань): mix повзе до target на step за виклик,
    під час переходу вихід = суміш обробленого й сухого. fn(fresh) — обробка на місці; fresh = щойно ввімкнули. */
template <typename Fn>
inline void runFaded (float& mix, bool target, float* const* data, int numCh, int n, float step,
                      juce::AudioBuffer<float>& scratch, Fn&& fn)
{
    const float t = target ? 1.0f : 0.0f;
    if (mix <= 0.0f && ! target) return;
    const float m0 = mix;
    const float m1 = std::abs (t - mix) <= step ? t : mix + (t > mix ? step : -step);
    mix = m1;
    if (m0 >= 1.0f && m1 >= 1.0f) { fn (false); return; }
    for (int ch = 0; ch < numCh; ++ch) std::copy_n (data[ch], n, scratch.getWritePointer (ch));
    fn (m0 <= 0.0f);
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* dry = scratch.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
        {
            const float k = m0 + (m1 - m0) * (float) i / (float) n;
            data[ch][i] = k * data[ch][i] + (1.0f - k) * dry[i];
        }
    }
}

/** Швидкий доступ до значень параметрів за ID без виділення пам'яті (безпечно в аудіопотоці). */
class ParamCache
{
public:
    void add (const juce::String& id, std::atomic<float>* v) { m[id.toStdString()] = v; }
    float get (const char* id) const noexcept
    {
        auto it = m.find (std::string_view (id));
        jassert (it != m.end());
        return it->second->load (std::memory_order_relaxed);
    }
private:
    std::map<std::string, std::atomic<float>*, std::less<>> m;
};

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
/** Однополюсний фільтр (для DC-блокера і м'якого завалу ВЧ). */
struct OnePole
{
    float a = 0.0f, z = 0.0f;
    void setLowpass (double fs, double hz) { a = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * hz / fs); }
    float lowpass (float x) noexcept  { z = x + a * (z - x); return z; }
    float highpass (float x) noexcept { return x - lowpass (x); }
    void reset() { z = 0.0f; }
};

//==============================================================================
/** Черга стерео-семплів аудіопотік → інтерфейс без блокувань (один писач, один читач). */
class StereoFifo
{
public:
    StereoFifo() : fifo (kSize) { buf.setSize (2, kSize); buf.clear(); }

    void push (const float* l, const float* r, int n) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);           // якщо читач не встигає — зайве відкидається
        if (n1 > 0) { buf.copyFrom (0, s1, l, n1); buf.copyFrom (1, s1, r != nullptr ? r : l, n1); }
        if (n2 > 0) { buf.copyFrom (0, s2, l + n1, n2); buf.copyFrom (1, s2, (r != nullptr ? r : l) + n1, n2); }
        fifo.finishedWrite (n1 + n2);
    }

    /** Забрати все накопичене; callback (const float* l, const float* r, int n). */
    template <typename Fn> void pull (Fn&& fn)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
        if (n1 > 0) fn (buf.getReadPointer (0, s1), buf.getReadPointer (1, s1), n1);
        if (n2 > 0) fn (buf.getReadPointer (0, s2), buf.getReadPointer (1, s2), n2);
        fifo.finishedRead (n1 + n2);
    }

private:
    static constexpr int kSize = 1 << 16;
    juce::AbstractFifo fifo;
    juce::AudioBuffer<float> buf;
};

//==============================================================================
/** Біквад (TDF-II, double) з формулами RBJ Audio EQ Cookbook.
    Перерахунок коефіцієнтів не виділяє пам'ять — безпечно в аудіопотоці і для плавної автоматизації. */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() noexcept { z1 = z2 = 0.0; }

    inline float process (float xf) noexcept
    {
        const double x = xf;
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }

    void copyCoeffs (const Biquad& o) noexcept { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }

    void setPeak (double fs, double f, double q, double db) noexcept
    {
        const double A = std::pow (10.0, db / 40.0), w = w0 (fs, f), cs = std::cos (w), al = std::sin (w) / (2.0 * q);
        set (1 + al * A, -2 * cs, 1 - al * A, 1 + al / A, -2 * cs, 1 - al / A);
    }
    void setLowShelf (double fs, double f, double q, double db) noexcept
    {
        const double A = std::pow (10.0, db / 40.0), w = w0 (fs, f), cs = std::cos (w), al = std::sin (w) / (2.0 * q), sa = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * cs + sa), 2 * A * ((A - 1) - (A + 1) * cs), A * ((A + 1) - (A - 1) * cs - sa),
             (A + 1) + (A - 1) * cs + sa, -2 * ((A - 1) + (A + 1) * cs), (A + 1) + (A - 1) * cs - sa);
    }
    void setHighShelf (double fs, double f, double q, double db) noexcept
    {
        const double A = std::pow (10.0, db / 40.0), w = w0 (fs, f), cs = std::cos (w), al = std::sin (w) / (2.0 * q), sa = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * cs + sa), -2 * A * ((A - 1) + (A + 1) * cs), A * ((A + 1) + (A - 1) * cs - sa),
             (A + 1) - (A - 1) * cs + sa, 2 * ((A - 1) - (A + 1) * cs), (A + 1) - (A - 1) * cs - sa);
    }
    void setLowPass (double fs, double f, double q) noexcept
    {
        const double w = w0 (fs, f), cs = std::cos (w), al = std::sin (w) / (2.0 * q);
        set ((1 - cs) / 2, 1 - cs, (1 - cs) / 2, 1 + al, -2 * cs, 1 - al);
    }
    void setHighPass (double fs, double f, double q = 0.70710678) noexcept
    {
        const double w = w0 (fs, f), cs = std::cos (w), al = std::sin (w) / (2.0 * q);
        set ((1 + cs) / 2, -(1 + cs), (1 + cs) / 2, 1 + al, -2 * cs, 1 - al);
    }
    /** Смуговий фільтр з підсиленням 0 дБ на центральній частоті. */
    void setBandPass (double fs, double f, double q) noexcept
    {
        const double w = w0 (fs, f), cs = std::cos (w), al = std::sin (w) / (2.0 * q);
        set (al, 0.0, -al, 1 + al, -2 * cs, 1 - al);
    }
    void setBypass() noexcept { b0 = 1; b1 = b2 = a1 = a2 = 0; }

    /** АЧХ фільтра на частоті f (дБ). */
    double magnitudeDb (double fs, double f) const noexcept
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / fs;
        const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        const double dr = 1 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        return 10.0 * std::log10 ((nr * nr + ni * ni) / std::max (dr * dr + di * di, 1e-30));
    }

private:
    static double w0 (double fs, double f) noexcept
    {
        return 2.0 * juce::MathConstants<double>::pi * std::min (f, 0.49 * fs) / fs;
    }
    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }
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
