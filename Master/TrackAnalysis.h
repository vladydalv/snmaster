#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "../Common/Analysis.h"

/** Аналіз усього треку. Живе в процесорі (а не у вікні), тож прослухане не губиться, коли закриваєш плагін.
    Крутиться на message thread (таймер процесора ~30 Гц): забирає аудіо з FIFO, рахує спектр усього треку,
    гучність і піки з поточними налаштуваннями динаміки, і раз на секунду оновлює стабільний звіт. */
class TrackAnalysis
{
public:
    TrackAnalysis (juce::AudioProcessorValueTreeState& s, sn::StereoFifo& in, sn::StereoFifo& out,
                   sn::LoudnessMeter& outLoud, sn::LoudnessMeter& inLoud)
        : apvts (s), inFifo (in), outFifo (out), loudness (outLoud), inLoudness (inLoud) {}

    void prepare (double sampleRate)
    {
        if (sampleRate == fsUsed) return;
        fsUsed = sampleRate;
        inAn.prepare (fsUsed);
        outAn.prepare (fsUsed);
        reset();
    }

    void reset()
    {
        inAn.resetSong();
        outLoudE = inLoudE = 0.0; outLoudN = inLoudN = 0; tpSince = 0.0f;
        report = {};
        soundsLike.clear(); character.clear();
    }

    void tick()
    {
        if (fsUsed <= 0.0) return;
        inFifo.pull  ([this] (const float* l, const float* r, int n) { inAn.push (l, r, n); });
        outFifo.pull ([this] (const float* l, const float* r, int n)
        {
            outAn.push (l, r, n);
            for (int i = 0; i < n; ++i) tickPeak = std::max (tickPeak, std::max (std::abs (l[i]), std::abs (r != nullptr ? r[i] : l[i])));
        });

        // Гучність і піки на виході — з поточними налаштуваннями динаміки (після зміни ручок міряємо заново)
        if (dynamicsChanged()) settleTicks = 30;
        if (settleTicks > 0)
        {
            --settleTicks;
            outLoudE = 0.0; outLoudN = 0; tpSince = 0.0f;
        }
        else
        {
            const float st = loudness.shortTerm.load();
            if (st > -60.0f) { outLoudE += std::pow (10.0, st / 10.0); ++outLoudN; }
            tpSince = std::max (tpSince, tickPeak);
        }
        const float stIn = inLoudness.shortTerm.load();
        if (stIn > -60.0f) { inLoudE += std::pow (10.0, stIn / 10.0); ++inLoudN; }
        tickPeak = 0.0f;

        if (++frameCounter % 30 == 0 || (report.verdicts.empty() && inAn.songSeconds() > 2.0))
            updateReport();
    }

    //--------------------------------------------------------------------------
    float targetYear() const { return 1965.0f + 10.0f * apvts.getRawParameterValue (ParamIDs::targetDecade)->load(); }
    int targetGenreIdx() const { return (int) apvts.getRawParameterValue (ParamIDs::targetGenre)->load(); }
    int loudIdx() const { return juce::jlimit (0, (int) kLoudTargets.size() - 1, (int) apvts.getRawParameterValue (ParamIDs::loudTarget)->load()); }
    float loudTargetLufs() const { return kLoudTargets[(size_t) loudIdx()]; }
    const char* loudTargetName() const
    {
        static const char* n[] { "Spotify / YouTube", "Apple Music", "Deezer", "a loud master" };
        return n[loudIdx()];
    }

    /** АЧХ еквалайзера Master на смугах аналізатора. */
    an::Spectrum eqResponse() const
    {
        an::Spectrum r {};
        auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
        if (v (ParamIDs::eqOn) < 0.5f) return r;
        constexpr double fs = 96000.0;
        sn::Biquad hp, lo, mid, hi;
        if (v (ParamIDs::hpfFreq) >= 15.0f) hp.setHighPass (fs, v (ParamIDs::hpfFreq)); else hp.setBypass();
        lo.setLowShelf  (fs, v (ParamIDs::lowFreq), 0.707, v (ParamIDs::lowGain));
        mid.setPeak     (fs, v (ParamIDs::midFreq), v (ParamIDs::midQ), v (ParamIDs::midGain));
        hi.setHighShelf (fs, v (ParamIDs::highFreq), 0.707, v (ParamIDs::highGain));
        for (int b = 0; b < an::kBands; ++b)
        {
            const double f = an::bandHz[(size_t) b];
            r[(size_t) b] = (float) (hp.magnitudeDb (fs, f) + lo.magnitudeDb (fs, f) + mid.magnitudeDb (fs, f) + hi.magnitudeDb (fs, f));
        }
        return r;
    }

    /** Весь трек (вхід Master) + поточний EQ. */
    an::Spectrum predicted() const
    {
        auto s = inAn.song();
        const auto eq = eqResponse();
        for (int b = 0; b < an::kBands; ++b) s[(size_t) b] += eq[(size_t) b];
        return s;
    }

    double outMeasuredSeconds() const { return outLoudN / 30.0; }
    /** Референс замість жанрової цілі (message thread). */
    void setReference (const an::Spectrum& sp, const juce::String& name, float lufs) { refSpec = sp; refName = name; refLufs = lufs; hasRef = true; updateReport(); }
    void clearReference() { hasRef = false; refName.clear(); updateReport(); }
    bool hasReference() const noexcept { return hasRef; }
    const juce::String& referenceName() const noexcept { return refName; }
    float referenceLufs() const noexcept { return refLufs; }
    an::Spectrum targetSpectrum() const { return hasRef ? refSpec : an::targetCurve (targetYear(), targetGenreIdx()); }

    float outLufs() const { return outLoudN > 30 ? (float) (10.0 * std::log10 (outLoudE / outLoudN)) : -100.0f; }
    float inLufs() const  { return inLoudN > 0 ? (float) (10.0 * std::log10 (inLoudE / inLoudN)) : -100.0f; }
    float truePeakDb() const { return sn::gainToDb (tpSince); }

    void updateReport()
    {
        if (inAn.songSeconds() < 2.0) return;
        const float lufs = outLufs();
        const float tp = truePeakDb();
        const auto mix = predicted();
        report = an::analyse (mix, lufs, tp, outAn.correlation(), outAn.lowSideDb(), targetYear(), targetGenreIdx(),
                              loudTargetLufs(), loudTargetName(), hasRef ? &refSpec : nullptr, refName);
        std::stable_sort (report.verdicts.begin(), report.verdicts.end(), [] (const an::Verdict& a, const an::Verdict& b) { return a.level > b.level; });

        // На що схожий трек (у межах жанру) — лише за тональним балансом
        const int g = targetGenreIdx();
        const auto m = an::normalise (mix);
        float best = 1e9f, bestYear = 1990.0f;
        for (float y = 1960.0f; y <= 2025.0f; y += 1.0f)
        {
            const auto t = an::normalise (an::targetCurve (y, g));
            double sum = 0.0; int n = 0;
            for (int b = 0; b < an::kBands; ++b)
                if (an::bandHz[(size_t) b] >= 40.0f && an::bandHz[(size_t) b] <= 12500.0f) { sum += std::abs (m[(size_t) b] - t[(size_t) b]); ++n; }
            const float score = (float) (sum / std::max (1, n));
            if (score < best) { best = score; bestYear = y; }
        }
        soundsLike = juce::String (an::genreName (g)) + " " + an::decadeName (bestYear) + " (" + juce::String (juce::jlimit (0, 100, juce::roundToInt (100.0f - best * 9.0f))) + "%)";

        // Характер словами (відносно типового рок-мастера тієї ж епохи)
        const auto ref = an::normalise (an::targetCurve (targetYear(), 0));
        auto dev = [&] (float lo, float hi)
        {
            double s = 0; int n = 0;
            for (int b = 0; b < an::kBands; ++b)
                if (an::bandHz[(size_t) b] >= lo && an::bandHz[(size_t) b] <= hi) { s += m[(size_t) b] - ref[(size_t) b]; ++n; }
            return (float) (s / std::max (1, n));
        };
        juce::StringArray tags;
        const float low = dev (50, 125), lowMid = dev (160, 400), mid = dev (500, 1250), pres = dev (1600, 5000), air = dev (6300, 16000);
        tags.add (low > 2.5f ? "heavy low end" : low < -2.5f ? "light low end" : "solid low end");
        if (lowMid > 2.0f) tags.add ("thick low-mids"); else if (lowMid < -2.0f) tags.add ("clean low-mids");
        if (mid < -2.0f) tags.add ("scooped mids"); else if (mid > 2.0f) tags.add ("mid-forward");
        tags.add (pres + air > 4.0f ? "bright" : pres + air < -4.0f ? "dark" : "balanced top");
        if (lufs > -70.0f)
        {
            const float plr = tp - lufs;
            tags.add (plr < 7.0f ? "squashed" : plr < 10.0f ? "dense" : plr > 15.0f ? "very dynamic" : "punchy");
            tags.add (juce::String (lufs, 1) + " LUFS");
        }
        character = tags.joinIntoString (" · ");
    }

    an::Analyzer inAn, outAn;
    an::Report report;
    juce::String soundsLike, character;

private:
    bool dynamicsChanged()
    {
        using namespace ParamIDs;
        static const char* ids[] { inGain, outGain, compOn, threshold, ratio, makeup, compMix, limOn, limGain, ceiling, clip };
        std::array<float, 11> now {};
        for (size_t i = 0; i < now.size(); ++i) now[i] = apvts.getRawParameterValue (ids[i])->load();
        const bool changed = now != dynSnapshot;
        dynSnapshot = now;
        return changed;
    }

    juce::AudioProcessorValueTreeState& apvts;
    sn::StereoFifo& inFifo;
    sn::StereoFifo& outFifo;
    sn::LoudnessMeter& loudness;
    sn::LoudnessMeter& inLoudness;
    double fsUsed = 0.0;
    int frameCounter = 0, settleTicks = 0, outLoudN = 0, inLoudN = 0;
    double outLoudE = 0.0, inLoudE = 0.0;
    float tpSince = 0.0f, tickPeak = 0.0f;
    std::array<float, 11> dynSnapshot {};
    an::Spectrum refSpec {};
    juce::String refName;
    float refLufs = -100.0f;
    bool hasRef = false;
};
