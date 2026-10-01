#pragma once

#include "SNCommon.h"
#include "PitchDetector.h"
#include "MixBus.h"

/** Аналіз однієї доріжки для SN Listen.
    Аудіопотік рахує підсумок кожних 100 мс (FrameMaker) і кладе в чергу без блокувань;
    статистику (гучність, спектр, шум, гул, стрій, тип інструмента) збирає ListenStats поза аудіопотоком. */
namespace mix
{
//==============================================================================
struct FrameStat
{
    int64_t idx = -1;               // номер кадру на таймлайні хоста (-1 — транспорт стоїть)
    int len = 0;
    float pow = 0, mono = 0, peak = 0;
    float bands[an::kBands] {};
    float lr = 0, l2 = 0, r2 = 0, lowMid = 0, lowSide = 0;
    float hum[6] {};                // частка потужності: 50, 100, 150, 60, 120, 180 Гц
    float pitchHz[8] {}, clarity[8] {};
    int nPitch = 0, clips = 0;
};

//==============================================================================
class FrameMaker
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        frameLen = std::max (1, (int) std::round (0.1 * fs));
        for (int b = 0; b < an::kBands; ++b) { bp[(size_t) b].setBandPass (fs, an::bandHz[(size_t) b], 4.32); bp[(size_t) b].reset(); }
        lpL.setLowPass (fs, 150.0, 0.707); lpR.setLowPass (fs, 150.0, 0.707); lpL.reset(); lpR.reset();
        static constexpr double humF[6] { 50, 100, 150, 60, 120, 180 };
        for (int k = 0; k < 6; ++k) coef[k] = 2.0 * std::cos (2.0 * juce::MathConstants<double>::pi * humF[k] / fs);
        pitch.prepare (fs, 38.0, 6000.0);
        pitch.setHopSeconds (0.02);
        cur = {};
        pos = 0;
        std::fill (std::begin (q1), std::end (q1), 0.0); std::fill (std::begin (q2), std::end (q2), 0.0);
    }

    /** startSample — позиція хоста першого семплу блоку (-1, якщо транспорт стоїть). */
    void process (const float* l, const float* r, int n, int64_t startSample)
    {
        for (int i = 0; i < n; ++i)
        {
            const int64_t abs = startSample >= 0 ? startSample + i : -1;
            const int64_t idx = abs >= 0 ? abs / frameLen : -1;
            if (pos == 0) cur.idx = idx;
            else if (idx != cur.idx) restart (idx);         // перемотка: незавершений кадр відкидаємо

            const float L = l[i], R = r != nullptr ? r[i] : L;
            const float m = 0.5f * (L + R);
            cur.pow += L * L + R * R;
            cur.mono += m * m;
            cur.peak = std::max (cur.peak, std::max (std::abs (L), std::abs (R)));
            if (std::abs (L) >= 0.999f || std::abs (R) >= 0.999f) ++cur.clips;
            for (int b = 0; b < an::kBands; ++b) { const float y = bp[(size_t) b].process (m); cur.bands[b] += y * y; }
            cur.lr += L * R; cur.l2 += L * L; cur.r2 += R * R;
            const float bl = lpL.process (L), br = lpR.process (R);
            cur.lowMid += 0.25f * (bl + br) * (bl + br);
            cur.lowSide += 0.25f * (bl - br) * (bl - br);
            for (int k = 0; k < 6; ++k)
            {
                const double s0 = m + coef[k] * q1[k] - q2[k];
                q2[k] = q1[k]; q1[k] = s0;
            }
            if (pitch.push (m) && cur.nPitch < 8)
            {
                cur.pitchHz[cur.nPitch] = pitch.hz;
                cur.clarity[cur.nPitch] = pitch.clarity;
                ++cur.nPitch;
            }

            if (++pos >= frameLen) finish();
        }
    }

    /** Готові кадри (читає ListenStats). */
    template <typename Fn> void pull (Fn&& fn)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) fn (ring[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) fn (ring[(size_t) (s2 + i)]);
        fifo.finishedRead (n1 + n2);
    }

private:
    void restart (int64_t idx)
    {
        cur = {}; cur.idx = idx; pos = 0;
        std::fill (std::begin (q1), std::end (q1), 0.0); std::fill (std::begin (q2), std::end (q2), 0.0);
    }

    void finish()
    {
        const float N = (float) frameLen;
        FrameStat f = cur;
        f.len = frameLen;
        f.pow /= 2.0f * N; f.mono /= N;
        for (auto& b : f.bands) b /= N;
        f.lr /= N; f.l2 /= N; f.r2 /= N; f.lowMid /= N; f.lowSide /= N;
        // Гёрцель: потужність синусоїди на частоті гулу відносно потужності кадру
        for (int k = 0; k < 6; ++k)
        {
            const double re = q1[k] - 0.5 * coef[k] * q2[k], im = q2[k] * std::sqrt (std::max (0.0, 1.0 - 0.25 * coef[k] * coef[k]));
            const double p = 2.0 * (re * re + im * im) / ((double) N * N);
            f.hum[k] = (float) (p / std::max (1.0e-20, (double) f.mono));
        }
        int s1, n1, s2, n2;
        fifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 > 0) ring[(size_t) s1] = f;
        fifo.finishedWrite (n1);
        restart (cur.idx >= 0 ? cur.idx + 1 : -1);
    }

    static constexpr int kFifo = 64;
    double fs = 48000.0;
    int frameLen = 4800, pos = 0;
    FrameStat cur;
    std::array<sn::Biquad, an::kBands> bp;
    sn::Biquad lpL, lpR;
    double coef[6] {}, q1[6] {}, q2[6] {};
    sn::PitchDetector pitch;
    juce::AbstractFifo fifo { kFifo };
    std::array<FrameStat, kFifo> ring;
};

//==============================================================================
/** Гістограма з кроком 0.5 дБ від -120 до +10 дБ: перцентилі без зберігання історії. */
struct DbHist
{
    static constexpr int kBins = 260;
    std::array<uint32_t, kBins> c {};
    uint32_t total = 0;
    void clear() { c.fill (0); total = 0; }
    void add (float db) { const int b = juce::jlimit (0, kBins - 1, (int) ((db + 120.0f) * 2.0f)); ++c[(size_t) b]; ++total; }
    /** Перцентиль серед значень не нижче floorDb. */
    float pct (float p, float floorDb = -200.0f) const
    {
        const int b0 = juce::jlimit (0, kBins - 1, (int) ((floorDb + 120.0f) * 2.0f));
        uint64_t n = 0;
        for (int b = b0; b < kBins; ++b) n += c[(size_t) b];
        if (n == 0) return -120.0f;
        const auto target = (uint64_t) std::ceil (p * (double) n);
        uint64_t acc = 0;
        for (int b = b0; b < kBins; ++b) { acc += c[(size_t) b]; if (acc >= std::max<uint64_t> (1, target)) return (float) b * 0.5f - 120.0f + 0.25f; }
        return 10.0f;
    }
    uint32_t countAbove (float db) const
    {
        uint32_t n = 0;
        for (int b = juce::jlimit (0, kBins - 1, (int) ((db + 120.0f) * 2.0f)); b < kBins; ++b) n += c[(size_t) b];
        return n;
    }
};

//==============================================================================
class ListenStats
{
public:
    void reset()
    {
        level.clear(); peaks.clear(); crest.clear();
        bandSum.fill (0.0); activeFrames = 0; frames = 0;
        lr = l2 = r2 = lowMid = lowSide = 0.0;
        quiet = 0; humHits50 = humHits60 = 0;
        tune.fill (0); tuneN = 0; spread.clear(); pitched = 0; hzHist.fill (0);
        maxPeak = 0.0f; clips = 0;
        noise.clear();
        drops = 0; prevActive = false; prevDb = -200.0f;
    }

    /** Повертає true, якщо кадр звучав (для оцінки фейдера). */
    bool add (const FrameStat& f)
    {
        ++frames;
        const float db = sn::gainToDb (std::sqrt (std::max (f.pow, 1.0e-12f)));
        const float pkDb = sn::gainToDb (std::max (f.peak, 1.0e-6f));
        maxPeak = std::max (maxPeak, f.peak);
        clips += f.clips;
        const float prev = prevDb;
        prevDb = db;
        if (db < -100.0f) { if (prevActive) ++drops; prevActive = false; return false; }   // цифрова тиша
        level.add (db);

        const float p90 = level.pct (0.9f, -90.0f);
        const bool active = db > -70.0f && db > p90 - 25.0f;
        const bool isQuiet = db < p90 - 30.0f && level.total > 30;

        if (prevActive && db < prev - 6.0f) ++drops;
        prevActive = active;
        if (active)
        {
            ++activeFrames;
            peaks.add (pkDb);
            crest.add (pkDb - db - 120.0f);                   // зсув: 0…40 дБ у межах гістограми
            for (int b = 0; b < an::kBands; ++b) bandSum[(size_t) b] += f.bands[b];
            lr += f.lr; l2 += f.l2; r2 += f.r2; lowMid += f.lowMid; lowSide += f.lowSide;

            // Висота: кадр із ≥3 надійними оцінками
            float cents[8]; int n = 0;
            for (int k = 0; k < f.nPitch; ++k)
                if (f.clarity[k] > 0.9f && f.pitchHz[k] > 30.0f && f.pitchHz[k] < 1200.0f)
                    cents[n++] = 1200.0f * std::log2 (f.pitchHz[k] / 440.0f);
            if (n >= 3)
            {
                const auto [mn, mx] = std::minmax_element (cents, cents + n);
                const float sp = *mx - *mn;
                if (sp < 80.0f)
                {
                    ++pitched;
                    spread.add (sp - 120.0f);                 // гістограма дБ-шкали: зсув, щоб уміщалось 0…130 центів
                    std::sort (cents, cents + n);
                    const float c = cents[n / 2];
                    const float off = c - 100.0f * std::round (c / 100.0f);
                    ++tune[(size_t) juce::jlimit (0, 100, (int) std::round (off) + 50)];
                    ++tuneN;
                    const float hz = 440.0f * std::exp2 (c / 1200.0f);
                    ++hzHist[(size_t) juce::jlimit (0, 63, (int) std::round (8.0f * std::log2 (hz / 30.0f)))];
                }
            }
        }
        else if (isQuiet)
        {
            ++quiet;
            noise.add (db);
            const float h50 = f.hum[0] + f.hum[1] + f.hum[2], h60 = f.hum[3] + f.hum[4] + f.hum[5];
            auto strong = [] (float a, float b, float c) { return (a > 0.03f) + (b > 0.03f) + (c > 0.03f) >= 2; };
            if (h50 > 0.25f && strong (f.hum[0], f.hum[1], f.hum[2])) ++humHits50;
            if (h60 > 0.25f && strong (f.hum[3], f.hum[4], f.hum[5])) ++humHits60;
        }
        return active;
    }

    Features features (float lufs) const
    {
        Features r;
        r.lufs = lufs;
        r.seconds = (float) frames * 0.1f;
        r.activity = frames > 0 ? (float) activeFrames / (float) frames : 0.0f;
        r.peakDb = sn::gainToDb (std::max (maxPeak, 1.0e-6f));
        r.clips = clips;
        r.valid = activeFrames >= 30 ? 1 : 0;
        if (activeFrames == 0) return r;

        const float p90 = level.pct (0.9f, -90.0f);
        const float floorDb = std::max (-70.0f, p90 - 25.0f);
        r.rangeDb = level.pct (0.9f, floorDb) - level.pct (0.1f, floorDb);
        r.crestDb = crest.pct (0.5f) + 120.0f;      // гістограма зсунута: значення від 0 дБ
        r.decayFrac = (float) drops / (float) activeFrames;
        r.corr = (float) (lr / std::sqrt (std::max (l2 * r2, 1.0e-30)));
        const double mid = 0.25 * (l2 + r2 + 2 * lr), side = 0.25 * (l2 + r2 - 2 * lr);
        r.sideDb = (float) (10.0 * std::log10 (std::max (side, 1.0e-20) / std::max (mid, 1.0e-20)));
        r.lowSideDb = (float) (10.0 * std::log10 (std::max (lowSide, 1.0e-20) / std::max (lowMid, 1.0e-20)));
        for (int b = 0; b < an::kBands; ++b)
            r.bands[b] = (float) (10.0 * std::log10 (std::max (bandSum[(size_t) b] / activeFrames, 1.0e-14)));

        if (quiet >= 20)
        {
            r.noiseDb = noise.pct (0.5f) - p90;
            const float h50 = (float) humHits50 / (float) quiet, h60 = (float) humHits60 / (float) quiet;
            r.hum = std::max (h50, h60);
            r.humHz = h60 > h50 ? 60.0f : 50.0f;
        }
        r.tuneFrames = (float) tuneN;
        if (tuneN > 0)
        {
            uint32_t acc = 0;
            for (int i = 0; i <= 100; ++i) { acc += tune[(size_t) i]; if (acc * 2 >= tuneN) { r.tuneCents = (float) (i - 50); break; } }
            r.pitchSpread = spread.pct (0.5f) + 120.0f;
            uint32_t a2 = 0;
            for (int i = 0; i < 64; ++i) { a2 += hzHist[(size_t) i]; if (a2 * 2 >= tuneN) { r.medianHz = 30.0f * std::exp2 ((float) i / 8.0f); break; } }
        }
        r.pitchedFrac = (float) pitched / (float) activeFrames;
        classify (r);
        return r;
    }

    int framesSeen() const noexcept { return frames; }

    /** Вгадати інструмент за спектром, атакою і поведінкою висоти. Це підказка — користувач може виправити. */
    static void classify (Features& r)
    {
        double tot = 0, low = 0, high = 0, mids = 0, cen = 0;
        for (int b = 0; b < an::kBands; ++b)
        {
            const double p = std::pow (10.0, r.bands[b] / 10.0), f = an::bandHz[(size_t) b];
            tot += p; cen += p * std::log2 (f);
            if (f < 150) low += p;
            if (f >= 5000) high += p;
            if (f >= 200 && f <= 5000) mids += p;
        }
        if (tot <= 0) { r.autoInst = Other; r.autoConf = 0; return; }
        const double lowS = low / tot, highS = high / tot;
        const double centroid = std::exp2 (cen / tot);
        juce::ignoreUnused (mids);
        // Ударні: звук швидко згасає після удару і майже не має стабільної висоти
        const bool percussive = r.decayFrac > 0.3f && r.pitchedFrac < 0.5f;
        const bool noisy = r.pitchedFrac < 0.2f;

        int inst = Other; float conf = 0.4f;
        if (highS > 0.3 && lowS < 0.15 && noisy)       { inst = Drums; conf = 0.7f; }   // тарілки / оверхеди
        else if (percussive && lowS > 0.5)             { inst = Kick;  conf = 0.8f; }
        else if (percussive && noisy && centroid > 150 && centroid < 3000) { inst = Snare; conf = 0.55f; }
        else if (percussive)                           { inst = Drums; conf = 0.45f; }
        else if ((lowS > 0.5) || (r.pitchedFrac > 0.25f && r.medianHz > 0 && r.medianHz < 200 && lowS > 0.3))
            { inst = Bass; conf = 0.75f; }
        else if (r.pitchedFrac > 0.2f && r.pitchSpread > 18.0f && centroid > 250)
            { inst = Vocal; conf = 0.55f; }
        else if (centroid > 200 && centroid < 4000 && highS < 0.15)
            { inst = Guitar; conf = 0.5f; }
        r.autoInst = inst;
        r.autoConf = conf;
    }

private:
    DbHist level, peaks, crest, spread, noise;
    std::array<double, an::kBands> bandSum {};
    int activeFrames = 0, frames = 0, quiet = 0, humHits50 = 0, humHits60 = 0, pitched = 0, clips = 0;
    double lr = 0, l2 = 0, r2 = 0, lowMid = 0, lowSide = 0;
    std::array<uint32_t, 101> tune {};
    std::array<uint32_t, 64> hzHist {};
    uint32_t tuneN = 0;
    float maxPeak = 0.0f, prevDb = -200.0f;
    int drops = 0;
    bool prevActive = false;
};
} // namespace mix
