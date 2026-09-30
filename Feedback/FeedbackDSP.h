#pragma once

#include "../Common/SNCommon.h"
#include <complex>

/*  Емуляція фідбеку гітари з кабінетом.
    Справжній фідбек — це петля «струна → звукознімач → підсилювач → кабінет → повітря → струна».
    Тут: визначаємо висоту ноти (YIN), налаштовуємо хвилеводну петлю із затримкою рівно в період
    потрібної гармоніки, у петлі — фільтр і насичення (лампове обмеження), струна «засіває» петлю
    через смуговий фільтр, тож фідбек виростає з самої струни й фазово з нею зв'язаний. */

namespace sn
{
//==============================================================================
/** Визначення висоти: YIN на децимованому сигналі + уточнення на повній частоті. */
class PitchDetector
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        R = std::max (1, (int) std::round (fs / 12000.0));
        dfs = fs / R;
        lp1.setLowPass (fs, 1800.0, 0.7071); lp2.setLowPass (fs, 1800.0, 0.7071);

        tauMin = std::max (2, (int) std::floor (dfs / 1400.0));
        tauMax = (int) std::ceil (dfs / 65.0);
        W = tauMax * 2;
        N = W + tauMax + 2;
        dbuf.assign ((size_t) N, 0.0f);
        lin.assign ((size_t) N, 0.0f);
        cm.assign ((size_t) tauMax + 2, 1.0f);
        hop = std::max (1, (int) (0.005 * dfs));

        Nb = juce::nextPowerOfTwo ((int) (0.09 * fs));
        fbuf.assign ((size_t) Nb, 0.0f);
        reset();
    }

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

//==============================================================================
/** Хвилеводна петля фідбеку: затримка = період, фільтр «кабінет + повітря», лампове насичення. */
class FeedbackLoop
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        size = (int) std::ceil (fs / 30.0) + 8;
        buf.assign ((size_t) size, 0.0f);
        dcR = (float) (1.0 - 2.0 * juce::MathConstants<double>::pi * 25.0 / fs);
        agcCoef = (float) std::exp (-1.0 / (0.05 * fs));
        reset();
    }

    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        pos = 0; t1 = t2 = 0.0f; dcX = dcY = 0.0f; agc = 0.0f; res.reset();
    }

    /** Налаштовує петлю на частоту f з компенсацією фазової затримки фільтрів (точний строй).
        У петлі — резонатор на f: самозбуджується лише потрібна гармоніка, вищі моди не «тягнуть» строй. */
    void setFrequency (double f, double toneHz)
    {
        f = juce::jlimit (30.0, fs * 0.2, f);
        res.setBandPass (fs, f, 1.5);
        toneA = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * std::min (toneHz, fs * 0.45) / fs);

        const double w = 2.0 * juce::MathConstants<double>::pi * f / fs;
        const std::complex<double> z1 = std::exp (std::complex<double> (0.0, -w)), z2 = z1 * z1;
        const auto Hres = (res.b0 + res.b1 * z1 + res.b2 * z2) / (1.0 + res.a1 * z1 + res.a2 * z2);
        const auto Hdc = (1.0 - z1) / (1.0 - (double) dcR * z1);
        const auto H = Hres * Hdc;
        const double phaseDelay = -std::arg (H) / w;           // у семплах

        delay = juce::jlimit (2.0, (double) size - 4.0, fs / f - phaseDelay);
        gain = (float) (1.3 / std::max (0.05, std::abs (H)));  // запас над 1: петля самозбуджується
    }

    /** inj — збудження від струни. Повертає нормований (≈ ±1) сигнал петлі після тон-фільтра. */
    float process (float inj) noexcept
    {
        const float yd = read (delay);
        const float r = res.process (yd);
        const float dc = r - dcX + dcR * dcY;                   // DC-блокер
        dcX = r; dcY = dc;

        const float y = std::tanh (gain * dc + inj + 1.0e-5f * (rng.nextFloat() - 0.5f));   // лампове обмеження
        buf[(size_t) pos] = y;
        pos = (pos + 1) % size;

        // Тон кабінету/повітря: завал ВЧ на виході (2 полюси)
        t1 = y + toneA * (t1 - y);
        t2 = t1 + toneA * (t2 - t1);

        agc = std::abs (t2) + agcCoef * (agc - std::abs (t2));
        return t2 / std::max (agc * 1.5708f, 0.3f);            // |y| у середньому → амплітуда ≈ 1
    }

private:
    float read (double d) const noexcept
    {
        const double rp = (double) pos - d;
        const int i1 = (int) std::floor (rp);
        const float t = (float) (rp - i1);
        auto at = [&] (int idx) { return buf[(size_t) (((idx % size) + size) % size)]; };
        const float y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    double fs = 48000.0, delay = 100.0;
    int size = 1600, pos = 0;
    std::vector<float> buf;
    Biquad res;
    float toneA = 0.7f, t1 = 0.0f, t2 = 0.0f, dcR = 0.997f, dcX = 0.0f, dcY = 0.0f, gain = 1.3f;
    float agc = 0.0f, agcCoef = 0.999f;
    juce::Random rng { 0x1f33d };
};

//==============================================================================
class FeedbackEngine
{
public:
    enum class State { idle, blooming, releasing };

    struct Settings
    {
        int mode = 0;            // 0 Auto, 1 Hold, 2 Auto + Hold
        bool hold = false;
        float delaySec = 1.2f;
        int harmonic = 3;        // 0 Tone, 1 Octave, 2 Fifth (3-тя гармоніка), 3 Auto
        float distance = 0.4f;   // 0…1
        float amount = 0.7f, morph = 0.5f, toneHz = 2500.0f, drift = 0.25f;
        int tuning = 0;          // стрій для фідбеку у паузах (див. openStringHz)
        int openString = 0;      // 0 Auto, 1…6 = струна (6 — найнижча)
    };

    /** Частоти відкритих струн (6-та … 1-ша) для строїв, популярних у стоунері й думі. */
    static float openStringHz (int tuning, int stringNumber)
    {
        static const float std[6] { 82.41f, 110.0f, 146.83f, 196.0f, 246.94f, 329.63f };     // E A D G B E
        //                       E Std  Eb Std  D Std  Drop D  C Std  Drop C  Drop B
        static const int shiftAll[7]   { 0, -1, -2,  0, -4, -2, -3 };
        static const int shiftSixth[7] { 0,  0,  0, -2,  0, -2, -2 };
        const int t = juce::jlimit (0, 6, tuning), sIdx = juce::jlimit (1, 6, stringNumber);
        const int semis = shiftAll[t] + (sIdx == 6 ? shiftSixth[t] : 0);
        return std[6 - sIdx] * std::pow (2.0f, (float) semis / 12.0f);
    }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        pitch.prepare (fs);
        loop.prepare (fs);
        auto c = [this] (double ms) { return (float) std::exp (-1.0 / (0.001 * ms * fs)); };
        fA = c (1.0); fR = c (10.0); sA = c (20.0); sR = c (150.0);
        driftLp.setLowpass (fs / 64.0, 0.6);
        reset();
    }

    void reset()
    {
        pitch.reset(); loop.reset(); driftLp.reset();
        envF = envS = 0.0f; noteActive = false; f0 = 0.0f; timer = 0; noteRef = 0.0f;
        state = State::idle; bloom = 0.0f; bp.reset(); byHold = false; openMode = false; sinceOnset = 1 << 30;
        muteHold = 0; driftCounter = 0; driftCents = 0.0f; wobble = 0.0f;
        targetHz.store (0.0f); detectedHz.store (0.0f); bloomLevel.store (0.0f);
    }

    void process (float* const* data, int numCh, int n, const Settings& s)
    {
        const float d = juce::jlimit (0.0f, 1.0f, s.distance);
        const double bloomTime = 0.08 * std::pow (75.0, (double) d);          // 0.08 … 6 с
        const float rate = (float) (11.5 / (bloomTime * fs));                 // логістичне наростання
        const float effDelay = s.delaySec * (0.35f + 0.65f * d);               // ближче — зривається раніше
        const float relSlow = (float) std::exp (-1.0 / ((0.2 + 1.5 * d) * fs));
        const float relFast = (float) std::exp (-1.0 / (0.05 * fs));
        const bool autoMode = s.mode != 1, holdMode = s.mode != 0;
        const float levelScale = s.amount * 1.4f * (1.0f - 0.3f * d);

        for (int i = 0; i < n; ++i)
        {
            float x = 0.0f;
            for (int c = 0; c < numCh; ++c) x += data[c][i];
            x /= (float) std::max (1, numCh);

            // --- Обвідні: атака медіатора і глушіння струни
            const float ax = std::abs (x);
            const float prevS = envS;
            envF = ax > envF ? ax + fA * (envF - ax) : ax + fR * (envF - ax);
            envS = ax > envS ? ax + sA * (envS - ax) : ax + sR * (envS - ax);
            ++sinceOnset;
            if (envF > 2.0f * prevS && envF > kGate && sinceOnset > (int) (0.08 * fs))
                sinceOnset = 0;
            const bool muted = envS > kGate && envF < 0.03f * envS;
            muteHold = muted ? muteHold + 1 : 0;
            if (muteHold > (int) (0.02 * fs)) { noteActive = false; timer = 0; }   // струну заглушили

            // --- Висота
            if (pitch.push (x))
                onPitch (pitch.hz, pitch.clarity);

            if (noteActive && envS > kGate)
            {
                ++timer;                                   // рахуємо лише поки нота звучить
                if (state == State::idle) noteRef = std::max (noteRef * 0.99999f, envS);
                else noteRef = std::max (noteRef, envS);   // під час фідбеку рівень не падає разом зі струною
            }

            // --- Тригери
            const bool autoOk = autoMode && noteActive && timer > (int) (effDelay * fs);
            const bool holdOk = holdMode && s.hold && noteActive && f0 > 0.0f;
            // Фідбек у паузі: гітара з відкритими (не заглушеними) струнами біля кабінету
            const bool openOk = holdMode && s.hold && ! noteActive;
            if ((autoOk || holdOk) && (state != State::blooming || openMode))
            {
                openMode = false;
                start (s);
                byHold = holdOk && ! autoOk;
            }
            else if (openOk && state != State::blooming)
            {
                startOpen (s);
                byHold = true;
            }

            if (state == State::blooming)
            {
                if (muteHold > (int) (0.02 * fs))                         release();
                else if (byHold && ! s.hold && ! autoOk)                  { state = State::releasing; slowRelease = true; }
            }

            // --- Огинаюча фідбеку
            if (state == State::blooming)
                bloom = std::min (1.0f, bloom + rate * bloom * (1.0f - bloom) + 1.0e-7f);
            else if (state == State::releasing)
            {
                bloom *= slowRelease ? relSlow : relFast;
                if (bloom < 1.0e-4f) { bloom = 0.0f; state = State::idle; openMode = false; loop.reset(); }
            }

            // --- Дрейф висоти й рівня (рух гітариста біля кабінету)
            if (++driftCounter >= 64)
            {
                driftCounter = 0;
                const float r = driftLp.lowpass (rng.nextFloat() * 2.0f - 1.0f) * 8.0f;
                driftCents = s.drift * 12.0f * juce::jlimit (-1.0f, 1.0f, r);
                wobble = s.drift * 0.2f * juce::jlimit (-1.0f, 1.0f, r * 0.7f);
                if (state != State::idle && curTarget > 0.0f)
                    loop.setFrequency (curTarget * std::pow (2.0, driftCents / 1200.0), s.toneHz);
            }

            float fb = 0.0f;
            if (state != State::idle)
            {
                const float inj = 0.6f * bp.process (x);
                const float ref = openMode ? kOpenLevel : noteRefPeak();
                fb = loop.process (inj) * bloom * levelScale * ref * (1.0f + wobble);
            }

            const float dry = 1.0f - 0.85f * s.morph * bloom;
            for (int c = 0; c < numCh; ++c)
                data[c][i] = data[c][i] * dry + fb;
        }

        bloomLevel.store (bloom);
        inputDb.store (gainToDb (envS * 1.5708f));
        const bool counting = autoMode && noteActive && state != State::blooming;
        sustainProgress.store (counting ? juce::jlimit (0.0f, 1.0f, (float) timer / (float) (effDelay * fs)) : 0.0f);
        listening.store (noteActive && envS > kGate);
        openActive.store (openMode && state != State::idle);
    }

    // Стан для інтерфейсу
    std::atomic<float> detectedHz { 0.0f }, targetHz { 0.0f }, bloomLevel { 0.0f };
    std::atomic<int> starts { 0 };
    std::atomic<float> inputDb { -100.0f }, sustainProgress { 0.0f };
    std::atomic<bool> listening { false }, openActive { false };   // діагностика: скільки разів фідбек запускався з нуля

private:
    static constexpr float kOpenLevel = 0.18f;   // рівень фідбеку у паузі (≈ -15 dBFS пік при Amount 70 %)
    static constexpr float kGate = 0.0005f;  // ≈ -66 dBFS (середнє |x|): працює і з тихим DI-входом

    float noteRefPeak() const noexcept { return noteRef * 1.5708f; }  // середнє |x| → амплітуда

    void onPitch (float hz, float clarity)
    {
        const bool voiced = hz > 60.0f && hz < 1500.0f && clarity > 0.8f && envS > kGate;
        if (! voiced) return;
        detectedHz.store (hz);

        if (! noteActive || f0 <= 0.0f)
        {
            newNote (hz);
            return;
        }
        const float cents = 1200.0f * std::log2 (hz / f0);
        if (std::abs (cents) < 40.0f)
            f0 += 0.1f * (hz - f0);                // вібрато/дрейф — лишаємося на ноті
        else if (std::abs (cents) > 80.0f)
        {
            // Інша нота: фідбек старої ноти швидко гасне, відлік починається заново
            if (state == State::blooming) release();
            newNote (hz);
        }
    }

    void newNote (float hz)
    {
        noteActive = true; f0 = hz; timer = 0; noteRef = envS; multChosen = 0;
    }

    int chooseMultiple (int harmonic, float f)
    {
        int m = harmonic == 0 ? 1 : harmonic == 1 ? 2 : harmonic == 2 ? 3 : 0;
        if (m == 0)
        {
            // Як у житті: низькі ноти частіше «зриваються» на октаву чи квінту, високі — на основний тон
            const float r = rng.nextFloat();
            if (f < 196.0f)      m = r < 0.2f ? 1 : (r < 0.65f ? 2 : 3);
            else if (f < 440.0f) m = r < 0.4f ? 1 : (r < 0.85f ? 2 : 3);
            else                 m = r < 0.7f ? 1 : 2;
        }
        while (m > 1 && f * (float) m > 1500.0f) --m;
        return m;
    }

    void start (const Settings& s)
    {
        if (multChosen == 0) multChosen = chooseMultiple (s.harmonic, f0);
        curTarget = f0 * (float) multChosen;
        targetHz.store (curTarget);
        bp.setBandPass (fs, curTarget, 4.0);
        loop.setFrequency (curTarget, s.toneHz);
        if (state == State::idle) { loop.reset(); starts.fetch_add (1); }
        state = State::blooming;
        slowRelease = false;
        bloom = std::max (bloom, 0.002f);
    }

    void release() { state = State::releasing; slowRelease = false; }

    void startOpen (const Settings& s)
    {
        int str = s.openString;
        if (str < 1 || str > 6)
        {
            // Найчастіше «підхоплюються» середні струни
            const float r = rng.nextFloat();
            str = r < 0.3f ? 4 : r < 0.6f ? 3 : r < 0.8f ? 5 : 6;
        }
        const float f = openStringHz (s.tuning, str);
        const int m = chooseMultiple (s.harmonic, f);
        curTarget = f * (float) m;
        targetHz.store (curTarget);
        detectedHz.store (f);
        bp.setBandPass (fs, curTarget, 4.0);
        loop.setFrequency (curTarget, s.toneHz);
        if (state == State::idle) { loop.reset(); starts.fetch_add (1); }
        state = State::blooming;
        slowRelease = false;
        openMode = true;
        bloom = std::max (bloom, 0.002f);
    }

    double fs = 48000.0;
    PitchDetector pitch;
    FeedbackLoop loop;
    Biquad bp;
    OnePole driftLp;
    juce::Random rng { 0xfeedb };

    float fA = 0, fR = 0, sA = 0, sR = 0, envF = 0, envS = 0;
    bool noteActive = false, byHold = false, slowRelease = false, openMode = false;
    float f0 = 0.0f, noteRef = 0.0f, curTarget = 0.0f;
    int timer = 0, sinceOnset = 1 << 30, muteHold = 0, multChosen = 0;
    State state = State::idle;
    float bloom = 0.0f;
    int driftCounter = 0;
    float driftCents = 0.0f, wobble = 0.0f;
};
} // namespace sn
