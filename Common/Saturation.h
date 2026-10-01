#pragma once

#include "SNCommon.h"

/*  Лампа і плівка. Усі стадії працюють в оверсемплованому домені (крім wow/flutter).
    Підсилення відкаліброване: на номінальному рівні -18 dBFS стадія має одиничне
    підсилення, тому Drive змінює характер, а не гучність. */

namespace sn
{
//==============================================================================
/** Тріодний каскад: асиметрична характеристика (парні гармоніки), зсув робочої точки
    від рівня сигналу (sag), завал ВЧ від ефекту Міллера. Малосигнальне підсилення = 1. */
class TubeStage
{
public:
    /** refAmp — пікова амплітуда синуса, на якій стадія має одиничне підсилення
        (-18 dBFS для доріжок, вище для майстер-шини). */
    void prepare (double osRate, int numChannels, float refAmp = 0.125892541f)
    {
        fs = osRate;
        ref = refAmp;
        ch.assign ((size_t) numChannels, {});
        envAtk = (float) std::exp (-1.0 / (0.005 * fs));
        envRel = (float) std::exp (-1.0 / (0.120 * fs));
        calibrate();
        reset();
    }

    void reset()
    {
        for (auto& c : ch) { c.dc.reset(); c.miller.reset(); c.env = 0.0f; c.q = 0.0f; c.uPrev = 0.0f; c.fPrev = 0.0f; }
        lastDrive = -1.0f;
    }

    void process (juce::dsp::AudioBlock<float>& block, float drivePct, float biasPct, float mix)
    {
        if (drivePct != lastDrive)
        {
            for (auto& c : ch)
            {
                c.dc.setLowpass (fs, 8.0);
                c.miller.setLowpass (fs, 22000.0 - 70.0 * drivePct);   // 22 → 15 кГц
            }
            lastDrive = drivePct;
        }

        const Coefs cf = coefs (drivePct, biasPct);
        const float norm = lookupNorm (drivePct, biasPct);
        const float dry = 1.0f - mix;

        for (size_t c = 0; c < block.getNumChannels() && c < ch.size(); ++c)
        {
            auto& st = ch[c];
            auto* d = block.getChannelPointer (c);
            for (size_t i = 0; i < block.getNumSamples(); ++i)
            {
                const float x = d[i];
                float y = shape (st, x, cf) * norm;
                y = st.dc.highpass (y);                           // прибираємо DC від асиметрії
                y = st.miller.lowpass (y);
                d[i] = dry * x + mix * y;
            }
        }
    }

private:
    struct Channel { OnePole dc, miller; float env = 0.0f, q = 0.0f, uPrev = 0.0f, fPrev = 0.0f; };
    struct Coefs { float k, beta, qAtk, qRel; };

    Coefs coefs (float drivePct, float biasPct) const noexcept
    {
        return { 1.0f + 0.09f * drivePct, 0.45f * biasPct * 0.01f,
                 1.0f - (float) std::exp (-1.0 / (0.002 * fs)), (float) std::exp (-1.0 / (0.15 * fs)) };
    }

    static float logCosh (float u) noexcept
    {
        const float a = std::abs (u);
        return a + std::log1p (std::exp (-2.0f * a)) - 0.69314718f;
    }

    /** Один семпл тріода:
        • сітковий струм: позитивні піки вище порога заряджають розділовий конденсатор (швидко), він розряджається
          повільно і зсуває робочу точку вниз — «сідання» і асиметрія залежать від історії сигналу, як у справжньої лампи;
        • tanh з антиаліасингом ADAA-1 (менше «цифрового» бруду на високому драйві). */
    static float shape (Channel& st, float x, const Coefs& cf) noexcept
    {
        const float pre = cf.k * x + cf.beta;
        const float over = std::max (0.0f, pre - 0.8f);                        // сітка відкривається
        st.q = over > st.q ? st.q + cf.qAtk * (over - st.q) : st.q * cf.qRel;
        const float b = cf.beta - 0.5f * st.q;
        const float u = cf.k * x + b;
        const float du = u - st.uPrev;
        const float fu = logCosh (u);
        const float y = std::abs (du) < 1.0e-4f ? std::tanh (0.5f * (u + st.uPrev)) : (fu - st.fPrev) / du;
        st.uPrev = u; st.fPrev = fu;
        return y - std::tanh (cf.beta);
    }

    /** Нормування: одиничне підсилення (RMS) на синусі refAmp для кожної пари Drive/Bias — симуляцією самої стадії. */
    void calibrate()
    {
        for (int di = 0; di <= 10; ++di)
            for (int bi = 0; bi <= 10; ++bi)
            {
                const Coefs cf = coefs ((float) di * 10.0f, (float) bi * 10.0f);
                Channel st;
                const int period = (int) (fs / 500.0), total = period * 40;
                double sx = 0.0, sy = 0.0, mean = 0.0;
                std::vector<float> ys;
                ys.reserve ((size_t) period * 10);
                for (int n = 0; n < total; ++n)
                {
                    const float x = ref * (float) std::sin (juce::MathConstants<double>::twoPi * n / period);
                    const float y = shape (st, x, cf);
                    if (n >= total - period * 10) { ys.push_back (y); mean += y; sx += (double) x * x; }
                }
                mean /= (double) ys.size();
                for (auto v : ys) sy += (v - mean) * (v - mean);
                norm[(size_t) di][(size_t) bi] = (float) std::sqrt (sx / std::max (sy, 1.0e-30));
            }
    }

    float lookupNorm (float drivePct, float biasPct) const noexcept
    {
        const float dp = juce::jlimit (0.0f, 10.0f, drivePct * 0.1f), bp = juce::jlimit (0.0f, 10.0f, biasPct * 0.1f);
        const int d0 = std::min (9, (int) dp), b0 = std::min (9, (int) bp);
        const float td = dp - (float) d0, tb = bp - (float) b0;
        auto at = [this] (int d, int b) { return norm[(size_t) d][(size_t) b]; };
        const float top = at (d0, b0) + tb * (at (d0, b0 + 1) - at (d0, b0));
        const float bot = at (d0 + 1, b0) + tb * (at (d0 + 1, b0 + 1) - at (d0 + 1, b0));
        return top + td * (bot - top);
    }

    std::array<std::array<float, 11>, 11> norm {};
    float ref = 0.125892541f;
    std::vector<Channel> ch;
    double fs = 176400.0;
    float envAtk = 0.0f, envRel = 0.0f, lastDrive = -1.0f;
};

//==============================================================================
/** Магнітний гістерезис плівки за моделлю Джайлза–Атертона
    (J. Chowdhury, "Real-time Physical Modelling for Analog Tape Machines", DAFx 2019).
    Розв'язувач RK2, похідна поля — за правилом трапецій. */
class Hysteresis
{
public:
    void prepare (double osRate) { fs = osRate; T = 1.0 / fs; reset(); }
    /** Bias стрічки (оборотність c): менше = недопідмагнічена стрічка, більше спотворень і «мертва зона» на тихих. */
    void setReversibility (double cNew) noexcept { c = juce::jlimit (0.3, 0.92, cNew); }
    void reset() { M = 0.0; Hp = 0.0; dHp = 0.0; }

    /** H — напруженість поля (А/м). Повертає намагніченість, нормовану до Ms. */
    double process (double H) noexcept
    {
        double dH = 2.0 * fs * (H - Hp) - dHp;
        dH = juce::jlimit (-maxSlew, maxSlew, dH);

        const double k1 = T * dMdt (M, Hp, dHp);
        const double k2 = T * dMdt (M + 0.5 * k1, 0.5 * (H + Hp), 0.5 * (dH + dHp));
        M += k2;

        if (! std::isfinite (M)) M = 0.0;
        M = juce::jlimit (-Ms, Ms, M);
        Hp = H; dHp = dH;
        return M / Ms;
    }

    /*  Ms, a, alpha, k — феритова стрічка з праці. Оборотність c піднята з 0.17 до 0.7
        (як «width» за замовчуванням у ChowTape): це імітує лінеаризацію від ВЧ-підмагнічування,
        без якого тихі сигнали розширюються (кросовер-спотворення). */
    static constexpr double Ms = 3.5e5, a = 2.2e4, alpha = 1.6e-3, kc = 2.7e4;
    double c = 0.7;

private:
    static double langevin (double x) noexcept
    {
        return std::abs (x) < 1.0e-4 ? x / 3.0 : 1.0 / std::tanh (x) - 1.0 / x;
    }
    static double dLangevin (double x) noexcept
    {
        if (std::abs (x) < 1.0e-4) return 1.0 / 3.0;
        const double ct = 1.0 / std::tanh (x);
        return 1.0 / (x * x) - ct * ct + 1.0;
    }

    double dMdt (double m, double H, double dH) const noexcept
    {
        const double Q = (H + alpha * m) / a;
        const double Man = Ms * langevin (Q);
        const double dL = dLangevin (Q);
        const double diff = Man - m;
        const double delta = dH >= 0.0 ? 1.0 : -1.0;
        const double deltaM = (delta > 0.0) == (diff > 0.0) ? 1.0 : 0.0;

        double den1 = (1.0 - c) * delta * kc - alpha * diff;
        if (std::abs (den1) < 1.0e-6) den1 = den1 < 0.0 ? -1.0e-6 : 1.0e-6;

        const double irr = (1.0 - c) * deltaM * diff / den1;
        const double rev = c * Ms / a * dL;
        return (irr + rev) * dH / (1.0 - c * alpha * Ms / a * dL);
    }

    double fs = 176400.0, T = 1.0 / 176400.0;
    double M = 0.0, Hp = 0.0, dHp = 0.0;
    static constexpr double maxSlew = 1.0e12;
};

//==============================================================================
/** Плівковий магнітофон: гістерезис + АЧХ швидкості стрічки (горб на басу, завал ВЧ). */
class TapeStage
{
public:
    enum Speed { ips7_5 = 0, ips15, ips30 };

    void prepare (double osRate, int numChannels, float refAmp = 0.125892541f)
    {
        fs = osRate;
        ref = refAmp;
        ch.assign ((size_t) numChannels, {});
        for (auto& c : ch) c.h.prepare (fs);
        calibrate();
        lastSpeed = -1.0f;
        reset();
    }

    void reset()
    {
        for (auto& c : ch) { c.h.reset(); for (auto& f : c.eq) f.reset(); c.pre.reset(); c.post.reset(); }
    }

    /** biasPct: 50 = номінал; нижче — недопідмагнічено (брудніше), вище — чистіше й м'якше. */
    void process (juce::dsp::AudioBlock<float>& block, float drivePct, float speed, float mix, float biasPct = 50.0f)
    {
        if (std::abs (speed - lastSpeed) > 0.005f) { updateEq (speed); lastSpeed = speed; }   // швидкість може плавно ковзати
        if (std::abs (biasPct - lastBias) > 0.05f)
        {
            lastBias = biasPct;
            for (auto& c : ch) c.h.setReversibility (revFor (biasPct));
        }

        const double hGain = fieldGain (drivePct);
        const float norm = outputNorm (drivePct, biasPct);
        const float dry = 1.0f - mix;

        for (size_t c = 0; c < block.getNumChannels() && c < ch.size(); ++c)
        {
            auto& st = ch[c];
            auto* d = block.getChannelPointer (c);
            for (size_t i = 0; i < block.getNumSamples(); ++i)
            {
                const float x = d[i];
                // Пре-емфазис → гістерезис → де-емфазис (як запис/відтворення): верх насичується раніше за низ
                const float xe = st.pre.process (x);
                float y = (float) st.h.process ((double) xe * hGain) * norm;
                y = st.post.process (y);
                for (auto& f : st.eq) y = f.process (y);
                d[i] = dry * x + mix * y;
            }
        }
    }

private:
    static double revFor (float biasPct) noexcept { return 0.45 + 0.45 * juce::jlimit (0.0, 1.0, biasPct * 0.01); }   // 50 % → 0.675 ≈ було 0.7

    double fieldGain (float drivePct) const noexcept
    {
        const double d = drivePct * 0.01;
        return Hysteresis::a * (0.6 + 11.0 * d * std::sqrt (d)) * (0.125892541 / ref);   // лінійно → насичено
    }

    float outputNorm (float drivePct, float biasPct) const noexcept
    {
        const float pos = juce::jlimit (0.0f, 10.0f, drivePct * 0.1f), bpos = juce::jlimit (0.0f, 4.0f, biasPct * 0.04f);
        const int i0 = std::min (9, (int) pos), b0 = std::min (3, (int) bpos);
        const float t = pos - (float) i0, tb = bpos - (float) b0;
        auto at = [this] (int i, int b) { return normTable[(size_t) b][(size_t) i]; };
        const float lo = at (i0, b0) + t * (at (i0 + 1, b0) - at (i0, b0));
        const float hi = at (i0, b0 + 1) + t * (at (i0 + 1, b0 + 1) - at (i0, b0 + 1));
        return lo + tb * (hi - lo);
    }

    /** Таблиця нормування: одиничне підсилення на синусі -18 dBFS / 1 кГц для кожної пари Drive/Bias. */
    void calibrate()
    {
        const double amp = ref;
        const int total = (int) (0.06 * fs), measureFrom = (int) (0.03 * fs);
        for (int bi = 0; bi <= 4; ++bi)
            for (int step = 0; step <= 10; ++step)
            {
                Hysteresis h; h.prepare (fs); h.setReversibility (revFor ((float) bi * 25.0f));
                const double g = fieldGain ((float) step * 10.0f);
                double sumIn = 0.0, sumOut = 0.0;
                for (int n = 0; n < total; ++n)
                {
                    const double x = amp * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * n / fs);
                    const double y = h.process (x * g);
                    if (n >= measureFrom) { sumIn += x * x; sumOut += y * y; }
                }
                normTable[(size_t) bi][(size_t) step] = (float) std::sqrt (sumIn / std::max (sumOut, 1.0e-30));
            }
    }

    void updateEq (float speed)
    {
        struct V { float bumpHz, bumpDb, lpHz, emphDb; };
        static constexpr V v[] { { 45.0f, 2.5f, 11000.0f, 6.0f }, { 65.0f, 2.0f, 17000.0f, 4.5f }, { 110.0f, 1.5f, 24000.0f, 3.0f } };
        // 0 = 7.5, 1 = 15, 2 = 30 ips; дробові значення — проміжні (для плавного переходу)
        const float sp = juce::jlimit (0.0f, 2.0f, speed);
        const int i0 = std::min (1, (int) sp);
        const float t = sp - (float) i0;
        const auto& a = v[i0]; const auto& b = v[i0 + 1];
        const auto lerpLog = [t] (float x, float y) { return x * std::pow (y / x, t); };
        const V s { lerpLog (a.bumpHz, b.bumpHz), a.bumpDb + t * (b.bumpDb - a.bumpDb), lerpLog (a.lpHz, b.lpHz), a.emphDb + t * (b.emphDb - a.emphDb) };

        Biquad bump, dip, lp, pre, post;
        bump.setPeak (fs, s.bumpHz, 1.2, s.bumpDb);
        dip.setPeak (fs, s.bumpHz * 2.4, 1.4, -1.0);
        lp.setLowPass (fs, s.lpHz, 0.6);
        pre.setHighShelf (fs, 3000.0, 0.707, s.emphDb);
        post.setHighShelf (fs, 3000.0, 0.707, -s.emphDb);
        for (auto& c : ch)
        {
            c.eq[0].copyCoeffs (bump); c.eq[1].copyCoeffs (dip); c.eq[2].copyCoeffs (lp);
            c.pre.copyCoeffs (pre); c.post.copyCoeffs (post);
        }
    }

    struct Channel { Hysteresis h; std::array<Biquad, 3> eq; Biquad pre, post; };
    std::vector<Channel> ch;
    std::array<std::array<float, 11>, 5> normTable {};
    float lastBias = -1.0f;
    float ref = 0.125892541f;
    double fs = 176400.0;
    float lastSpeed = -1.0f;
};

//==============================================================================
/** Детонація стрічки: wow (повільна) + flutter (швидка), спільна для каналів.
    Постійна затримка, щоб компенсація латентності не змінювалась. */
class WowFlutter
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        fs = sampleRate;
        baseDelay = std::ceil (0.003 * fs);
        size = (int) baseDelay * 2 + 8;
        buf.assign ((size_t) numChannels, std::vector<float> ((size_t) size, 0.0f));
        noiseLp.setLowpass (fs, 0.8);
        reset();
    }

    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        pos = 0; phWow = phFl = 0.0; noiseLp.reset();
    }

    int getLatency() const noexcept { return (int) baseDelay; }

    void process (juce::AudioBuffer<float>& buffer, int numCh, float amount)
    {
        const double depthWow = 0.0006 * fs * amount;     // до 0.6 мс
        const double depthFl  = 0.000015 * fs * amount;   // до 15 мкс
        const double incWow = 0.55 / fs, incFl = 7.3 / fs;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float rnd = noiseLp.lowpass (rng.nextFloat() * 2.0f - 1.0f) * 6.0f;
            phWow += incWow; if (phWow >= 1.0) phWow -= 1.0;
            phFl += incFl;   if (phFl >= 1.0) phFl -= 1.0;
            const double twoPi = juce::MathConstants<double>::twoPi;
            const double mod = depthWow * (0.7 * std::sin (twoPi * phWow) + 0.3 * juce::jlimit (-1.0f, 1.0f, rnd))
                             + depthFl * std::sin (twoPi * phFl);
            const double delay = baseDelay + mod;

            for (int c = 0; c < numCh; ++c)
            {
                auto& b = buf[(size_t) c];
                b[(size_t) pos] = buffer.getSample (c, i);
                buffer.setSample (c, i, read (b, delay));
            }
            pos = (pos + 1) % size;
        }
    }

private:
    float read (const std::vector<float>& b, double delay) const noexcept
    {
        const double rp = (double) pos - delay;
        const int i1 = (int) std::floor (rp);
        const float t = (float) (rp - i1);
        auto at = [&] (int idx) { return b[(size_t) ((idx % size + size) % size)]; };
        const float y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);
        // Кубічна інтерполяція Ерміта
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    double fs = 48000.0, baseDelay = 144.0, phWow = 0.0, phFl = 0.0;
    int size = 300, pos = 0;
    std::vector<std::vector<float>> buf;
    OnePole noiseLp;
    juce::Random rng { 0x5eed };
};

//==============================================================================
/** М'яка симетрична сатурація (tanh), одиничне підсилення на піку refAmp. */
struct SoftClip
{
    static void process (juce::dsp::AudioBlock<float>& block, float drivePct, float mix, float refAmp = 0.25f)
    {
        const float k = (1.0f + 0.04f * drivePct) * 0.25f / refAmp;
        const float norm = refAmp / std::tanh (k * refAmp);
        const float dry = 1.0f - mix;
        for (size_t c = 0; c < block.getNumChannels(); ++c)
        {
            auto* d = block.getChannelPointer (c);
            for (size_t i = 0; i < block.getNumSamples(); ++i)
                d[i] = dry * d[i] + mix * std::tanh (k * d[i]) * norm;
        }
    }
};
} // namespace sn
