#pragma once

#include "MixBus.h"

/** Поради щодо зведення: перевірки окремої доріжки (запис, тон, динаміка, стерео)
    і всього міксу (баланс за жанром, конфлікти частот, стрій між доріжками).
    Пороги — узагальнені практики зведення року, відправна точка, а не закон. */
namespace mix
{
struct Advice { int sev; juce::String title, text; };

inline juce::String hzText (float hz) { return hz >= 1000.0f ? (hz >= 10000.0f ? juce::String (juce::roundToInt (hz / 1000.0f)) : juce::String (hz / 1000.0f, 1)) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz"; }
inline juce::String dbText (float db) { return juce::String (juce::roundToInt (std::abs (db))) + " dB"; }

/** Енергія смуги частот [lo, hi] Гц, дБ. */
inline float regionDb (const float* bandsDb, float lo, float hi)
{
    double p = 0.0;
    for (int b = 0; b < an::kBands; ++b)
        if (an::bandHz[(size_t) b] >= lo && an::bandHz[(size_t) b] <= hi) p += std::pow (10.0, bandsDb[b] / 10.0);
    return (float) (10.0 * std::log10 (std::max (p, 1.0e-20)));
}

inline float peakHz (const float* bandsDb, float lo, float hi)
{
    float best = -1.0e9f, hz = lo;
    for (int b = 0; b < an::kBands; ++b)
        if (an::bandHz[(size_t) b] >= lo && an::bandHz[(size_t) b] <= hi && bandsDb[b] > best) { best = bandsDb[b]; hz = an::bandHz[(size_t) b]; }
    return hz;
}

inline const char* genreLabel (int g)
{
    static const char* n[] { "rock", "stoner", "psych", "space rock", "grunge" };
    return n[juce::jlimit (0, 4, g)];
}

//==============================================================================
/** Перевірки однієї доріжки: не потребують інших доріжок. */
inline void trackChecks (const Features& f, std::vector<Advice>& out)
{
    const int inst = f.inst;
    const juce::String nm = instName (inst);

    // --- Запис
    if (f.clips > 10)
        out.push_back ({ 2, "Clipping", "Peaks hit 0 dBFS " + juce::String (f.clips) + " times. If this is the raw recording, lower the input gain on your "
                                         "interface (peaks around -10 dBFS) and re-record; if it comes from plugins, lower their output." });
    else if (f.peakDb > -0.3f)
        out.push_back ({ 1, "Peaks at 0 dBFS", "Peaks touch 0 dBFS. Leave some headroom: aim for peaks around -10 to -6 dBFS when recording." });
    if (f.lufs > -100.0f && f.lufs < -42.0f && f.peakDb < -24.0f)
        out.push_back ({ 1, "Recorded very quiet", "Plays at " + juce::String (juce::roundToInt (f.lufs)) + " LUFS, peaks " + juce::String (juce::roundToInt (f.peakDb))
                                                   + " dBFS. If this is the recording, raise the interface gain so peaks reach about -10 dBFS: quiet takes bring up noise." });
    if (f.noiseDb < 0.0f && f.noiseDb > -45.0f && inst != Kick && inst != Snare && inst != Drums)
        out.push_back ({ f.noiseDb > -35.0f ? 2 : 1, "Noise between notes",
                         "Background noise is only " + dbText (f.noiseDb) + " under the playing. Use a noise gate, check cables, "
                         "and turn the guitar volume down when you don't play." });
    if (f.hum >= 0.5f)
    {
        const int h = juce::roundToInt (f.humHz);
        out.push_back ({ f.hum >= 0.75f ? 2 : 1, "Mains hum " + juce::String (h) + " Hz",
                         "Steady " + juce::String (h) + " Hz hum in the quiet parts. Check grounding, try another outlet, move away from the computer screen "
                         "(single-coil pickups catch it most). In the mix: narrow cuts at " + juce::String (h) + "/" + juce::String (2 * h) + "/"
                         + juce::String (3 * h) + " Hz or a gate." });
    }
    if ((inst == Guitar || inst == Bass || inst == Keys) && f.tuneFrames >= 40.0f && std::abs (f.tuneCents) >= 12.0f)
        out.push_back ({ std::abs (f.tuneCents) >= 20.0f ? 2 : 1, "Tuning",
                         "Notes sit on average " + juce::String (juce::roundToInt (std::abs (f.tuneCents))) + " cents " + (f.tuneCents > 0 ? "sharp" : "flat")
                         + " of A = 440. Retune, or make sure every track uses the same reference." });

    // --- Тон
    const float* B = f.bands;
    const float sub = regionDb (B, 20, 55), low = regionDb (B, 60, 160), lowMid = regionDb (B, 200, 500), mid = regionDb (B, 600, 1700),
                hiMid = regionDb (B, 2000, 4000), pres = regionDb (B, 5000, 8500), air = regionDb (B, 10000, 20000);

    const int hp = inst == Vocal ? 100 : inst == Guitar ? 80 : inst == Snare ? 80 : inst == Keys ? 60 : 0;
    if (hp > 0 && sub - mid > -14.0f)
        out.push_back ({ 1, "Rumble below 60 Hz", "Energy under 60 Hz that a " + nm.toLowerCase() + " doesn't need: it eats headroom and muddies the bass. "
                                                   "High-pass around " + juce::String (hp) + " Hz." });
    switch (inst)
    {
        case Guitar:
            if (lowMid - mid > 4.0f)
                out.push_back ({ lowMid - mid > 8.0f ? 2 : 1, "Muddy low-mids", "Too much 200-500 Hz. Cut 2-4 dB around " + hzText (peakHz (B, 200, 500))
                                 + ". With heavy distortion try less gain: more gain = more mud." });
            if (hiMid - mid > 2.0f)
                out.push_back ({ 1, "Harsh 2-5 kHz", "Upper mids poke out. Cut 2-3 dB around " + hzText (peakHz (B, 2000, 5000)) + "." });
            if (air - mid > -14.0f)
                out.push_back ({ 1, "Fizz above 10 kHz", "Fizzy top end (typical for fuzz and amp sims). Low-pass at 8-10 kHz." });
            break;
        case Vocal:
            if (lowMid - mid > 3.0f)
                out.push_back ({ 1, "Boxy vocal", "Boomy 200-500 Hz: cut 2-3 dB at " + hzText (peakHz (B, 200, 500)) + ", or sing a bit further from the mic." });
            if (hiMid - mid > 4.0f)
                out.push_back ({ 1, "Harsh vocal", "Piercing 2-4 kHz: cut 2 dB at " + hzText (peakHz (B, 2000, 4000)) + "." });
            if (pres - mid > 0.0f)
                out.push_back ({ 1, "Sibilance", "Loud esses at 5-8 kHz: use a de-esser (Tone has one)." });
            else if (pres - mid < -18.0f)
                out.push_back ({ 1, "Dull vocal", "Little air: add a 2-3 dB shelf above 8 kHz or Tone's exciter." });
            break;
        case Bass:
            if (sub - low > 3.0f)
                out.push_back ({ 1, "Too much sub", "Below 55 Hz dominates: phones and small speakers lose it. Trim under 40 Hz and add a little saturation (Tone / Stomp)." });
            if (lowMid - low > 0.0f)
                out.push_back ({ 1, "Boxy bass", "More 200-500 Hz than real low end. Cut 2 dB at " + hzText (peakHz (B, 200, 500)) + ", boost 60-100 Hz." });
            if (regionDb (B, 600, 4000) - low < -28.0f)
                out.push_back ({ 1, "Bass vanishes on phones", "No growl above 600 Hz. Add some 700-1500 Hz or parallel distortion (Stomp with Clean Bass)." });
            break;
        case Kick:
            if (lowMid - low > -8.0f)
                out.push_back ({ 1, "Boxy kick", "Cardboard-like 200-500 Hz: cut 3-4 dB at " + hzText (peakHz (B, 250, 500)) + "." });
            if (hiMid - low < -30.0f)
                out.push_back ({ 1, "No beater click", "The kick may disappear on small speakers: boost 2-4 kHz by 2-3 dB." });
            break;
        case Drums:
            if (pres - mid > 6.0f)
                out.push_back ({ 1, "Harsh cymbals", "Cymbals bite at 5-9 kHz: cut 2-3 dB at " + hzText (peakHz (B, 5000, 10000)) + "." });
            break;
        case Keys:
            if (lowMid - mid > 5.0f)
                out.push_back ({ 1, "Muddy keys", "Heavy 200-500 Hz: cut 2-3 dB at " + hzText (peakHz (B, 200, 500)) + " or high-pass to leave room for bass and guitars." });
            break;
        default: break;
    }

    // --- Динаміка
    if ((inst == Kick || inst == Snare) && f.crestDb > 0.0f && f.crestDb < 9.0f)
        out.push_back ({ 1, "Flat transients", "Hits peak only " + dbText (f.crestDb) + " over the body: little punch. Slower compressor attack or a transient shaper (Tone's Transient)." });
    if (inst == Vocal && f.rangeDb > 14.0f)
        out.push_back ({ 1, "Vocal jumps in level", "Phrases vary by " + dbText (f.rangeDb) + ". Compress 3:1-4:1 and/or ride the fader so quiet words stay audible." });
    if (inst == Vocal && f.rangeDb > 0.0f && f.rangeDb < 3.0f)
        out.push_back ({ 1, "Vocal over-squashed", "Almost no level movement: it may sound flat and tiring. Use less compression." });
    if (inst == Bass && f.rangeDb > 12.0f)
        out.push_back ({ 1, "Uneven bass", "Notes vary by " + dbText (f.rangeDb) + ". Compress ~4:1 with ~30 ms attack so the low end stays steady." });

    // --- Стерео
    if (f.corr < -0.2f && f.sideDb > -20.0f)
        out.push_back ({ 2, "Phase problem", "Left and right cancel in mono (correlation " + juce::String (f.corr, 2) + "). Flip polarity on one side/mic or check the widener." });
    if ((inst == Kick || inst == Bass || inst == Snare || inst == Vocal) && f.lowSideDb > -12.0f && f.sideDb > -20.0f)
        out.push_back ({ 1, "Wide low end", "Keep the " + nm.toLowerCase() + " centred: low end below ~120 Hz should be mono (Master's Mono Bass)." });
}

/** Статус і до kItems порад, найважливіші першими. */
inline Verdict makeVerdict (std::vector<Advice> items, bool valid)
{
    Verdict v;
    std::stable_sort (items.begin(), items.end(), [] (const Advice& a, const Advice& b) { return a.sev > b.sev; });
    v.numItems = std::min ((int) items.size(), Verdict::kItems);
    for (int i = 0; i < v.numItems; ++i) setItem (v.items[i], items[(size_t) i].sev, items[(size_t) i].title, items[(size_t) i].text);
    v.status = ! valid ? -1 : items.empty() ? 0 : items.front().sev;
    if (! valid && v.numItems == 0)
    {
        v.numItems = 1;
        setItem (v.items[0], 0, "Listening...", "Play the song (or keep playing): advice appears after a few seconds of sound.");
    }
    return v;
}

//==============================================================================
/** Баланс: гучність категорії відносно всього міксу (поки вона звучить), дБ, за жанрами Rock/Stoner/Psych/Space/Grunge. */
inline float balanceTarget (int inst, int genre, bool hasKickOrSnare)
{
    static constexpr float t[kNumInst][5] {
        { -9, -9, -11, -11, -9 },      // Kick
        { -9, -10, -10, -11, -8 },     // Snare
        { -12, -12, -11, -11, -11 },   // Drums (оверхеди/рум)
        { -8, -6, -8, -8, -7 },        // Bass
        { -5, -3, -6, -6, -4 },        // Guitar (усі разом)
        { -5, -8, -7, -8, -6 },        // Vocal
        { -11, -12, -8, -6, -12 },     // Keys
        { 0, 0, 0, 0, 0 } };
    const int g = juce::jlimit (0, 4, genre);
    if (inst == Drums && ! hasKickOrSnare) return -5.0f;   // уся установка однією доріжкою
    return t[juce::jlimit (0, kNumInst - 1, inst)][g];
}

/** Пари, що б'ються за частоти, і що з цим робити. */
struct Clash { int a, b; float lo, hi; const char* fix; };
inline const std::array<Clash, 5>& clashes()
{
    static const std::array<Clash, 5> c {{
        { Kick, Bass, 40, 125, "Decide who owns the lowest octave: cut 2-3 dB at %f in one of them, or sidechain the bass to the kick." },
        { Bass, Guitar, 100, 315, "High-pass the guitars around 90-120 Hz and cut ~2 dB at %f in the guitars." },
        { Guitar, Vocal, 1000, 4000, "Cut 2-3 dB at %f in the guitars (a dynamic EQ keyed from the vocal is ideal)." },
        { Keys, Vocal, 1000, 4000, "Cut 2-3 dB at %f in the keys, or pan them away from the vocal." },
        { Keys, Guitar, 250, 2500, "Give each its own range: cut ~2 dB at %f in one, boost it in the other." } }};
    return c;
}
} // namespace mix
