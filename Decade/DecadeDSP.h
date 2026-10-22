#pragma once

#include <juce_dsp/juce_dsp.h>
#include "DecadeModel.h"
#include "RoomEngine.h"
#include "FormatEngine.h"
#include "../Common/SNCommon.h"
#include "../Common/Saturation.h"
#include "../Master/MasterDSP.h"
#include "../Tone/ToneDSP.h"

/** Модулі «стійки» SN Decade. Кожен приймає вже обчислені налаштування (епоха + поправки + Authenticity). */
namespace dec
{
/** Що отримує стійка на кожен блок. */
struct Resolved
{
    float lowDb = 0, lowMidDb = 0, presDb = 0, topDb = 0;
    float drive = 0; int grit = Tape;
    float hpHz = 20, lpHz = 20000, reso = 0;
    float ratio = 1, thrDb = 0, attackMs = 10, releaseMs = 120, compMix = 0;
    float hitAttack = 0, hitSustain = 0;
    Space space { 0.6f, 0.5f, 0.5f, 8000, 3, 0, 0.6f };
    float roomWet = 0;          // лінійний рівень кімнати
    int roomType = Live;
    float width = 1;
};

enum Module : int { ModTone = 0, ModGrit, ModFilter, ModSquash, ModHit, ModRoom, kNumModules };
inline const char* moduleName (int m) { static const char* n[] { "TONE", "GRIT", "FILTER", "SQUASH", "HIT", "ROOM" }; return n[juce::jlimit (0, 5, m)]; }

//==============================================================================
class ToneMod
{
public:
    void prepare (double sampleRate) { fs = sampleRate; for (auto& c : f) for (auto& b : c) b.reset(); last = { -99, -99, -99, -99 }; }
    void process (juce::AudioBuffer<float>& buf, int numCh, const Resolved& r)
    {
        const std::array<float, 4> now { r.lowDb, r.lowMidDb, r.presDb, r.topDb };
        if (now != last)
        {
            sn::Biquad a, b, c, d;
            a.setLowShelf (fs, 80.0, 0.707, r.lowDb);
            b.setPeak (fs, 400.0, 0.9, r.lowMidDb);
            c.setPeak (fs, 4000.0, 0.9, r.presDb);
            d.setHighShelf (fs, 10000.0, 0.707, r.topDb);
            for (auto& ch : f) { ch[0].copyCoeffs (a); ch[1].copyCoeffs (b); ch[2].copyCoeffs (c); ch[3].copyCoeffs (d); }
            last = now;
        }
        for (int ch = 0; ch < numCh; ++ch)
        {
            float* x = buf.getWritePointer (ch);
            for (auto& bq : f[(size_t) ch])
                for (int i = 0; i < buf.getNumSamples(); ++i) x[i] = bq.process (x[i]);
        }
    }

private:
    double fs = 48000.0;
    std::array<std::array<sn::Biquad, 4>, 2> f;
    std::array<float, 4> last {};
};

//==============================================================================
/** Grit: плівка, лампа, «семплер» (12 біт, ~26 кГц) або жорсткий кліп. Нелінійності — на 4x. */
class GritMod
{
public:
    void prepare (double sampleRate, int numCh, int maxBlock)
    {
        fs = sampleRate;
        using OS = juce::dsp::Oversampling<float>;
        os = std::make_unique<OS> ((size_t) numCh, 2, OS::filterHalfBandFIREquiripple, true, true);
        os->initProcessing ((size_t) maxBlock);
        {
            const double D = (double) latency() - os->getLatencyInSamples();     // 1…2 семпли
            thiranA = (float) ((1.0 - D) / (1.0 + D));
            thiranZ = { 0.0f, 0.0f };
        }
        tape.prepare (fs * 4.0, numCh); tube.prepare (fs * 4.0, numCh);
        oldBuf.setSize (numCh, maxBlock);
        cur = -1; prev = -1; fade = 0;
        holdPhase = 0.0; for (auto& h : hold) h = 0.0f;
    }

    /** Ціла латентність: дробову частину оверсемплінгу доводимо all-pass фільтром Тірана (щоб Mix не давав гребінки). */
    int latency() const { return os != nullptr ? (int) std::ceil (os->getLatencyInSamples()) + 1 : 0; }

    void process (juce::AudioBuffer<float>& buf, int numCh, const Resolved& r)
    {
        const int n = buf.getNumSamples();
        const int want = r.drive < 0.001f ? kBypass : r.grit;    // без драйву — чиста затримка (точно, без фільтрів оверсемплінгу)
        if (want != cur)
        {
            prev = cur; cur = want;
            fade = prev >= 0 ? (int) (0.03 * fs) : 0;
        }
        if (fade > 0 && prev >= 0)
        {
            // Плавна зміна характеру: старий і новий паралельно ~30 мс
            for (int c = 0; c < numCh; ++c) oldBuf.copyFrom (c, 0, buf, c, 0, n);
            run (oldBuf, numCh, n, prev, r.drive);
            run (buf, numCh, n, cur, r.drive);
            const int total = (int) (0.03 * fs);
            for (int i = 0; i < n; ++i)
            {
                const float t = fade > 0 ? (float) fade / (float) total : 0.0f;
                for (int c = 0; c < numCh; ++c) buf.setSample (c, i, (1.0f - t) * buf.getSample (c, i) + t * oldBuf.getSample (c, i));
                if (fade > 0) --fade;
            }
        }
        else run (buf, numCh, n, cur, r.drive);
        level = r.drive;
    }

    float level = 0.0f;

private:
    static constexpr int kBypass = 100;

    void run (juce::AudioBuffer<float>& buf, int numCh, int n, int ch, float drive)
    {
        if (ch == kBypass) { delayForLatency (buf, numCh, n, 1); return; }
        const float pct = juce::jlimit (0.0f, 100.0f, drive * 100.0f);
        if (ch == Sampler)
        {
            // Семплер 80-х: менша частота (утримання) і 12 біт — це і є «зернистість», без оверсемплінгу
            const double step = 26040.0 / fs;
            const float bits = juce::jmap (drive, 0.0f, 1.0f, 16.0f, 9.0f), q = std::exp2 (bits - 1.0f);
            for (int i = 0; i < n; ++i)
            {
                holdPhase += step;
                const bool take = holdPhase >= 1.0;
                if (take) holdPhase -= 1.0;
                for (int c = 0; c < numCh && c < 2; ++c)
                {
                    if (take) hold[(size_t) c] = std::round (buf.getSample (c, i) * q) / q;
                    buf.setSample (c, i, (1.0f - drive) * buf.getSample (c, i) + drive * hold[(size_t) c]);
                }
            }
            // Затримка як в оверсемплінгу, щоб латентність не залежала від характеру
            delayForLatency (buf, numCh, n, 0);
            return;
        }
        juce::dsp::AudioBlock<float> block (buf.getArrayOfWritePointers(), (size_t) numCh, (size_t) n);
        auto up = os->processSamplesUp (block);
        if (pct > 0.1f)
        {
            if (ch == Tape) tape.process (up, pct, 1.0f, 1.0f);
            else if (ch == Tube) tube.process (up, pct, 50.0f, 1.0f);
            else
            {
                const float k = 1.0f + 6.0f * drive, norm = 1.0f / std::tanh (k * 0.25f) * 0.25f;
                for (size_t c = 0; c < up.getNumChannels(); ++c)
                {
                    auto* d = up.getChannelPointer (c);
                    for (size_t i = 0; i < up.getNumSamples(); ++i) d[i] = juce::jlimit (-1.0f, 1.0f, std::tanh (k * d[i]) * norm);
                }
            }
        }
        os->processSamplesDown (block);
        for (int c = 0; c < numCh && c < 2; ++c)
        {
            auto* d = buf.getWritePointer (c);
            float& z = thiranZ[(size_t) c];
            for (int i = 0; i < n; ++i)
            {
                const float x = d[i];
                const float y = thiranA * x + z;
                z = x - thiranA * y;
                d[i] = y;
            }
        }
    }

    void delayForLatency (juce::AudioBuffer<float>& buf, int numCh, int n, int which)
    {
        const int L = latency();
        if (L <= 0) return;
        auto& line = lines[(size_t) which];
        int& lpos = lposArr[(size_t) which];
        if ((int) line.size() != numCh || (int) line[0].size() != L) { line.assign ((size_t) numCh, std::vector<float> ((size_t) L, 0.0f)); lpos = 0; }
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < numCh; ++c)
            {
                auto& l = line[(size_t) c];
                const float x = buf.getSample (c, i);
                buf.setSample (c, i, l[(size_t) lpos]);   // те, що записали L семплів тому
                l[(size_t) lpos] = x;
            }
            lpos = (lpos + 1) % L;
        }
    }

    double fs = 48000.0, holdPhase = 0.0;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    sn::TapeStage tape;
    sn::TubeStage tube;
    juce::AudioBuffer<float> oldBuf;
    int cur = -1, prev = -1, fade = 0;
    std::array<int, 2> lposArr {};
    std::array<float, 2> hold {}, thiranZ {};
    float thiranA = 0.0f;
    std::array<std::vector<std::vector<float>>, 2> lines;
};

//==============================================================================
class FilterMod
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& c : st) c = {};
        hpS = lpS = -1.0f; qS = 0.707f;
    }

    void process (juce::AudioBuffer<float>& buf, int numCh, const Resolved& r)
    {
        // Цілі: частоти в log-домені, згладжені (~25 мс) — без стрибків і клацань при обертанні ручки
        const float hpT = std::log (juce::jlimit (20.0f, 2000.0f, r.hpHz));
        const float lpT = std::log (juce::jlimit (1000.0f, 20000.0f, r.lpHz));
        const float reso = juce::jlimit (0.0f, 1.0f, r.reso);
        if (hpS < 0.0f) { hpS = hpT; lpS = lpT; resoS = reso; }
        const float k = 1.0f - std::exp (-1.0f / (0.025f * (float) fs));
        const int n = buf.getNumSamples();
        float* x[2] { buf.getWritePointer (0), numCh > 1 ? buf.getWritePointer (1) : nullptr };

        for (int i0 = 0; i0 < n; i0 += kSub)
        {
            const int len = std::min (kSub, n - i0);
            const float kk = 1.0f - std::pow (1.0f - k, (float) len);
            hpS += (hpT - hpS) * kk; lpS += (lpT - lpS) * kk; resoS += (reso - resoS) * kk;
            if (std::abs (hpT - hpS) < 1.0e-4f) hpS = hpT;
            if (std::abs (lpT - lpS) < 1.0e-4f) lpS = lpT;
            if (std::abs (reso - resoS) < 1.0e-4f) resoS = reso;

            // Нейтральні межі: на 20 Гц / 20 кГц фільтр плавно виходить із сигналу (точний нуль, без перемикань)
            const float wHp = smooth01 ((hpS - std::log (20.0f)) / (std::log (30.0f) - std::log (20.0f)));
            const float wLp = smooth01 ((std::log (20000.0f) - lpS) / (std::log (20000.0f) - std::log (14000.0f)));
            if (wHp <= 0.0f && wLp <= 0.0f) { for (auto& c : st) c = {}; continue; }

            // Резонанс — головним чином на зрізі верху (як у вінтажних фільтрах), трохи на низу
            const Coef h = coef (std::exp (hpS), 0.707f + 0.6f * resoS);
            const Coef l = coef (std::exp (lpS), 0.707f + 3.3f * resoS * resoS + 0.6f * resoS);
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& s = st[(size_t) ch];
                float* y = x[ch] + i0;
                for (int i = 0; i < len; ++i)
                {
                    float v = y[i];
                    if (wHp > 0.0f) { const float o = svf (s.h, h, v, true); v += wHp * (o - v); }
                    if (wLp > 0.0f) { const float o = svf (s.l, l, v, false); v += wLp * (o - v); }
                    y[i] = v;
                }
            }
        }
    }

private:
    static constexpr int kSub = 16;
    struct Coef { float g, k, a1, a2, a3; };
    struct State { float ic1 = 0, ic2 = 0; };
    struct Ch { State h, l; };

    static float smooth01 (float t) { t = juce::jlimit (0.0f, 1.0f, t); return t * t * (3.0f - 2.0f * t); }

    Coef coef (float hz, float q) const
    {
        const float g = (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (5.0, 0.49 * fs, (double) hz) / fs);
        const float kq = 1.0f / q;
        const float a1 = 1.0f / (1.0f + g * (g + kq));
        return { g, kq, a1, g * a1, g * g * a1 };
    }

    /** TPT state-variable filter (Zavalishin): стабільний при модуляції частоти щосемпла. */
    static float svf (State& s, const Coef& c, float v0, bool high)
    {
        const float v3 = v0 - s.ic2;
        const float v1 = c.a1 * s.ic1 + c.a2 * v3;
        const float v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
        s.ic1 = 2.0f * v1 - s.ic1;
        s.ic2 = 2.0f * v2 - s.ic2;
        return high ? v0 - c.k * v1 - v2 : v2;
    }

    double fs = 48000.0;
    std::array<Ch, 2> st;
    float hpS = -1.0f, lpS = -1.0f, resoS = 0.0f, qS = 0.707f;
};

//==============================================================================
/** Squash: компресор шини; поріг — відносно рівня програми, тож працює однаково на тихих і гучних доріжках. */
class SquashMod
{
public:
    void prepare (double sampleRate) { fs = sampleRate; comp.prepare (fs, 2); programDb = -24.0f; avgGr = 0.0f; }
    void process (juce::AudioBuffer<float>& buf, int numCh, const Resolved& r)
    {
        const int n = buf.getNumSamples();
        // Рівень програми (~3 с)
        double e = 0.0;
        for (int c = 0; c < numCh; ++c) { const float rms = buf.getRMSLevel (c, 0, n); e += rms * rms; }
        const float db = sn::gainToDb (std::sqrt ((float) (e / std::max (1, numCh))) + 1.0e-9f);
        if (db > -70.0f) programDb += (db - programDb) * std::min (1.0f, (float) n / (float) (3.0 * fs));
        if (r.ratio <= 1.01f || r.compMix <= 0.001f) { gr = 0.0f; return; }
        const sn::Compressor::Settings s { juce::jlimit (-60.0f, 0.0f, programDb + 6.0f - r.thrDb), r.ratio, r.attackMs, r.releaseMs,
                                           6.0f, 60.0f, 0.0f, r.compMix, false };
        float* ptrs[2] { buf.getWritePointer (0), numCh > 1 ? buf.getWritePointer (1) : buf.getWritePointer (0) };
        gr = comp.process (ptrs, numCh, n, s);
        // Повернути гучність (половину середнього стиснення) — щоб порівнювати характер, а не рівень
        avgGr += (gr * r.compMix - avgGr) * std::min (1.0f, (float) n / (float) (1.5 * fs));
        const float g = sn::dbToGain (0.6f * avgGr);
        for (int c = 0; c < numCh; ++c) buf.applyGain (c, 0, n, g);
    }

    float gr = 0.0f;

private:
    double fs = 48000.0;
    sn::Compressor comp;
    float programDb = -24.0f, avgGr = 0.0f;
};

//==============================================================================
class HitMod
{
public:
    void prepare (double sampleRate) { shaper.prepare (sampleRate); }
    void process (juce::AudioBuffer<float>& buf, int numCh, const Resolved& r)
    {
        if (std::abs (r.hitAttack) < 0.5f && std::abs (r.hitSustain) < 0.5f) return;
        float* ptrs[2] { buf.getWritePointer (0), numCh > 1 ? buf.getWritePointer (1) : nullptr };
        shaper.process (ptrs, numCh, buf.getNumSamples(), r.hitAttack, r.hitSustain);
    }

private:
    sn::TransientShaper shaper;
};

//==============================================================================
/** Кімната: див. RoomEngine (геометричні ранні відбиття + 16-лінійна мережа з трисмуговим згасанням + gated). */
class RoomMod
{
public:
    void prepare (double sampleRate) { engine.prepare (sampleRate); tailView = 0.0f; idle = 0; }

    void process (juce::AudioBuffer<float>& buf, int numCh, const Resolved& r)
    {
        const int n = buf.getNumSamples();
        if (r.roomWet < 1.0e-4f)
        {
            // Без кімнати: дати хвосту згаснути ~4 с, потім не рахувати зовсім
            if (idle > (int) (4.0 * 48000)) { tailView = 0.0f; return; }
            idle += n;
        }
        else idle = 0;
        RoomEngine::Params p;
        p.type = r.roomType;
        p.size = r.space.size; p.rtMid = r.space.decay; p.dampHz = r.space.dampHz; p.er = r.space.er;
        p.predelayMs = r.space.predelayMs; p.gate = std::max (r.space.gate, r.roomType == Gated ? 1.0f : 0.0f); p.diffusion = r.space.diffusion;
        engine.process (buf.getWritePointer (0), numCh > 1 ? buf.getWritePointer (1) : nullptr, n, p, r.roomWet);
        tailView = engine.tail;
    }

    float tailView = 0.0f;

private:
    RoomEngine engine;
    int idle = 0;
};

} // namespace dec
