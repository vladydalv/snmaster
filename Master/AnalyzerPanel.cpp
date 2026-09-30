#include "AnalyzerPanel.h"

using namespace juce;
using namespace snui;

namespace
{
// Нахил відображення: +3 дБ/октаву навколо 1 кГц. Музика природно спадає до верху,
// з компенсацією «рівний» графік = збалансований звук, і різницю між цілями видно.
constexpr float kTiltDbPerOct = 3.0f;
float tiltAt (float f) { return kTiltDbPerOct * std::log2 (f / 1000.0f); }

String mmss (double s)
{
    const int t = (int) s;
    return String (t / 60) + ":" + String (t % 60).paddedLeft ('0', 2);
}
}

AnalyzerPanel::AnalyzerPanel (SpacenerdMasterProcessor& p)
    : proc (p),
      genreSel (p.apvts, ParamIDs::targetGenre),
      decadeSel (p.apvts, ParamIDs::targetDecade, "Decade")
{
    fsUsed = p.getSampleRate() > 0 ? p.getSampleRate() : 48000.0;
    inAn.prepare (fsUsed);
    outAn.prepare (fsUsed);

    resetButton.setTooltip ("Start listening again (e.g. for another song). The analyzer averages everything it hears since RESET.");
    resetButton.onClick = [this] { resetListening(); };
    assistButton.setTooltip ("Set EQ, compressor, limiter and mono bass towards the target (after ~10 s of listening)");
    assistButton.onClick = [this] { applyAssist(); };
    assistButton.setEnabled (false);

    for (auto* c : std::initializer_list<Component*> { &genreSel, &decadeSel, &resetButton, &assistButton })
        addAndMakeVisible (c);
}

float AnalyzerPanel::targetYear() const
{
    return 1965.0f + 10.0f * proc.apvts.getRawParameterValue (ParamIDs::targetDecade)->load();
}

int AnalyzerPanel::targetGenreIdx() const
{
    return (int) proc.apvts.getRawParameterValue (ParamIDs::targetGenre)->load();
}

void AnalyzerPanel::resetListening()
{
    inAn.resetSong();
    outLoudE = inLoudE = 0.0; outLoudN = inLoudN = 0; tpSince = 0.0f;
    report = {};
    soundsLike.clear(); character.clear();
    assistButton.setEnabled (false);
}

void AnalyzerPanel::feedForTest()
{
    proc.inFifo.pull  ([this] (const float* l, const float* r, int n) { inAn.push (l, r, n); });
    proc.outFifo.pull ([this] (const float* l, const float* r, int n)
    {
        outAn.push (l, r, n);
        for (int i = 0; i < n; ++i) tickPeak = std::max (tickPeak, std::max (std::abs (l[i]), std::abs (r[i])));
    });
}

an::Spectrum AnalyzerPanel::eqResponse() const
{
    an::Spectrum r {};
    auto v = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
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

an::Spectrum AnalyzerPanel::predicted() const
{
    auto s = inAn.song();
    const auto eq = eqResponse();
    for (int b = 0; b < an::kBands; ++b) s[(size_t) b] += eq[(size_t) b];
    return s;
}

/** Змінились налаштування, що впливають на гучність/динаміку → міряємо вихід заново. */
bool AnalyzerPanel::dynamicsChanged()
{
    using namespace ParamIDs;
    static const char* ids[] { inGain, outGain, compOn, threshold, ratio, makeup, compMix, limOn, limGain, ceiling };
    std::array<float, 10> now {};
    for (size_t i = 0; i < now.size(); ++i) now[i] = proc.apvts.getRawParameterValue (ids[i])->load();
    const bool changed = now != dynSnapshot;
    dynSnapshot = now;
    return changed;
}

void AnalyzerPanel::tick()
{
    if (proc.getSampleRate() > 0 && proc.getSampleRate() != fsUsed)
    {
        fsUsed = proc.getSampleRate();
        inAn.prepare (fsUsed); outAn.prepare (fsUsed);
    }
    feedForTest();

    // Гучність: короткострокові значення, усереднені по енергії (тиша не рахується)
    if (dynamicsChanged()) settleTicks = 30;                  // 1 с після зміни ручки — перехідний процес
    if (settleTicks > 0)
    {
        --settleTicks;
        outLoudE = 0.0; outLoudN = 0; tpSince = 0.0f;
    }
    else
    {
        const float st = proc.loudness.shortTerm.load();
        if (st > -60.0f) { outLoudE += std::pow (10.0, st / 10.0); ++outLoudN; }
        tpSince = std::max (tpSince, tickPeak);
    }
    const float stIn = proc.inLoudness.shortTerm.load();
    if (stIn > -60.0f) { inLoudE += std::pow (10.0, stIn / 10.0); ++inLoudN; }
    tickPeak = 0.0f;

    assistButton.setEnabled (inAn.songSeconds() >= 8.0);
    if (++frameCounter % 30 == 0 || (report.verdicts.empty() && inAn.songSeconds() > 2.0))
        updateReport();
    if (assistMessageFrames > 0) --assistMessageFrames;
    repaint();
}

void AnalyzerPanel::updateReport()
{
    if (inAn.songSeconds() < 2.0) return;
    const float lufs = outLoudN > 30 ? (float) (10.0 * std::log10 (outLoudE / outLoudN)) : -100.0f;
    const float tp = sn::gainToDb (tpSince);
    const auto mix = predicted();
    report = an::analyse (mix, lufs, tp, outAn.correlation(), outAn.lowSideDb(), targetYear(), targetGenreIdx());

    // Найважливіше спершу: проблеми, потім зауваження
    std::stable_sort (report.verdicts.begin(), report.verdicts.end(), [] (const an::Verdict& a, const an::Verdict& b) { return a.level > b.level; });

    // На що схожий трек (у межах обраного жанру): найближчий рік за формою спектра і гучністю
    const int g = targetGenreIdx();
    const auto m = an::normalise (mix);
    float best = 1e9f, bestYear = 1990.0f;
    for (float y = 1960.0f; y <= 2025.0f; y += 1.0f)
    {
        const auto t = an::normalise (an::targetCurve (y, g));
        double sum = 0.0; int n = 0;
        for (int b = 0; b < an::kBands; ++b)
            if (an::bandHz[(size_t) b] >= 40.0f && an::bandHz[(size_t) b] <= 12500.0f) { sum += std::abs (m[(size_t) b] - t[(size_t) b]); ++n; }
        const float score = (float) (sum / std::max (1, n)) + (lufs > -70.0f ? 0.35f * std::abs (lufs - era::forYear (y, g).targetLufs) : 0.0f);
        if (score < best) { best = score; bestYear = y; }
    }
    soundsLike = String (an::genreName (g)) + " " + an::decadeName (bestYear) + " (" + String (jlimit (0, 100, roundToInt (100.0f - best * 9.0f))) + "%)";

    // Характер: коротко словами (відносно типового рок-мастера тієї ж епохи)
    const auto ref = an::normalise (an::targetCurve (targetYear(), 0));
    auto dev = [&] (float lo, float hi)
    {
        double s = 0; int n = 0;
        for (int b = 0; b < an::kBands; ++b)
            if (an::bandHz[(size_t) b] >= lo && an::bandHz[(size_t) b] <= hi) { s += m[(size_t) b] - ref[(size_t) b]; ++n; }
        return (float) (s / std::max (1, n));
    };
    StringArray tags;
    const float low = dev (50, 125), lowMid = dev (160, 400), mid = dev (500, 1250), pres = dev (1600, 5000), air = dev (6300, 16000);
    tags.add (low > 2.5f ? "heavy low end" : low < -2.5f ? "light low end" : "solid low end");
    if (lowMid > 2.0f) tags.add ("thick low-mids"); else if (lowMid < -2.0f) tags.add ("clean low-mids");
    if (mid < -2.0f) tags.add ("scooped mids"); else if (mid > 2.0f) tags.add ("mid-forward");
    tags.add (pres + air > 4.0f ? "bright" : pres + air < -4.0f ? "dark" : "balanced top");
    if (lufs > -70.0f)
    {
        const float plr = tp - lufs;
        tags.add (plr < 7.0f ? "squashed" : plr < 10.0f ? "dense" : plr > 15.0f ? "very dynamic" : "punchy");
        tags.add (lufs > -9.0f ? "very loud" : lufs > -13.0f ? "loud" : lufs > -17.0f ? "moderate level" : "quiet");
    }
    character = tags.joinIntoString (" · ");
}

void AnalyzerPanel::applyAssist()
{
    if (inAn.songSeconds() < 2.0) return;

    const auto tgt = an::normalise (an::targetCurve (targetYear(), targetGenreIdx()));
    const auto in = an::normalise (inAn.song());
    an::Spectrum dev;
    for (int b = 0; b < an::kBands; ++b) dev[(size_t) b] = in[(size_t) b] - tgt[(size_t) b];

    auto avgDev = [&] (float lo, float hi)
    {
        double s = 0; int n = 0;
        for (int b = 0; b < an::kBands; ++b)
            if (an::bandHz[(size_t) b] >= lo && an::bandHz[(size_t) b] <= hi) { s += dev[(size_t) b]; ++n; }
        return (float) (s / std::max (1, n));
    };

    // Найбільше відхилення в середині (згладжене по 3 смугах)
    int midBand = -1; float midDev = 0.0f;
    for (int b = 1; b < an::kBands - 1; ++b)
    {
        const float f = an::bandHz[(size_t) b];
        if (f < 160.0f || f > 5000.0f) continue;
        const float d = (dev[(size_t) b - 1] + dev[(size_t) b] + dev[(size_t) b + 1]) / 3.0f;
        if (std::abs (d) > std::abs (midDev)) { midDev = d; midBand = b; }
    }

    const float lufsIn = inLoudN > 0 ? (float) (10.0 * std::log10 (inLoudE / inLoudN)) : -20.0f;
    const auto eraS = era::forYear (targetYear(), targetGenreIdx());

    auto set = [this] (const char* id, float v)
    {
        auto* prm = proc.apvts.getParameter (id);
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
        prm->endChangeGesture();
    };

    const float lowG  = jlimit (-6.0f, 6.0f, -avgDev (63.0f, 125.0f));
    const float highG = jlimit (-6.0f, 6.0f, -avgDev (6300.0f, 16000.0f));
    const float midG  = std::abs (midDev) > 1.0f ? jlimit (-6.0f, 6.0f, -0.8f * midDev) : 0.0f;
    const bool cutSub = avgDev (31.5f, 50.0f) > 3.0f;
    const bool monoBass = outAn.lowSideDb() > -15.0f || inAn.lowSideDb() > -15.0f;
    // Gain staging: спершу виводимо вхід на робочий рівень -18 LUFS, далі компресор і лімітер від нього
    constexpr float workLufs = -18.0f;
    const float inGainDb = jlimit (-24.0f, 24.0f, workLufs - lufsIn);
    const float limGain = jlimit (0.0f, 18.0f, eraS.targetLufs - workLufs);

    using namespace ParamIDs;
    set (eqOn, 1.0f);
    set (hpfFreq, cutSub ? 30.0f : 10.0f);
    set (lowFreq, 100.0f);  set (lowGain, lowG);
    if (midBand >= 0) { set (midFreq, an::bandHz[(size_t) midBand]); set (midQ, 1.0f); }
    set (midGain, midG);
    set (highFreq, 8000.0f); set (highGain, highG);

    set (compOn, 1.0f); set (ratio, 2.0f); set (attack, 20.0f); set (release, 150.0f); set (knee, 6.0f);
    set (scHpf, 100.0f); set (compAuto, 1.0f); set (compMix, 100.0f); set (makeup, 0.0f);
    set (inGain, inGainDb);
    set (threshold, workLufs + 4.0f);

    set (widthOn, 1.0f);
    if (monoBass) set (ParamIDs::monoBass, 120.0f);

    set (limOn, 1.0f); set (ceiling, -1.0f); set (limRel, 60.0f); set (ParamIDs::limGain, limGain);
    set (gainMatch, 0.0f);
    proc.resetMeters();

    String msg = "Assist: input " + String (inGainDb, 1) + " dB, low " + String (lowG, 1) + " dB, ";
    if (midBand >= 0 && midG != 0.0f) msg << String (midG, 1) << " dB @ " << String (roundToInt (an::bandHz[(size_t) midBand])) << " Hz, ";
    msg << "high " << String (highG, 1) << " dB, limiter +" << String (limGain, 1) << " dB";
    if (monoBass) msg << ", mono bass";
    assistMessage = msg;
    assistMessageFrames = 400;
    updateReport();
}

void AnalyzerPanel::drawSpectrum (Graphics& g, Rectangle<float> r)
{
    g.setColour (Theme::bg.withAlpha (0.5f));
    g.fillRoundedRectangle (r, 6.0f);

    constexpr float fLo = 25.0f, fHi = 20000.0f, dbLo = -15.0f, dbHi = 15.0f;
    auto xOf = [&] (float f) { return r.getX() + r.getWidth() * std::log (f / fLo) / std::log (fHi / fLo); };
    auto yOf = [&] (float db) { return r.getBottom() - 16.0f - (r.getHeight() - 40.0f) * (jlimit (dbLo, dbHi, db) - dbLo) / (dbHi - dbLo); };

    // Сітка
    g.setFont (FontOptions (9.0f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xOf (f);
        g.setColour (Theme::border.withAlpha (0.6f));
        g.drawVerticalLine (roundToInt (x), r.getY(), r.getBottom());
        g.setColour (Theme::muted.withAlpha (0.8f));
        g.drawText (f >= 1000.0f ? String (roundToInt (f / 1000.0f)) + "k" : String (roundToInt (f)),
                    Rectangle<float> (x + 2.0f, r.getBottom() - 12.0f, 30.0f, 10.0f), Justification::centredLeft);
    }
    for (float db : { -10.0f, 0.0f, 10.0f })
    {
        g.setColour (Theme::border.withAlpha (db == 0.0f ? 0.9f : 0.4f));
        g.drawHorizontalLine (roundToInt (yOf (db)), r.getX(), r.getRight());
    }
    // Підписи зон
    g.setColour (Theme::muted.withAlpha (0.55f));
    g.setFont (FontOptions (9.0f, Font::bold));
    const float zy = r.getY() + 22.0f;
    for (auto [lo, hi, name] : { std::tuple<float, float, const char*> { 30, 120, "LOWS" }, { 120, 500, "LOW-MIDS" }, { 500, 2000, "MIDS" },
                                 { 2000, 6000, "PRESENCE" }, { 6000, 18000, "AIR" } })
        g.drawText (name, Rectangle<float> (xOf (lo), zy, xOf (hi) - xOf (lo), 12.0f), Justification::centred);

    auto pathOf = [&] (const an::Spectrum& s)
    {
        Path p;
        for (int b = 0; b < an::kBands; ++b)
        {
            const float f = an::bandHz[(size_t) b];
            const Point<float> pt (xOf (f), yOf (s[(size_t) b] + tiltAt (f)));
            if (b == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        return p;
    };
    auto dashedStroke = [&] (const Path& src, Colour c, float w)
    {
        Path dashed;
        const float dashes[] { 5.0f, 4.0f };
        PathStrokeType (w).createDashedStroke (dashed, src, dashes, 2);
        g.setColour (c);
        g.fillPath (dashed);
    };

    // Для порівняння: типовий рок-мастер тієї ж епохи (коли обрано інший жанр — видно, чим жанр відрізняється)
    if (targetGenreIdx() != 0)
        dashedStroke (pathOf (an::normalise (an::targetCurve (targetYear(), 0))), Theme::muted.withAlpha (0.45f), 1.0f);

    const auto tgt = an::normalise (an::targetCurve (targetYear(), targetGenreIdx()));
    const bool haveSong = inAn.songSeconds() >= 2.0;

    // «Зараз» — ледь помітно, лише щоб бачити, що аналізатор слухає
    if (outAn.hasSignal())
    {
        g.setColour (Theme::muted.withAlpha (0.28f));
        g.strokePath (pathOf (an::normalise (outAn.average())), PathStrokeType (1.0f));
    }

    if (haveSong)
    {
        const auto mix = an::normalise (predicted());
        for (int b = 0; b < an::kBands; ++b)
        {
            const float d = mix[(size_t) b] - tgt[(size_t) b];
            if (std::abs (d) < 2.5f) continue;
            const float f = an::bandHz[(size_t) b];
            const float x = xOf (f);
            const float y0 = yOf (tgt[(size_t) b] + tiltAt (f)), y1 = yOf (mix[(size_t) b] + tiltAt (f));
            g.setColour ((d > 0 ? Theme::hot : Theme::accent2).withAlpha (std::abs (d) > 4.5f ? 0.35f : 0.2f));
            g.fillRect (Rectangle<float> (x - 5.0f, std::min (y0, y1), 10.0f, std::abs (y1 - y0)));
        }
        g.setColour (Theme::accent);
        g.strokePath (pathOf (mix), PathStrokeType (2.4f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    dashedStroke (pathOf (tgt), Theme::accent2.withAlpha (0.9f), 1.6f);

    // Легенда
    g.setFont (FontOptions (10.0f, Font::bold));
    auto leg = Rectangle<float> (r.getX() + 8.0f, r.getY() + 6.0f, r.getWidth() - 16.0f, 12.0f);
    g.setColour (Theme::accent);
    g.drawText ("YOUR TRACK (whole song + EQ)", leg.removeFromLeft (190.0f), Justification::centredLeft);
    g.setColour (Theme::accent2);
    g.drawText ("TARGET: " + String (an::genreName (targetGenreIdx())).toUpperCase() + " " + an::decadeName (targetYear()),
                leg.removeFromLeft (170.0f), Justification::centredLeft);
    if (targetGenreIdx() != 0)
    {
        g.setColour (Theme::muted);
        g.drawText ("ROCK " + an::decadeName (targetYear()), leg.removeFromLeft (90.0f), Justification::centredLeft);
    }
    g.setColour (Theme::muted.withAlpha (0.8f));
    g.setFont (FontOptions (9.5f));
    g.drawText ("tonal balance, not loudness  |  flat = balanced", leg, Justification::centredRight);
}

void AnalyzerPanel::paint (Graphics& g)
{
    drawCard (g, getLocalBounds().toFloat(), "TRACK ANALYZER");
    drawSpectrum (g, graphArea.toFloat());

    // Статус праворуч від заголовка
    g.setColour (Theme::muted);
    g.setFont (FontOptions (11.0f));
    const double sec = inAn.songSeconds();
    g.drawText (sec >= 1.0 ? "listened " + mmss (sec) + " of the track" : String ("press play: listens to the whole track"),
                Rectangle<int> (200, 10, getWidth() - 220, 24), Justification::centredRight);

    auto v = verdictArea.toFloat();
    const bool have = sec >= 2.0 && ! report.verdicts.empty();

    g.setColour (Theme::muted);
    g.setFont (FontOptions (10.0f, Font::bold));
    g.drawText ("MATCH TO TARGET", v.removeFromTop (14.0f), Justification::centredLeft);
    auto line = v.removeFromTop (30.0f);
    const int pct = report.matchPercent;
    g.setColour (! have ? Theme::muted : pct >= 85 ? Theme::good : pct >= 65 ? Theme::warn : Theme::hot);
    g.setFont (FontOptions (26.0f, Font::bold));
    g.drawText (have ? String (pct) + "%" : String ("--"), line.removeFromLeft (80.0f), Justification::centredLeft);
    g.setColour (Theme::text);
    g.setFont (FontOptions (12.0f));
    g.drawFittedText (have ? "sounds most like: " + soundsLike : String ("play the song (ideally all of it)"),
                      line.toNearestInt(), Justification::centredLeft, 1, 0.85f);
    if (have && character.isNotEmpty())
    {
        g.setColour (Theme::accent2);
        g.setFont (FontOptions (11.5f, Font::bold));
        g.drawFittedText (character, v.removeFromTop (30.0f).toNearestInt(), Justification::centredLeft, 2, 0.85f);
    }
    v.removeFromTop (4.0f);

    auto drawLine = [&] (const String& text, Colour c, const String& fix = {})
    {
        if (v.getHeight() < 16.0f) return;
        auto l = v.removeFromTop (18.0f);
        g.setColour (c);
        g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre ({ l.getX() + 4.0f, l.getCentreY() }));
        g.setColour (Theme::text);
        g.setFont (FontOptions (12.0f));
        g.drawFittedText (text, l.withTrimmedLeft (14.0f).toNearestInt(), Justification::centredLeft, 1, 0.85f);
        if (fix.isNotEmpty() && v.getHeight() >= 15.0f)
        {
            auto f = v.removeFromTop (16.0f).withTrimmedLeft (14.0f);
            g.setColour (Theme::accent2);
            g.setFont (FontOptions (11.0f, Font::bold));
            g.drawFittedText (String (CharPointer_UTF8 ("\xe2\x86\x92 ")) + fix, f.toNearestInt(), Justification::centredLeft, 1, 0.85f);
        }
        v.removeFromTop (3.0f);
    };

    if (assistMessageFrames > 0 && assistMessage.isNotEmpty())
        drawLine (assistMessage, Theme::accent);
    if (! have)
        drawLine ("Averages the whole song, so the verdict is stable", Theme::muted,
                  "EQ moves the curve instantly; COMP / LIMITER change loudness and dynamics");
    else
    {
        int shown = 0;
        for (auto& vd : report.verdicts)
        {
            if (++shown > 3) break;
            drawLine (vd.text, vd.level == 0 ? Theme::good : vd.level == 1 ? Theme::warn : Theme::hot, vd.fix);
        }
    }
}

void AnalyzerPanel::resized()
{
    auto r = getLocalBounds().withTrimmedTop (44).reduced (14, 10);
    auto right = r.removeFromRight (440);
    r.removeFromRight (12);
    graphArea = r;

    auto controls = right.removeFromTop (42);
    genreSel.setBounds (controls.removeFromLeft (310).withTrimmedTop (10));
    controls.removeFromLeft (6);
    decadeSel.setBounds (controls);
    right.removeFromTop (6);
    auto buttons = right.removeFromBottom (26);
    resetButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 3));
    buttons.removeFromLeft (6);
    assistButton.setBounds (buttons);
    right.removeFromBottom (6);
    verdictArea = right;
}
