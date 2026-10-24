#pragma once

#include <juce_dsp/juce_dsp.h>
#include "../Common/SNCommon.h"

/** Модальний синтез: тіло (чаша, дзвін, трубка, язичок) — набір резонансів зі своїми частотами, гучністю і
    згасанням. Збудження — удар (жорсткість калатала) і/або тертя по краю (сталий «спів» чаші).
    Кожна мода — пара трохи розстроєних резонаторів: биття, як у справжніх тибетських чашах. */
namespace syn
{
enum BowlType : int { BSinging = 0, BCrystal, BBell, BChime, BKalimba, BGlass, kNumBowl };

struct BowlParams
{
    int type = BSinging;
    float decay = 0.6f, hard = 0.4f, rub = 0.0f, beat = 0.4f, bright = 0.5f;
};

class ModalModel
{
public:
    static constexpr int kModes = 8;

    void prepare (double sampleRate) { fs = sampleRate; reset(); }
    void reset()
    {
        for (auto& m : modes) for (auto& r : m.r) r.y1 = r.y2 = 0.0f;
        strikeLeft = 0; rubEnv = 0.0f; gate = false; designedNote = -1000.0f; exLp = 0.0f;
    }

    void noteOn (float vel, const BowlParams& p)
    {
        gate = true;
        velocity = vel;
        strikeLen = (int) ((0.0006 + 0.006 * (1.0 - p.hard)) * fs);   // м'яке калатало — довший, темніший удар
        strikeLeft = strikeLen;
        exLpK = 1.0f - std::exp (-6.2831853f * (300.0f + 9000.0f * p.hard * p.hard) / (float) fs);
    }
    void noteOff() { gate = false; }

    void render (float* L, float* R, int len, float pitchSemi, const BowlParams& p)
    {
        if (std::abs (pitchSemi - designedNote) > 0.01f || p.type != designedType || std::abs (p.decay - designedDecay) > 0.002f
            || std::abs (p.beat - designedBeat) > 0.002f || std::abs (p.bright - designedBright) > 0.002f)
            design (pitchSemi, p);

        const float rubTarget = gate ? p.rub : 0.0f;
        const float rubK = 1.0f - std::exp (-1.0f / ((gate ? 0.4f : 0.15f) * (float) fs));
        for (int i = 0; i < len; ++i)
        {
            // Збудження: удар (короткий фільтрований шум + імпульс) і тертя (тихий сталий шум)
            float ex = 0.0f;
            if (strikeLeft > 0)
            {
                const float t = 1.0f - (float) strikeLeft / (float) strikeLen;
                const float win = std::sin (3.14159265f * t);
                ex = win * (0.6f + 0.4f * rng.nextFloat()) * velocity * (0.6f / (float) strikeLen);   // площа удару стала: м'яке калатало — не тихіше, а темніше
                --strikeLeft;
            }
            rubEnv += (rubTarget - rubEnv) * rubK;
            const float rubX = rubEnv * (rng.nextFloat() * 2.0f - 1.0f);
            exLp += exLpK * (ex - exLp);

            float l = 0.0f, r = 0.0f;
            for (int k = 0; k < numModes; ++k)
            {
                auto& m = modes[(size_t) k];
                float s = 0.0f;
                for (auto& rs : m.r)
                {
                    const float y = rs.a1 * rs.y1 - rs.a2 * rs.y2 + exLp * m.in + rubX * m.inRub;
                    rs.y2 = rs.y1; rs.y1 = y;
                    s += y;
                }
                l += s * m.gL; r += s * m.gR;
            }
            L[i] = l; R[i] = r;
        }
    }

    bool ringing() const { return gate || rubEnv > 1.0e-4f; }

private:
    struct Res { float a1 = 0, a2 = 0, y1 = 0, y2 = 0; };
    struct Mode { std::array<Res, 2> r; float in = 0, inRub = 0, gL = 0, gR = 0; };

    void design (float pitchSemi, const BowlParams& p)
    {
        designedNote = pitchSemi; designedType = p.type; designedDecay = p.decay; designedBeat = p.beat; designedBright = p.bright;
        const float f0 = 440.0f * std::exp2 ((pitchSemi - 69.0f) / 12.0f);
        // Співвідношення частот мод і їхня відносна гучність/тривалість (з вимірювань реальних інструментів, наближено)
        struct Set { float ratio[kModes]; float amp[kModes]; float t60[kModes]; int n; };
        static const Set sets[kNumBowl] {
            { { 1.0f, 2.71f, 5.15f, 8.17f, 11.7f, 15.8f, 20.4f, 25.6f }, { 1.0f, 0.7f, 0.45f, 0.3f, 0.2f, 0.12f, 0.08f, 0.05f }, { 1.0f, 0.8f, 0.55f, 0.4f, 0.3f, 0.22f, 0.16f, 0.12f }, 8 },  // тибетська чаша
            { { 1.0f, 2.95f, 5.92f, 9.8f, 0, 0, 0, 0 },                       { 1.0f, 0.18f, 0.06f, 0.03f },                          { 1.0f, 0.7f, 0.5f, 0.35f }, 4 },                                          // кришталева: майже чистий тон
            { { 0.5f, 1.0f, 1.19f, 1.5f, 2.0f, 2.51f, 2.66f, 3.01f }, { 0.55f, 0.8f, 0.6f, 0.35f, 1.0f, 0.3f, 0.25f, 0.2f }, { 1.0f, 0.7f, 0.6f, 0.45f, 0.4f, 0.3f, 0.28f, 0.22f }, 8 },     // церковний дзвін: hum, prime, tierce…
            { { 1.0f, 2.756f, 5.404f, 8.933f, 13.34f, 0, 0, 0 },              { 1.0f, 0.6f, 0.35f, 0.2f, 0.1f },                    { 1.0f, 0.6f, 0.4f, 0.28f, 0.2f }, 5 },                                    // трубчасті дзвіночки
            { { 1.0f, 6.27f, 17.55f, 0, 0, 0, 0, 0 },                          { 1.0f, 0.25f, 0.08f },                               { 1.0f, 0.18f, 0.06f }, 3 },                                                // калімба (язичок)
            { { 1.0f, 2.0f, 3.01f, 4.03f, 5.06f, 6.1f, 0, 0 },                 { 1.0f, 0.5f, 0.32f, 0.2f, 0.12f, 0.08f },           { 1.0f, 0.6f, 0.4f, 0.3f, 0.22f, 0.16f }, 6 } };                           // скло
        const auto& s = sets[juce::jlimit (0, kNumBowl - 1, p.type)];
        const float baseT60 = (p.type == BKalimba ? 0.4f : 1.5f) * std::exp2 (6.0f * p.decay - 1.0f);   // ~0.75 … 48 с
        const float beatHz = 0.15f + 3.0f * p.beat * p.beat;
        numModes = 0;
        for (int k = 0; k < s.n; ++k)
        {
            const float f = f0 * s.ratio[k];
            if (f > 0.45f * (float) fs || f < 20.0f) continue;
            auto& m = modes[(size_t) numModes];
            const float tilt = std::pow (s.ratio[k] / s.ratio[0], (p.bright - 0.5f) * 1.6f);
            const float t60 = baseT60 * s.t60[k] * (k == 0 ? 1.0f : std::pow (0.9f, p.bright * 4.0f - 2.0f));
            const float rr = std::pow (10.0f, -3.0f / (std::max (0.02f, t60) * (float) fs));
            for (int j = 0; j < 2; ++j)
            {
                const float fj = f + (j == 0 ? -0.5f : 0.5f) * beatHz * (k == 0 ? 1.0f : 1.6f) * (p.type == BKalimba || p.type == BGlass ? 0.2f : 1.0f);
                const float w = 6.2831853f * fj / (float) fs;
                m.r[(size_t) j].a1 = 2.0f * rr * std::cos (w);
                m.r[(size_t) j].a2 = rr * rr;
            }
            // Нормування: амплітуда дзвону = вхід / sin(w) — вирівнюємо, щоб гучність мод не залежала від частоти;
            // для тертя (сталий шум) — ще й на тривалість (довга мода накопичує більше енергії)
            const float w0 = 6.2831853f * f / (float) fs;
            m.in = s.amp[k] * tilt * std::sin (w0) * 4.0f;
            m.inRub = s.amp[k] * tilt * std::sin (w0) * std::sqrt (2.0f * (1.0f - rr)) * 0.5f;
            const float pan = (numModes % 2 == 0 ? -1.0f : 1.0f) * std::min (0.7f, 0.15f * (float) numModes);
            m.gL = std::sqrt (0.5f * (1.0f - pan)) * 1.41421f; m.gR = std::sqrt (0.5f * (1.0f + pan)) * 1.41421f;
            ++numModes;
        }
    }

    double fs = 96000.0;
    std::array<Mode, kModes> modes;
    int numModes = 0, strikeLeft = 0, strikeLen = 1, designedType = -1;
    float velocity = 0.8f, rubEnv = 0.0f, exLp = 0.0f, exLpK = 0.5f;
    float designedNote = -1000.0f, designedDecay = -1.0f, designedBeat = -1.0f, designedBright = -1.0f;
    bool gate = false;
    juce::Random rng { 3 };
};
} // namespace syn
