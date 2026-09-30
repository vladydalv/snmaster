#pragma once

#include "../Common/SNCommon.h"

/*  Stomp: педаль з плавним морфом схем, модуляція і плівкове ехо.
    Схеми — узагальнення характеру класичних типів педалей і підсилювачів (не копії конкретних виробів). */

namespace st
{
//==============================================================================
struct CircuitParams
{
    float inHpf;                    // вхідний розділовий конденсатор
    float preMidHz, preMidDb, preMidQ;
    float preHighDb;                // підйом верху перед клиппінгом (shelf 1.5 кГц)
    float gainMinDb, gainMaxDb;
    float stage2;                   // 0 — один каскад, 1 — два
    float hardness;                 // 2 — м'яке (лампа/германій), 10 — жорстке (діоди)
    float asym;                     // асиметрія/зміщення (парні гармоніки)
    float interLpHz;                // завал між каскадами
    float postMidHz, postMidDb, postMidQ;
    float postLpHz;
    float sag;                      // чутливість до «сідання» живлення
    float gate;                     // сучасний гейт
};

inline const std::array<CircuitParams, 5> circuits
{{
    // '65 Germanium fuzz: товстий, круглий, асиметричний, чутливий до батарейки
    { 40,  1000, 0, 0.7f, 0,   10, 46, 0.3f, 2.2f, 0.35f, 7000, 700,  1.0f, 0.7f, 4500, 1.0f, 0.0f },
    // '70 British drive (напрям Orange): два лампові каскади, щільна середина, «жує»
    { 70,   900, 5, 0.8f, 2,    0, 34, 1.0f, 2.0f, 0.20f, 6500, 450,  2.0f, 0.7f, 6000, 0.4f, 0.0f },
    // '73 Triangle fuzz: два каскади діодів, провал середини, безкінечний сустейн
    { 90,  1000, 0, 0.7f, 0,   20, 56, 1.0f, 3.5f, 0.00f, 5000, 1000, -8.0f, 0.6f, 5500, 0.3f, 0.0f },
    // '81 Op-amp distortion: жорсткий кліп, яскравий вхід, фільтр після
    { 60,  1500, 2, 0.7f, 6,   12, 52, 0.0f, 8.0f, 0.05f, 9000, 800,  0.0f, 0.7f, 3800, 0.1f, 0.0f },
    // Modern: тугий низ, середина вперед, гейт
    { 140,  750, 4, 1.0f, 3,   20, 60, 1.0f, 6.0f, 0.00f, 8000, 500, -3.0f, 0.8f, 7000, 0.0f, 0.8f },
}};

inline float lerpF (float a, float b, float t) { return a + (b - a) * t; }
inline float lerpLog (float a, float b, float t) { return std::exp (lerpF (std::log (a), std::log (b), t)); }

inline CircuitParams morph (float x)
{
    x = juce::jlimit (0.0f, 4.0f, x);
    const int i = std::min (3, (int) x);
    const float t = x - (float) i;
    const auto& a = circuits[(size_t) i];
    const auto& b = circuits[(size_t) i + 1];
    CircuitParams c;
    c.inHpf = lerpLog (a.inHpf, b.inHpf, t);
    c.preMidHz = lerpLog (a.preMidHz, b.preMidHz, t); c.preMidDb = lerpF (a.preMidDb, b.preMidDb, t); c.preMidQ = lerpF (a.preMidQ, b.preMidQ, t);
    c.preHighDb = lerpF (a.preHighDb, b.preHighDb, t);
    c.gainMinDb = lerpF (a.gainMinDb, b.gainMinDb, t); c.gainMaxDb = lerpF (a.gainMaxDb, b.gainMaxDb, t);
    c.stage2 = lerpF (a.stage2, b.stage2, t);
    c.hardness = lerpLog (a.hardness, b.hardness, t);
    c.asym = lerpF (a.asym, b.asym, t);
    c.interLpHz = lerpLog (a.interLpHz, b.interLpHz, t);
    c.postMidHz = lerpLog (a.postMidHz, b.postMidHz, t); c.postMidDb = lerpF (a.postMidDb, b.postMidDb, t); c.postMidQ = lerpF (a.postMidQ, b.postMidQ, t);
    c.postLpHz = lerpLog (a.postLpHz, b.postLpHz, t);
    c.sag = lerpF (a.sag, b.sag, t);
    c.gate = lerpF (a.gate, b.gate, t);
    return c;
}

/** Обмежувач з регульованою «жорсткістю» коліна: p=2 — м'яко, p→∞ — жорстко. */
inline float clipShape (float x, float p) noexcept
{
    const float ax = std::abs (x);
    return x / std::pow (1.0f + std::pow (ax, p), 1.0f / p);
}

//==============================================================================
/** Педаль: працює в оверсемплованому домені. */
class Pedal
{
public:
    void prepare (double osRate, int numChannels)
    {
        fs = osRate;
        ch.assign ((size_t) numChannels, {});
        envAtk = (float) std::exp (-1.0 / (0.001 * fs));
        envRel = (float) std::exp (-1.0 / (0.08 * fs));
        gateCoef = (float) std::exp (-1.0 / (0.01 * fs));
        lastKey = { -1.0f, -1.0f };
        calibrate();
        reset();
    }

    void reset()
    {
        for (auto& c : ch) { for (auto& f : c.f) f.reset(); c.env = 0.0f; c.gateG = 1.0f; c.dcX = c.dcY = 0.0f; }
    }

    struct Settings { float circuit, gainPct, tonePct, batteryPct, levelDb; };

    void process (juce::dsp::AudioBlock<float>& block, const Settings& s)
    {
        updateFilters (s);
        const auto& c = cur;
        const float g = sn::dbToGain (lerpF (c.gainMinDb, c.gainMaxDb, s.gainPct * 0.01f));
        const float bat = s.batteryPct * 0.01f;
        const float supply = 1.0f - 0.55f * bat;                    // запас по напрузі падає
        const float bias = c.asym + 0.45f * bat;                    // «сіла» батарейка зсуває робочу точку → сплатер
        const float cb1 = clipShape (bias, c.hardness), cb2 = clipShape (0.5f * bias, c.hardness);
        const float norm = lookupNorm (s.circuit, s.gainPct) * sn::dbToGain (s.levelDb);
        const float gateThr = 0.004f * c.gate;
        const float dcR = (float) (1.0 - 2.0 * juce::MathConstants<double>::pi * 10.0 / fs);

        for (size_t k = 0; k < block.getNumChannels() && k < ch.size(); ++k)
        {
            auto& st = ch[k];
            auto* d = block.getChannelPointer (k);
            for (size_t i = 0; i < block.getNumSamples(); ++i)
            {
                float x = d[i];
                x = st.f[0].process (x);               // вхідний HPF
                x = st.f[1].process (x);               // pre mid
                x = st.f[2].process (x);               // pre high shelf

                const float ax = std::abs (x);
                st.env = ax > st.env ? ax + envAtk * (st.env - ax) : ax + envRel * (st.env - ax);

                // Сучасний гейт (глушить шум між нотами)
                if (gateThr > 0.0f)
                {
                    const float target = st.env > gateThr ? 1.0f : 0.0f;
                    st.gateG = target + gateCoef * (st.gateG - target);
                }

                // Просідання живлення від рівня сигналу (германій — найчутливіший)
                const float level = supply / (1.0f + c.sag * (0.3f + 1.5f * bat) * std::min (1.0f, st.env * g * 0.5f));
                float u = x * g / level;
                float y = (clipShape (u + bias, c.hardness) - cb1) * level;

                if (c.stage2 > 0.001f)
                {
                    const float y1 = st.f[3].process (y);        // міжкаскадний завал
                    const float u2 = y1 * 3.0f / level;
                    const float y2 = (clipShape (u2 + 0.5f * bias, c.hardness) - cb2) * level;
                    y = (1.0f - c.stage2) * y + c.stage2 * y2;
                }

                // «Голодна» батарейка: тихі хвости нот обриваються і тріщать
                if (bat > 0.0f)
                {
                    const float e = st.env * g, t = 0.3f * bat;
                    y *= e * e / (e * e + t * t);
                }

                y = st.f[4].process (y);               // post mid
                y = st.f[5].process (y);               // post LP
                y = st.f[6].process (y);               // tone: низи
                y = st.f[7].process (y);               // tone: верх
                const float dc = y - st.dcX + dcR * st.dcY;       // DC від асиметрії
                st.dcX = y; st.dcY = dc;
                d[i] = dc * norm * (gateThr > 0.0f ? st.gateG : 1.0f);
            }
        }
    }

private:
    void updateFilters (const Settings& s)
    {
        const std::array<float, 2> key { s.circuit, s.tonePct };
        if (key == lastKey) return;
        lastKey = key;
        cur = morph (s.circuit);
        const auto& c = cur;
        sn::Biquad f[8];
        f[0].setHighPass (fs, c.inHpf, 0.707);
        f[1].setPeak (fs, c.preMidHz, c.preMidQ, c.preMidDb);
        f[2].setHighShelf (fs, 1500.0, 0.707, c.preHighDb);
        f[3].setLowPass (fs, c.interLpHz, 0.707);
        f[4].setPeak (fs, c.postMidHz, c.postMidQ, c.postMidDb);
        f[5].setLowPass (fs, c.postLpHz, 0.707);
        const float tilt = s.tonePct * 0.01f * 8.0f;          // ±8 дБ нахил навколо ~800 Гц
        f[6].setLowShelf (fs, 400.0, 0.707, -0.5f * tilt);
        f[7].setHighShelf (fs, 1600.0, 0.707, 0.5f * tilt);
        for (auto& st : ch) for (int k = 0; k < 8; ++k) st.f[(size_t) k].copyCoeffs (f[k]);
    }

    /** Нормування гучності: на -18 dBFS вихід ≈ вхід для кожної схеми/гейну (перемикання не стрибає). */
    void calibrate()
    {
        for (int ci = 0; ci <= 8; ++ci)
            for (int gi = 0; gi <= 4; ++gi)
            {
                Pedal p; p.fs = fs; p.ch.assign (1, {});
                p.envAtk = envAtk; p.envRel = envRel; p.gateCoef = gateCoef;
                for (auto& row : p.normTable) row.fill (1.0f);
                const Settings s { (float) ci * 0.5f, (float) gi * 25.0f, 0.0f, 0.0f, 0.0f };
                const int n = (int) (0.08 * fs);
                juce::AudioBuffer<float> b (1, n);
                const float amp = 0.125892541f;
                for (int i = 0; i < n; ++i) b.setSample (0, i, amp * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / fs));
                juce::dsp::AudioBlock<float> blk (b);
                p.process (blk, s);
                const float outRms = b.getRMSLevel (0, n / 2, n / 2);
                normTable[(size_t) ci][(size_t) gi] = outRms > 1e-6f ? (amp * 0.70710678f) / outRms : 1.0f;
            }
    }

    float lookupNorm (float circuitPos, float gainPct) const noexcept
    {
        const float cp = juce::jlimit (0.0f, 8.0f, circuitPos * 2.0f), gp = juce::jlimit (0.0f, 4.0f, gainPct / 25.0f);
        const int c0 = std::min (7, (int) cp), g0 = std::min (3, (int) gp);
        const float tc = cp - (float) c0, tg = gp - (float) g0;
        auto at = [this] (int c, int g) { return normTable[(size_t) c][(size_t) g]; };
        const float a = at (c0, g0) + tg * (at (c0, g0 + 1) - at (c0, g0));
        const float b = at (c0 + 1, g0) + tg * (at (c0 + 1, g0 + 1) - at (c0 + 1, g0));
        return a + tc * (b - a);
    }

    struct Channel { std::array<sn::Biquad, 8> f; float env = 0.0f, gateG = 1.0f, dcX = 0.0f, dcY = 0.0f; };
    std::vector<Channel> ch;
    std::array<std::array<float, 5>, 9> normTable {};
    CircuitParams cur {};
    std::array<float, 2> lastKey { -1.0f, -1.0f };   // «ще не налаштовано»
    double fs = 176400.0;
    float envAtk = 0, envRel = 0, gateCoef = 0;
};

//==============================================================================
/** Модуляція: тремоло, гармонічне тремоло (низи/верхи в протифазі), стерео-пан, вібрато.
    Rise: після кожної атаки модуляція наростає поступово (як вібрато у співака). */
class Modulator
{
public:
    enum Mode { tremolo = 0, harmonic, pan, vibrato };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        split.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
        split.prepare ({ fs, 512, 2 });
        split.setCutoffFrequency (700.0f);
        const int size = (int) (0.03 * fs) + 8;
        vbuf.assign (2, std::vector<float> ((size_t) size, 0.0f));
        vsize = size;
        auto c = [this] (double ms) { return (float) std::exp (-1.0 / (0.001 * ms * fs)); };
        fA = c (2.0); fR = c (15.0); sA = c (30.0); sR = c (150.0);
        reset();
    }

    void reset()
    {
        phase = 0.0; riseEnv = 1.0f; envF = envS = 0.0f; split.reset(); vpos = 0; depthSm = 0.0f;
        for (auto& b : vbuf) std::fill (b.begin(), b.end(), 0.0f);
    }

    /** rateHz уже з урахуванням sync; data — стерео (numCh 1 або 2). */
    void process (float* const* data, int numCh, int n, int mode, float rateHz, float depthPct, float shapePct, float riseMs,
                  const float* onsetSource)
    {
        const double inc = rateHz / fs;
        const float k = 1.0f + shapePct * 0.2f;           // 1 → синус, 21 → майже меандр
        const float tk = std::tanh (k);
        const float riseCoef = riseMs > 1.0f ? (float) std::exp (-1.0 / (0.001 * riseMs * fs)) : 0.0f;
        const float depthT = depthPct * 0.01f;
        const float depthCoef = (float) std::exp (-1.0 / (0.02 * fs));

        for (int i = 0; i < n; ++i)
        {
            // Детектор атаки для Rise
            const float ax = std::abs (onsetSource[i]);
            const float prevS = envS;
            envF = ax > envF ? ax + fA * (envF - ax) : ax + fR * (envF - ax);
            envS = ax > envS ? ax + sA * (envS - ax) : ax + sR * (envS - ax);
            if (riseCoef > 0.0f)
            {
                if (envF > 1.8f * prevS && envF > 0.003f) riseEnv = 0.0f;    // нова нота — модуляція з нуля
                riseEnv = 1.0f + riseCoef * (riseEnv - 1.0f);
            }
            else riseEnv = 1.0f;

            depthSm = depthT + depthCoef * (depthSm - depthT);
            const float d = depthSm * riseEnv;

            phase += inc; if (phase >= 1.0) phase -= 1.0;
            const float sn = (float) std::sin (juce::MathConstants<double>::twoPi * phase);
            const float lfo = std::tanh (k * sn) / tk;     // -1…1, форма від синуса до «рубаної»
            lastLfo = lfo * d;

            float* L = data[0];
            float* R = numCh > 1 ? data[1] : nullptr;
            switch (mode)
            {
                case tremolo:
                {
                    const float g = 1.0f - d * (0.5f - 0.5f * lfo);
                    L[i] *= g; if (R) R[i] *= g;
                    break;
                }
                case harmonic:
                {
                    const float gl = 1.0f - d * (0.5f - 0.5f * lfo), gh = 1.0f - d * (0.5f + 0.5f * lfo);
                    for (int c = 0; c < numCh; ++c)
                    {
                        float lo, hi;
                        split.processSample (c, data[c][i], lo, hi);
                        data[c][i] = lo * gl + hi * gh;
                    }
                    break;
                }
                case pan:
                {
                    if (R == nullptr) { L[i] *= 1.0f - d * (0.5f - 0.5f * lfo); break; }
                    const float p = d * lfo;                                   // -1 ліво … +1 право
                    const float a = (p + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
                    const float m = 0.5f * (L[i] + R[i]);
                    L[i] = m * std::cos (a) * 1.41421356f;
                    R[i] = m * std::sin (a) * 1.41421356f;
                    break;
                }
                default: // vibrato
                {
                    const double delay = 0.006 * fs + 0.004 * fs * d * (0.5 + 0.5 * lfo);   // до 4 мс розмаху
                    for (int c = 0; c < numCh; ++c)
                    {
                        auto& b = vbuf[(size_t) c];
                        b[(size_t) vpos] = data[c][i];
                        data[c][i] = read (b, delay);
                    }
                    vpos = (vpos + 1) % vsize;
                    break;
                }
            }
        }
    }

    float getRise() const noexcept { return riseEnv; }
    float getDepth() const noexcept { return depthSm; }
    float getLfo() const noexcept { return lastLfo; }
    /** Прив'язка фази до сітки хоста (sync). */
    void setPhase (double p) noexcept { phase = p - std::floor (p); }

private:
    float read (const std::vector<float>& b, double delay) const noexcept
    {
        const double rp = (double) vpos - delay;
        const int i1 = (int) std::floor (rp);
        const float t = (float) (rp - i1);
        auto at = [&] (int idx) { return b[(size_t) ((idx % vsize + vsize) % vsize)]; };
        const float y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    double fs = 48000.0, phase = 0.0;
    juce::dsp::LinkwitzRileyFilter<float> split;
    std::vector<std::vector<float>> vbuf;
    int vsize = 1, vpos = 0;
    float fA = 0, fR = 0, sA = 0, sR = 0, envF = 0, envS = 0, riseEnv = 1.0f, depthSm = 0.0f, lastLfo = 0.0f;
};

//==============================================================================
/** Плівкове ехо: насичення, завал верху і детонація в петлі зворотного зв'язку.
    Зміна часу — плавна (як на стрічковому ехо: висота «пливе»). */
class Echo
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        size = (int) (2.6 * fs);
        buf.assign (2, std::vector<float> ((size_t) size, 0.0f));
        noiseLp.setLowpass (fs, 1.0);
        reset();
    }

    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& f : lp) f.reset();
        for (auto& f : hp) f.reset();
        pos = 0; delaySm = -1.0; phase = 0.0; noiseLp.reset(); feed = 1.0f; tailLevel = 0.0f;
    }

    /** feedInput=false: нові ноти в ехо не потрапляють, але «хвости» догравають (як у педалі з trails). */
    void process (float* const* data, int numCh, int n, float timeMs, float feedbackPct, float toneHz, float mixPct, float wearPct,
                  bool feedInput = true)
    {
        const float feedT = feedInput ? 1.0f : 0.0f;
        const float feedCoef = (float) std::exp (-1.0 / (0.005 * fs));
        float peak = 0.0f;
        const double target = juce::jlimit (1.0, (double) size - 8.0, timeMs * 0.001 * fs);
        if (delaySm < 0.0) delaySm = target;
        const double glide = std::exp (-1.0 / (0.15 * fs));
        for (auto& f : lp) f.setLowPass (fs, toneHz, 0.707);
        for (auto& f : hp) f.setHighPass (fs, 90.0, 0.707);
        const float fb = feedbackPct * 0.01f, mix = mixPct * 0.01f, wear = wearPct * 0.01f;
        const float drive = 1.0f + 3.0f * wear;

        for (int i = 0; i < n; ++i)
        {
            feed = feedT + feedCoef * (feed - feedT);
            delaySm = target + glide * (delaySm - target);
            phase += 0.7 / fs; if (phase >= 1.0) phase -= 1.0;
            const float rnd = noiseLp.lowpass (rng.nextFloat() * 2.0f - 1.0f) * 6.0f;
            const double mod = wear * (0.0008 * fs * std::sin (juce::MathConstants<double>::twoPi * phase) + 0.0004 * fs * juce::jlimit (-1.0f, 1.0f, rnd));
            const double d = juce::jlimit (1.0, (double) size - 8.0, delaySm + mod);

            for (int c = 0; c < numCh && c < 2; ++c)
            {
                auto& b = buf[(size_t) c];
                const float wet = read (b, d);
                float loop = lp[(size_t) c].process (hp[(size_t) c].process (wet)) * fb;
                loop = std::tanh (drive * loop) / drive;                      // плівка не дає повторам «вибухнути»
                b[(size_t) pos] = feed * data[c][i] + loop;
                data[c][i] = data[c][i] + mix * wet;
                peak = std::max (peak, std::abs (wet));
            }
            pos = (pos + 1) % size;
        }
        tailLevel = peak;
    }

    /** Пік мокрого сигналу за останній блок: коли ехо вимкнене і хвіст згас — можна не рахувати. */
    float getTailLevel() const noexcept { return tailLevel; }

private:
    float read (const std::vector<float>& b, double delay) const noexcept
    {
        const double rp = (double) pos - delay;
        const int i1 = (int) std::floor (rp);
        const float t = (float) (rp - i1);
        auto at = [&] (int idx) { return b[(size_t) ((idx % size + size) % size)]; };
        const float y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    double fs = 48000.0, delaySm = -1.0, phase = 0.0;
    float feed = 1.0f, tailLevel = 0.0f;
    int size = 1, pos = 0;
    std::vector<std::vector<float>> buf;
    std::array<sn::Biquad, 2> lp, hp;
    sn::OnePole noiseLp;
    juce::Random rng { 0xec40 };
};
} // namespace st
