#pragma once

#include <juce_dsp/juce_dsp.h>
#include "DecadeModel.h"
#include "../Common/SNCommon.h"

/** Носії для SN Decade.
    MP3 — справжній перцептуальний кодек (спектр по кадрах, поріг маскування за смугами Барка,
    квантування з «мертвою зоною», joint stereo): «дірки» в спектрі, «бульки» на верху і пре-ехо з'являються самі,
    як у справжнього кодека на низькому бітрейті.
    Вініл / касета / ацетат — фізичні явища носія (тріск і пили, рокіт, детонація, «горб» головки, самостирання
    верху, азимут, провали). */
namespace dec
{
//==============================================================================
/** Спрощений перцептуальний кодек (STFT 1024 / 512, синус-вікно, точна реконструкція без квантування). */
class PerceptualCodec
{
public:
    static constexpr int order = 10, N = 1 << order, H = N / 2, B = N / 2 + 1;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (int i = 0; i < N; ++i) win[(size_t) i] = (float) std::sin (juce::MathConstants<double>::pi * (i + 0.5) / N);
        for (auto& c : ch) { c.in.fill (0.0f); c.acc.fill (0.0f); c.out.fill (0.0f); }
        filled = N - H; outPos = 0;     // ніби попередній кадр (тиші) вже був — повна реконструкція з першого семпла

        // Масштаб пари forward + inverse (перевіряємо, а не припускаємо)
        std::array<float, 2 * N> t {};
        t[3] = 1.0f;
        fft.performRealOnlyForwardTransform (t.data());
        fft.performRealOnlyInverseTransform (t.data());
        ifftScale = std::abs (t[3]) > 1.0e-9f ? 1.0f / t[3] : 1.0f;

        // Смуги Барка і поріг чутності на кожен бін
        static constexpr float edges[] { 0, 100, 200, 300, 400, 510, 630, 770, 920, 1080, 1270, 1480, 1720, 2000, 2320, 2700,
                                         3150, 3700, 4400, 5300, 6400, 7700, 9500, 12000, 15500, 30000 };
        numBands = (int) std::size (edges) - 1;
        const double binHz = fs / N;
        double winSum = 0.0; for (float w : win) winSum += w;
        const double fullScaleBin = (winSum * 0.5) * (winSum * 0.5);     // потужність біна від синуса 0 dBFS
        for (int k = 0; k < B; ++k)
        {
            const float f = (float) (k * binHz);
            int b = 0; while (b < numBands - 1 && f >= edges[b + 1]) ++b;
            band[(size_t) k] = b;
            const double fk = std::max (0.02, f / 1000.0);
            const double athSpl = 3.64 * std::pow (fk, -0.8) - 6.5 * std::exp (-0.6 * (fk - 3.3) * (fk - 3.3)) + 0.001 * std::pow (fk, 4.0);
            ath[(size_t) k] = (float) (fullScaleBin * std::pow (10.0, (std::min (athSpl, 90.0) - 96.0) / 10.0));
        }
        width.fill (0);
        for (int k = 0; k < B; ++k) ++width[(size_t) band[(size_t) k]];
    }

    static constexpr int latency() { return N; }

    /** amount 0 — без втрат; 1 — дуже низький бітрейт. cutoffHz — смуга, яку лишає кодер. */
    void process (float& m, float& s, float amount, float cutoffHz)
    {
        ch[0].in[(size_t) filled] = m; ch[1].in[(size_t) filled] = s;
        m = ch[0].out[(size_t) outPos]; s = ch[1].out[(size_t) outPos];
        ++outPos;
        if (++filled == N)
        {
            frame (amount, cutoffHz);
            // зсув вхідного вікна на H
            for (auto& c : ch) std::copy (c.in.begin() + H, c.in.end(), c.in.begin());
            filled = N - H;
            outPos = 0;
        }
    }

private:
    struct Ch { std::array<float, N> in {}, acc {}, out {}; };

    void frame (float amount, float cutoffHz)
    {
        const bool lossy = amount > 0.001f;
        if (! lossy)
        {
            // Без втрат результат кадру = вхід × w² — рахуємо напряму, без FFT (дешево, і стан готовий до перемикання)
            for (auto& C : ch)
            {
                for (int i = 0; i < N; ++i) C.acc[(size_t) i] += C.in[(size_t) i] * win[(size_t) i] * win[(size_t) i];
                std::copy (C.acc.begin(), C.acc.begin() + H, C.out.begin());
                std::copy (C.acc.begin() + H, C.acc.end(), C.acc.begin());
                std::fill (C.acc.begin() + H, C.acc.end(), 0.0f);
            }
            return;
        }
        // Amount → «бітрейт»: від майже прозорого (~256 kbps) до грубого (~64 kbps)
        const float smrDb = juce::jmap (amount, 0.0f, 1.0f, 30.0f, 6.0f);
        const int cutBin = lossy ? juce::jlimit (8, B, (int) (cutoffHz / (fs / N))) : B;

        std::array<std::array<float, 2 * N>, 2> spec {};
        std::array<std::array<double, 32>, 2> E {};
        for (int c = 0; c < 2; ++c)
        {
            auto& x = spec[(size_t) c];
            for (int i = 0; i < N; ++i) x[(size_t) i] = ch[(size_t) c].in[(size_t) i] * win[(size_t) i];
            fft.performRealOnlyForwardTransform (x.data());
            for (int k = 0; k < B; ++k) E[(size_t) c][(size_t) band[(size_t) k]] += (double) x[(size_t) (2 * k)] * x[(size_t) (2 * k)] + (double) x[(size_t) (2 * k + 1)] * x[(size_t) (2 * k + 1)];
        }

        if (lossy)
            for (int c = 0; c < 2; ++c)
            {
                // Поріг маскування: розмазування енергії смуг (вгору -10 дБ/Барк, вниз -25 дБ/Барк) мінус SMR
                std::array<double, 32> T {};
                for (int b = 0; b < numBands; ++b)
                {
                    double t = 0.0;
                    for (int j = 0; j < numBands; ++j)
                    {
                        const int d = b - j;
                        t += E[(size_t) c][(size_t) j] * std::pow (10.0, -(d >= 0 ? 10.0 * d : 25.0 * -d) / 10.0);
                    }
                    T[(size_t) b] = t * std::pow (10.0, -(smrDb - (c == 1 ? 6.0f : 0.0f)) / 10.0);   // сторона (side) — грубіше, як у joint stereo
                }
                auto& x = spec[(size_t) c];
                for (int k = 0; k < B; ++k)
                {
                    float& re = x[(size_t) (2 * k)];
                    float& im = x[(size_t) (2 * k + 1)];
                    if (k >= cutBin) { re = im = 0.0f; continue; }
                    const int b = band[(size_t) k];
                    const double allowed = std::max (T[(size_t) b] / std::max (1, width[(size_t) b]), (double) ath[(size_t) k]);
                    const float step = (float) std::sqrt (6.0 * allowed);
                    auto q = [step] (float v)
                    {
                        // Квантування з мертвою зоною: дрібне зникає зовсім — звідси «дірки» і «бульки»
                        const float a = std::abs (v) / step;
                        const float n = std::floor (a + 0.5f - 0.1f);
                        return n <= 0.0f ? 0.0f : std::copysign (n * step, v);
                    };
                    re = q (re); im = q (im);
                }
            }

        for (int c = 0; c < 2; ++c)
        {
            auto& x = spec[(size_t) c];
            fft.performRealOnlyInverseTransform (x.data());
            auto& C = ch[(size_t) c];
            for (int i = 0; i < N; ++i) C.acc[(size_t) i] += x[(size_t) i] * ifftScale * win[(size_t) i];
            // Перша половина готова — це вихід на наступні H семплів
            std::copy (C.acc.begin(), C.acc.begin() + H, C.out.begin());
            std::copy (C.acc.begin() + H, C.acc.end(), C.acc.begin());
            std::fill (C.acc.begin() + H, C.acc.end(), 0.0f);
        }
    }

    double fs = 48000.0;
    juce::dsp::FFT fft { order };
    std::array<float, N> win {};
    std::array<Ch, 2> ch;
    std::array<int, B> band {};
    std::array<float, B> ath {};
    std::array<int, 32> width {};
    int numBands = 25, filled = 0, outPos = 0;
    float ifftScale = 1.0f;
};

//==============================================================================
class FormatMod
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        wowBase = (int) std::ceil (0.004 * fs);
        base = PerceptualCodec::latency() + wowBase;
        lineLen = base + 2 * wowBase + 16;
        for (auto& l : line) l.assign ((size_t) lineLen, 0.0f);
        mp3Len = wowBase + 4;
        for (auto& l : mp3Line) l.assign ((size_t) mp3Len, 0.0f);
        wpos = mpos = 0;
        codec.prepare (fs);
        for (auto& v : voices) v = {};
        for (int t = 0; t < kNumFormat; ++t) designType (t, 0.3f);
        cur = -1; prev = -1; fade = 0;
        avgIn = avgOut = 0.0f; makeup = 1.0f; lastAge = -1.0f;
    }

    int latency() const { return base; }

    void process (juce::AudioBuffer<float>& buf, int numCh, int type, float amount, float age)
    {
        const int n = buf.getNumSamples();
        double eIn = 0.0;
        for (int c = 0; c < numCh; ++c) { const float r = buf.getRMSLevel (c, 0, n); eIn += r * r; }
        if (std::abs (age - lastAge) > 0.01f) { for (int t = 0; t < kNumFormat; ++t) designType (t, age); lastAge = age; }
        if (type != cur) { prev = cur; cur = type; fade = prev >= 0 ? (int) (0.05 * fs) : 0; }
        const int total = (int) (0.05 * fs);

        for (int i = 0; i < n; ++i)
        {
            const float L = buf.getSample (0, i), R = numCh > 1 ? buf.getSample (1, i) : L;
            line[0][(size_t) wpos] = L; line[1][(size_t) wpos] = R;

            // Кодек працює завжди (готовий до перемикання без «прогріву»); вихід вирівнюємо до спільної латентності
            float m = 0.5f * (L + R), s = 0.5f * (L - R);
            codec.process (m, s, cur == MP3 || prev == MP3 ? amount : 0.0f, juce::jmap (age, 16000.0f, 11000.0f));
            mp3Line[0][(size_t) mpos] = m + s; mp3Line[1][(size_t) mpos] = m - s;

            float oL, oR;
            frame (cur, amount, age, oL, oR);
            if (fade > 0 && prev >= 0)
            {
                float pL, pR;
                frame (prev, amount, age, pL, pR);
                const float t = (float) fade / (float) total;
                oL = (1.0f - t) * oL + t * pL; oR = (1.0f - t) * oR + t * pR;
                --fade;
            }
            buf.setSample (0, i, oL);
            if (numCh > 1) buf.setSample (1, i, oR);
            wpos = (wpos + 1) % lineLen;
            mpos = (mpos + 1) % mp3Len;
        }

        // Вузька смуга старих носіїв «з'їдає» гучність — повертаємо (лише вгору, до +9 дБ): порівнюємо характер, а не рівень
        double eOut = 0.0;
        for (int c = 0; c < numCh; ++c) { const float r = buf.getRMSLevel (c, 0, n); eOut += r * r; }
        const float k = std::min (1.0f, (float) n / (float) (2.0 * fs));
        if (eIn > 1.0e-10) { avgIn += k * ((float) eIn - avgIn); avgOut += k * ((float) eOut - avgOut); }
        const bool comp = amount > 0.001f && (cur == Acetate || cur == Vinyl || cur == Cassette || cur == MP3);
        const float target = comp && avgOut > 1.0e-12f ? juce::jlimit (1.0f, 2.8f, std::sqrt (avgIn / avgOut)) : 1.0f;
        const float g0 = makeup;
        makeup += std::min (1.0f, (float) n / (float) (0.3 * fs)) * (target - makeup);
        for (int c = 0; c < numCh; ++c) buf.applyGainRamp (c, 0, n, g0, makeup);
    }

private:
    /** Стан і фільтри кожного носія окремо (під час перехресного переходу вони не заважають одне одному). */
    struct Voice
    {
        std::array<sn::Biquad, 2> lpL, lpR;            // смуга носія
        sn::Biquad hp, horn, bump, dip, bumpR, dipR, hfL, hfR, sideHp, trace, traceR, hissHp, clickBp;
        float hfEnvL = 0, hfEnvR = 0, rumble = 0, drift = 0, noiseLp = 0;
        float clickAmp = 0, clickPh = 0, clickInc = 0, clickDecay = 0, thump = 0, thumpPh = 0, clickPanL = 1, clickPanR = 1;
        int clickLeft = 0;
        float drop = 1, dropT = 1; int dropLeft = 0;
        std::array<float, 64> azBuf {}; int azPos = 0;
        double phWow = 0, phFl = 0, phScrape = 0;
    };

    void designType (int t, float age)
    {
        auto& v = voices[(size_t) t];
        float lp = 20000.0f;
        switch (t)
        {
            case Acetate:  lp = juce::jmap (age, 5000.0f, 3500.0f); break;
            case Vinyl:    lp = juce::jmap (age, 18000.0f, 10000.0f); break;     // знос і внутрішні доріжки тьмяніють
            case Cassette: lp = juce::jmap (age, 14000.0f, 7000.0f); break;      // ферит, заграна стрічка
            case CD:       lp = 20500.0f; break;
            case Stream:   lp = 19500.0f; break;
            default: break;
        }
        lp = std::min (lp, (float) (0.45 * fs));
        sn::Biquad a, b; a.setLowPass (fs, lp, 0.54); b.setLowPass (fs, lp, 1.31);
        for (auto* arr : { &v.lpL, &v.lpR }) { (*arr)[0].copyCoeffs (a); (*arr)[1].copyCoeffs (b); }
        v.hp.setHighPass (fs, t == Acetate ? 140.0 : 25.0, 0.707);
        v.horn.setPeak (fs, 1600.0, 1.4, 4.0);                 // рупор / динамік лампового приймача
        v.bump.setPeak (fs, 62.0, 1.1, 2.5); v.bumpR.copyCoeffs (v.bump);   // «горб» головки касети
        v.dip.setPeak (fs, 130.0, 1.4, -1.2); v.dipR.copyCoeffs (v.dip);
        v.hfL.setHighPass (fs, 4500.0, 0.707); v.hfR.copyCoeffs (v.hfL);
        v.sideHp.setHighPass (fs, 150.0, 0.707);
        v.trace.setHighPass (fs, 2500.0, 0.707); v.traceR.copyCoeffs (v.trace);
        v.hissHp.setHighPass (fs, 2000.0, 0.6);
        v.clickBp.setBandPass (fs, 3500.0, 0.8);
    }

    float tap (int c, double delay) const
    {
        const double rp = (double) wpos - delay;
        const int i0 = (int) std::floor (rp);
        const float t = (float) (rp - i0);
        auto at = [&] (int k) { return line[(size_t) c][(size_t) ((k % lineLen + lineLen) % lineLen)]; };
        return at (i0) + t * (at (i0 + 1) - at (i0));
    }

    float noise() { return rng.nextFloat() * 2.0f - 1.0f; }

    static float lowpass (std::array<sn::Biquad, 2>& f, float x, float amt)
    {
        const float y = f[1].process (f[0].process (x));
        return x + std::min (1.0f, amt * 4.0f) * (y - x);
    }

    /** Тріск і пили вінілу/шелаку: короткі сплески різної довжини, зрідка «поп» із низьким ударом; випадкова панорама. */
    void crackle (Voice& v, float rate, float level, float& l, float& r)
    {
        if (v.clickLeft <= 0 && rng.nextFloat() < rate / (float) fs)
        {
            const bool pop = rng.nextFloat() < 0.08f;
            v.clickLeft = (int) ((pop ? 0.004 : 0.0002 + 0.0012 * rng.nextFloat()) * fs);
            v.clickAmp = level * (pop ? 0.25f : 0.04f + 0.12f * rng.nextFloat() * rng.nextFloat());
            v.clickDecay = std::exp (-1.0f / ((pop ? 0.002f : 0.0004f) * (float) fs));
            v.thump = pop ? level * 0.15f : 0.0f; v.thumpPh = 0.0f;
            const float pan = rng.nextFloat();
            v.clickPanL = std::sqrt (1.0f - pan) * (rng.nextBool() ? 1.0f : -1.0f);
            v.clickPanR = std::sqrt (pan) * (rng.nextFloat() < 0.6f ? (v.clickPanL > 0 ? 1.0f : -1.0f) : (v.clickPanL > 0 ? -1.0f : 1.0f));
        }
        float c = 0.0f;
        if (v.clickLeft > 0)
        {
            --v.clickLeft;
            c = v.clickBp.process (noise() * v.clickAmp);
            v.clickAmp *= v.clickDecay;
        }
        if (v.thump > 1.0e-5f)
        {
            v.thumpPh += (float) (juce::MathConstants<double>::twoPi * 90.0 / fs);
            c += v.thump * std::sin (v.thumpPh);
            v.thump *= std::exp (-1.0f / (0.015f * (float) fs));
        }
        l += c * v.clickPanL; r += c * v.clickPanR;
    }

    void frame (int type, float amt, float age, float& oL, float& oR)
    {
        auto& v = voices[(size_t) juce::jlimit (0, kNumFormat - 1, type)];
        const double base0 = (double) base;
        double d = base0;
        const double twoPi = juce::MathConstants<double>::twoPi;

        if (type == Vinyl || type == Cassette)
        {
            // Детонація: вініл — ексцентриситет 33 об/хв + повільний дрейф; касета — wow, flutter і «шкрябання» стрічки
            v.drift += 0.00002f * (noise() - v.drift);
            v.phWow += (type == Vinyl ? 0.5556 : 0.9) / fs; if (v.phWow > 1) v.phWow -= 1;
            v.phFl += 7.3 / fs; if (v.phFl > 1) v.phFl -= 1;
            const double depth = (type == Vinyl ? 0.35 : 1.0) * (0.25 + 0.75 * age) * amt;
            d += depth * fs * (0.0010 * std::sin (twoPi * v.phWow) + 0.0006 * v.drift * 40.0
                               + (type == Cassette ? 0.00007 * std::sin (twoPi * v.phFl) + 0.00002 * noise() : 0.0));
            d = juce::jlimit (base0 - wowBase + 2.0, base0 + wowBase - 2.0, d);
        }
        float L = tap (0, d), R = tap (1, d);

        switch (type)
        {
            case Acetate:
            {
                // Шелак / ламповий приймач: моно, вузька смуга з «рупорним» піком, перевантаження, гул, тріск і шерех
                float m = 0.5f * (L + R);
                float y = v.horn.process (v.hp.process (m));
                y = lowpass (v.lpL, y, 1.0f);
                const float drive = 1.0f + 3.0f * amt;
                y = std::tanh (drive * y) / std::tanh (drive * 0.5f) * 0.5f;
                counter += 1;
                const double t = (double) counter / fs;
                y += amt * 0.004f * (float) (std::sin (twoPi * 50.0 * t) + 0.5 * std::sin (twoPi * 100.0 * t));
                v.noiseLp += 0.3f * (noise() - v.noiseLp);
                y += amt * (0.002f + 0.004f * age) * v.noiseLp;
                float cl = 0.0f, cr = 0.0f;
                crackle (v, (4.0f + 30.0f * age) * amt, amt * (0.6f + age), cl, cr);
                y += 0.7f * (cl + cr);
                m = (1.0f - amt) * m + amt * y;
                L = R = m;
                break;
            }
            case Vinyl:
            {
                // Моно низ (так ріже різець), спотворення стеження на верху (внутрішні доріжки), шерех, тріск, рокіт
                float mid = 0.5f * (L + R), side = 0.5f * (L - R);
                side = (1.0f - amt) * side + amt * v.sideHp.process (side);
                L = mid + side; R = mid - side;
                const float tk = 3.0f * age * amt;
                const float hl = v.trace.process (L), hr = v.traceR.process (R);
                L += tk * hl * std::abs (hl); R += tk * hr * std::abs (hr);
                L = lowpass (v.lpL, L, amt); R = lowpass (v.lpR, R, amt);
                v.rumble += 0.0025f * (noise() - v.rumble);
                const float surf = amt * (0.0006f + 0.002f * age) * v.hissHp.process (noise());
                float cl = 0.0f, cr = 0.0f;
                crackle (v, (0.3f + 12.0f * age * age) * amt, amt * (0.3f + 0.7f * age), cl, cr);
                const float rum = amt * age * 0.03f * v.rumble;
                L += cl + surf + rum; R += cr + surf + rum;
                break;
            }
            case Cassette:
            {
                // Головка: «горб» на низу; ферит: насичення і самостирання верху на гучному; азимут; шип; провали
                L = v.dip.process (v.bump.process (L)); R = v.dipR.process (v.bumpR.process (R));
                const float k = 1.0f + 2.0f * amt;
                L = std::tanh (k * L) / k; R = std::tanh (k * R) / k;
                const float hl = v.hfL.process (L), hr = v.hfR.process (R);
                v.hfEnvL = std::max (std::abs (hl), v.hfEnvL * 0.999f); v.hfEnvR = std::max (std::abs (hr), v.hfEnvR * 0.999f);
                const float sl = 1.0f / (1.0f + 6.0f * amt * v.hfEnvL), sr = 1.0f / (1.0f + 6.0f * amt * v.hfEnvR);
                L += hl * (sl - 1.0f); R += hr * (sr - 1.0f);
                // Азимут: верх правого каналу трохи запізнюється (до ~0.3 мс із віком) — у моно верх «плаває»
                v.azBuf[(size_t) v.azPos] = hr * sr;
                const int azD = juce::jlimit (0, 63, (int) (age * amt * 0.0003 * fs));
                const float hrDelayed = v.azBuf[(size_t) ((v.azPos - azD + 64) % 64)];
                v.azPos = (v.azPos + 1) % 64;
                R += hrDelayed - hr * sr;
                L = lowpass (v.lpL, L, amt); R = lowpass (v.lpR, R, amt);
                const float hiss = amt * (0.0012f + 0.004f * age) * v.hissHp.process (noise());
                if (v.dropLeft <= 0 && rng.nextFloat() < age * age * 0.4f / (float) fs) { v.dropLeft = (int) ((0.03 + 0.05 * rng.nextFloat()) * fs); v.dropT = 0.35f; }
                if (v.dropLeft > 0 && --v.dropLeft == 0) v.dropT = 1.0f;
                v.drop += 0.002f * (v.dropT - v.drop);
                const float g = 1.0f - amt * (1.0f - v.drop);
                L = L * g + hiss; R = R * g + 0.9f * hiss;
                break;
            }
            case CD:
            {
                // 16 біт із дизером + «війна гучності» (жорстке обмеження) з Amount
                const float push = sn::dbToGain (6.0f * amt), ceil = 0.95f;
                auto brick = [&] (float x) { x *= push; return amt > 0.01f ? ceil * std::tanh (x / ceil) : x; };
                const float q = 32768.0f;
                L = std::round (brick (L) * q + (rng.nextFloat() - rng.nextFloat())) / q;
                R = std::round (brick (R) * q + (rng.nextFloat() - rng.nextFloat())) / q;
                break;
            }
            case MP3:
            {
                // Вихід кодека (він уже затриманий на N), дотягнутий до спільної латентності
                const int back = (mpos - wowBase + mp3Len) % mp3Len;
                L = mp3Line[0][(size_t) back]; R = mp3Line[1][(size_t) back];
                break;
            }
            case Stream:
            {
                // Майже прозоро: межа true peak −1 dBTP
                const float c = 0.89f;
                if (amt > 0.01f) { L = c * std::tanh (L / c); R = c * std::tanh (R / c); }
                L = lowpass (v.lpL, L, amt); R = lowpass (v.lpR, R, amt);
                break;
            }
            default: break;
        }
        oL = L; oR = R;
    }

    double fs = 48000.0;
    int wowBase = 192, base = 1216, lineLen = 1600, mp3Len = 200, wpos = 0, mpos = 0, cur = -1, prev = -1, fade = 0;
    long long counter = 0;
    float avgIn = 0.0f, avgOut = 0.0f, makeup = 1.0f, lastAge = -1.0f;
    std::array<std::vector<float>, 2> line, mp3Line;
    std::array<Voice, kNumFormat> voices;
    PerceptualCodec codec;
    juce::Random rng { 0x1950 };
};
} // namespace dec
