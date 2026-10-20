#pragma once

#include <juce_core/juce_core.h>
#include <array>

/** Звук барабанів по роках: як їх записували й зводили в кожну епоху.
    Опорні роки інтерполюються; вісь Room — від сухого близького звуку до простору епохи.
    Значення — узагальнення типових прийомів студій, а не копія конкретних записів. */
namespace dec
{
enum Grit : int { Tape = 0, Tube, Sampler, Clip, kNumGrit };
enum RoomType : int { Studio = 0, Live, Chamber, Hall, Garage, Gated, kNumRoom };
enum Format : int { Acetate = 0, Vinyl, Cassette, CD, MP3, Stream, kNumFormat };

inline const char* gritName (int g)   { static const char* n[] { "Tape", "Tube", "Sampler", "Clip" }; return n[juce::jlimit (0, 3, g)]; }
inline const char* roomName (int r)   { static const char* n[] { "Studio", "Live room", "Chamber", "Hall", "Garage", "Gated" }; return n[juce::jlimit (0, 5, r)]; }
inline const char* formatName (int f) { static const char* n[] { "Acetate", "Vinyl", "Cassette", "Compact disc", "MP3", "Stream" }; return n[juce::jlimit (0, 5, f)]; }

/** Простір: розмір, час згасання (с, на максимумі осі Room), ранні відбиття, глушіння верху, передзатримка, гейт, щільність. */
struct Space { float size, decay, er, dampHz, predelayMs, gate, diffusion; };

inline const Space& roomPreset (int r)
{
    static const Space s[] {
        { 0.60f, 0.55f, 0.60f, 8000.0f,  3.0f, 0.0f, 0.60f },   // Studio
        { 1.20f, 1.40f, 0.70f, 7000.0f, 12.0f, 0.0f, 0.70f },   // Live room
        { 0.90f, 1.80f, 0.30f, 5000.0f,  8.0f, 0.0f, 0.90f },   // Chamber
        { 1.80f, 2.80f, 0.40f, 6000.0f, 25.0f, 0.0f, 0.85f },   // Hall
        { 0.70f, 0.90f, 0.90f, 3500.0f,  2.0f, 0.0f, 0.40f },   // Garage
        { 1.40f, 2.20f, 0.80f, 9000.0f,  4.0f, 1.0f, 0.80f } }; // Gated
    return s[juce::jlimit (0, kNumRoom - 1, r)];
}

/** Налаштування модулів після епохи (до ручних поправок). */
struct Era
{
    float lowDb, lowMidDb, presDb, topDb;       // Tone: 80 Гц полиця, 400 Гц, 4 кГц, 10 кГц полиця
    float drive; int grit;                       // Grit
    float hpHz, lpHz;                            // Filter
    float ratio, thrDb, attackMs, releaseMs, compMix;   // Squash (поріг — нижче рівня програми)
    float hitAttack, hitSustain;                 // Hit, %
    Space space; int roomType;                   // Room (на максимумі осі)
    float roomFloor;                             // скільки кімнати навіть унизу осі (1950-ті: один мікрофон у кімнаті)
    float width;                                 // 0 моно … 1 стерео
    int format;                                  // носій за замовчуванням
    const char* blurb;
};

struct Anchor { float year; Era e; };

inline const std::array<Anchor, 13>& anchors()
{
    static const std::array<Anchor, 13> a {{
        { 1950, { -2, 1.0f, -1, -8,   0.35f, Tube,    80,  6000,  2.0f,  3,  20, 300, 0.5f,   0,  10, roomPreset (Chamber), Chamber, 0.35f, 0.0f, Acetate,  "one mic in the room, mono, dark, tube" } },
        { 1958, { -1, 1.0f,  0, -6,   0.30f, Tube,    60,  8000,  2.5f,  4,  15, 250, 0.6f,   5,   5, roomPreset (Chamber), Chamber, 0.25f, 0.1f, Vinyl,    "echo chamber, warm and narrow" } },
        { 1965, {  0, 1.5f,  1, -4,   0.40f, Tape,    50, 10000,  3.0f,  6,  10, 200, 0.7f,  10,   0, roomPreset (Live),    Live,    0.15f, 0.4f, Vinyl,    "4-track tape, live room, few mics" } },
        { 1969, {  1, 1.0f, 1.5f, -3, 0.55f, Tape,    40, 11000,  4.0f,  9,   8, 150, 0.8f,  15,  -5, { 0.6f, 0.7f, 0.5f, 6000, 5, 0, 0.6f }, Studio, 0.10f, 0.6f, Vinyl, "damped toms, pumping compression, saturated tape" } },
        { 1972, {  2, 0.0f,  1, -2,   0.50f, Tape,    35, 12000,  6.0f, 10,  15, 120, 0.5f,  10,  10, { 1.5f, 2.0f, 0.7f, 7000, 15, 0, 0.7f }, Live, 0.10f, 0.8f, Vinyl, "big stone-room thunder, parallel smash" } },
        { 1977, {  2, -1.0f, 2, -3,   0.35f, Tape,    40, 12000,  3.0f,  6,  12, 120, 0.8f,  20, -15, { 0.5f, 0.45f, 0.4f, 7000, 2, 0, 0.6f }, Studio, 0.05f, 0.9f, Vinyl, "dead and dry: towels on drums, close mics" } },
        { 1983, {  1, -3.0f, 3,  2,   0.20f, Clip,    40, 16000,  4.0f,  8,   3, 100, 1.0f,  35, -10, roomPreset (Gated),   Gated,   0.05f, 1.0f, Cassette, "gated room, bright snap, console bus comp" } },
        { 1987, {  2, -3.0f, 3,  3,   0.25f, Sampler, 30, 18000,  4.0f,  8,   5, 120, 1.0f,  30, -10, { 1.6f, 2.4f, 0.7f, 9000, 8, 0.7f, 0.85f }, Gated, 0.05f, 1.0f, Cassette, "drum machines meet live kits, huge gated halls" } },
        { 1991, {  3, -2.0f, 2,  0,   0.40f, Tape,    30, 16000,  5.0f, 10,  10, 120, 0.6f,  15,  15, { 1.3f, 1.5f, 0.8f, 7000, 10, 0, 0.7f }, Live, 0.10f, 1.0f, CD, "loud live room, ambient mics, analog tape" } },
        { 1996, {  3, -3.0f, 2,  1,   0.30f, Tape,    30, 18000,  4.0f,  9,  10, 100, 0.7f,  20,   0, roomPreset (Live),    Live,    0.05f, 1.0f, CD,       "polished rock, controlled room" } },
        { 2003, {  3, -4.0f, 3,  3,   0.35f, Clip,    30, 20000,  6.0f, 12,   3,  60, 1.0f,  35, -10, { 0.6f, 0.5f, 0.5f, 9000, 2, 0, 0.6f }, Studio, 0.02f, 1.0f, MP3, "sample-replaced, tight and loud" } },
        { 2012, {  4, -3.0f, 2,  2,   0.25f, Tape,    25, 20000,  4.0f, 10,   5,  80, 0.7f,  25,  -5, { 0.8f, 0.8f, 0.6f, 8000, 4, 0, 0.7f }, Studio, 0.03f, 1.0f, Stream, "punchy modern, parallel compression" } },
        { 2025, {  4, -2.0f, 1.5f, 1.5f, 0.20f, Tube, 25, 20000,  3.0f,  8,   8,  80, 0.5f,  20,   0, roomPreset (Live),    Live,    0.03f, 1.0f, Stream,   "modern natural: real room, gentle glue" } } }};
    return a;
}

inline float lerpF (float a, float b, float t) { return a + (b - a) * t; }
inline float lerpLog (float a, float b, float t) { return std::exp (lerpF (std::log (a), std::log (b), t)); }

inline Space lerpSpace (const Space& a, const Space& b, float t)
{
    return { lerpF (a.size, b.size, t), lerpF (a.decay, b.decay, t), lerpF (a.er, b.er, t), lerpLog (a.dampHz, b.dampHz, t),
             lerpF (a.predelayMs, b.predelayMs, t), lerpF (a.gate, b.gate, t), lerpF (a.diffusion, b.diffusion, t) };
}

/** Епоха для довільного року (плавна інтерполяція між опорними). */
inline Era forYear (float year)
{
    const auto& A = anchors();
    year = juce::jlimit (A.front().year, A.back().year, year);
    size_t i = 0;
    while (i + 2 < A.size() && year > A[i + 1].year) ++i;
    const auto& a = A[i].e; const auto& b = A[i + 1].e;
    const float t = (year - A[i].year) / (A[i + 1].year - A[i].year);
    Era r = t < 0.5f ? a : b;     // дискретні (характер грязі, тип кімнати, носій, опис) — найближча опора
    r.lowDb = lerpF (a.lowDb, b.lowDb, t); r.lowMidDb = lerpF (a.lowMidDb, b.lowMidDb, t);
    r.presDb = lerpF (a.presDb, b.presDb, t); r.topDb = lerpF (a.topDb, b.topDb, t);
    r.drive = lerpF (a.drive, b.drive, t);
    r.hpHz = lerpLog (a.hpHz, b.hpHz, t); r.lpHz = lerpLog (a.lpHz, b.lpHz, t);
    r.ratio = lerpF (a.ratio, b.ratio, t); r.thrDb = lerpF (a.thrDb, b.thrDb, t);
    r.attackMs = lerpLog (a.attackMs, b.attackMs, t); r.releaseMs = lerpLog (a.releaseMs, b.releaseMs, t);
    r.compMix = lerpF (a.compMix, b.compMix, t);
    r.hitAttack = lerpF (a.hitAttack, b.hitAttack, t); r.hitSustain = lerpF (a.hitSustain, b.hitSustain, t);
    r.space = lerpSpace (a.space, b.space, t);
    r.roomFloor = lerpF (a.roomFloor, b.roomFloor, t);
    r.width = lerpF (a.width, b.width, t);
    return r;
}

/** Носій за роком (як слухали тоді). */
inline int formatForYear (float year)
{
    return year < 1958 ? Acetate : year < 1980 ? Vinyl : year < 1990 ? Cassette : year < 2000 ? CD : year < 2010 ? MP3 : Stream;
}
} // namespace dec
