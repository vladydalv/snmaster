#pragma once

#include <juce_dsp/juce_dsp.h>
#include "../Common/SNCommon.h"

/** Модель баритон-саксофона (спектрально-фізична).
    Тростина — коливання, яке тиск подиху «стискає» в несиметричну хвилю (тростина б'ється об мундштук:
    тихо — майже синус, голосно — гострі імпульси з усіма гармоніками). Далі — яскравість, що росте з тиском,
    резонанси корпусу баритона, шум повітря, синхронний з відкриттям тростини, гроул, підтяжка на атаці
    й дрібна жива нестабільність висоти. Висота задається напряму — строй точний на всьому діапазоні. */
namespace syn
{
struct SaxParams
{
    float breath = 0.6f, reed = 0.5f, growl = 0.0f, air = 0.3f, body = 0.6f, scoop = 0.25f;
};

class SaxModel
{
public:
    void prepare (double sampleRate) { fs = sampleRate; radK = 1.0f - std::exp (-6.2831853f * 170.0f / (float) fs); reset(); }

    void reset()
    {
        ph = 0.0f; subPh = 0.0f; env = 0.0f; gate = false; scoopOff = 0.0f; chiff = 0;
        s1 = s2 = 0.0f; nzHp = 0.0f; radZ = 0.0f; jitter = 0.0f; jitT = 0.0f; rough = 0.0f;
        for (auto& b : bodyEq) b.reset();
        designedBody = -1.0f;
    }

    void noteOn (float note, float vel, bool legato, const SaxParams& p)
    {
        gate = true;
        velocity = vel;
        attackMs = legato ? 25.0f : 70.0f - 50.0f * vel;     // сильніший удар — швидша атака
        scoopOff = legato ? -0.3f * p.scoop : -2.4f * p.scoop * (1.15f - 0.45f * vel);
        if (! legato) chiff = (int) (0.018 * fs);
        juce::ignoreUnused (note);
    }

    void noteOff() { gate = false; }

    /** pitchSemi — MIDI-нота з вібрато/бендом (може бути дробовою). */
    void render (float* out, int len, float pitchSemi, const SaxParams& p)
    {
        if (std::abs (p.body - designedBody) > 0.005f) designBody (p.body);

        // Підтяжка висоти на атаці і повільна жива нестабільність (±3 центи)
        scoopOff *= std::exp (-(float) len / (0.045f * (float) fs));
        if (rng.nextFloat() < (float) len / (0.25f * (float) fs)) jitT = rng.nextFloat() * 2.0f - 1.0f;
        jitter += (jitT - jitter) * (1.0f - std::exp (-(float) len / (0.12f * (float) fs)));
        const float f0 = 440.0f * std::exp2 ((pitchSemi + scoopOff + 0.03f * jitter - 69.0f) / 12.0f);
        const float inc = f0 / (float) fs;

        // Тиск: швидкість удару × подих; атака і згасання — як у живого дихання
        const float P = (0.35f + 0.65f * velocity) * (0.45f + 0.75f * p.breath);
        const float target = gate ? P : 0.0f;
        const float envK = 1.0f - std::exp (-1.0f / (0.001f * (gate ? attackMs : 90.0f) * (float) fs));

        // Яскравість: тиск відкриває верх (тихо — «оксамит», голосно — «гарчання»), тростина додає краю
        const float key = juce::jlimit (0.7f, 2.0f, 1.0f + (f0 - 110.0f) / 400.0f);
        const float edge = 0.6f + 0.6f * p.reed;
        const float growlInc = 0.5f * f0 * 1.007f / (float) fs;
        const float airLv = 0.004f + 0.035f * p.air;
        const float bias = 0.12f + 0.22f * p.reed;

        for (int i = 0; i < len; ++i)
        {
            env += (target - env) * envK;
            const float e = env;

            ph += inc; if (ph >= 1.0f) ph -= 1.0f;
            const float s = std::sin (6.2831853f * ph) + 0.18f * std::sin (12.566371f * ph + 0.6f);   // конічна труба: сильна 2-га

            // Тростина: несиметричне м'яке обмеження, сила росте з тиском
            const float g = 1.0f + 22.0f * e * e * edge;   // тростина закривається різкіше — гостріші фронти, більше верху
            float y = (std::tanh (g * (s + bias)) - std::tanh (g * bias)) / std::tanh (g);

            // Гроул: спів у інструмент — биття на субгармоніці й хрипіння
            if (p.growl > 0.0f)
            {
                subPh += growlInc; if (subPh >= 1.0f) subPh -= 1.0f;
                rough += 0.02f * ((rng.nextFloat() * 2.0f - 1.0f) - rough);
                y *= 1.0f + p.growl * (0.45f * std::sin (6.2831853f * subPh) + 3.0f * rough);
                y = std::tanh (y * (1.0f + 2.0f * p.growl)) / (1.0f + p.growl);
            }

            // Фільтр яскравості (2 полюси), частота стежить за тиском
            const float fc = juce::jlimit (200.0f, 0.4f * (float) fs, (700.0f + 5200.0f * std::pow (e, 1.4f) * edge) * key);
            const float gg = std::tan (3.14159265f * fc / (float) fs), R = 0.75f;
            const float hp = (y - (2.0f * R + gg) * s1 - s2) / (1.0f + 2.0f * R * gg + gg * gg);
            const float bp = gg * hp + s1, lp = gg * bp + s2;
            s1 = 2.0f * bp - s1; s2 = 2.0f * lp - s2;
            const float tone = lp * e;

            // Повітря: шум, що проривається, коли тростина відкрита (синхронно з періодом)
            const float nz = rng.nextFloat() * 2.0f - 1.0f;
            nzHp += 0.25f * (nz - nzHp);
            const float open = std::max (0.0f, s);
            float air = (nz - nzHp) * (0.3f + 0.7f * open * open) * e * airLv * 6.0f;

            // Атака язика: короткий шумовий «чіф»
            if (chiff > 0) { air += (nz - nzHp) * 0.06f * velocity * (float) chiff / (float) (0.018 * fs); --chiff; }

            // Випромінювання розтруба — ФВЧ (у баритона основний тон слабший за 2–4 гармоніки) + без постійної складової
            radZ += radK * (tone - radZ);
            float o = (tone - radZ) + air;
            for (auto& b : bodyEq) o = b.process (o);
            out[i] = o * 0.9f;
        }
    }

    bool sounding() const { return gate || env > 1.0e-4f; }

private:
    void designBody (float body)
    {
        designedBody = body;
        // Баритон: гул ~ 200 Гц, «хонк» ~ 520 Гц, носова зона ~ 1.3 кГц, придушений верх
        bodyEq[0].setPeak (fs, 200.0, 0.9, 3.0 * body);
        bodyEq[1].setPeak (fs, 520.0, 1.2, 5.0 * body);
        bodyEq[2].setPeak (fs, 1300.0, 1.5, 3.0 * body);
        bodyEq[3].setHighShelf (fs, 5000.0, 0.7, -4.0 - 4.0 * body);
    }

    double fs = 96000.0;
    float ph = 0.0f, subPh = 0.0f, env = 0.0f, velocity = 0.8f, attackMs = 40.0f, scoopOff = 0.0f;
    float s1 = 0.0f, s2 = 0.0f, nzHp = 0.0f, radZ = 0.0f, radK = 0.01f, jitter = 0.0f, jitT = 0.0f, rough = 0.0f, designedBody = -1.0f;
    int chiff = 0;
    bool gate = false;
    std::array<sn::Biquad, 4> bodyEq;
    juce::Random rng { 7 };
};
} // namespace syn
