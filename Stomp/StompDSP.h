#pragma once

#include "../Common/SNCommon.h"
#include "../Common/PitchDetector.h"

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
//==============================================================================
/** Октавер: три голоси (−1, −2, +1 октава), кожен — суміш двох двигунів (Character):

    Poly   — ERB-подібний банк комплексних смугових фільтрів; у кожній смузі фаза аналітичного сигналу
             масштабується (×½, ×¼ — вниз; ×2 — вгору). Фази сусідніх смуг прив'язані до найближчого піку
             спектра (phase locking), тож смуги однієї гармоніки складаються когерентно, а не гасять одна одну.
             Тримає акорди. Низи суб-голосів підсилені: у гітари слабка основна частота, без цього суб «худий».
    Analog — трекер YIN (знаходить основну навіть коли вона слабка) + вузький смуговий фільтр на ній →
             тригери-дільники, синхронізовані осцилятори ½ і ¼. Суб = вхід × «майже меандр» (як у класичних
             аналогових октаверах: зберігає тембр і атаку) + чистий синусоїдальний суб для маси.
             +1: випрямлювач «октавія» (класичний дзвінкий октав-ап, найкраще перед фузом).

    Bloom — октави наростають після атаки; Wobble — резонансний ФНЧ на октавах з LFO. */
class Octaver
{
public:
    struct Settings { float sub1Pct, sub2Pct, upPct, dryPct, characterPct, toneHz, bloomMs, wobblePct, wobbleHz; };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        pitch.prepare (fs, 35.0, 6000.0);
        xlp1.setLowPass (fs, 1200.0, 0.707); xlp2.setLowPass (fs, 1200.0, 0.707);
        upBp1.setHighPass (fs, 90.0, 0.707); upBp2.setLowPass (fs, 2500.0, 0.707);
        upDc.setHighPass (fs, 40.0, 0.707);
        subLp1.setLowPass (fs, 160.0, 0.707); subLp2.setLowPass (fs, 160.0, 0.707);
        auto c = [this] (double ms) { return (float) std::exp (-1.0 / (0.001 * ms * fs)); };
        eA = c (1.0); eR = c (50.0); fA = c (2.0); fR = c (15.0); sA = c (30.0); sR = c (150.0);
        gA = c (3.0); gR = c (40.0); sm = c (20.0);
        bpA = c (1.0); bpR = c (20.0);
        holdSamples = (int) (0.08 * fs);

        for (int k = 0; k < kBands; ++k)
        {
            const double fc = 30.0 * std::pow (3000.0 / 30.0, (double) k / (kBands - 1));
            const double r = std::exp (-juce::MathConstants<double>::pi * 0.2 * fc / fs);
            auto& b = bands[(size_t) k];
            b.pr = (float) (r * std::cos (juce::MathConstants<double>::twoPi * fc / fs));
            b.pi = (float) (r * std::sin (juce::MathConstants<double>::twoPi * fc / fs));
            b.g = (float) (1.0 - r);
            b.wSub = (float) juce::jlimit (1.0, 2.2, std::sqrt (160.0 / fc));
        }
        norm = {};
        norm.fill (1.0f);
        reset();
        calibrate();
        reset();
    }

    void reset()
    {
        pitch.reset();
        for (auto* f : { &xlp1, &xlp2, &upBp1, &upBp2, &upDc, &toneLp, &fb1, &fb2, &subLp1, &subLp2 }) f->reset();
        envX = envR = envBp = envF = envS = 0.0f; bloomEnv = 1.0f;
        armed = false; ff1 = ff2 = false; f0 = 0.0; valid = 0; analogGate = 0.0f;
        ph0 = ph1 = ph2 = 0.0;
        g1 = g2 = gu = 0.0f; gd = 1.0f; fresh = true;
        for (auto& b : bands) b.clear();
        svfIc1 = svfIc2 = 0.0f; wobPhase = 0.0;
    }

    void setWobblePhase (double p) noexcept { wobPhase = p - std::floor (p); }

    /** subClean (може бути nullptr): низ октав (< 160 Гц) окремо — щоб у режимі Pre суб не зрізав вхідний фільтр драйву. */
    void process (float* const* data, int numCh, int n, const Settings& s, float* subClean = nullptr)
    {
        const float c = juce::jlimit (0.0f, 1.0f, s.characterPct * 0.01f);
        const float polyW = std::cos (c * juce::MathConstants<float>::halfPi), anaW = std::sin (c * juce::MathConstants<float>::halfPi);
        const float t1 = s.sub1Pct * 0.01f, t2 = s.sub2Pct * 0.01f, tu = s.upPct * 0.01f, td = s.dryPct * 0.01f;
        const float bloomCoef = s.bloomMs > 1.0f ? (float) std::exp (-1.0 / (0.001 * s.bloomMs * fs)) : 0.0f;
        toneLp.setLowPass (fs, juce::jlimit (150.0, 0.45 * fs, (double) s.toneHz), 0.707);
        const float wob = s.wobblePct * 0.01f;
        const double wobInc = s.wobbleHz / fs;
        if (fresh) { g1 = t1; g2 = t2; gu = tu; gd = td; fresh = false; }

        for (int i = 0; i < n; ++i)
        {
            float x = data[0][i];
            if (numCh > 1) x = 0.5f * (x + data[1][i]);

            g1 = t1 + sm * (g1 - t1); g2 = t2 + sm * (g2 - t2); gu = tu + sm * (gu - tu); gd = td + sm * (gd - td);

            // --- Bloom
            const float ax = std::abs (x);
            const float prevS = envS;
            envF = ax > envF ? ax + fA * (envF - ax) : ax + fR * (envF - ax);
            envS = ax > envS ? ax + sA * (envS - ax) : ax + sR * (envS - ax);
            if (bloomCoef > 0.0f)
            {
                if (envF > 1.8f * prevS && envF > 0.003f) bloomEnv = 0.0f;
                bloomEnv = 1.0f + bloomCoef * (bloomEnv - 1.0f);
            }
            else bloomEnv = 1.0f;

            float p1, p2, pu, a1, a2, au;
            voices (x, p1, p2, pu, a1, a2, au);

            float oct = g1 * (polyW * p1 * norm[0] + anaW * a1 * norm[3])
                      + g2 * (polyW * p2 * norm[1] + anaW * a2 * norm[4])
                      + gu * (polyW * pu * norm[2] + anaW * au * norm[5]);
            oct = toneLp.process (oct);

            if (wob > 0.001f)
            {
                wobPhase += wobInc; if (wobPhase >= 1.0) wobPhase -= 1.0;
                if ((i & 15) == 0)
                {
                    const float lfo = 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * wobPhase);
                    // Верх — Tone (не вище 3 кГц), низ — до 60 Гц (глибина = Wobble)
                    const float top = std::min (s.toneHz, 3000.0f);
                    const float fc = juce::jlimit (50.0f, (float) (0.4 * fs), top * std::pow (std::min (1.0f, 60.0f / top), wob * (1.0f - lfo)));
                    svfG = (float) std::tan (juce::MathConstants<double>::pi * fc / fs);
                    const float q = 0.7f + 1.8f * wob;
                    svfK = 1.0f / q;
                    svfComp = 1.0f / (1.0f + 0.35f * (q - 0.7f));
                }
                const float v3 = oct - svfIc2;
                const float v1 = (svfIc1 + svfG * v3) / (1.0f + svfG * (svfG + svfK));
                const float v2 = svfIc2 + svfG * v1;
                svfIc1 = 2.0f * v1 - svfIc1; svfIc2 = 2.0f * v2 - svfIc2;
                oct = v2 * svfComp;
            }

            oct *= bloomEnv;
            if (subClean != nullptr) subClean[i] = subLp2.process (subLp1.process (oct));
            for (int ch = 0; ch < numCh; ++ch)
                data[ch][i] = gd * data[ch][i] + oct;
        }
    }

    float getTrackedHz() const noexcept { return analogGate > 0.5f ? (float) f0 : 0.0f; }
    bool isIdle() const noexcept { return g1 < 1.0e-4f && g2 < 1.0e-4f && gu < 1.0e-4f && gd > 0.9999f; }

private:
    //--------------------------------------------------------------------------
    /** Усі шість голосів (до нормування) для одного семпла. */
    void voices (float x, float& p1, float& p2, float& pu, float& a1, float& a2, float& au) noexcept
    {
        // --- Analog: трекер
        if (pitch.push (x))
        {
            if (pitch.hz > 0.0f && pitch.clarity > 0.7f) { onPitch (pitch.hz); valid = holdSamples; }
        }
        if (valid > 0) --valid;

        const float xl = xlp2.process (xlp1.process (x));
        const float axl = std::abs (xl);
        envX = axl > envX ? axl + eA * (envX - axl) : axl + eR * (envX - axl);
        const bool tracking = valid > 0 && f0 > 0.0 && envX > 3.0e-4f;
        const float gt = tracking ? 1.0f : 0.0f;
        analogGate = gt + (tracking ? gA : gR) * (analogGate - gt);

        if (f0 > 0.0)
        {
            // Смуговий фільтр на основній: чисті тригери навіть коли основна слабка
            const float bp = fb2.process (fb1.process (xl));
            const float abp = std::abs (bp);
            envBp = abp > envBp ? abp + bpA * (envBp - abp) : abp + bpR * (envBp - abp);
            const float thr = 0.3f * envBp + 1.0e-7f;
            if (bp < -thr) armed = true;
            if (armed && bp > thr)
            {
                armed = false;
                ph0 += 0.5 * wrapHalf (-ph0);
                ff1 = ! ff1;
                if (ff1)
                {
                    ph1 += 0.5 * wrapHalf (-ph1);
                    ff2 = ! ff2;
                    if (ff2) ph2 += 0.5 * wrapHalf (-ph2);
                }
            }
            const double inc = f0 / fs;
            ph0 += inc;        ph0 -= std::floor (ph0);
            ph1 += 0.5 * inc;  ph1 -= std::floor (ph1);
            ph2 += 0.25 * inc; ph2 -= std::floor (ph2);
        }
        const float s1 = sinTurn (ph1), s2 = sinTurn (ph2);
        a1 = analogGate * (xl * sq (s1) + 0.7f * envX * s1);
        a2 = analogGate * (xl * sq (s2) + 0.7f * envX * s2);
        // «Октавія»: випрямлення; рівень приведено до огинаючої входу (не залежить від форми хвилі)
        const float rect = upDc.process (std::abs (upBp2.process (upBp1.process (x))));
        const float ar = std::abs (rect);
        envR = ar > envR ? ar + eA * (envR - ar) : ar + eR * (envR - ar);
        au = rect * envX / (envR + 1.0e-6f);

        // --- Poly
        bank (x, p1, p2, pu);
    }

    static float sinTurn (double ph) noexcept { return (float) std::sin (juce::MathConstants<double>::twoPi * ph); }
    static float sq (float s) noexcept { return std::tanh (4.0f * s) * 1.00067f; }          // «майже меандр» без різких фронтів
    static double wrapHalf (double v) noexcept { return v - std::round (v); }

    void onPitch (float hz)
    {
        if (f0 <= 0.0 || std::abs (std::log2 (hz / f0)) > 0.04) f0 = hz;   // нова нота — одразу
        else f0 = 0.7 * f0 + 0.3 * hz;                                      // та сама — згладжено (вібрато, бенди)
        if (std::abs (f0 - bpHz) > 0.01 * bpHz)
        {
            bpHz = f0;
            fb1.setBandPass (fs, f0, 1.2); fb2.setBandPass (fs, f0, 1.2);
        }
    }

    //--------------------------------------------------------------------------
    struct Band
    {
        float pr = 0, pi = 0, g = 0, wSub = 1;
        float ar = 0, ai = 0, br = 0, bi = 0;
        float qr = 1, qi = 0;
        float h1r = 1, h1i = 0, h2r = 1, h2i = 0;
        void clear() { ar = ai = br = bi = 0; qr = 1; qi = 0; h1r = 1; h1i = 0; h2r = 1; h2i = 0; }
    };
    static constexpr int kBands = 40;

    static inline void halfAngle (float c, float s, float& hc, float& hs) noexcept
    {
        hc = std::sqrt (std::max (0.0f, 0.5f * (1.0f + c)));
        hs = std::copysign (std::sqrt (std::max (0.0f, 0.5f * (1.0f - c))), s);
    }

    static inline void rotate (float& r, float& i, float c, float s) noexcept
    {
        const float t = r * c - i * s; i = r * s + i * c; r = t;
    }

    void bank (float x, float& sub1Out, float& sub2Out, float& upOut) noexcept
    {
        sub1Out = sub2Out = upOut = 0.0f;
        std::array<float, kBands> mag, ur, ui;
        for (int k = 0; k < kBands; ++k)
        {
            auto& b = bands[(size_t) k];
            float nr = b.g * x + b.pr * b.ar - b.pi * b.ai;
            float ni = b.pr * b.ai + b.pi * b.ar;
            b.ar = nr; b.ai = ni;
            nr = b.g * b.ar + b.pr * b.br - b.pi * b.bi;
            ni = b.g * b.ai + b.pr * b.bi + b.pi * b.br;
            b.br = nr; b.bi = ni;
            const float m = std::sqrt (b.br * b.br + b.bi * b.bi);
            mag[(size_t) k] = m;
            if (m < 1.0e-12f) { ur[(size_t) k] = b.qr; ui[(size_t) k] = b.qi; continue; }
            ur[(size_t) k] = b.br / m; ui[(size_t) k] = b.bi / m;
            // Власне накопичення фази (для піків)
            const float dr = ur[(size_t) k] * b.qr + ui[(size_t) k] * b.qi, di = ui[(size_t) k] * b.qr - ur[(size_t) k] * b.qi;
            b.qr = ur[(size_t) k]; b.qi = ui[(size_t) k];
            float c1, s1, c2, s2;
            halfAngle (dr, di, c1, s1);
            halfAngle (c1, s1, c2, s2);
            rotate (b.h1r, b.h1i, c1, s1);
            rotate (b.h2r, b.h2i, c2, s2);
            const float n1 = 1.5f - 0.5f * (b.h1r * b.h1r + b.h1i * b.h1i), n2 = 1.5f - 0.5f * (b.h2r * b.h2r + b.h2i * b.h2i);
            b.h1r *= n1; b.h1i *= n1; b.h2r *= n2; b.h2i *= n2;
        }

        // Прив'язка фаз до піку: смуга бере фазу свого піку + половину (чверть) різниці фаз з ним
        for (int k = 0; k < kBands; ++k)
        {
            int p = k;
            for (;;)
            {
                if (p + 1 < kBands && mag[(size_t) p + 1] > mag[(size_t) p]) ++p;
                else if (p > 0 && mag[(size_t) p - 1] > mag[(size_t) p]) --p;
                else break;
            }
            auto& b = bands[(size_t) k];
            if (p != k)
            {
                const auto& pk = bands[(size_t) p];
                const float rr = ur[(size_t) k] * ur[(size_t) p] + ui[(size_t) k] * ui[(size_t) p];
                const float ri = ui[(size_t) k] * ur[(size_t) p] - ur[(size_t) k] * ui[(size_t) p];
                float c1, s1, c2, s2;
                halfAngle (rr, ri, c1, s1);
                halfAngle (c1, s1, c2, s2);
                b.h1r = pk.h1r; b.h1i = pk.h1i; rotate (b.h1r, b.h1i, c1, s1);
                b.h2r = pk.h2r; b.h2i = pk.h2i; rotate (b.h2r, b.h2i, c2, s2);
            }
            const float m = mag[(size_t) k];
            sub1Out += m * b.wSub * b.h1r;
            sub2Out += m * b.wSub * b.h2r;
            upOut   += m * (ur[(size_t) k] * ur[(size_t) k] - ui[(size_t) k] * ui[(size_t) k]);
        }
    }

    /** Нормування на гітароподібних тонах: кожен голос на 100 % звучить так само голосно, як вхід. */
    void calibrate()
    {
        std::array<double, 6> e {};
        double inE = 0.0;
        for (double f : { 82.4, 110.0, 196.0 })
        {
            reset();
            const int n = (int) (0.6 * fs);
            for (int i = 0; i < n; ++i)
            {
                float x = 0.0f;
                for (int h = 1; h <= 10; ++h) x += 0.3f / (float) h * (float) std::sin (juce::MathConstants<double>::twoPi * f * h * i / fs);
                float v[6];
                voices (x, v[0], v[1], v[2], v[3], v[4], v[5]);
                if (i > n / 2)
                {
                    inE += (double) x * x;
                    for (int k = 0; k < 6; ++k) e[(size_t) k] += (double) v[k] * v[k];
                }
            }
        }
        for (size_t k = 0; k < 6; ++k) norm[k] = e[k] > 0.0 ? (float) std::sqrt (inE / e[k]) : 1.0f;
    }

    double fs = 48000.0;
    sn::PitchDetector pitch;
    sn::Biquad xlp1, xlp2, upBp1, upBp2, upDc, toneLp, fb1, fb2, subLp1, subLp2;
    std::array<Band, kBands> bands {};
    std::array<float, 6> norm {};
    float eA = 0, eR = 0, fA = 0, fR = 0, sA = 0, sR = 0, gA = 0, gR = 0, sm = 0, bpA = 0, bpR = 0;
    float envX = 0, envR = 0, envBp = 0, envF = 0, envS = 0, bloomEnv = 1.0f, analogGate = 0.0f;
    bool armed = false, ff1 = false, ff2 = false, fresh = true;
    int valid = 0, holdSamples = 1;
    double f0 = 0.0, bpHz = 0.0, ph0 = 0.0, ph1 = 0.0, ph2 = 0.0, wobPhase = 0.0;
    float g1 = 0, g2 = 0, gu = 0, gd = 1.0f;
    float svfIc1 = 0, svfIc2 = 0, svfG = 0.1f, svfK = 1.0f, svfComp = 1.0f;
};
} // namespace st
