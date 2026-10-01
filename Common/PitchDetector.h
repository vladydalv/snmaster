#pragma once

#include "SNCommon.h"

namespace sn
{
//==============================================================================
/** Визначення висоти: YIN на децимованому сигналі + уточнення на повній частоті. */
class PitchDetector
{
public:
    /** minHz — найнижча нота; decimatedRate — частота аналізу (менше = дешевше). */
    void prepare (double sampleRate, double minHz = 65.0, double decimatedRate = 12000.0)
    {
        fs = sampleRate;
        R = std::max (1, (int) std::round (fs / decimatedRate));
        dfs = fs / R;
        const double lpHz = std::min (1800.0, 0.3 * fs / R);
        lp1.setLowPass (fs, lpHz, 0.7071); lp2.setLowPass (fs, lpHz, 0.7071);

        tauMin = std::max (2, (int) std::floor (dfs / 1400.0));
        tauMax = (int) std::ceil (dfs / minHz);
        W = tauMax * 2;
        N = W + tauMax + 2;
        dbuf.assign ((size_t) N, 0.0f);
        lin.assign ((size_t) N, 0.0f);
        cm.assign ((size_t) tauMax + 2, 1.0f);
        hop = std::max (1, (int) (0.005 * dfs));

        Nb = juce::nextPowerOfTwo ((int) (std::max (0.09, 6.0 / minHz) * fs));
        fbuf.assign ((size_t) Nb, 0.0f);
        reset();
    }

    /** Як часто оновлювати оцінку (за замовчуванням 5 мс). */
    void setHopSeconds (double sec) { hop = std::max (1, (int) (sec * dfs)); }

    void reset()
    {
        std::fill (dbuf.begin(), dbuf.end(), 0.0f);
        std::fill (fbuf.begin(), fbuf.end(), 0.0f);
        lp1.reset(); lp2.reset();
        wpos = filled = decCount = hopCount = 0; fpos = 0;
        hz = 0.0f; clarity = 0.0f;
    }

    /** Повертає true, коли з'явилась нова оцінка (hz, clarity). */
    bool push (float x) noexcept
    {
        const float y = lp2.process (lp1.process (x));
        fbuf[(size_t) fpos] = y;
        fpos = (fpos + 1) & (Nb - 1);

        if (++decCount < R) return false;
        decCount = 0;
        dbuf[(size_t) wpos] = y;
        wpos = (wpos + 1) % N;
        filled = std::min (filled + 1, N);
        if (++hopCount < hop || filled < N) return false;
        hopCount = 0;
        analyse();
        return true;
    }

    float hz = 0.0f, clarity = 0.0f;

private:
    void analyse() noexcept
    {
        for (int i = 0; i < N; ++i) lin[(size_t) i] = dbuf[(size_t) ((wpos + i) % N)];

        // Різницева функція і кумулятивна нормалізація (YIN)
        double running = 0.0;
        cm[0] = 1.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double sum = 0.0;
            for (int j = 0; j < W; ++j)
            {
                const float d = lin[(size_t) j] - lin[(size_t) (j + tau)];
                sum += (double) d * d;
            }
            running += sum;
            cm[(size_t) tau] = running > 1.0e-12 ? (float) (sum * tau / running) : 1.0f;
        }

        int best = -1;
        for (int tau = tauMin; tau < tauMax - 1; ++tau)
            if (cm[(size_t) tau] < 0.12f)
            {
                while (tau + 1 < tauMax - 1 && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        if (best < 0)
        {
            int m = tauMin;
            for (int tau = tauMin; tau < tauMax - 1; ++tau) if (cm[(size_t) tau] < cm[(size_t) m]) m = tau;
            if (cm[(size_t) m] < 0.25f) best = m;
        }
        if (best < 0) { hz = 0.0f; clarity = 0.0f; return; }

        const float s0 = cm[(size_t) best - 1], s1 = cm[(size_t) best], s2 = cm[(size_t) best + 1];
        const float den = s0 - 2.0f * s1 + s2;
        const float shift = std::abs (den) > 1.0e-9f ? juce::jlimit (-1.0f, 1.0f, 0.5f * (s0 - s2) / den) : 0.0f;
        const double tauCoarse = ((double) best + shift) * R;
        clarity = 1.0f - s1;
        hz = (float) (fs / refine (tauCoarse));
    }

    /** Уточнення періоду на повній частоті дискретизації. */
    double refine (double tauCoarse) const noexcept
    {
        const int t0 = (int) std::round (tauCoarse);
        const int span = R + 1;
        const int maxLag = t0 + span + 1;
        const int Wb = std::min ((int) (2.5 * tauCoarse), Nb - maxLag - 1);
        if (Wb < 16 || t0 - span < 2) return tauCoarse;

        auto at = [this] (int back) { return fbuf[(size_t) ((fpos - 1 - back) & (Nb - 1))]; };
        auto diff = [&] (int lag)
        {
            double s = 0.0;
            for (int j = 0; j < Wb; ++j) { const float d = at (j) - at (j + lag); s += (double) d * d; }
            return s;
        };

        int bestLag = t0; double bestVal = 1.0e300;
        for (int lag = t0 - span; lag <= t0 + span; ++lag)
        {
            const double v = diff (lag);
            if (v < bestVal) { bestVal = v; bestLag = lag; }
        }
        const double a = diff (bestLag - 1), b = bestVal, c = diff (bestLag + 1);
        const double den = a - 2.0 * b + c;
        const double sh = den > 1.0e-18 ? juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / den) : 0.0;
        return (double) bestLag + sh;
    }

    double fs = 48000.0, dfs = 12000.0;
    int R = 4, tauMin = 8, tauMax = 185, W = 370, N = 560, hop = 60;
    int wpos = 0, filled = 0, decCount = 0, hopCount = 0;
    int Nb = 4096, fpos = 0;
    Biquad lp1, lp2;
    std::vector<float> dbuf, lin, cm, fbuf;
};

} // namespace sn
