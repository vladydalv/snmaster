#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "SNCommon.h"
#include "EraModel.h"

/*  Аналіз міксу: спектр по третинах октави, цільова крива жанру й епохи, вердикти, схожість.
    Цільові криві — узагальнення (типовий баланс рок-мастерів + характер епохи з EraModel),
    а не виміри конкретних релізів. Для порівняння з реальною платівкою — Reference Match в Era. */

namespace an
{
inline constexpr int kBands = 28;
inline const std::array<float, kBands> bandHz { 31.5f, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630,
                                                800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000,
                                                10000, 12500, 16000 };

/** Базовий баланс (дБ на третину октави відносно 1 кГц) — узагальнення рок-мастерів. */
inline const std::array<float, kBands> baseCurve { -4.0f, 0.0f, 2.0f, 3.0f, 3.5f, 3.5f, 3.0f, 2.5f, 2.0f, 1.5f, 1.0f, 0.5f,
                                                   0.0f, 0.0f, 0.0f, 0.0f, -0.5f, -1.0f, -1.5f, -2.5f, -3.5f, -4.5f,
                                                   -5.5f, -7.0f, -8.5f, -10.0f, -12.0f, -16.0f };

using Spectrum = std::array<float, kBands>;

/** Цільова крива: база + характер епохи/жанру (АЧХ «епохального» EQ). */
inline Spectrum targetCurve (float year, int genre)
{
    const auto s = era::forYear (year, genre);
    constexpr double fs = 96000.0;
    sn::Biquad cut, lo, mid, hi, top;
    cut.setHighPass (fs, s.lowCutHz);
    lo.setLowShelf (fs, s.lowHz, 0.707, s.lowDb);
    mid.setPeak (fs, s.midHz, s.midQ, s.midDb);
    hi.setHighShelf (fs, s.highHz, 0.707, s.highDb);
    top.setLowPass (fs, std::min (s.topLpHz, 21000.0f), 0.707);
    Spectrum t;
    for (int b = 0; b < kBands; ++b)
    {
        const double f = bandHz[(size_t) b];
        t[(size_t) b] = baseCurve[(size_t) b] + (float) (cut.magnitudeDb (fs, f) + lo.magnitudeDb (fs, f) + mid.magnitudeDb (fs, f)
                                                         + hi.magnitudeDb (fs, f) + top.magnitudeDb (fs, f));
    }
    return t;
}

/** Вирівнює криву за середнім у 125 Гц … 5 кГц (порівнюємо форму, не гучність). */
inline Spectrum normalise (const Spectrum& s)
{
    double sum = 0.0; int n = 0;
    for (int b = 0; b < kBands; ++b)
        if (bandHz[(size_t) b] >= 125.0f && bandHz[(size_t) b] <= 5000.0f) { sum += s[(size_t) b]; ++n; }
    const float m = (float) (sum / std::max (1, n));
    Spectrum r;
    for (int b = 0; b < kBands; ++b) r[(size_t) b] = s[(size_t) b] - m;
    return r;
}

//==============================================================================
/** FFT-аналізатор: третинооктавний спектр, довгострокове середнє, накопичення для Learn,
    кореляція і «моно-сумісність» басу. Працює не в аудіопотоці. */
class Analyzer
{
public:
    static constexpr int order = 14, size = 1 << order;   // 16384: ≈ 2.9 Гц на точку

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        window.resize (size);
        for (int i = 0; i < size; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / (size - 1));
        fftData.assign ((size_t) size * 2, 0.0f);
        ring.assign ((size_t) size, 0.0f);
        ringPos = 0; filled = 0; sinceFrame = 0;
        avgPow.fill (0.0f); instDb.fill (-120.0f); learnPow.fill (0.0); learnFrames = 0; resetSong();
        corrNum = corrL = corrR = 0.0; lowMid = lowSide = 0.0;
        lpL.setLowPass (fs, 150.0, 0.707); lpR.setLowPass (fs, 150.0, 0.707);
        framesSeen = 0;
    }

    /** Весь трек: середній спектр усіх кадрів із сигналом від останнього скидання (тиша не рахується). */
    void resetSong() { songPow.fill (0.0); songFrames = 0; }
    Spectrum song() const
    {
        Spectrum s;
        for (int b = 0; b < kBands; ++b)
            s[(size_t) b] = (float) (10.0 * std::log10 (std::max (songPow[(size_t) b] / std::max (1, songFrames), 1e-14)));
        return s;
    }
    double songSeconds() const noexcept { return songFrames * (double) (size / 4) / fs; }

    void setLearning (bool on) { if (on && ! learning) { learnPow.fill (0.0); learnFrames = 0; } learning = on; }
    bool isLearning() const noexcept { return learning; }
    int learnedFrames() const noexcept { return learnFrames; }

    /** Подати стерео-семпли (L, R). */
    void push (const float* l, const float* r, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float L = l[i], R = r != nullptr ? r[i] : l[i];
            ring[(size_t) ringPos] = 0.5f * (L + R);
            ringPos = (ringPos + 1) & (size - 1);
            filled = std::min (filled + 1, size);

            // Кореляція (повільне середнє) і стерео в басу
            constexpr double a = 0.99999;     // ≈ 2 с: стабільні показники стерео
            corrNum = a * corrNum + (1 - a) * (double) L * R;
            corrL = a * corrL + (1 - a) * (double) L * L;
            corrR = a * corrR + (1 - a) * (double) R * R;
            const double bl = lpL.process (L), br = lpR.process (R);
            lowMid  = a * lowMid  + (1 - a) * 0.25 * (bl + br) * (bl + br);
            lowSide = a * lowSide + (1 - a) * 0.25 * (bl - br) * (bl - br);

            if (++sinceFrame >= size / 4 && filled == size) { sinceFrame = 0; frame(); }
        }
    }

    /** Довгострокове середнє, дБ (≈ 3 с). */
    Spectrum average() const
    {
        Spectrum s;
        for (int b = 0; b < kBands; ++b) s[(size_t) b] = 10.0f * std::log10 (std::max (avgPow[(size_t) b], 1e-14f));
        return s;
    }
    Spectrum instant() const { return instDb; }
    Spectrum learned() const
    {
        Spectrum s;
        for (int b = 0; b < kBands; ++b)
            s[(size_t) b] = (float) (10.0 * std::log10 (std::max (learnPow[(size_t) b] / std::max (1, learnFrames), 1e-14)));
        return s;
    }

    float correlation() const { return (float) (corrNum / std::sqrt (std::max (corrL * corrR, 1e-20))); }
    /** Сторона/середина в басу нижче 150 Гц, дБ (менше -15 дБ ≈ моно). */
    float lowSideDb() const { return (float) (10.0 * std::log10 (std::max (lowSide, 1e-20) / std::max (lowMid, 1e-20))); }
    bool hasSignal() const { return framesSeen > 8 && average()[15] > -110.0f; }

private:
    void frame()
    {
        for (int i = 0; i < size; ++i)
            fftData[(size_t) i] = ring[(size_t) ((ringPos + i) & (size - 1))] * window[(size_t) i];
        std::fill (fftData.begin() + size, fftData.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        const double binHz = fs / size;
        const float norm = 4.0f / ((float) size * (float) size);
        constexpr float frameAvg = 0.22f;   // ≈ 0.4 с: результат повороту ручки видно одразу
        for (int b = 0; b < kBands; ++b)
        {
            // Межі смуги в точках FFT; крайні точки враховуються частково (важливо для низьких смуг)
            const double lo = bandHz[(size_t) b] / std::pow (2.0, 1.0 / 6.0) / binHz, hi = bandHz[(size_t) b] * std::pow (2.0, 1.0 / 6.0) / binHz;
            const int k0 = std::max (1, (int) std::floor (lo + 0.5)), k1 = std::min (size / 2 - 1, (int) std::floor (hi + 0.5));
            double p = 0.0;
            for (int k = k0; k <= k1; ++k)
            {
                const double w = std::max (0.0, std::min (hi, k + 0.5) - std::max (lo, k - 0.5));
                p += w * (double) fftData[(size_t) k] * fftData[(size_t) k];
            }
            const float pw = (float) p * norm;
            avgPow[(size_t) b] += frameAvg * (pw - avgPow[(size_t) b]);
            instDb[(size_t) b] = 10.0f * std::log10 (std::max (pw, 1e-14f));
            instPow[(size_t) b] = pw;
            if (learning) learnPow[(size_t) b] += pw;
        }
        if (learning) ++learnFrames;
        double tot = 0.0;
        for (int b = 0; b < kBands; ++b) tot += instPow[(size_t) b];
        if (tot > 1.0e-8)                                  // ≈ -80 dBFS: тишу між піснями не враховуємо
        {
            for (int b = 0; b < kBands; ++b) songPow[(size_t) b] += instPow[(size_t) b];
            ++songFrames;
        }
        ++framesSeen;
    }

    double fs = 48000.0;
    juce::dsp::FFT fft { order };
    std::vector<float> window, fftData, ring;
    int ringPos = 0, filled = 0, sinceFrame = 0, framesSeen = 0;
    Spectrum avgPow {}, instDb {};
    std::array<double, kBands> learnPow {}, songPow {};
    std::array<float, kBands> instPow {};
    int learnFrames = 0, songFrames = 0;
    bool learning = false;
    double corrNum = 0, corrL = 0, corrR = 0, lowMid = 0, lowSide = 0;
    sn::Biquad lpL, lpR;
};

//==============================================================================
struct Verdict { juce::String text; int level; juce::String fix = {}; };   // level: 0 добре, 1 увага, 2 проблема; fix — яку ручку крутити

/** Регіон спектру: назва проблеми і підказка, яку ручку Master крутити. */
struct Region { const char* tooMuch; const char* tooLittle; const char* fixMuch; const char* fixLittle; float lo, hi; };
inline const std::array<Region, 6> regions
{{
    { "Boomy sub",               "No sub weight",   "EQ: LOW CUT 30-40 Hz, or LOW 60 Hz + LOW GAIN -",  "EQ: LOW 60 Hz, LOW GAIN +",          31.5f,   50.0f },
    { "Boomy bass",              "Thin bass",       "EQ: LOW 100 Hz, LOW GAIN -",                        "EQ: LOW 100 Hz, LOW GAIN +",         63.0f,  125.0f },
    { "Muddy / woolly low-mids", "Hollow low-mids", "EQ: MID 250 Hz, MID GAIN -",                        "EQ: MID 250 Hz, MID GAIN +",        160.0f,  400.0f },
    { "Boxy / honky mids",       "Scooped mids",    "EQ: MID 800 Hz, MID GAIN -",                        "EQ: MID 800 Hz, MID GAIN +",        500.0f, 1250.0f },
    { "Harsh presence",          "Dull, no bite",   "EQ: MID 3 kHz, MID GAIN -",                         "EQ: MID 3 kHz, MID GAIN +",        1600.0f, 5000.0f },
    { "Fizzy top",               "No air on top",   "EQ: HIGH 8 kHz, HIGH GAIN -",                       "EQ: HIGH 10 kHz, HIGH GAIN +",     6300.0f, 16000.0f },
}};

/** Середнє відхилення форми міксу від цілі в межах регіону. */
inline float regionDeviation (const Spectrum& mixN, const Spectrum& tgtN, const Region& r)
{
    double sum = 0.0; int n = 0;
    for (int b = 0; b < kBands; ++b)
        if (bandHz[(size_t) b] >= r.lo && bandHz[(size_t) b] <= r.hi) { sum += mixN[(size_t) b] - tgtN[(size_t) b]; ++n; }
    return (float) (sum / std::max (1, n));
}

struct Report
{
    std::vector<Verdict> verdicts;
    int matchPercent = 0;
    Spectrum deviation {};
};

inline juce::String decadeName (float year)
{
    const int d = ((int) year / 10) * 10;
    return "'" + juce::String (d % 100).paddedLeft ('0', 2) + "s";
}

inline const char* genreName (int g)
{
    static const char* n[] { "Rock", "Stoner", "Psych", "Space Rock", "Grunge" };
    return n[juce::jlimit (0, 4, g)];
}

/** Повний звіт: тональний баланс, гучність, динаміка, стерео. */
/** loudTargetLufs/loudName: ціль гучності (напр. стрімінг −14 LUFS); якщо не задано — типова гучність епохи. */
inline Report analyse (const Spectrum& mix, float lufsIntegrated, float truePeakDb, float correlation, float lowSideDb,
                       float year, int genre, float loudTargetLufs = -100.0f, const char* loudName = nullptr,
                       const Spectrum* customTarget = nullptr, const juce::String& customName = {})
{
    Report rep;
    const auto tgt = normalise (customTarget != nullptr ? *customTarget : targetCurve (year, genre));
    const auto m = normalise (mix);
    double absSum = 0.0; int n = 0;
    for (int b = 0; b < kBands; ++b)
    {
        rep.deviation[(size_t) b] = m[(size_t) b] - tgt[(size_t) b];
        if (bandHz[(size_t) b] >= 40.0f && bandHz[(size_t) b] <= 12500.0f) { absSum += std::abs (rep.deviation[(size_t) b]); ++n; }
    }
    const float meanAbs = (float) (absSum / std::max (1, n));

    for (auto& r : regions)
    {
        const float d = regionDeviation (m, tgt, r);
        if (std::abs (d) > 2.5f)
            rep.verdicts.push_back ({ juce::String (d > 0 ? r.tooMuch : r.tooLittle) + "  " + (d > 0 ? "+" : "") + juce::String (d, 1)
                                      + " dB  (" + juce::String (juce::roundToInt (r.lo)) + "-" + juce::String (juce::roundToInt (r.hi)) + " Hz)",
                                      std::abs (d) > 4.5f ? 2 : 1,
                                      juce::String (d > 0 ? r.fixMuch : r.fixLittle) + juce::String (juce::roundToInt (std::min (6.0f, std::abs (d)))) });
    }

    const bool streaming = loudTargetLufs > -60.0f && loudName != nullptr;
    const float targetLufs = streaming ? loudTargetLufs : era::forYear (year, genre).targetLufs;
    float loudPenalty = 0.0f, dynPenalty = 0.0f;
    if (lufsIntegrated > -70.0f)
    {
        const float dl = lufsIntegrated - targetLufs;
        const juce::String where = streaming ? juce::String (loudName) : juce::String (genreName (genre)) + " " + decadeName (year);
        if (dl < -2.0f)
            rep.verdicts.push_back ({ "Quiet for " + where + ": " + juce::String (lufsIntegrated, 1) + " LUFS (target " + juce::String (juce::roundToInt (targetLufs)) + ")", 1,
                                      "LIMITER: DRIVE +" + juce::String (juce::roundToInt (-dl)) + " dB" });
        else if (dl > 1.5f)
            rep.verdicts.push_back ({ streaming ? "Louder than " + where + ": it will be turned down " + juce::String (dl, 1) + " dB, punch lost for nothing"
                                                : "Louder than typical: " + juce::String (lufsIntegrated, 1) + " LUFS",
                                      dl > 4.0f ? 2 : 1, "LIMITER: DRIVE -" + juce::String (juce::roundToInt (dl)) + " dB" });
        else if (streaming)
            rep.verdicts.push_back ({ "Loudness on target for " + where + " (" + juce::String (lufsIntegrated, 1) + " LUFS)", 0 });
        loudPenalty = std::max (0.0f, std::abs (dl) - 1.5f);

        const float plr = truePeakDb - lufsIntegrated;
        if (plr < 6.0f)       { rep.verdicts.push_back ({ "Over-compressed / squashed (PLR " + juce::String (plr, 1) + " dB)", 2, "LIMITER: DRIVE -, COMP: THRESHOLD +" }); dynPenalty = (6.0f - plr) * 2.0f; }
        else if (plr < 8.0f)  rep.verdicts.push_back ({ "Dense, little punch left (PLR " + juce::String (plr, 1) + " dB)", 1, "COMP: ATTACK 20-30 ms, or LIMITER: DRIVE -" });
        else if (plr > 16.0f) rep.verdicts.push_back ({ "Very dynamic: may sound weak on phones (PLR " + juce::String (plr, 1) + " dB)", 1, "COMP: THRESHOLD -, RATIO 2-3" });
        // Spotify: true peak нижче -1 dBTP; для майстрів гучніших за -14 LUFS — нижче -2 dBTP (кодування додає піки)
        const float tpLimit = lufsIntegrated > -14.0f ? -2.0f : -1.0f;
        if (truePeakDb > tpLimit + 0.1f)
            rep.verdicts.push_back ({ "Peaks " + juce::String (truePeakDb, 1) + " dBTP: may distort after streaming encode (keep "
                                      + juce::String ((int) tpLimit) + " dBTP" + (tpLimit < -1.5f ? " for masters louder than -14 LUFS)" : ")"),
                                      2, "LIMITER: CEILING " + juce::String ((int) tpLimit) + " dB" });
    }
    if (lowSideDb > -12.0f) rep.verdicts.push_back ({ "Bass is wide (not mono)", 1, "STEREO: MONO BASS 100-150 Hz" });
    if (correlation < 0.0f)  rep.verdicts.push_back ({ "Phase problem: mix collapses in mono", 2, "STEREO: WIDTH down; check stereo tracks" });

    rep.matchPercent = juce::jlimit (0, 100, juce::roundToInt (100.0f - meanAbs * 9.0f - loudPenalty * 4.0f - dynPenalty));
    if (rep.verdicts.empty())
        rep.verdicts.push_back ({ customTarget != nullptr ? "Balanced like your reference (" + customName + ")"
                                                          : "Balanced. Sounds like " + juce::String (genreName (genre)) + " " + decadeName (year) + "!", 0 });
    else if (rep.matchPercent >= 85)
        rep.verdicts.insert (rep.verdicts.begin(), { customTarget != nullptr ? "Close to your reference, small tweaks left"
                                                                             : "Close to real " + juce::String (genreName (genre)) + " " + decadeName (year) + ", small tweaks left", 0 });
    return rep;
}
//==============================================================================
/** Reference Match: на яку «епоху» (при заданому жанрі) найбільше схожа платівка. */
struct RefResult { float year = 1975.0f; int match = 0; float lufs = -100.0f; };

inline RefResult matchReference (const juce::AudioBuffer<float>& audio, double fs, int genre)
{
    Analyzer a; a.prepare (fs);
    sn::LoudnessMeter lm; lm.prepare (fs, std::min (2, audio.getNumChannels()));
    const int nCh = audio.getNumChannels();
    constexpr int block = 4096;
    for (int pos = 0; pos < audio.getNumSamples(); pos += block)
    {
        const int n = std::min (block, audio.getNumSamples() - pos);
        a.push (audio.getReadPointer (0, pos), nCh > 1 ? audio.getReadPointer (1, pos) : nullptr, n);
        juce::AudioBuffer<float> view (const_cast<float* const*> (audio.getArrayOfReadPointers()), std::min (2, nCh), pos, n);
        lm.process (view);
    }

    RefResult r;
    r.lufs = lm.integrated.load();
    const auto ref = normalise (a.song());          // увесь файл, а не останні пів секунди
    float best = 1e9f;
    for (float y = 1960.0f; y <= 2025.0f; y += 0.5f)
    {
        const auto t = normalise (targetCurve (y, genre));
        double sum = 0.0; int n = 0;
        for (int b = 0; b < kBands; ++b)
            if (bandHz[(size_t) b] >= 40.0f && bandHz[(size_t) b] <= 12500.0f) { sum += std::abs (ref[(size_t) b] - t[(size_t) b]); ++n; }
        const float tonal = (float) (sum / std::max (1, n));
        const float loud = r.lufs > -70.0f ? std::abs (r.lufs - era::forYear (y, genre).targetLufs) : 0.0f;
        const float score = tonal + 0.35f * loud;
        if (score < best) { best = score; r.year = y; }
    }
    r.match = juce::jlimit (0, 100, juce::roundToInt (100.0f - best * 9.0f));
    return r;
}
} // namespace an
