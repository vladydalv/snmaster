#pragma once

#include "../Common/SNCommon.h"
#include "../Common/Analysis.h"

/** Динамічний EQ із сайдчейном: вирізає на основній доріжці лише ту частоту, де вона б'ється з ключем
    (бочка, вокал), і лише коли ключ звучить. В Auto сам знаходить частоту конфлікту за спектрами обох. */
class Unmasker
{
public:
    struct Settings { bool autoFreq; float lo, hi, manualHz, depthDb, q, attackMs, releaseMs, sensPct; bool delta; };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (int b = 0; b < an::kBands; ++b)
        {
            bpMain[(size_t) b].setBandPass (fs, an::bandHz[(size_t) b], 4.32); bpMain[(size_t) b].reset();
            bpKey[(size_t) b].setBandPass (fs, an::bandHz[(size_t) b], 4.32);  bpKey[(size_t) b].reset();
        }
        accMain.fill (0.0); accKey.fill (0.0); avgMain.fill (1.0e-12); avgKey.fill (1.0e-12);
        accN = 0; sinceChoose = 0;
        env = 0.0f; amount = 0.0f; keyPeakDb = -120.0f; gainDb = 0.0f;
        for (auto& f : dyn) f.reset();
        keyBp.reset();
        freqSm = 300.0f; designedHz = -1.0f;
    }

    /** main — основний сигнал (змінюється на місці), key — моно ключ (nullptr, якщо сайдчейн не підключено). */
    void process (float* const* main, int numCh, const float* key, int n, const Settings& s)
    {
        // Детектор ключа — швидкий (~3 мс); Attack/Release — швидкість самого вирізу
        const float aDet = std::exp (-1.0f / (0.003f * (float) fs));
        const float aA = std::exp (-16.0f / (0.001f * s.attackMs * (float) fs));
        const float aR = std::exp (-16.0f / (0.001f * s.releaseMs * (float) fs));
        const float peakFall = (float) (6.0 / fs);                     // пам'ять піку ключа падає 6 дБ/с
        const float range = juce::jmap (s.sensPct, 0.0f, 100.0f, 36.0f, 8.0f);

        // Частота: вручну або знайдена, з плавним ковзанням (до 1 октави за секунду)
        const float target = s.autoFreq && foundHz > 0.0f ? foundHz : s.manualHz;
        const float maxStep = (float) n / (float) fs;
        const float oct = std::log2 (target / freqSm);
        freqSm *= std::exp2 (juce::jlimit (-maxStep, maxStep, oct));

        float grMax = 0.0f;
        for (int start = 0; start < n; start += 16)
        {
            const int len = std::min (16, n - start);
            if (std::abs (freqSm - designedHz) > 0.01f * designedHz || std::abs (s.q - designedQ) > 0.01f)
            {
                keyBp.setBandPass (fs, freqSm, s.q);
                designedHz = freqSm; designedQ = s.q;
            }

            // Ключ: енергія в смузі конфлікту
            for (int i = 0; i < len; ++i)
            {
                float k = 0.0f;
                if (key != nullptr)
                {
                    const float y = keyBp.process (key[start + i]);
                    k = y * y;
                }
                env = k + aDet * (env - k);
            }
            const float envDb = 10.0f * std::log10 (std::max (env, 1.0e-12f));
            keyPeakDb = std::max (envDb, keyPeakDb - peakFall * (float) len);
            float target = 0.0f;
            if (key != nullptr && keyPeakDb > -70.0f)
                target = juce::jlimit (0.0f, 1.0f, (envDb - (keyPeakDb - range)) / range);
            amount = target > amount ? target + aA * (amount - target) : target + aR * (amount - target);
            gainDb = -s.depthDb * amount;
            grMax = std::max (grMax, -gainDb);

            sn::Biquad c; c.setPeak (fs, freqSm, s.q, gainDb);
            for (int ch = 0; ch < numCh && ch < 2; ++ch)
            {
                dyn[(size_t) ch].copyCoeffs (c);
                float* d = main[ch] + start;
                for (int i = 0; i < len; ++i)
                {
                    const float x = d[i];
                    const float y = dyn[(size_t) ch].process (x);
                    d[i] = s.delta ? x - y : y;
                }
            }
        }
        grView.store (std::max (grMax, grView.load() * 0.9f));
        freqView.store (freqSm);

        analyse (main, numCh, key, n, s);
    }

    std::atomic<float> grView { 0.0f }, freqView { 300.0f };
    std::array<std::atomic<float>, an::kBands> mainView {}, keyView {};
    std::atomic<bool> keyPresent { false };
    float foundHz = 0.0f;

private:
    /** Довгі спектри обох сигналів (до обробки ключа — бо основний вже змінено, що для пошуку неважливо). */
    void analyse (float* const* main, int numCh, const float* key, int n, const Settings& s)
    {
        for (int i = 0; i < n; ++i)
        {
            float m = main[0][i];
            if (numCh > 1) m = 0.5f * (m + main[1][i]);
            const float k = key != nullptr ? key[i] : 0.0f;
            for (int b = 0; b < an::kBands; ++b)
            {
                const float ym = bpMain[(size_t) b].process (m), yk = bpKey[(size_t) b].process (k);
                accMain[(size_t) b] += ym * ym; accKey[(size_t) b] += yk * yk;
            }
        }
        accN += n;
        sinceChoose += n;
        if (accN < (int) (0.1 * fs)) return;

        // Кадр 100 мс → повільне середнє (~8 с)
        constexpr double a = 0.0125;
        double keyTot = 0.0;
        for (int b = 0; b < an::kBands; ++b)
        {
            const double pm = accMain[(size_t) b] / accN, pk = accKey[(size_t) b] / accN;
            keyTot += pk;
            if (pm > 1.0e-10) avgMain[(size_t) b] += a * (pm - avgMain[(size_t) b]);
            if (pk > 1.0e-10) avgKey[(size_t) b] += a * (pk - avgKey[(size_t) b]);
            mainView[(size_t) b].store ((float) (10.0 * std::log10 (std::max (avgMain[(size_t) b], 1.0e-12))));
            keyView[(size_t) b].store ((float) (10.0 * std::log10 (std::max (avgKey[(size_t) b], 1.0e-12))));
        }
        keyPresent.store (keyTot > 1.0e-9);
        accMain.fill (0.0); accKey.fill (0.0); accN = 0;

        if (sinceChoose < (int) (0.5 * fs)) return;
        sinceChoose = 0;
        // Де обидва сильні: максимум мінімуму (рівень відносно власного піку в діапазоні)
        double mMax = 1.0e-12, kMax = 1.0e-12;
        for (int b = 0; b < an::kBands; ++b)
            if (an::bandHz[(size_t) b] >= s.lo && an::bandHz[(size_t) b] <= s.hi)
            { mMax = std::max (mMax, avgMain[(size_t) b]); kMax = std::max (kMax, avgKey[(size_t) b]); }
        if (kMax < 1.0e-9 || mMax < 1.0e-9) return;
        double best = -1.0e9;
        float hz = 0.0f;
        for (int b = 0; b < an::kBands; ++b)
        {
            const float f = an::bandHz[(size_t) b];
            if (f < s.lo || f > s.hi) continue;
            const double sm = 10.0 * std::log10 (avgMain[(size_t) b] / mMax), sk = 10.0 * std::log10 (avgKey[(size_t) b] / kMax);
            const double score = std::min (sm, sk);
            if (score > best) { best = score; hz = f; }
        }
        if (hz > 0.0f) foundHz = hz;
    }

    double fs = 48000.0;
    std::array<sn::Biquad, an::kBands> bpMain, bpKey;
    std::array<double, an::kBands> accMain {}, accKey {}, avgMain {}, avgKey {};
    int accN = 0, sinceChoose = 0;
    sn::Biquad keyBp;
    std::array<sn::Biquad, 2> dyn;
    float env = 0.0f, amount = 0.0f, keyPeakDb = -120.0f, gainDb = 0.0f, freqSm = 300.0f, designedHz = -1.0f, designedQ = 0.0f;
};
