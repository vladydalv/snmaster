#pragma once

#include <juce_core/juce_core.h>
#include "DecadeModel.h"
#include "../Common/SNCommon.h"

/** Реверб кімнати для барабанів.
    1) Ранні відбиття — з геометрії «коробки» (метод уявних джерел, 1-й і 2-й порядок) для пари мікрофонів:
       природна стереобаза й «характер» кімнати (гараж — щільний флаттер, зал — рідкі пізні відбиття).
    2) Хвіст — мережа затримок зі зворотним зв'язком (16 ліній, матриця Адамара), у кожній лінії —
       трисмугове згасання (низ / середина / верх мають свій RT60, як у реальних приміщеннях) і повільна модуляція
       довжини (без «металу»).
    3) Gated: щільна «хмара» відбиттів із рівною обвідною і різкий зріз гейтом після удару — як у 80-х. */
namespace dec
{
class RoomEngine
{
public:
    static constexpr int N = 16;

    struct Params
    {
        int type = Live;
        float size = 1.0f;        // масштаб розмірів кімнати
        float rtMid = 1.2f;       // RT60 середини, с
        float dampHz = 7000.0f;   // визначає, наскільки швидше згасає верх
        float er = 0.6f;          // рівень ранніх відбиттів
        float predelayMs = 10.0f;
        float gate = 0.0f;        // 0…1
        float diffusion = 0.7f;
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& l : lines) l.assign ((size_t) (0.25 * fs) + 8, 0.0f);
        lineLen = (int) lines[0].size();
        preBuf.assign ((size_t) (0.4 * fs), 0.0f);
        for (auto& a : inAp) a.buf.assign ((size_t) (0.02 * fs), 0.0f);
        for (auto& ch : erAp) for (auto& a : ch) a.buf.assign ((size_t) (0.01 * fs), 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
        std::fill (preBuf.begin(), preBuf.end(), 0.0f);
        for (auto& a : inAp) { std::fill (a.buf.begin(), a.buf.end(), 0.0f); a.pos = 0; }
        for (auto& ch : erAp) for (auto& a : ch) { std::fill (a.buf.begin(), a.buf.end(), 0.0f); a.pos = 0; }
        for (auto& b : bands) b = {};
        wpos = ppos = 0;
        env = peak = 0.0f; gateG = 1.0f; hold = 0;
        inLp = { 0.0f, 0.0f }; inHp = { 0.0f, 0.0f };
        designed = {};
        designedValid = false;
        len.fill (0.0f);
        for (int k = 0; k < N; ++k)
        {
            const double ph = juce::MathConstants<double>::twoPi * k / N;
            oscC[(size_t) k] = (float) std::cos (ph); oscS[(size_t) k] = (float) std::sin (ph);
        }
    }

    /** Додає кімнату до сигналу: out = in + wet × reverb(in). */
    void process (float* L, float* R, int n, const Params& prm, float wet)
    {
        if (! designedValid || changed (prm)) design (prm);

        const float envA = std::exp (-1.0f / (0.0005f * (float) fs)), envR = std::exp (-1.0f / (0.025f * (float) fs));
        const float peakFall = std::exp (-1.0f / (0.6f * (float) fs));
        const float gOpen = std::exp (-1.0f / (0.0008f * (float) fs)), gClose = std::exp (-1.0f / (0.03f * (float) fs));
        const int holdLen = (int) (holdMs * 0.001f * (float) fs);
        const int P = (int) preBuf.size();
        double energy = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const float xl = L[i], xr = R != nullptr ? R[i] : L[i];

            // Гейт за входом (ключ — самі барабани): відкривається на ударі, тримає holdMs, різко закривається
            const float a = std::max (std::abs (xl), std::abs (xr));
            env = a > env ? a + envA * (env - a) : a + envR * (env - a);
            peak = std::max (env, peak * peakFall);
            if (env > 0.25f * peak && env > 3.0e-4f) hold = holdLen;
            const float gt = hold > 0 ? 1.0f : 0.0f;
            if (hold > 0) --hold;
            gateG = gt > gateG ? gt + gOpen * (gateG - gt) : gt + gClose * (gateG - gt);
            const float gate = 1.0f - prm.gate * (1.0f - gateG);

            // Вхід реверба: смуга (як мікрофони в кімнаті), моно з дещицею сторони
            float m = 0.5f * (xl + xr), s = 0.5f * (xl - xr);
            for (int c = 0; c < 2; ++c)
            {
                float& x = c == 0 ? m : s;
                inHp[(size_t) c] += hpA * (x - inHp[(size_t) c]);
                x -= inHp[(size_t) c];                          // зріз низу ~60 Гц (бочка не «гуде» в кімнаті)
                inLp[(size_t) c] += lpA * (x - inLp[(size_t) c]);
                x = inLp[(size_t) c];
            }

            // Передзатримка
            preBuf[(size_t) ppos] = m;
            auto preAt = [&] (int d) { return preBuf[(size_t) ((ppos - d + 2 * P) % P)]; };
            sideBuf = s;

            // Ранні відбиття (геометрія кімнати) — окремо для L і R мікрофона
            float eL = 0.0f, eR = 0.0f;
            for (int k = 0; k < numEr; ++k)
            {
                const float v = preAt (erDelay[(size_t) k]);
                eL += erGL[(size_t) k] * v;
                eR += erGR[(size_t) k] * v;
            }
            // Щільна «хмара» для gated
            for (int k = 0; k < numCloud; ++k)
            {
                const float v = preAt (cloudDelay[(size_t) k]);
                eL += cloudGL[(size_t) k] * v;
                eR += cloudGR[(size_t) k] * v;
            }
            eL = erAp[0][1].run (erAp[0][0].run (eL, 0.5f), 0.5f);
            eR = erAp[1][1].run (erAp[1][0].run (eR, 0.5f), 0.5f);

            // Дифузія входу хвоста
            float x = preAt (preLen) + 0.25f * sideBuf;
            for (auto& ap : inAp) x = ap.run (x, apG);

            // FDN: читання з модуляцією
            float y[N];
            for (int k = 0; k < N; ++k)
            {
                len[(size_t) k] += 0.0005f * (lenTarget[(size_t) k] - len[(size_t) k]);   // зміна розміру ковзає, без клацань
                // Генератор модуляції без sin(): поворот вектора (дешево, 16 ліній × кожен семпл)
                const float oc = oscC[(size_t) k], os = oscS[(size_t) k];
                oscC[(size_t) k] = oc * rotC[(size_t) k] - os * rotS[(size_t) k];
                oscS[(size_t) k] = os * rotC[(size_t) k] + oc * rotS[(size_t) k];
                const float d = len[(size_t) k] + modDepth * oscS[(size_t) k];
                y[k] = readCubic (lines[(size_t) k], d);
                // Трисмугове згасання: low < 250 Гц, high > 4 кГц, середина — решта
                auto& b = bands[(size_t) k];
                b.lo += loA * (y[k] - b.lo);
                b.hi += hiA * (y[k] - b.hi);
                const float low = b.lo, high = y[k] - b.hi, mid = y[k] - low - high;
                y[k] = gLow[(size_t) k] * low + gMid[(size_t) k] * mid + gHigh[(size_t) k] * high;
            }
            hadamard (y);
            for (int k = 0; k < N; ++k)
                lines[(size_t) k][(size_t) wpos] = y[k] + inGain[(size_t) k] * x;

            float tl = 0.0f, tr = 0.0f;
            for (int k = 0; k < N; ++k) { tl += outL[(size_t) k] * y[k]; tr += outR[(size_t) k] * y[k]; }

            const float wl = (prm.er * erLevel * eL + lateLevel * tl) * gate, wr = (prm.er * erLevel * eR + lateLevel * tr) * gate;
            energy += (double) wl * wl + (double) wr * wr;
            L[i] = xl + wet * wl;
            if (R != nullptr) R[i] = xr + wet * wr;

            wpos = (wpos + 1) % lineLen;
            ppos = (ppos + 1) % P;
        }
        tail = (float) std::sqrt (energy / std::max (1, n)) * wet;
        for (int k = 0; k < N; ++k)
        {
            const float m = 1.0f / std::sqrt (oscC[(size_t) k] * oscC[(size_t) k] + oscS[(size_t) k] * oscS[(size_t) k] + 1.0e-12f);
            oscC[(size_t) k] *= m; oscS[(size_t) k] *= m;     // утримати амплітуду 1
        }
    }

    float tail = 0.0f;

private:
    struct AP
    {
        std::vector<float> buf; int pos = 0, len = 1;
        float run (float x, float g)
        {
            const int sz = (int) buf.size();
            const float d = buf[(size_t) ((pos - len + sz) % sz)];
            const float v = x + g * d;
            buf[(size_t) pos] = v;
            pos = (pos + 1) % sz;
            return d - g * v;
        }
    };
    struct Bands { float lo = 0.0f, hi = 0.0f; };
    struct V3
    {
        float x, y, z;
        V3 operator- (const V3& o) const { return { x - o.x, y - o.y, z - o.z }; }
        float length() const { return std::sqrt (x * x + y * y + z * z); }
    };

    /** Розміри (м) і відбивна здатність стін для кожного типу простору; множники RT низу/верху. */
    struct Geo { float w, l, h, refl, lowMul, hiMul, holdMs, er, late; };
    static const Geo& geo (int t)
    {
        static const Geo g[] {
            {  4.0f,  5.0f, 3.0f, 0.55f, 1.25f, 0.55f, 0.0f,  1.0f, 0.9f },   // Studio: оброблена кімната, верх швидко гасне
            {  7.0f,  9.5f, 4.0f, 0.75f, 1.15f, 0.65f, 0.0f,  1.0f, 1.0f },   // Live room
            {  3.5f,  6.0f, 3.0f, 0.85f, 1.05f, 0.80f, 0.0f,  0.6f, 1.15f },  // Chamber: бетон, щільний, рівний
            { 18.0f, 30.0f, 12.0f, 0.70f, 1.30f, 0.55f, 0.0f, 0.7f, 1.0f },   // Hall
            {  5.0f,  6.0f, 2.6f, 0.90f, 1.00f, 0.70f, 0.0f,  1.3f, 0.85f },  // Garage: паралельні стіни, «коробка»
            { 10.0f, 13.0f, 5.0f, 0.80f, 1.10f, 0.80f, 260.0f, 1.0f, 1.0f } }; // Gated
        return g[juce::jlimit (0, kNumRoom - 1, t)];
    }

    bool changed (const Params& p) const
    {
        return p.type != designed.type || std::abs (p.size - designed.size) > 0.01f * designed.size || std::abs (p.rtMid - designed.rtMid) > 0.01f * designed.rtMid
            || std::abs (p.dampHz - designed.dampHz) > 10.0f || std::abs (p.predelayMs - designed.predelayMs) > 0.2f || std::abs (p.diffusion - designed.diffusion) > 0.01f;
    }

    void design (const Params& p)
    {
        designed = p; designedValid = true;
        const auto& G = geo (p.type);
        const float sz = juce::jlimit (0.3f, 2.5f, p.size);
        const float W = G.w * sz, Ln = G.l * sz, H = G.h * std::sqrt (sz);
        constexpr float c = 343.0f;

        // Довжини ліній: взаємно прості, у межах ~ середнього вільного пробігу кімнати
        static constexpr int primesBase[N] { 1009, 1201, 1361, 1499, 1657, 1801, 1949, 2111, 2273, 2423, 2591, 2729, 2887, 3049, 3209, 3371 };
        const float mfp = 4.0f * (W * Ln * H) / (2.0f * (W * Ln + W * H + Ln * H));   // середній вільний пробіг, м
        const float scale = juce::jlimit (0.25f, 3.5f, (mfp / c) * (float) fs / 1800.0f);
        modDepth = (float) (0.00025 * fs) * (0.4f + 0.6f * p.diffusion);
        for (int k = 0; k < N; ++k)
        {
            lenTarget[(size_t) k] = juce::jlimit (8.0f, (float) (lineLen - 8) - modDepth, (float) primesBase[k] * scale * (float) (fs / 48000.0));
            if (len[(size_t) k] <= 0.0f) len[(size_t) k] = lenTarget[(size_t) k];
            modRate[(size_t) k] = 0.11 + 0.07 * k;   // повільні, різні для кожної лінії
            const double w = juce::MathConstants<double>::twoPi * modRate[(size_t) k] / fs;
            rotC[(size_t) k] = (float) std::cos (w); rotS[(size_t) k] = (float) std::sin (w);
        }

        // RT60 по смугах → коефіцієнти на лінію: g = 10^(-3·T/RT)
        const float hiMul = G.hiMul * juce::jmap (juce::jlimit (3000.0f, 12000.0f, p.dampHz), 3000.0f, 12000.0f, 0.6f, 1.25f);
        const float rtM = std::max (0.08f, p.rtMid), rtL = rtM * G.lowMul, rtH = std::max (0.05f, rtM * std::min (1.0f, hiMul));
        for (int k = 0; k < N; ++k)
        {
            const float T = lenTarget[(size_t) k] / (float) fs;
            gLow[(size_t) k]  = std::min (0.9995f, std::pow (10.0f, -3.0f * T / rtL));
            gMid[(size_t) k]  = std::min (0.9995f, std::pow (10.0f, -3.0f * T / rtM));
            gHigh[(size_t) k] = std::min (0.9995f, std::pow (10.0f, -3.0f * T / rtH));
            inGain[(size_t) k] = ((k * 7) % 3 == 0 ? -1.0f : 1.0f) / std::sqrt ((float) N);
            outL[(size_t) k] = (k & 1 ? -1.0f : 1.0f) * ((k & 4) ? 0.8f : 1.0f) / std::sqrt ((float) N);
            outR[(size_t) k] = (k & 2 ? -1.0f : 1.0f) * ((k & 8) ? 0.8f : 1.0f) / std::sqrt ((float) N);
        }
        loA = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 250.0f / (float) fs);
        hiA = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 4000.0f / (float) fs);
        lpA = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * juce::jlimit (2000.0f, 16000.0f, p.dampHz * 1.4f) / (float) fs);
        hpA = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 60.0f / (float) fs);

        // Дифузія входу: 4 all-pass, довжини ростуть із кімнатою
        static constexpr float apMs[4] { 1.3f, 2.1f, 3.7f, 5.3f };
        apG = 0.5f + 0.25f * p.diffusion;
        for (int k = 0; k < 4; ++k) inAp[(size_t) k].len = std::max (1, std::min ((int) inAp[(size_t) k].buf.size() - 1, (int) (apMs[k] * (0.6f + 0.4f * sz) * 0.001f * (float) fs)));
        static constexpr float erApMs[2][2] { { 0.71f, 1.53f }, { 0.83f, 1.37f } };
        for (int c2 = 0; c2 < 2; ++c2) for (int k = 0; k < 2; ++k) erAp[(size_t) c2][(size_t) k].len = std::max (1, (int) (erApMs[c2][k] * 0.001f * (float) fs));

        // Ранні відбиття: уявні джерела 1-го і 2-го порядку; барабани в центрі, пара мікрофонів (±0.6 м) перед ними
        const V3 src { 0.5f * W, 0.35f * Ln, 0.9f };
        const V3 micL { 0.5f * W - 0.6f, 0.35f * Ln + 1.6f, 1.6f }, micR { 0.5f * W + 0.6f, 0.35f * Ln + 1.6f, 1.6f };
        const float dDirL = (src - micL).length(), dDirR = (src - micR).length();
        numEr = 0;
        const int P = (int) preBuf.size();
        preLen = juce::jlimit (1, P - 1, (int) (p.predelayMs * 0.001f * (float) fs));
        auto addImage = [&] (V3 img, int order)
        {
            if (numEr >= kMaxEr) return;
            const float dl = (img - micL).length(), dr = (img - micR).length();
            const float dm = std::min (dl, dr);
            const int delay = (int) ((dm - std::min (dDirL, dDirR)) / c * (float) fs);
            if (delay <= 0 || delay >= P - 1) return;
            const float refl = std::pow (G.refl, (float) order);
            erDelay[(size_t) numEr] = delay;
            // Різниця відстаней до L/R мікрофонів — і в рівні, і (спрощено) в часі через знак
            erGL[(size_t) numEr] = refl * dDirL / dl * (order & 1 ? -1.0f : 1.0f);
            erGR[(size_t) numEr] = refl * dDirR / dr * (order & 1 ? -1.0f : 1.0f);
            ++numEr;
        };
        const float ix[] { -src.x, 2.0f * W - src.x }, iy[] { -src.y, 2.0f * Ln - src.y }, iz[] { -src.z, 2.0f * H - src.z };
        for (float x0 : ix) addImage ({ x0, src.y, src.z }, 1);
        for (float y0 : iy) addImage ({ src.x, y0, src.z }, 1);
        for (float z0 : iz) addImage ({ src.x, src.y, z0 }, 1);
        for (float x0 : ix) for (float y0 : iy) addImage ({ x0, y0, src.z }, 2);
        for (float x0 : ix) for (float z0 : iz) addImage ({ x0, src.y, z0 }, 2);
        for (float y0 : iy) for (float z0 : iz) addImage ({ src.x, y0, z0 }, 2);
        // Флаттер у гаражі: повторні відбиття між паралельними стінами
        if (p.type == Garage)
            for (int k = 2; k <= 6; ++k) addImage ({ src.x + (float) k * W * (k & 1 ? -1.0f : 1.0f), src.y, src.z }, k);
        erLevel = 0.9f * G.er;

        // «Хмара» gated: рівномірні відбиття протягом утримання
        numCloud = 0;
        holdMs = G.holdMs > 0.0f ? G.holdMs * (0.7f + 0.3f * sz) : 180.0f + 120.0f * sz;
        if (p.type == Gated)
        {
            juce::Random rnd (1984);
            const int count = kMaxCloud;
            for (int k = 0; k < count; ++k)
            {
                const float t = (5.0f + (holdMs - 10.0f) * ((float) k + rnd.nextFloat()) / (float) count) * 0.001f;
                cloudDelay[(size_t) k] = juce::jlimit (1, P - 2, (int) (t * (float) fs));
                const float gk = 0.22f * (0.7f + 0.3f * rnd.nextFloat());
                cloudGL[(size_t) k] = gk * (rnd.nextBool() ? 1.0f : -1.0f);
                cloudGR[(size_t) k] = gk * (rnd.nextBool() ? 1.0f : -1.0f);
            }
            numCloud = count;
        }
        lateLevel = 0.55f * G.late;
    }

    float readCubic (const std::vector<float>& b, float delay) const
    {
        const float rp = (float) wpos - delay;
        const int i1 = (int) std::floor (rp);
        const float t = rp - (float) i1;
        auto at = [&] (int idx) { return b[(size_t) ((idx % lineLen + lineLen) % lineLen)]; };
        const float y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    static void hadamard (float* y)
    {
        // Швидке перетворення Адамара 16×16, нормоване (без втрат енергії)
        for (int h = 1; h < N; h <<= 1)
            for (int i = 0; i < N; i += h << 1)
                for (int j = i; j < i + h; ++j) { const float a = y[j], b = y[j + h]; y[j] = a + b; y[j + h] = a - b; }
        for (int k = 0; k < N; ++k) y[k] *= 0.25f;   // 1/sqrt(16)
    }

    static constexpr int kMaxEr = 32, kMaxCloud = 40;
    double fs = 48000.0;
    std::array<std::vector<float>, N> lines;
    int lineLen = 1, wpos = 0, ppos = 0, preLen = 1, numEr = 0, numCloud = 0, hold = 0;
    std::vector<float> preBuf;
    std::array<AP, 4> inAp;
    std::array<std::array<AP, 2>, 2> erAp;
    std::array<Bands, N> bands;
    std::array<float, N> len {}, lenTarget {}, gLow {}, gMid {}, gHigh {}, inGain {}, outL {}, outR {};
    std::array<double, N> modRate {};
    std::array<float, N> oscC {}, oscS {}, rotC {}, rotS {};
    std::array<int, kMaxEr> erDelay {};
    std::array<float, kMaxEr> erGL {}, erGR {};
    std::array<int, kMaxCloud> cloudDelay {};
    std::array<float, kMaxCloud> cloudGL {}, cloudGR {};
    std::array<float, 2> inLp {}, inHp {};
    float modDepth = 10.0f, loA = 0.03f, hiA = 0.4f, lpA = 0.5f, hpA = 0.008f, apG = 0.6f, erLevel = 1.0f, lateLevel = 0.55f, holdMs = 250.0f, sideBuf = 0.0f;
    float env = 0.0f, peak = 0.0f, gateG = 1.0f;
    Params designed;
    bool designedValid = false;
};
} // namespace dec
