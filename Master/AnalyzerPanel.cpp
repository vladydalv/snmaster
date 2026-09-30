#include "AnalyzerPanel.h"

using namespace juce;
using namespace snui;

AnalyzerPanel::AnalyzerPanel (SpacenerdMasterProcessor& p)
    : proc (p),
      genreSel (p.apvts, ParamIDs::targetGenre),
      decadeSel (p.apvts, ParamIDs::targetDecade, "Decade")
{
    fsUsed = p.getSampleRate() > 0 ? p.getSampleRate() : 48000.0;
    inAn.prepare (fsUsed);
    outAn.prepare (fsUsed);

    learnButton.setTooltip ("Play the loudest, most typical part of the song for ~10 s");
    learnButton.onClick = [this]
    {
        learnLufs.clear();
        inAn.setLearning (! inAn.isLearning());
        assistMessage.clear();
    };
    assistButton.setTooltip ("Set EQ, compressor, limiter and mono bass towards the target");
    assistButton.onClick = [this] { applyAssist(); };
    assistButton.setEnabled (false);

    for (auto* c : std::initializer_list<Component*> { &genreSel, &decadeSel, &learnButton, &assistButton })
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

void AnalyzerPanel::feedForTest()
{
    proc.inFifo.pull  ([this] (const float* l, const float* r, int n) { inAn.push (l, r, n); });
    proc.outFifo.pull ([this] (const float* l, const float* r, int n)
    {
        outAn.push (l, r, n);
        for (int i = 0; i < n; ++i) tickPeak = std::max (tickPeak, std::max (std::abs (l[i]), std::abs (r[i])));
    });
}

void AnalyzerPanel::tick()
{
    if (proc.getSampleRate() > 0 && proc.getSampleRate() != fsUsed)
    {
        fsUsed = proc.getSampleRate();
        inAn.prepare (fsUsed); outAn.prepare (fsUsed);
    }
    feedForTest();

    if (inAn.isLearning())
    {
        const float st = proc.inLoudness.shortTerm.load();
        if (st > -70.0f) learnLufs.push_back (st);
        const double fs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
        const int needed = (int) (10.0 * fs / (an::Analyzer::size / 4));
        const int pct = jlimit (0, 100, inAn.learnedFrames() * 100 / std::max (1, needed));
        learnButton.setButtonText ("LEARNING " + String (pct) + "%");
        if (inAn.learnedFrames() >= needed)
        {
            inAn.setLearning (false);
            assistButton.setEnabled (true);
            assistMessage = "Learned. Press ASSIST to set the chain.";
            assistMessageFrames = 240;
        }
    }
    else
        learnButton.setButtonText (assistButton.isEnabled() ? "RE-LEARN" : "LEARN");

    recentPeaks[(size_t) peakPos] = tickPeak;
    peakPos = (peakPos + 1) % (int) recentPeaks.size();
    tickPeak = 0.0f;

    if (++frameCounter % 6 == 0 && outAn.hasSignal())
    {
        // Останні ~3 с, а не від початку відтворення: результат повороту ручки видно одразу
        const float lufs = proc.loudness.shortTerm.load();
        const float tp = sn::gainToDb (*std::max_element (recentPeaks.begin(), recentPeaks.end()));
        report = an::analyse (outAn.average(), lufs, tp, outAn.correlation(), outAn.lowSideDb(), targetYear(), targetGenreIdx());
    }
    if (assistMessageFrames > 0) --assistMessageFrames;
    repaint();
}

void AnalyzerPanel::applyAssist()
{
    if (inAn.learnedFrames() < 8) return;

    const auto tgt = an::normalise (an::targetCurve (targetYear(), targetGenreIdx()));
    const auto in = an::normalise (inAn.learned());
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

    float lufsIn = -20.0f;
    if (! learnLufs.empty())
    {
        double s = 0; for (auto v : learnLufs) s += v;
        lufsIn = (float) (s / (double) learnLufs.size());
    }
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
}

void AnalyzerPanel::drawSpectrum (Graphics& g, Rectangle<float> r)
{
    g.setColour (Theme::bg.withAlpha (0.5f));
    g.fillRoundedRectangle (r, 6.0f);

    constexpr float fLo = 25.0f, fHi = 20000.0f, dbLo = -24.0f, dbHi = 12.0f;
    auto xOf = [&] (float f) { return r.getX() + r.getWidth() * std::log (f / fLo) / std::log (fHi / fLo); };
    auto yOf = [&] (float db) { return r.getBottom() - r.getHeight() * (jlimit (dbLo, dbHi, db) - dbLo) / (dbHi - dbLo); };

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
    g.setColour (Theme::border.withAlpha (0.8f));
    g.drawHorizontalLine (roundToInt (yOf (0.0f)), r.getX(), r.getRight());

    auto pathOf = [&] (const an::Spectrum& s)
    {
        Path p;
        for (int b = 0; b < an::kBands; ++b)
        {
            const Point<float> pt (xOf (an::bandHz[(size_t) b]), yOf (s[(size_t) b]));
            if (b == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        return p;
    };

    const auto tgt = an::normalise (an::targetCurve (targetYear(), targetGenreIdx()));
    if (outAn.hasSignal())
    {
        const auto mix = an::normalise (outAn.average());
        // Відхилення: заливка між міксом і ціллю
        for (int b = 0; b < an::kBands; ++b)
        {
            const float d = mix[(size_t) b] - tgt[(size_t) b];
            if (std::abs (d) < 2.5f) continue;
            const float x = xOf (an::bandHz[(size_t) b]);
            const float y0 = yOf (tgt[(size_t) b]), y1 = yOf (mix[(size_t) b]);
            g.setColour ((d > 0 ? Theme::hot : Theme::accent2).withAlpha (std::abs (d) > 4.5f ? 0.35f : 0.2f));
            g.fillRect (Rectangle<float> (x - 5.0f, std::min (y0, y1), 10.0f, std::abs (y1 - y0)));
        }
        auto mp = pathOf (mix);
        g.setColour (Theme::accent);
        g.strokePath (mp, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    auto tp = pathOf (tgt);
    Path dashed;
    const float dashes[] { 5.0f, 4.0f };
    PathStrokeType (1.6f).createDashedStroke (dashed, tp, dashes, 2);
    g.setColour (Theme::accent2.withAlpha (0.9f));
    g.fillPath (dashed);

    // Легенда
    g.setFont (FontOptions (10.0f, Font::bold));
    g.setColour (Theme::accent);
    g.drawText ("YOUR MIX", Rectangle<float> (r.getX() + 8.0f, r.getY() + 6.0f, 70.0f, 12.0f), Justification::centredLeft);
    g.setColour (Theme::accent2);
    g.drawText ("TARGET: " + String (an::genreName (targetGenreIdx())).toUpperCase() + " " + an::decadeName (targetYear()),
                Rectangle<float> (r.getX() + 80.0f, r.getY() + 6.0f, 220.0f, 12.0f), Justification::centredLeft);
}

void AnalyzerPanel::paint (Graphics& g)
{
    drawCard (g, getLocalBounds().toFloat(), "MIX ANALYZER");
    drawSpectrum (g, graphArea.toFloat());

    auto v = verdictArea.toFloat();
    // Схожість з ціллю
    const bool sig = outAn.hasSignal();
    g.setColour (Theme::muted);
    g.setFont (FontOptions (10.0f, Font::bold));
    g.drawText ("MATCH", v.removeFromTop (14.0f), Justification::centredLeft);
    auto line = v.removeFromTop (30.0f);
    const int pct = report.matchPercent;
    g.setColour (! sig ? Theme::muted : pct >= 85 ? Theme::good : pct >= 65 ? Theme::warn : Theme::hot);
    g.setFont (FontOptions (26.0f, Font::bold));
    g.drawText (sig ? String (pct) + "%" : String ("--"), line.removeFromLeft (80.0f), Justification::centredLeft);
    g.setColour (Theme::muted);
    g.setFont (FontOptions (12.0f));
    g.drawText (sig ? "to " + String (an::genreName (targetGenreIdx())) + " " + an::decadeName (targetYear()) : String ("play the mix..."),
                line, Justification::centredLeft);
    v.removeFromTop (6.0f);

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
    if (! sig)
        drawLine ("Press play: the analyzer listens to the Master output", Theme::muted,
                  "Only EQ changes the curve. COMP / LIMITER change loudness and punch.");
    if (sig)
    {
        int shown = 0;
        for (auto& vd : report.verdicts)
        {
            if (++shown > 4) break;
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
    learnButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 3));
    buttons.removeFromLeft (6);
    assistButton.setBounds (buttons);
    right.removeFromBottom (6);
    verdictArea = right;
}
