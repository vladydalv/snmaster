#include "PluginEditor.h"

using namespace juce;
using namespace snui;

//==============================================================================
MeterPanel::MeterPanel (SpacenerdMasterProcessor& p)
    : proc (p),
      inKnob  (p.apvts, ParamIDs::inGain,  "Input",  true, Theme::accent2),
      outKnob (p.apvts, ParamIDs::outGain, "Output", false, Theme::accent2)
{
    resetButton.onClick = [this] { proc.resetMeters(); };
    resetButton.setTooltip ("Reset integrated loudness and true peak");
    addAndMakeVisible (resetButton);
    addAndMakeVisible (inKnob);
    addAndMakeVisible (outKnob);
}

void MeterPanel::update()
{
    for (size_t ch = 0; ch < 2; ++ch)
    {
        in[ch].feed  (sn::gainToDb (proc.inPeak[ch].take()),  false);
        out[ch].feed (sn::gainToDb (proc.outPeak[ch].take()), false);
    }
    comp.feed (proc.compGr.take(), true);
    lim.feed  (proc.limGr.take(),  true);

    lufsM = proc.loudness.momentary.load();
    lufsS = proc.loudness.shortTerm.load();
    lufsI = proc.loudness.integrated.load();
    tpMax = sn::gainToDb (proc.truePeakMax.load());
    repaint (barsArea.getUnion (lufsArea).expanded (0, 18));
}

void MeterPanel::paint (Graphics& g)
{
    drawCard (g, getLocalBounds().toFloat(), "METERS");

    auto bars = barsArea.toFloat();
    constexpr float labelsH = 14.0f;
    auto meterR = bars.withTrimmedBottom (labelsH);
    const float groupW = meterR.getWidth() / 3.0f;
    constexpr float barW = 10.0f, gap = 4.0f;

    auto group = [&] (int idx, const MeterBar& a, const MeterBar& b, bool isGr, const String& name)
    {
        const float cx = meterR.getX() + groupW * ((float) idx + 0.5f);
        a.draw (g, { cx - barW - gap * 0.5f, meterR.getY(), barW, meterR.getHeight() }, isGr);
        b.draw (g, { cx + gap * 0.5f,        meterR.getY(), barW, meterR.getHeight() }, isGr);
        g.setColour (Theme::muted);
        g.setFont (FontOptions (9.5f, Font::bold));
        g.drawText (name, Rectangle<float> (cx - groupW * 0.5f, meterR.getBottom() + 2.0f, groupW, labelsH - 2.0f),
                    Justification::centred);
    };
    group (0, in[0], in[1], false, "IN");
    group (1, comp, lim, true, "GR C / L");
    group (2, out[0], out[1], false, "OUT");

    // Числа над смугами: GR лімітера і true peak виходу
    g.setFont (FontOptions (9.5f, Font::bold));
    g.setColour (Theme::muted);
    g.drawText (lim.hold > 0.05f ? "-" + String (lim.hold, 1) : String ("0.0"),
                Rectangle<float> (meterR.getX() + groupW, meterR.getY() - 15.0f, groupW, 12.0f), Justification::centred);
    g.setColour (tpMax > -0.05f ? Theme::hot : Theme::muted);
    g.drawText (tpMax > -99.0f ? "TP " + String (tpMax, 1) : String ("TP --"),
                Rectangle<float> (meterR.getRight() - groupW - 6.0f, meterR.getY() - 15.0f, groupW + 12.0f, 12.0f),
                Justification::centred);

    // LUFS
    auto la = lufsArea.toFloat();
    auto row = [&] (const String& tag, float v, bool big)
    {
        auto line = la.removeFromTop (big ? 26.0f : 18.0f);
        g.setColour (Theme::muted);
        g.setFont (FontOptions (10.0f, Font::bold));
        g.drawText (tag, line.removeFromLeft (70.0f), Justification::centredLeft);
        g.setColour (big ? Theme::accent2 : Theme::text);
        g.setFont (FontOptions (big ? 20.0f : 13.0f, Font::bold));
        g.drawText (v > -99.0f ? String (v, 1) : String ("--"), line, Justification::centredRight);
    };
    row ("INTEGRATED", lufsI, true);
    row ("SHORT-TERM", lufsS, false);
    row ("MOMENTARY", lufsM, false);
}

void MeterPanel::resized()
{
    auto r = getLocalBounds().withTrimmedTop (44).reduced (14, 8);
    auto knobs = r.removeFromBottom (100);
    const int kw = knobs.getWidth() / 2;
    inKnob.setBounds (knobs.removeFromLeft (kw));
    outKnob.setBounds (knobs);

    r.removeFromBottom (6);
    auto lufs = r.removeFromBottom (86);
    resetButton.setBounds (lufs.removeFromBottom (20).removeFromRight (64));
    lufsArea = lufs.withTrimmedBottom (4);

    r.removeFromTop (18);
    barsArea = r;
}

//==============================================================================
MainContent::MainContent (SpacenerdMasterProcessor& p)
    : meters (p),
      analyzer (p),
      proc (p),
      presetBox (p),
      matchButton (p.apvts, ParamIDs::gainMatch, "GAIN MATCH", Theme::gr),
      eq    (p.apvts, "EQ",         ParamIDs::eqOn,    4),
      comp  (p.apvts, "COMPRESSOR", ParamIDs::compOn,  4),
      sat   (p.apvts, "SATURATION", ParamIDs::satOn,   2),
      width (p.apvts, "STEREO",     ParamIDs::widthOn, 2),
      lim   (p.apvts, "LIMITER",    ParamIDs::limOn,   2)
{
    auto& s = p.apvts;

    eq.knob (s, ParamIDs::hpfFreq,  "Low Cut");
    eq.knob (s, ParamIDs::lowFreq,  "Low");
    eq.knob (s, ParamIDs::midFreq,  "Mid");
    eq.knob (s, ParamIDs::highFreq, "High");
    eq.knob (s, ParamIDs::midQ,     "Mid Q");
    eq.knob (s, ParamIDs::lowGain,  "Low Gain",  true);
    eq.knob (s, ParamIDs::midGain,  "Mid Gain",  true);
    eq.knob (s, ParamIDs::highGain, "High Gain", true);

    comp.knob (s, ParamIDs::threshold, "Threshold");
    comp.knob (s, ParamIDs::ratio,     "Ratio");
    comp.knob (s, ParamIDs::attack,    "Attack");
    comp.knob (s, ParamIDs::release,   "Release");
    comp.knob (s, ParamIDs::knee,      "Knee");
    comp.knob (s, ParamIDs::scHpf,     "SC Filter");
    comp.knob (s, ParamIDs::makeup,    "Makeup");
    comp.knob (s, ParamIDs::compMix,   "Mix");
    comp.add (std::make_unique<Component>(), 3, 26);
    comp.add (std::make_unique<PillToggle> (s, ParamIDs::compAuto, "AUTO REL"), 1, 24);

    sat.add (std::make_unique<Segmented> (s, ParamIDs::satType), 2, 34);
    sat.knob (s, ParamIDs::drive,  "Drive");
    sat.knob (s, ParamIDs::satMix, "Mix");

    width.knob (s, ParamIDs::width,    "Width", true);
    width.knob (s, ParamIDs::monoBass, "Mono Bass");

    lim.knob (s, ParamIDs::limGain, "Gain");
    lim.knob (s, ParamIDs::ceiling, "Ceiling");
    lim.knob (s, ParamIDs::limRel,  "Release");

    matchButton.setTooltip ("Gain Match: output level follows input loudness, so Bypass compares at equal loudness. Turn off before bouncing.");

    for (auto* c : std::initializer_list<Component*> { &presetBox, &matchButton, &analyzer, &eq, &comp, &sat, &width, &lim, &meters })
        addAndMakeVisible (c);
}

void MainContent::tick()
{
    meters.update();
    analyzer.tick();
    presetBox.sync();
    const float m = proc.matchDb.load();
    if (std::abs (m - shownMatch) > 0.05f) { shownMatch = m; repaint (0, 0, getWidth(), 56); }
}

void MainContent::paint (Graphics& g)
{
    g.fillAll (Theme::bg);
    drawHeader (g, getWidth(), "MASTER", "EQ > COMP > SAT > STEREO > LIMIT   |   4x oversampled");

    if (matchButton.getToggleState())
    {
        g.setColour (Theme::gr);
        g.setFont (FontOptions (12.0f, Font::bold));
        g.drawText (String (shownMatch, 1) + " dB", matchButton.getRight() + 8, 14, 70, 28, Justification::centredLeft);
    }
}

void MainContent::resized()
{
    presetBox.setBounds (300, 14, 240, 28);
    matchButton.setBounds (556, 16, 96, 24);

    auto r = getLocalBounds().withTrimmedTop (56).reduced (16, 0).withTrimmedBottom (16);
    constexpr int gap = 10;
    analyzer.setBounds (r.removeFromTop (SpacenerdMasterEditor::analyzerH));
    r.removeFromTop (gap);

    eq.setBounds   (r.removeFromLeft (280)); r.removeFromLeft (gap);
    comp.setBounds (r.removeFromLeft (280)); r.removeFromLeft (gap);

    auto col = r.removeFromLeft (172); r.removeFromLeft (gap);
    sat.setBounds (col.removeFromTop (224));
    col.removeFromTop (gap);
    width.setBounds (col);

    lim.setBounds (r.removeFromLeft (152)); r.removeFromLeft (gap);
    meters.setBounds (r);
}

//==============================================================================
SpacenerdMasterEditor::SpacenerdMasterEditor (SpacenerdMasterProcessor& p)
    : AudioProcessorEditor (p), content (p)
{
    addAndMakeVisible (content);
    setLookAndFeel (&lnf);          // після додавання вмісту, щоб оновились усі нащадки
    content.setBounds (0, 0, baseW, baseH);

    setResizable (true, true);
    setResizeLimits (baseW * 3 / 4, baseH * 3 / 4, baseW * 2, baseH * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseW / (double) baseH);
    setSize (baseW, baseH);

    startTimerHz (30);
}

SpacenerdMasterEditor::~SpacenerdMasterEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SpacenerdMasterEditor::paint (Graphics& g) { g.fillAll (Theme::bg); }

void SpacenerdMasterEditor::resized()
{
    content.setTransform (AffineTransform::scale ((float) getWidth() / (float) baseW));
}
