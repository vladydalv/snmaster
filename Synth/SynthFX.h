#pragma once

#include <juce_dsp/juce_dsp.h>
#include "../Common/SNCommon.h"
#include "../Decade/RoomEngine.h"

namespace syn
{
/** Хорус у дусі BBD (Juno): дві модульовані затримки в протифазі, з м'яким верхом. */
class Chorus
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& b : buf) b.assign ((size_t) (0.04 * fs) + 4, 0.0f);
        pos = 0; ph = 0.0f;
        for (auto& f : lp) { f.setLowpass (fs, 9000.0); f.reset(); }
    }
    void process (float* L, float* R, int n, float rate, float depth, float mix)
    {
        const int sz = (int) buf[0].size();
        const float inc = rate / (float) fs;
        const float base = 0.0065f * (float) fs, dep = (0.0005f + 0.0045f * depth) * (float) fs;
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);
            buf[0][(size_t) pos] = lp[0].lowpass (m);
            ph += inc; if (ph >= 1.0f) ph -= 1.0f;
            const float tri = 4.0f * std::abs (ph - 0.5f) - 1.0f;
            float w[2];
            for (int c = 0; c < 2; ++c)
            {
                const float d = base + dep * (c == 0 ? tri : -tri);
                float rp = (float) pos - d; if (rp < 0.0f) rp += (float) sz;
                const int i0 = std::min ((int) rp, sz - 1); /* rp може округлитись до sz */ const float a = rp - (float) i0;
                w[c] = buf[0][(size_t) i0] + a * (buf[0][(size_t) ((i0 + 1) % sz)] - buf[0][(size_t) i0]);
            }
            pos = (pos + 1) % sz;
            L[i] = L[i] * (1.0f - 0.5f * mix) + w[0] * mix;
            R[i] = R[i] * (1.0f - 0.5f * mix) + w[1] * mix;
        }
    }

private:
    double fs = 48000.0;
    std::array<std::vector<float>, 1> buf;
    std::array<sn::OnePole, 2> lp;
    int pos = 0;
    float ph = 0.0f;
};

/** Темпова затримка з пінг-понгом; у петлі — зріз низу й верху (кожен повтор темніший, без «бруду» на басу). */
class Delay
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& b : buf) b.assign ((size_t) (2.5 * fs) + 4, 0.0f);
        pos = 0; timeSm = -1.0f;
        for (auto& f : hp) f.setHighPass (fs, 160.0, 0.7071);
        for (auto& f : lp) f.setLowPass (fs, 5500.0, 0.7071);
        for (auto& f : hp) f.reset();
        for (auto& f : lp) f.reset();
    }
    void process (float* L, float* R, int n, float seconds, float fb, float mix, bool ping)
    {
        const int sz = (int) buf[0].size();
        const float target = juce::jlimit (0.01f, 2.4f, seconds) * (float) fs;
        if (timeSm < 0.0f) timeSm = target;
        const float k = 1.0f - std::exp (-1.0f / (0.08f * (float) fs));
        for (int i = 0; i < n; ++i)
        {
            timeSm += (target - timeSm) * k;
            float rp = (float) pos - timeSm; if (rp < 0.0f) rp += (float) sz;
            const int i0 = std::min ((int) rp, sz - 1); /* rp може округлитись до sz */ const float a = rp - (float) i0;
            float y[2];
            for (int c = 0; c < 2; ++c)
                y[c] = buf[(size_t) c][(size_t) i0] + a * (buf[(size_t) c][(size_t) ((i0 + 1) % sz)] - buf[(size_t) c][(size_t) i0]);
            const float fl = lp[0].process (hp[0].process (y[0])), fr = lp[1].process (hp[1].process (y[1]));
            if (ping)
            {
                buf[0][(size_t) pos] = 0.5f * (L[i] + R[i]) + fb * fr;
                buf[1][(size_t) pos] = fb * fl;
            }
            else
            {
                buf[0][(size_t) pos] = L[i] + fb * fl;
                buf[1][(size_t) pos] = R[i] + fb * fr;
            }
            pos = (pos + 1) % sz;
            L[i] += mix * y[0];
            R[i] += mix * y[1];
        }
    }

private:
    double fs = 48000.0;
    std::array<std::vector<float>, 2> buf;
    std::array<sn::Biquad, 2> hp, lp;
    int pos = 0;
    float timeSm = -1.0f;
};

/** Октавний зсув вгору (гранулярний, дві голівки з перехресним згасанням) — для shimmer. */
class OctaveUp
{
public:
    void prepare (double fs) { buf.assign (8192, 0.0f); win = (int) (0.07 * fs); pos = 0; ph = 0.0f; }
    inline float process (float x) noexcept
    {
        const int sz = (int) buf.size();
        buf[(size_t) pos] = x;
        ph += 1.0f / (float) win; if (ph >= 1.0f) ph -= 1.0f;
        float y = 0.0f;
        for (int h = 0; h < 2; ++h)
        {
            const float p = std::fmod (ph + 0.5f * (float) h, 1.0f);
            const float d = (1.0f - p) * (float) win + 2.0f;          // затримка зменшується на 1 за семпл → швидкість читання ×2
            float rp = (float) pos - d; if (rp < 0.0f) rp += (float) sz;
            const int i0 = std::min ((int) rp, sz - 1); /* rp може округлитись до sz */ const float a = rp - (float) i0;
            const float v = buf[(size_t) i0] + a * (buf[(size_t) ((i0 + 1) % sz)] - buf[(size_t) i0]);
            const float w = std::sin (3.14159265f * p);
            y += v * w * w;
        }
        pos = (pos + 1) % sz;
        return y;
    }

private:
    std::vector<float> buf;
    int win = 3000, pos = 0;
    float ph = 0.0f;
};

/** Реверб: та сама FDN-кімната, що в SN Decade (16 ліній, трисмугове згасання), налаштована як зал.
    Хвіст до ~30 с; Shimmer — октава вгору в петлі (хвіст «підіймається» й світиться); Freeze — нескінченна хмара. */
class Reverb
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        engine.prepare (sampleRate);
        for (auto& b : tmp) b.assign ((size_t) maxBlock, 0.0f);
        for (auto& b : last) b.assign ((size_t) maxBlock, 0.0f);
        for (auto& o : oct) o.prepare (sampleRate);
        for (auto& f : shHp) { f.setHighPass (fs, 250.0, 0.7071); f.reset(); }
        for (auto& f : shLp) { f.setLowPass (fs, 9000.0, 0.7071); f.reset(); }
        lastN = 0;
    }
    void reset() { engine.reset(); }
    void process (float* L, float* R, int n, float size, float mix, float shimmer, bool freeze)
    {
        dec::RoomEngine::Params p;
        p.type = dec::Hall;
        p.size = 0.5f + 2.0f * size;
        p.rtMid = freeze ? 120.0f : 0.5f * std::exp2 (5.9f * size);
        p.dampHz = freeze ? 9000.0f : 7500.0f - 2500.0f * size;
        p.er = 0.25f;
        p.predelayMs = 8.0f + 30.0f * size;
        p.gate = 0.0f;
        p.diffusion = 0.85f;
        const float inGain = freeze ? 0.0f : 1.0f;
        const float fb = shimmer * 0.58f, inject = shimmer * 0.6f;   // петля (хвіст росте вгору) + октава від самого звуку
        for (int c = 0; c < 2; ++c)
        {
            const float* x = c == 0 ? L : R;
            float* t = tmp[(size_t) c].data();
            for (int i = 0; i < n; ++i)
            {
                float s = 0.0f;
                if (fb > 0.0f)
                {
                    const float prev = i < lastN ? last[(size_t) c][(size_t) i] : 0.0f;
                    s = shLp[(size_t) c].process (shHp[(size_t) c].process (oct[(size_t) c].process (fb * prev + inject * x[i] * inGain)));
                }
                t[i] = x[i] * inGain + s;
            }
        }
        std::copy_n (tmp[0].data(), n, last[0].data());
        std::copy_n (tmp[1].data(), n, last[1].data());
        engine.process (tmp[0].data(), tmp[1].data(), n, p, 1.0f);
        for (int c = 0; c < 2; ++c)
        {
            float* x = c == 0 ? L : R;
            for (int i = 0; i < n; ++i)
            {
                const float rev = tmp[(size_t) c][(size_t) i] - last[(size_t) c][(size_t) i];
                last[(size_t) c][(size_t) i] = rev;
                x[i] += mix * 0.9f * rev;
            }
        }
        lastN = n;
    }

private:
    double fs = 48000.0;
    dec::RoomEngine engine;
    std::array<std::vector<float>, 2> tmp, last;
    std::array<OctaveUp, 2> oct;
    std::array<sn::Biquad, 2> shHp, shLp;
    int lastN = 0;
};
} // namespace syn
