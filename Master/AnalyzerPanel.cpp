#include "AnalyzerPanel.h"
#include "AlbumView.h"

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
    : proc (p), ta (p.analysis),
      genreSel (p.apvts, ParamIDs::targetGenre),
      decadeSel (p.apvts, ParamIDs::targetDecade, "Decade"),
      loudSel (p.apvts, ParamIDs::loudTarget, "Release for")
{
    resetButton.setTooltip ("Start listening again (e.g. for another song). The analyzer averages everything it hears since RESET.");
    resetButton.onClick = [this] { ta.reset(); proc.mixWatch.requestReset(); proc.mixWatch.refreshNow(); };
    assistButton.setTooltip ("Set EQ, compressor, limiter and mono bass towards the tonal target and the streaming loudness target");
    assistButton.onClick = [this] { applyAssist(); };
    assistButton.setEnabled (false);
    loudSel.setTooltip ("Where the song will be released: loudness verdicts and ASSIST aim for this level at -1 dBTP (-2 dBTP for masters louder than -14 LUFS, as Spotify recommends)");

    albumButton.setTooltip ("Album: save each finished song and see which one differs in loudness, density or tone");
    albumButton.onClick = [this]
    {
        auto* top = getTopLevelComponent();
        CallOutBox::launchAsynchronously (std::make_unique<AlbumView> (proc), top->getLocalArea (&albumButton, albumButton.getLocalBounds()), top);
    };

    for (auto* c : std::initializer_list<Component*> { &genreSel, &decadeSel, &loudSel, &resetButton, &assistButton, &albumButton })
        addAndMakeVisible (c);
}

void AnalyzerPanel::tick()
{
    assistButton.setEnabled (ta.inAn.songSeconds() >= 8.0);

    // Точне попадання в ціль гучності: після ASSIST міряємо результат і доводимо драйв лімітера (до 3 кроків)
    if (refineSteps > 0 && ta.outMeasuredSeconds() >= 4.0 && ta.outLufs() > -70.0f)
    {
        const float delta = ta.loudTargetLufs() - ta.outLufs();
        if (std::abs (delta) > 0.4f)
        {
            auto* prm = proc.apvts.getParameter (ParamIDs::limGain);
            const float now = proc.apvts.getRawParameterValue (ParamIDs::limGain)->load();
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (jlimit (0.0f, 18.0f, now + 0.9f * delta)));
            prm->endChangeGesture();
            --refineSteps;
        }
        else refineSteps = 0;
    }
    if (assistMessageFrames > 0) --assistMessageFrames;
    repaint();
}

void AnalyzerPanel::applyAssist()
{
    if (ta.inAn.songSeconds() < 2.0) return;

    const auto tgt = an::normalise (ta.targetSpectrum());
    const auto in = an::normalise (ta.inAn.song());
    an::Spectrum dev;
    for (int b = 0; b < an::kBands; ++b) dev[(size_t) b] = in[(size_t) b] - tgt[(size_t) b];

    auto avgDev = [&] (float lo, float hi)
    {
        double s = 0; int n = 0;
        for (int b = 0; b < an::kBands; ++b)
            if (an::bandHz[(size_t) b] >= lo && an::bandHz[(size_t) b] <= hi) { s += dev[(size_t) b]; ++n; }
        return (float) (s / std::max (1, n));
    };

    // Quick Low / Mid / High уже діють поверх EQ — EQ підбираємо з їх урахуванням
    {
        auto v = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
        sn::Biquad ql, qm, qh;
        ql.setLowShelf (96000.0, kQuickLowHz, kQuickLowQ, v (ParamIDs::qLow));
        qm.setPeak (96000.0, kQuickMidHz, kQuickMidQ, v (ParamIDs::qMid));
        qh.setHighShelf (96000.0, kQuickHighHz, kQuickHighQ, v (ParamIDs::qHigh));
        an::Spectrum q;
        for (int b = 0; b < an::kBands; ++b)
        {
            const double f = an::bandHz[(size_t) b];
            q[(size_t) b] = (float) (ql.magnitudeDb (96000.0, f) + qm.magnitudeDb (96000.0, f) + qh.magnitudeDb (96000.0, f));
        }
        const auto qn = an::normalise (q);
        for (int b = 0; b < an::kBands; ++b) dev[(size_t) b] += qn[(size_t) b];
    }

    // Де найбільше відхилення (згладжене по 3 смугах) — туди ставимо дзвони низької середини й середини
    auto worstIn = [&] (float lo, float hi, float fallback)
    {
        int best = -1; float bestD = 0.0f;
        for (int b = 1; b < an::kBands - 1; ++b)
        {
            const float f = an::bandHz[(size_t) b];
            if (f < lo || f > hi) continue;
            const float d = (dev[(size_t) b - 1] + dev[(size_t) b] + dev[(size_t) b + 1]) / 3.0f;
            if (std::abs (d) > std::abs (bestD)) { bestD = d; best = b; }
        }
        return std::abs (bestD) > 1.0f && best >= 0 ? an::bandHz[(size_t) best] : fallback;
    };
    const float lmHz = worstIn (150.0f, 600.0f, 300.0f);
    const float midHz = worstIn (700.0f, 5000.0f, 1500.0f);

    // Усі чотири смуги підбираються РАЗОМ (найменші квадрати по всій кривій 40 Гц–16 кГц):
    // полиці й дзвони перекриваються, і окремий підбір кожної давав горб або провал на низькій середині
    constexpr int K = 4;
    constexpr double efs = 96000.0;
    auto response = [&] (const std::array<float, K>& gains)
    {
        sn::Biquad lo, lm, mid, hi;
        lo.setLowShelf (efs, 100.0, 0.707, gains[0]);
        lm.setPeak (efs, lmHz, kLowMidQ, gains[1]);
        mid.setPeak (efs, midHz, 1.0, gains[2]);
        hi.setHighShelf (efs, 8000.0, 0.707, gains[3]);
        an::Spectrum r;
        for (int b = 0; b < an::kBands; ++b)
        {
            const double f = an::bandHz[(size_t) b];
            r[(size_t) b] = (float) (lo.magnitudeDb (efs, f) + lm.magnitudeDb (efs, f) + mid.magnitudeDb (efs, f) + hi.magnitudeDb (efs, f));
        }
        return an::normalise (r);
    };
    auto weight = [] (float f) { return f < 40.0f || f > 16000.0f ? 0.0 : (f < 63.0f || f > 10000.0f ? 0.5 : 1.0); };
    std::array<float, K> g {};
    std::array<bool, K> fixed {};
    for (int iter = 0; iter < 6; ++iter)
    {
        // Лінеаризація навколо поточних підсилень: стовпці = відгук на +1 дБ кожної смуги
        const auto base = response (g);
        std::array<an::Spectrum, K> col;
        for (int k = 0; k < K; ++k)
        {
            auto gk = g; gk[(size_t) k] += 1.0f;
            const auto rk = response (gk);
            for (int b = 0; b < an::kBands; ++b) col[(size_t) k][(size_t) b] = rk[(size_t) b] - base[(size_t) b];
        }
        double A[K][K] {}, y[K] {};
        for (int b = 0; b < an::kBands; ++b)
        {
            const double w = weight (an::bandHz[(size_t) b]);
            if (w <= 0.0) continue;
            const double resid = dev[(size_t) b] + base[(size_t) b];
            for (int i = 0; i < K; ++i)
            {
                y[i] -= w * col[(size_t) i][(size_t) b] * resid;
                for (int j = 0; j < K; ++j) A[i][j] += w * col[(size_t) i][(size_t) b] * col[(size_t) j][(size_t) b];
            }
        }
        for (int i = 0; i < K; ++i) A[i][i] += 0.15;          // м'яка регуляризація: без зайвих рухів
        for (int i = 0; i < K; ++i) if (fixed[(size_t) i]) { for (int j = 0; j < K; ++j) A[i][j] = A[j][i] = 0.0; A[i][i] = 1.0; y[i] = 0.0; }
        // Гаусс 4×4
        for (int c = 0; c < K; ++c)
        {
            int piv = c; for (int r2 = c + 1; r2 < K; ++r2) if (std::abs (A[r2][c]) > std::abs (A[piv][c])) piv = r2;
            for (int j = 0; j < K; ++j) std::swap (A[c][j], A[piv][j]); std::swap (y[c], y[piv]);
            for (int r2 = c + 1; r2 < K; ++r2) { const double m = A[r2][c] / A[c][c]; for (int j = c; j < K; ++j) A[r2][j] -= m * A[c][j]; y[r2] -= m * y[c]; }
        }
        double d[K] {};
        for (int c = K - 1; c >= 0; --c) { double sum = y[c]; for (int j = c + 1; j < K; ++j) sum -= A[c][j] * d[j]; d[c] = sum / A[c][c]; }
        for (int k = 0; k < K; ++k)
        {
            g[(size_t) k] = (float) (g[(size_t) k] + d[k]);
            if (std::abs (g[(size_t) k]) > 6.0f) { g[(size_t) k] = jlimit (-6.0f, 6.0f, g[(size_t) k]); fixed[(size_t) k] = true; }
        }
    }
    for (auto& x : g) x = std::round (x * 10.0f) / 10.0f;

    const float lufsIn = ta.inLufs() > -70.0f ? ta.inLufs() : -20.0f;

    auto set = [this] (const char* id, float v)
    {
        auto* prm = proc.apvts.getParameter (id);
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
        prm->endChangeGesture();
    };

    const float lowG = g[0], lmG = g[1], midG = g[2], highG = g[3];
    const bool cutSub = avgDev (31.5f, 50.0f) > 3.0f;
    const bool monoBass = ta.outAn.lowSideDb() > -15.0f || ta.inAn.lowSideDb() > -15.0f;
    // Gain staging: спершу виводимо вхід на робочий рівень -18 LUFS, далі компресор і лімітер від нього
    constexpr float workLufs = -18.0f;
    const float inGainDb = jlimit (-24.0f, 24.0f, workLufs - lufsIn);
    // Лімітер дотягує до цілі стрімінгу (компресор дає ще ~1 дБ щільності)
    const float limGain = jlimit (0.0f, 18.0f, ta.loudTargetLufs() - workLufs - 1.0f);

    using namespace ParamIDs;
    set (eqOn, 1.0f);
    set (hpfFreq, cutSub ? 30.0f : 10.0f);
    set (lowFreq, 100.0f);  set (lowGain, lowG);
    set (lmFreq, lmHz);     set (lmGain, lmG);
    set (midFreq, midHz);   set (midQ, 1.0f); set (midGain, midG);
    set (highFreq, 8000.0f); set (highGain, highG);

    set (compOn, 1.0f); set (ratio, 2.0f); set (attack, 20.0f); set (release, 150.0f); set (knee, 6.0f);
    set (scHpf, 100.0f); set (compAuto, 1.0f); set (compMix, 100.0f); set (makeup, 0.0f);
    set (inGain, inGainDb);
    set (threshold, workLufs + 4.0f);

    set (widthOn, 1.0f);
    if (monoBass) set (ParamIDs::monoBass, 120.0f);

    set (limOn, 1.0f); set (ceiling, ta.loudTargetLufs() > -14.0f ? -2.0f : -1.0f);   // Spotify: гучніше -14 LUFS — стеля -2 dBTP
    set (limRel, 60.0f); set (ParamIDs::limGain, limGain);
    set (gainMatch, 0.0f);
    proc.resetMeters();

    String msg = "Assist: input " + String (inGainDb, 1) + " dB, low " + String (lowG, 1) + " dB, ";
    if (lmG != 0.0f) msg << String (lmG, 1) << " dB @ " << String (roundToInt (lmHz)) << " Hz, ";
    if (midG != 0.0f) msg << String (midG, 1) << " dB @ " << String (roundToInt (midHz)) << " Hz, ";
    msg << "high " << String (highG, 1) << " dB, limiter +" << String (limGain, 1) << " dB → " << String (roundToInt (ta.loudTargetLufs())) << " LUFS";
    if (monoBass) msg << ", mono bass";
    assistMessage = msg;
    assistMessageFrames = 400;
    refineSteps = 3;
    ta.updateReport();
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
    if (ta.targetGenreIdx() != 0 && ! ta.hasReference())
        dashedStroke (pathOf (an::normalise (an::targetCurve (ta.targetYear(), 0))), Theme::muted.withAlpha (0.45f), 1.0f);

    const auto tgt = an::normalise (ta.targetSpectrum());
    const bool haveSong = ta.inAn.songSeconds() >= 2.0;

    // «Зараз» — ледь помітно, лише щоб бачити, що аналізатор слухає

    if (haveSong)
    {
        const auto mix = an::normalise (ta.predicted());
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
    g.drawText (ta.hasReference() ? "REFERENCE: " + ta.referenceName().toUpperCase()
                                  : "TARGET: " + String (an::genreName (ta.targetGenreIdx())).toUpperCase() + " " + an::decadeName (ta.targetYear()),
                leg.removeFromLeft (ta.hasReference() ? 260.0f : 170.0f), Justification::centredLeft);
    if (ta.targetGenreIdx() != 0 && ! ta.hasReference())
    {
        g.setColour (Theme::muted);
        g.drawText ("ROCK " + an::decadeName (ta.targetYear()), leg.removeFromLeft (90.0f), Justification::centredLeft);
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
    const double sec = ta.inAn.songSeconds();
    g.drawText (sec >= 1.0 ? "listened " + mmss (sec) + " of the track  |  target " + String (roundToInt (ta.loudTargetLufs())) + " LUFS, -1 dBTP" : String ("press play: listens to the whole track"),
                Rectangle<int> (200, 10, getWidth() - 220, 24), Justification::centredRight);

    auto v = verdictArea.toFloat();
    const auto& report = ta.report;
    const bool have = sec >= 2.0 && ! report.verdicts.empty();

    g.setColour (Theme::muted);
    g.setFont (FontOptions (10.0f, Font::bold));
    g.drawText (ta.hasReference() ? "MATCH TO REFERENCE" : "MATCH TO TARGET", v.removeFromTop (14.0f), Justification::centredLeft);
    auto line = v.removeFromTop (30.0f);
    const int pct = report.matchPercent;
    g.setColour (! have ? Theme::muted : pct >= 85 ? Theme::good : pct >= 65 ? Theme::warn : Theme::hot);
    g.setFont (FontOptions (26.0f, Font::bold));
    g.drawText (have ? String (pct) + "%" : String ("--"), line.removeFromLeft (80.0f), Justification::centredLeft);
    g.setColour (Theme::text);
    g.setFont (FontOptions (12.0f));
    g.drawFittedText (have ? "sounds most like: " + ta.soundsLike : String ("play the song (ideally all of it)"),
                      line.toNearestInt(), Justification::centredLeft, 1, 0.85f);
    if (have && ta.character.isNotEmpty())
    {
        g.setColour (Theme::accent2);
        g.setFont (FontOptions (11.5f, Font::bold));
        g.drawFittedText (ta.character, v.removeFromTop (30.0f).toNearestInt(), Justification::centredLeft, 2, 0.85f);
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
    genreSel.setBounds (controls.removeFromLeft (300).withTrimmedTop (10));
    controls.removeFromLeft (6);
    decadeSel.setBounds (controls);
    right.removeFromTop (4);
    {
        auto row = right.removeFromTop (42);
        loudSel.setBounds (row.removeFromLeft (220));
        albumButton.setBounds (row.removeFromRight (120).withTrimmedTop (14).withTrimmedBottom (2));
    }
    right.removeFromTop (6);
    auto buttons = right.removeFromBottom (26);
    resetButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 3));
    buttons.removeFromLeft (6);
    assistButton.setBounds (buttons);
    right.removeFromBottom (6);
    verdictArea = right;
}
