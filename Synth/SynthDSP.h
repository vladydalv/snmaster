#pragma once

#include <juce_dsp/juce_dsp.h>
#include "../Common/SNCommon.h"
#include "Parameters.h"
#include "SaxModel.h"
#include "ModalModel.h"
#include "SampledSax.h"

/** SN Synth: голоси рахуються на подвоєній частоті (2×) — осцилятори, фільтр і драйв не дають аліасингу,
    сума голосів проходить один напівсмуговий дециматор. */
namespace syn
{
constexpr int kMaxVoices = 12, kMaxUni = 7, kSub = 16;

inline float frac (float x) noexcept { return x - std::floor (x); }

/** PolyBLEP: згладжування розриву пилки/меандру на 2 семпли (на 2× — аліасинг нижче -80 дБ у звуковій смузі). */
inline float blep (float t, float dt) noexcept
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

inline float fastTanh (float x) noexcept
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

struct Rng
{
    uint32_t s = 0x2545f491u;
    float next() noexcept { s = s * 1664525u + 1013904223u; return (float) (int32_t) s * (1.0f / 2147483648.0f); }
};

//==============================================================================
/** Аналогова обвідна: атака — до 1.3 (як RC-ланцюг), спад і згасання — експоненти. */
struct Adsr
{
    enum Stage { Idle, Attack, Decay, Sustain, Release };
    Stage st = Idle;
    float v = 0.0f, aC = 0.1f, dC = 0.01f, rC = 0.01f, sus = 1.0f;

    static float coef (double fs, float ms, float div) { return 1.0f - (float) std::exp (-1.0 / (std::max (0.05f, ms) / div * 0.001 * fs)); }
    void set (double fs, float aMs, float dMs, float s, float rMs)
    {
        aC = coef (fs, aMs, 1.466f); dC = coef (fs, dMs, 4.6f); rC = coef (fs, rMs, 4.6f); sus = s;
    }
    void gateOn (bool retrigger) { if (retrigger || st == Idle || st == Release) st = Attack; }
    void gateOff() { if (st != Idle) st = Release; }
    void kill() { st = Idle; v = 0.0f; }
    inline float tick() noexcept
    {
        switch (st)
        {
            case Attack:  v += (1.3f - v) * aC; if (v >= 1.0f) { v = 1.0f; st = Decay; } break;
            case Decay:   v = sus + (v - sus) * (1.0f - dC); if (std::abs (v - sus) < 1.0e-4f) { v = sus; st = Sustain; } break;
            case Sustain: v = sus; break;
            case Release: v -= v * rC; if (v < 1.0e-5f) { v = 0.0f; st = Idle; } break;
            case Idle: default: v = 0.0f; break;
        }
        return v;
    }
};

//==============================================================================
/** Фільтр голосу: ZDF-ladder (Moog, сатурація на вході кола зворотного зв'язку), MS-20 (SVF із діодним
    обмеженням у резонансі — «кричить» і не розвалюється), смуговий і ВЧ (TPT SVF). */
struct VoiceFilter
{
    float s[4] {}, s1 = 0.0f, s2 = 0.0f;
    float G = 0.1f, b = 0.9f, G2 = 0.01f, G3 = 0.001f, G4 = 0.0001f, k = 0.0f, comp = 1.0f;
    float g = 0.1f, R = 1.0f, d = 1.0f;
    int type = FLadder24;

    void reset() { for (auto& x : s) x = 0.0f; s1 = s2 = 0.0f; }

    void set (int t, float hz, float res, double fs)
    {
        type = t;
        g = (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (16.0, 0.45 * fs, (double) hz) / fs);
        if (t <= FLadder12)
        {
            G = g / (1.0f + g); b = 1.0f / (1.0f + g);
            G2 = G * G; G3 = G2 * G; G4 = G3 * G;
            k = 3.98f * res * res * (3.0f - 2.0f * res);   // крива: більше «солодкої» зони до самозбудження
            comp = 1.0f + 0.55f * k;                       // компенсація втрати низу (бас лишається потужним)
        }
        else
        {
            R = t == FMS20 ? 1.0f - 0.97f * res : 1.0f - 0.96f * res;
            d = 1.0f / (1.0f + 2.0f * R * g + g * g);
        }
    }

    inline float process (float x) noexcept
    {
        if (type <= FLadder12)
        {
            const float S = b * (G3 * s[0] + G2 * s[1] + G * s[2] + s[3]);
            float u = (x * comp - k * S) / (1.0f + k * G4);
            u = 2.0f * fastTanh (0.5f * u);
            float in = u, y2 = 0.0f, y = 0.0f;
            for (int i = 0; i < 4; ++i)
            {
                const float v = (in - s[i]) * G;
                y = v + s[i];
                s[i] = y + v;
                in = y;
                if (i == 1) y2 = y;
            }
            return type == FLadder24 ? y : y2;
        }
        const float hp = (x - (2.0f * R + g) * s1 - s2) * d;
        const float bp = g * hp + s1;
        const float lp = g * bp + s2;
        if (type == FMS20)
        {
            // Діодна пара в колі резонансу: несиметричне м'яке обмеження
            const float bc = bp > 0.0f ? fastTanh (bp * 0.9f) / 0.9f : fastTanh (bp * 1.15f) / 1.15f;
            s1 = 2.0f * bc - s1;
            s2 = 2.0f * lp - s2;
            return lp;
        }
        s1 = 2.0f * bp - s1;
        s2 = 2.0f * lp - s2;
        return type == FBand ? bp * 2.0f * R : hp;
    }
};

//==============================================================================
/** Драйв з антиаліасингом через первісну (ADAA 1-го порядку) і чистим низом (LR4-кросовер):
    низ іде повз дисторшн — бас брудний, але «тіло» лишається щільним. */
struct Drive
{
    float x1 = 0.0f, F1 = 0.0f;
    sn::Biquad lp1, lp2, hp1, hp2, tone;
    void reset() { x1 = F1 = 0.0f; lastType = -1; lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); tone.reset(); }

    static float logcosh (float x) noexcept { const float a = std::abs (x); return a + std::log1p (std::exp (-2.0f * a)) - 0.69314718f; }
    static constexpr float bias = 0.25f;
    static float f (int t, float x) noexcept
    {
        switch (t)
        {
            case DWarm: return std::tanh (x + bias) - std::tanh (bias);
            case DFold: return std::sin (x);
            default:    return juce::jlimit (-1.0f, 1.0f, x);
        }
    }
    static float F (int t, float x) noexcept
    {
        switch (t)
        {
            case DWarm: return logcosh (x + bias) - x * std::tanh (bias);
            case DFold: return -std::cos (x);
            default:    return std::abs (x) <= 1.0f ? 0.5f * x * x : std::abs (x) - 0.5f;
        }
    }
    int lastType = -1;
    inline float shape (int t, float x) noexcept
    {
        if (t != lastType) { lastType = t; F1 = F (t, x1); }   // первісна з правильної точки — без клацання на старті
        if (t == DRectify) x = x + 0.75f * std::abs (x) - 0.2f;     // октавний фузз: випрямлення + обмеження
        const float Fx = F (t, x);
        const float y = std::abs (x - x1) > 1.0e-4f ? (Fx - F1) / (x - x1) : f (t, 0.5f * (x + x1));
        x1 = x; F1 = Fx;
        return y;
    }
};

//==============================================================================
/** Осцилятор з унісоном (до 7 голосів, розстроювання як у суперсо, розкладка по стерео). */
struct Osc
{
    std::array<float, kMaxUni> ph {};
    std::array<float, kMaxUni> lastVal {};

    void resetPhases (Rng& rng, bool zero, int uni)
    {
        for (int i = 0; i < kMaxUni; ++i) ph[(size_t) i] = (zero && uni == 1) ? 0.0f : 0.5f + 0.5f * rng.next();
    }

    static inline float wave (int w, float t, float dt, float shape) noexcept
    {
        switch (w)
        {
            case WSaw:
            {
                // Shape: друга пилка зі зсувом фази — від класичної пилки до «порожнистої» октави
                const float sa = 2.0f * t - 1.0f - blep (t, dt);
                if (shape < 0.001f) return sa;
                const float t2 = frac (t + 0.5f * shape);
                return 0.5f * (sa + 2.0f * t2 - 1.0f - blep (t2, dt));
            }
            case WSquare:
            {
                const float pw = 0.5f - 0.45f * shape;
                float y = t < pw ? 1.0f : -1.0f;
                y += blep (t, dt) - blep (frac (t + 1.0f - pw), dt);
                return y + (2.0f * pw - 1.0f);                     // без постійної складової
            }
            case WTri:
            {
                const float tri = 4.0f * std::abs (t - 0.5f) - 1.0f;
                return shape < 0.001f ? tri : std::sin (tri * (1.0f + 4.0f * shape) * 1.5707963f);   // складання (wavefold)
            }
            case WSine:
            {
                const float s = std::sin (6.2831853f * t);
                return shape < 0.001f ? s : fastTanh (s * (1.0f + 6.0f * shape)) / fastTanh (1.0f + 6.0f * shape);
            }
            default: // WSuper
            {
                return 2.0f * t - 1.0f - blep (t, dt);
            }
        }
    }
};

//==============================================================================
/** Значення параметрів, прочитані раз на блок. */
struct Global
{
    double fs2 = 96000.0;
    int engine = EngAnalog;
    int wave[2] { WSaw, WSquare }, uni[2] { 1, 1 };
    float pitchOff[2] {}, level[2] { 0.8f, 0.0f }, shape[2] {}, detune[2] {}, spread[2] {};
    bool sync = false; float fm = 0.0f;
    float subLevel = 0.0f, noise = 0.0f; int subOct = 0, subWave = 0;
    int fType = FLadder24; float cutoff = 2500.0f, reso = 0.15f, fDriveGain = 1.0f, fEnv = 0.3f, fKey = 0.5f, fVel = 0.3f;
    float fA = 2, fD = 350, fS = 0.3f, fR = 300;
    float aA = 2, aD = 400, aS = 0.8f, aR = 180, ampVel = 0.4f;
    bool dOn = true; int dType = DWarm; float dAmount = 0.0f, dTone = 9000.0f, dLow = 20.0f, dMix = 1.0f;
    float mA = 1, mD = 300;
    int mSrc[4] {}, mDst[4] {}; float mAmt[4] {};
    int lfoDst[2] {}; float lfoAmt[2] {};
    float glideMs = 0.0f; int mode = ModePoly;
    // Живі контролери (на блок)
    float bendSemi = 0.0f, wheel = 0.0f, at = 0.0f;
    float lfo[2] {}, vib = 0.0f, vibDepth = 0.0f;
    SaxParams sax;
    BowlParams bowl;
    float drift = 0.1f, vibAuto = 0.0f;
};

//==============================================================================
class Voice
{
public:
    int note = -1;
    float vel = 0.0f;
    bool held = false;
    uint64_t age = 0;

    void prepare (double fs2) { fs = fs2; sax.prepare (fs2); ssax.prepare (fs2); modal.prepare (fs2); driftRng.s ^= (uint32_t) reinterpret_cast<uintptr_t> (this); noiseRng.s += (uint32_t) (reinterpret_cast<uintptr_t> (this) >> 4); reset(); }
    void reset()
    {
        amp.kill(); fenv.kill(); menv.kill();
        for (auto& f : flt) f.reset();
        for (auto& d : drv) d.reset();
        sax.reset(); ssax.reset(); modal.reset();
        note = -1; held = false;
    }
    bool active() const { return amp.st != Adsr::Idle; }
    float level() const { return amp.v; }

    /** glideFrom — звідки ковзати (нота, < 0 — без глайду); retrigger — перезапуск обвідних; legato — зв'язна нота. */
    void start (int n, float velocity, float glideFrom, bool retrigger, bool legato, const Global& g, Rng& rngIn)
    {
        const bool fresh = ! active() || amp.v < 0.01f;
        note = n; vel = velocity; held = true;
        target = (float) n;
        pitch = (g.glideMs > 0.5f && glideFrom >= 0.0f) ? glideFrom : target;
        random = rngIn.next();
        if (fresh)
        {
            oscs[0].resetPhases (rngIn, true, g.uni[0]);
            oscs[1].resetPhases (rngIn, true, g.uni[1]);
            subPh = 0.0f;
            for (auto& f : flt) f.reset();
            for (auto& d : drv) d.reset();
            amp.v = fenv.v = menv.v = 0.0f;
            if (g.engine == EngSax) sax.reset();
        }
        if (g.engine == EngSax)
        {
            if (SaxLibrary::get().ready()) { if (fresh) ssax.reset(); ssax.noteOn (n, velocity, legato && ! fresh, g.sax, g.glideMs); }
            else sax.noteOn ((float) n, velocity, legato && ! fresh, g.sax);
        }
        if (g.engine == EngBowls && retrigger) { if (fresh) modal.reset(); modal.noteOn (velocity, g.bowl); }
        if (retrigger) { noteTime = 0.0f; driftStatic = rngIn.next(); }
        amp.gateOn (retrigger); fenv.gateOn (retrigger); menv.gateOn (retrigger);
    }

    float currentPitch() const { return pitch; }

    void release (const Global& g)
    {
        held = false;
        amp.gateOff(); fenv.gateOff(); menv.gateOff();
        if (g.engine == EngSax) { sax.noteOff(); ssax.noteOff (note, g.sax); }
        if (g.engine == EngBowls) modal.noteOff();
    }

    void glideTo (int n) { note = n; target = (float) n; }

    /** Рахує len семплів (на 2×) і додає до L/R. */
    void render (float* L, float* R, int len, const Global& g)
    {
        // Обвідні (параметри — раз на підблок)
        amp.set (fs, g.aA, g.aD, g.aS, g.aR);
        fenv.set (fs, g.fA, g.fD, g.fS, g.fR);
        menv.set (fs, g.mA, g.mD, 0.0f, g.mD);

        // Глайд
        if (g.glideMs > 0.5f) { const float c = 1.0f - std::exp (-(float) len / (0.001f * g.glideMs / 3.0f * (float) fs)); pitch += (target - pitch) * c; if (std::abs (target - pitch) < 1.0e-3f) pitch = target; }
        else pitch = target;

        // Мод-матриця
        std::array<float, kNumDst> m {};
        for (int k = 0; k < 4; ++k)
        {
            if (g.mSrc[k] == SNone || g.mDst[k] == DNone || g.mAmt[k] == 0.0f) continue;
            float sv = 0.0f;
            switch (g.mSrc[k])
            {
                case SLfo1: sv = g.lfo[0]; break;
                case SLfo2: sv = g.lfo[1]; break;
                case SModEnv: sv = menv.v; break;
                case SVel: sv = vel; break;
                case SWheel: sv = g.wheel; break;
                case SAT: sv = g.at; break;
                case SKey: sv = ((float) note - 60.0f) / 24.0f; break;
                case SRandom: sv = random; break;
                default: break;
            }
            m[(size_t) g.mDst[k]] += sv * g.mAmt[k];
        }
        for (int k = 0; k < 2; ++k)   // пряма ціль кожного LFO (без матриці)
            if (g.lfoDst[k] != DNone && g.lfoAmt[k] != 0.0f) m[(size_t) g.lfoDst[k]] += g.lfo[k] * g.lfoAmt[k];

        // Дрейф (повільне «дихання» висоти й тембру, як у живого аналога) і автовібрато після атаки
        noteTime += (float) len / (float) fs;
        if (g.drift > 0.0f)
        {
            const float fl = (float) len / (float) fs;
            if (driftRng.next() * 0.5f + 0.5f < fl / 0.9f) { driftTp = driftRng.next(); driftTc = driftRng.next(); }
            const float k = 1.0f - std::exp (-fl / 0.7f);
            driftP += (driftTp - driftP) * k; driftC += (driftTc - driftC) * k;
        }
        const float driftSemi = g.drift * (0.12f * driftP + 0.04f * driftStatic);
        const float autoVib = g.vibAuto * 0.35f * juce::jlimit (0.0f, 1.0f, (noteTime - 0.25f) / 0.6f);
        const float basePitch = pitch + g.bendSemi + g.vib * (g.vibDepth + autoVib) + 12.0f * m[DPitch] + driftSemi;
        const float velAmp = 1.0f - g.ampVel * (1.0f - vel * vel);
        const float ampMod = juce::jlimit (0.0f, 2.0f, 1.0f + m[DAmp]);
        const float pan = juce::jlimit (-1.0f, 1.0f, m[DPan]);
        const float panL = std::sqrt (0.5f * (1.0f - pan)) * 1.41421f, panR = std::sqrt (0.5f * (1.0f + pan)) * 1.41421f;

        // Фільтр: частота з обвідною, клавіатурою, швидкістю і модуляцією
        float cutSemi = 12.0f * std::log2 (g.cutoff / 440.0f) + 72.0f * g.fEnv * fenv.v + g.fKey * ((float) note - 60.0f)
                        + 30.0f * g.fVel * (vel - 0.8f) + 72.0f * m[DCutoff] + 4.0f * g.drift * driftC;
        const float resoNow = juce::jlimit (0.0f, 1.0f, g.reso + m[DReso]);
        const float driveNow = juce::jlimit (0.0f, 1.0f, g.dAmount + m[DDrive]);

        float out[2][kSub];
        const bool stereo = g.engine == EngBowls || (g.engine == EngAnalog && (g.uni[0] > 1 || g.uni[1] > 1 || g.wave[0] == WSuper || g.wave[1] == WSuper));
        if (g.engine == EngBowls) modal.render (out[0], out[1], len, basePitch, g.bowl);
        else if (g.engine == EngSax)
        {
            SaxParams sp = g.sax;
            sp.breath = juce::jlimit (0.0f, 1.0f, sp.breath + m[DBreath]);
            sp.growl = juce::jlimit (0.0f, 1.0f, sp.growl + m[DGrowl]);
            if (SaxLibrary::get().ready()) ssax.render (out[0], len, basePitch, sp);
            else sax.render (out[0], len, basePitch, sp);
            std::copy_n (out[0], len, out[1]);
        }
        else renderOscs (out, len, g, basePitch, m, stereo);

        // Фільтр + драйв + підсилювач, посемплово
        const int nCh = stereo ? 2 : 1;
        float fenvStart = fenv.v;
        const float keyCut = cutSemi - 72.0f * g.fEnv * fenvStart;
        float envBuf[kSub], ampBuf[kSub];
        for (int i = 0; i < len; ++i) { envBuf[i] = fenv.tick(); ampBuf[i] = amp.tick(); menv.tick(); }

        const bool driveOn = g.dOn && driveNow > 0.001f;
        const float preDb = driveNow * (g.dType == DFuzz ? 48.0f : g.dType == DFold ? 30.0f : 36.0f);
        const float pre = sn::dbToGain (preDb);
        const float makeup = 1.0f / std::sqrt (1.0f + 8.0f * (1.0f - 1.0f / pre)) * (g.dType == DFold ? 1.2f : 1.0f);
        const bool split = driveOn && g.dLow >= 25.0f;
        if (driveOn && (std::abs (g.dLow - lastLow) > 0.5f || std::abs (g.dTone - lastTone) > 1.0f))
        {
            for (auto& d : drv)
            {
                d.lp1.setLowPass (fs, g.dLow, 0.7071); d.lp2.copyCoeffs (d.lp1);
                d.hp1.setHighPass (fs, g.dLow, 0.7071); d.hp2.copyCoeffs (d.hp1);
                d.tone.setLowPass (fs, g.dTone, 0.7071);
            }
            lastLow = g.dLow; lastTone = g.dTone;
        }

        for (int i0 = 0; i0 < len; i0 += 4)
        {
            const int l4 = std::min (4, len - i0);
            const float cs = keyCut + 72.0f * g.fEnv * envBuf[i0];
            const float hz = 440.0f * std::exp2 (cs / 12.0f);
            for (int c = 0; c < nCh; ++c) flt[(size_t) c].set (g.fType, hz, resoNow, fs);
            for (int c = 0; c < nCh; ++c)
            {
                auto& f = flt[(size_t) c];
                auto& d = drv[(size_t) c];
                for (int i = i0; i < i0 + l4; ++i)
                {
                    float y = f.process (out[c][i] * g.fDriveGain) / std::sqrt (g.fDriveGain);
                    if (driveOn)
                    {
                        float lo = 0.0f, hi = y;
                        if (split) { lo = d.lp2.process (d.lp1.process (y)); hi = d.hp2.process (d.hp1.process (y)); }
                        const float wet = d.tone.process (d.shape (g.dType, hi * pre) * makeup);
                        y = lo + hi * (1.0f - g.dMix) + wet * g.dMix;
                    }
                    out[c][i] = y * ampBuf[i];
                }
            }
        }

        const float gain = 1.4f * velAmp * ampMod;
        if (! stereo) for (int i = 0; i < len; ++i) { L[i] += out[0][i] * gain * panL; R[i] += out[0][i] * gain * panR; }
        else for (int i = 0; i < len; ++i) { L[i] += out[0][i] * gain * panL; R[i] += out[1][i] * gain * panR; }
        juce::ignoreUnused (cutSemi);
    }

private:
    void renderOscs (float (&out)[2][kSub], int len, const Global& g, float basePitch, const std::array<float, kNumDst>& m, bool stereo)
    {
        for (int c = 0; c < 2; ++c) std::fill_n (out[c], len, 0.0f);
        const float detMod = m[DDetune];
        float inc[2], shapeNow[2];
        for (int o = 0; o < 2; ++o)
        {
            const float semi = basePitch + g.pitchOff[o] + 12.0f * (o == 0 ? m[DPitchA] : m[DPitchB]);
            inc[o] = (float) (440.0 * std::exp2 ((semi - 69.0f) / 12.0f) / fs);
            shapeNow[o] = juce::jlimit (0.0f, 1.0f, g.shape[o] + (o == 0 ? m[DShapeA] : m[DShapeB]));
        }
        const float fmIdx = juce::jlimit (0.0f, 1.0f, g.fm + m[DFm]) * 1.5f;
        const float noiseLv = juce::jlimit (0.0f, 1.0f, g.noise + m[DNoise]) * 0.7f;

        // Розкладка унісону: зсув (центи), рівні й панорама кожного голосу
        struct Uni { int n; float incMul[kMaxUni], gL[kMaxUni], gR[kMaxUni]; };
        Uni u[2];
        for (int o = 0; o < 2; ++o)
        {
            const bool sup = g.wave[o] == WSuper;
            const int n = sup ? 7 : g.uni[o];
            u[o].n = n;
            const float det = juce::jlimit (0.0f, 1.0f, g.detune[o] + detMod);
            const float maxCents = sup ? 6.0f + 60.0f * det * det + 20.0f * det : 2.0f + 45.0f * det * det + 10.0f * det;
            const float norm = 1.0f / std::sqrt ((float) n);
            for (int i = 0; i < n; ++i)
            {
                const float pos = n == 1 ? 0.0f : -1.0f + 2.0f * (float) i / (float) (n - 1);
                // Суперсо: нерівномірні відступи (як у JP-8000) — без «биття» в такт
                static constexpr float jp[7] { -1.0f, -0.62f, -0.28f, 0.0f, 0.3f, 0.65f, 0.97f };
                const float p = sup ? jp[i] : pos;
                u[o].incMul[i] = std::exp2 (p * maxCents / 1200.0f);
                float lv = norm;
                if (sup) lv = (i == 3 ? 1.0f - 0.5f * shapeNow[o] : 0.25f + 0.75f * shapeNow[o]) * 0.55f;
                const float pan = stereo ? p * g.spread[o] : 0.0f;
                u[o].gL[i] = lv * std::sqrt (0.5f * (1.0f - pan)) * 1.41421f;
                u[o].gR[i] = lv * std::sqrt (0.5f * (1.0f + pan)) * 1.41421f;
            }
        }

        const float subInc = inc[0] * (g.subOct == 0 ? 0.5f : 0.25f);
        const float subGain = g.subLevel * 0.9f;
        const float lvA = g.level[0], lvB = g.level[1];
        const float shA = g.wave[0] == WSuper ? 0.0f : shapeNow[0], shB = g.wave[1] == WSuper ? 0.0f : shapeNow[1];
        auto& A = oscs[0];
        auto& B = oscs[1];

        for (int i = 0; i < len; ++i)
        {
            // Осцилятор B (модулятор FM, підлеглий при синхронізації)
            float bL = 0.0f, bR = 0.0f, bMono = 0.0f;
            if (lvB > 0.0f || fmIdx > 0.0f)
                for (int k = 0; k < u[1].n; ++k)
                {
                    const float dt = inc[1] * u[1].incMul[k];
                    const float v = Osc::wave (g.wave[1], B.ph[(size_t) k], dt, shB);
                    bL += v * u[1].gL[k]; bR += v * u[1].gR[k];
                    if (k == 0) bMono = v;
                    B.ph[(size_t) k] += dt; if (B.ph[(size_t) k] >= 1.0f) B.ph[(size_t) k] -= 1.0f;
                }
            // Осцилятор A
            float aL = 0.0f, aR = 0.0f;
            bool wrapped = false; float wrapFrac = 0.0f;
            for (int k = 0; k < u[0].n; ++k)
            {
                const float dt = inc[0] * u[0].incMul[k];
                const float t = fmIdx > 0.0f ? frac (A.ph[(size_t) k] + fmIdx * bMono * 0.5f) : A.ph[(size_t) k];
                const float v = Osc::wave (g.wave[0], t, dt, shA);
                aL += v * u[0].gL[k]; aR += v * u[0].gR[k];
                A.ph[(size_t) k] += dt;
                if (A.ph[(size_t) k] >= 1.0f) { A.ph[(size_t) k] -= 1.0f; if (k == 0) { wrapped = true; wrapFrac = A.ph[0] / dt; } }
            }
            // Жорстка синхронізація B від A
            if (g.sync && wrapped)
                for (int k = 0; k < u[1].n; ++k) B.ph[(size_t) k] = wrapFrac * inc[1] * u[1].incMul[k];

            // Суб і шум
            float sub = 0.0f;
            if (subGain > 0.0f)
            {
                sub = g.subWave == 0 ? std::sin (6.2831853f * subPh)
                                     : (subPh < 0.5f ? 1.0f : -1.0f) + blep (subPh, subInc) - blep (frac (subPh + 0.5f), subInc);
                subPh += subInc; if (subPh >= 1.0f) subPh -= 1.0f;
                sub *= subGain;
            }
            const float nz = noiseLv > 0.0f ? noiseRng.next() * noiseLv : 0.0f;

            out[0][i] = lvA * aL + lvB * bL + sub + nz;
            out[1][i] = lvA * aR + lvB * bR + sub + nz;
        }
    }

    double fs = 96000.0;
    Adsr amp, fenv, menv;
    std::array<Osc, 2> oscs;
    std::array<VoiceFilter, 2> flt;
    std::array<Drive, 2> drv;
    SaxModel sax;
    SampledSax ssax;
    ModalModel modal;
    Rng noiseRng, driftRng, rng;
    float noteTime = 0.0f, driftP = 0.0f, driftC = 0.0f, driftTp = 0.0f, driftTc = 0.0f, driftStatic = 0.0f;
    float pitch = 60.0f, target = 60.0f, random = 0.0f, subPh = 0.0f, lastLow = -1.0f, lastTone = -1.0f;
};

//==============================================================================
/** Напівсмуговий дециматор 2× (FIR 63 відводи, вікно Кайзера; придушення > 90 дБ вище 26 кГц при 48 кГц). */
class Decimator
{
public:
    Decimator()
    {
        constexpr int N = kTaps;
        const double beta = 9.5, fc = 0.5 * (22000.0 + 26000.0) / 96000.0;   // відносно 2× частоти
        auto bessel = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
        double sum = 0.0;
        for (int i = 0; i < N; ++i)
        {
            const double m = i - (N - 1) / 2.0;
            const double sinc = m == 0.0 ? 2.0 * fc : std::sin (2.0 * juce::MathConstants<double>::pi * fc * m) / (juce::MathConstants<double>::pi * m);
            const double r = 2.0 * i / (N - 1) - 1.0;
            h[(size_t) i] = (float) (sinc * bessel (beta * std::sqrt (1.0 - r * r)) / bessel (beta));
            sum += h[(size_t) i];
        }
        for (auto& x : h) x = (float) (x / sum);
        reset();
    }
    void reset() { for (auto& b : buf) b.fill (0.0f); pos = 0; }
    /** in: 2n семплів на канал → out: n. */
    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            for (int j = 0; j < 2; ++j)
            {
                buf[0][(size_t) pos] = buf[0][(size_t) (pos + kTaps)] = inL[2 * i + j];
                buf[1][(size_t) pos] = buf[1][(size_t) (pos + kTaps)] = inR[2 * i + j];
                pos = (pos + 1) % kTaps;
            }
            float l = 0.0f, r = 0.0f;
            const float* bl = buf[0].data() + pos;
            const float* br = buf[1].data() + pos;
            for (int k = 0; k < kTaps; ++k) { l += h[(size_t) k] * bl[k]; r += h[(size_t) k] * br[k]; }
            outL[i] = l; outR[i] = r;
        }
    }

private:
    static constexpr int kTaps = 63;
    std::array<float, kTaps> h {};
    std::array<std::array<float, 2 * kTaps>, 2> buf {};
    int pos = 0;
};

//==============================================================================
/** LFO (вільний або прив'язаний до темпу/позиції хоста). */
struct Lfo
{
    float ph = 0.0f, sh = 0.0f, shSm = 0.0f, out = 0.0f;
    Rng rng;
    float value (int shape, float p) noexcept
    {
        switch (shape)
        {
            case LSine: return std::sin (6.2831853f * p);
            case LTri: return 1.0f - 4.0f * std::abs (frac (p + 0.25f) - 0.5f);
            case LSaw: return 1.0f - 2.0f * p;
            case LSquare: return p < 0.5f ? 1.0f : -1.0f;
            default: return sh;
        }
    }
    void advance (int shape, float inc, float smoothK)
    {
        const float prev = ph;
        ph += inc; if (ph >= 1.0f) ph -= std::floor (ph);
        if (ph < prev) sh = rng.next();
        if (shape == LSH) { shSm += (sh - shSm) * smoothK; out = shSm; }
        else out = value (shape, ph);
    }
    void sync (int shape, float phase, float smoothK)
    {
        const float prev = ph;
        ph = frac (phase);
        if (ph < prev - 0.5f) sh = rng.next();
        if (shape == LSH) { shSm += (sh - shSm) * smoothK; out = shSm; }
        else out = value (shape, ph);
    }
};

} // namespace syn
