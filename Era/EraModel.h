#pragma once

#include <array>
#include <cmath>
#include <juce_core/juce_core.h>

/*  Модель «звуку епохи» для мастерингу. Кожна опорна точка — узагальнення типових прийомів
    свого часу (не копія конкретних записів): АЧХ носія і мастерингу, характер компресії,
    насичення (лампа / плівка / цифровий кліп), стереобаза, типова гучність релізів.
    Між опорними точками параметри плавно інтерполюються. */

namespace era
{
struct Settings
{
    float lowCutHz;                     // обрізання низу
    float lowDb, lowHz;                 // low shelf
    float midDb, midHz, midQ;           // характерна середина
    float highDb, highHz;               // high shelf
    float topLpHz;                      // завал верху носія
    float ratio, thrOffsetDb;           // компресія: поріг відносно рівня програми
    float attackMs, releaseMs;
    float tube, tape, clip;             // 0…100: насичення
    int   tapeSpeed;                    // 0 = 7.5, 1 = 15, 2 = 30 ips
    float width, monoBassHz;            // стерео
    float targetLufs;                   // гучність, до якої «дотягує» лімітер
};

struct Anchor { float year; Settings s; };

//                     cut  lowDb lowHz midDb midHz  Q    hiDb  hiHz   topLP   ratio thr  atk  rel   tube tape clip spd width mono  LUFS
inline const std::array<Anchor, 7> anchors
{{
    { 1960, { 60,  1.5f, 100, 2.0f, 1200, 0.8f, -3.0f, 6000,  12000, 2.5f,  2.0f, 20, 300, 45, 45,  0, 1,  70, 200, -17.0f } },
    { 1970, { 35,  2.0f,  80, 1.0f,  800, 0.7f, -1.5f, 8000,  15000, 2.0f,  1.0f, 30, 250, 25, 50,  0, 1,  95, 120, -15.0f } },
    { 1980, { 30,  0.0f,  80, 1.5f, 3000, 0.9f,  3.0f, 10000, 20000, 3.0f,  0.0f,  5, 120, 10, 15, 10, 2, 120,  80, -13.0f } },
    { 1990, { 25,  2.0f,  70,-1.0f,  400, 0.8f,  2.0f, 10000, 20000, 3.0f, -1.0f, 10, 150, 15, 30, 20, 2, 110, 100, -10.0f } },
    { 2000, { 30,  3.0f,  60,-2.0f,  350, 0.8f,  3.5f, 11000, 20000, 4.0f, -3.0f,  3,  80,  5,  0, 60, 2, 115, 120,  -6.5f } },
    { 2010, { 28,  2.5f,  55,-1.5f,  300, 0.8f,  2.5f, 12000, 20000, 3.0f, -1.5f, 10, 100, 10, 10, 35, 2, 120, 120,  -8.5f } },
    { 2025, { 25,  2.0f,  50,-1.0f,  300, 0.8f,  2.0f, 13000, 20000, 2.5f,  0.0f, 15, 120, 10, 10, 20, 2, 115, 110, -10.0f } },
}};

/** Жанр зсуває цілі кожної епохи. */
struct GenreOffset
{
    float lowDb, midDb, midHz /*0 = не змінювати*/, highDb, ratioMul, releaseMul, tube, tape, width, lufs;
};

inline const std::array<GenreOffset, 5> genres
{{
    { 0,     0,    0,    0,    1.0f, 1.0f,  0,  0,   0,  0.0f },   // Neutral
    { 1.5f,  1.0f, 250, -1.0f, 1.0f, 1.2f, 10, 15,  -5, -1.0f },   // Stoner: щільний низ і низька середина, темніше, динамічніше
    { 0,     0,    0,    1.0f, 0.8f, 1.3f,  5, 15,  15, -2.0f },   // Psych: повітря, ширина, м'якша компресія
    { 0.5f,  -0.5f,0,    1.5f, 0.9f, 1.5f,  5,  5,  25, -1.5f },   // Space Rock: широко, довгі «хвости»
    { 0.5f,  1.5f, 1500, 0.0f, 1.2f, 0.8f, 15,  0,   0,  1.0f },   // Grunge / Alt: присутність гітар, панч, гучніше
}};

/** «Класичні» роки жанру — підсвічуються на дисплеї. */
inline std::vector<std::pair<float, float>> classicZones (int genre)
{
    switch (genre)
    {
        case 1:  return { { 1969, 1976 }, { 1992, 2003 } };   // Stoner: протостоунер 70-х і пустельна сцена 90-х
        case 2:  return { { 1966, 1972 }, { 2010, 2018 } };   // Psych: 60-ті й нова хвиля психоделії
        case 3:  return { { 1970, 1977 }, { 1995, 2005 } };   // Space Rock
        case 4:  return { { 1989, 1996 } };                    // Grunge
        default: return {};
    }
}

inline float lerp (float a, float b, float t) { return a + (b - a) * t; }
inline float lerpLog (float a, float b, float t) { return std::exp (lerp (std::log (a), std::log (b), t)); }

inline Settings interpolate (const Settings& a, const Settings& b, float t)
{
    Settings s;
    s.lowCutHz = lerpLog (a.lowCutHz, b.lowCutHz, t);
    s.lowDb = lerp (a.lowDb, b.lowDb, t);   s.lowHz = lerpLog (a.lowHz, b.lowHz, t);
    s.midDb = lerp (a.midDb, b.midDb, t);   s.midHz = lerpLog (a.midHz, b.midHz, t); s.midQ = lerp (a.midQ, b.midQ, t);
    s.highDb = lerp (a.highDb, b.highDb, t); s.highHz = lerpLog (a.highHz, b.highHz, t);
    s.topLpHz = lerpLog (a.topLpHz, b.topLpHz, t);
    s.ratio = lerp (a.ratio, b.ratio, t);   s.thrOffsetDb = lerp (a.thrOffsetDb, b.thrOffsetDb, t);
    s.attackMs = lerpLog (a.attackMs, b.attackMs, t); s.releaseMs = lerpLog (a.releaseMs, b.releaseMs, t);
    s.tube = lerp (a.tube, b.tube, t); s.tape = lerp (a.tape, b.tape, t); s.clip = lerp (a.clip, b.clip, t);
    s.tapeSpeed = t < 0.5f ? a.tapeSpeed : b.tapeSpeed;
    s.width = lerp (a.width, b.width, t);   s.monoBassHz = lerp (a.monoBassHz, b.monoBassHz, t);
    s.targetLufs = lerp (a.targetLufs, b.targetLufs, t);
    return s;
}

/** Налаштування епохи для року з урахуванням жанру (на повній інтенсивності). */
inline Settings forYear (float year, int genre)
{
    year = juce::jlimit (anchors.front().year, anchors.back().year, year);
    size_t i = 0;
    while (i + 2 < anchors.size() && year > anchors[i + 1].year) ++i;
    const auto& a = anchors[i];
    const auto& b = anchors[i + 1];
    const float t = (year - a.year) / (b.year - a.year);
    // Плавний перехід (smoothstep), щоб між декадами не було «зламів»
    Settings s = interpolate (a.s, b.s, t * t * (3.0f - 2.0f * t));

    const auto& g = genres[(size_t) juce::jlimit (0, (int) genres.size() - 1, genre)];
    s.lowDb += g.lowDb;
    s.midDb += g.midDb;
    if (g.midHz > 0.0f) s.midHz = g.midHz;
    s.highDb += g.highDb;
    s.ratio = 1.0f + (s.ratio - 1.0f) * g.ratioMul;
    s.releaseMs *= g.releaseMul;
    s.tube = juce::jlimit (0.0f, 100.0f, s.tube + g.tube);
    s.tape = juce::jlimit (0.0f, 100.0f, s.tape + g.tape);
    s.width = juce::jlimit (0.0f, 200.0f, s.width + g.width);
    s.targetLufs += g.lufs;
    return s;
}

/** Масштаб інтенсивності: 0 — прозоро, 1 — повністю «як тоді». */
inline Settings applyIntensity (const Settings& e, float k)
{
    Settings s = e;
    s.lowCutHz = lerpLog (10.0f, e.lowCutHz, k);
    s.lowDb *= k; s.midDb *= k; s.highDb *= k;
    s.topLpHz = lerpLog (22000.0f, e.topLpHz, k);
    s.ratio = 1.0f + (e.ratio - 1.0f) * k;
    s.tube *= k; s.tape *= k; s.clip *= k;
    s.width = 100.0f + (e.width - 100.0f) * k;
    s.monoBassHz *= k;
    return s;
}

/** Короткий опис для дисплея. */
inline juce::String describe (const Settings& s)
{
    juce::StringArray parts;
    if (s.tape > 15.0f) parts.add (juce::String ("tape ") + (s.tapeSpeed == 0 ? "7.5" : s.tapeSpeed == 1 ? "15" : "30") + " ips");
    if (s.tube > 15.0f) parts.add ("tube");
    if (s.clip > 30.0f) parts.add ("digital clip");
    if (s.width < 90.0f) parts.add ("narrow");
    else if (s.width > 118.0f) parts.add ("wide");
    parts.add ((s.highDb < -0.5f ? "dark" : s.highDb > 2.5f ? "bright" : "balanced"));
    parts.add (juce::String (juce::roundToInt (s.targetLufs)) + " LUFS");
    return parts.joinIntoString ("  \xc2\xb7  ");
}
} // namespace era
