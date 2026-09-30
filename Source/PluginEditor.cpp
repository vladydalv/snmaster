#include "PluginEditor.h"

using namespace juce;

//==============================================================================
Knob::Knob (AudioProcessorValueTreeState& state, const String& paramId, const String& title,
            bool bipolar, Colour colour)
    : attachment (state, paramId, slider)
{
    label.setText (title.toUpperCase(), dontSendNotification);
    label.setJustificationType (Justification::centred);
    label.setFont (FontOptions (10.0f, Font::bold));
    label.setColour (Label::textColourId, Theme::muted);
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);

    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::TextBoxBelow, false, 72, 16);
    slider.setColour (Slider::rotarySliderFillColourId, colour);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (220);
    slider.setVelocityBasedMode (false);
    slider.getProperties().set ("bipolar", bipolar);

    if (auto* param = state.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

    slider.setPopupMenuEnabled (false);
    addAndMakeVisible (slider);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (14));
    slider.setBounds (r);
}

//==============================================================================
void PowerButton::paintButton (Graphics& g, bool over, bool)
{
    const auto r = getLocalBounds().toFloat().reduced (3.0f);
    const auto c = r.getCentre();
    const float rad = jmin (r.getWidth(), r.getHeight()) * 0.5f;
    const auto col = getToggleState() ? Theme::accent : Theme::muted.withAlpha (over ? 0.9f : 0.6f);

    if (getToggleState())
    {
        g.setColour (Theme::accent.withAlpha (0.18f));
        g.fillEllipse (Rectangle<float> (rad * 2.6f, rad * 2.6f).withCentre (c));
    }

    Path p;
    p.addCentredArc (c.x, c.y, rad * 0.72f, rad * 0.72f, 0.0f,
                     MathConstants<float>::pi * 0.22f, MathConstants<float>::pi * 1.78f, true);
    p.startNewSubPath (c.x, c.y - rad * 0.95f);
    p.lineTo (c.x, c.y - rad * 0.2f);
    g.setColour (col);
    g.strokePath (p, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
}

//==============================================================================
Section::Section (AudioProcessorValueTreeState& state, const String& t, const String& powerId, int cols)
    : title (t), columns (cols)
{
    power.setTooltip ("On / Off");
    addAndMakeVisible (power);
    powerAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (state, powerId, power);
    power.onStateChange = [this] { updateAlpha(); };
}

Knob& Section::add (std::unique_ptr<Knob> k)
{
    addAndMakeVisible (*k);
    knobs.push_back (std::move (k));
    updateAlpha();
    return *knobs.back();
}

void Section::updateAlpha()
{
    const float a = power.getToggleState() ? 1.0f : 0.38f;
    for (auto& k : knobs) k->setAlpha (a);
    repaint();
}

void Section::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (Theme::card);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (Theme::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

    g.setColour (power.getToggleState() ? Theme::text : Theme::muted);
    g.setFont (FontOptions (12.0f, Font::bold));
    g.drawText (title, 16, 10, getWidth() - 60, 18, Justification::centredLeft);

    g.setColour (Theme::border);
    g.fillRect (12.0f, 36.0f, r.getWidth() - 24.0f, 1.0f);
}

void Section::resized()
{
    power.setBounds (getWidth() - 36, 7, 24, 24);

    auto area = getLocalBounds().withTrimmedTop (40).reduced (12, 6);
    const int rows = (int) (knobs.size() + (size_t) columns - 1) / columns;
    if (rows == 0) return;

    const int cellW = area.getWidth() / columns;
    const int cellH = jmin (104, area.getHeight() / rows);
    const int gapY = rows > 1 ? (area.getHeight() - cellH * rows) / (rows + 1) : (area.getHeight() - cellH) / 2;

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const int row = (int) i / columns, col = (int) i % columns;
        knobs[i]->setBounds (area.getX() + col * cellW,
                             area.getY() + gapY + row * (cellH + gapY),
                             cellW, cellH);
    }
}

//==============================================================================
MeterPanel::MeterPanel (SpacenerdMasterProcessor& p)
    : proc (p),
      inKnob  (p.apvts, ParamIDs::inGain,  "Input",  true, Theme::accent2),
      outKnob (p.apvts, ParamIDs::outGain, "Output", false, Theme::accent2)
{
    resetButton.onClick = [this] { proc.loudness.requestReset(); };
    resetButton.setTooltip ("Reset integrated loudness");
    addAndMakeVisible (resetButton);
    addAndMakeVisible (inKnob);
    addAndMakeVisible (outKnob);
}

void MeterPanel::feed (Bar& b, float db, bool isGr)
{
    // Плавне падіння, миттєвий зліт + утримання піку
    if (isGr)
        b.value = db > b.value ? db : b.value + (db - b.value) * 0.25f;
    else
        b.value = db > b.value ? db : jmax (db, b.value - 1.2f);

    if (b.value >= b.hold) { b.hold = b.value; b.holdFrames = 45; }
    else if (--b.holdFrames <= 0) b.hold = jmax (b.value, b.hold - 1.5f);
}

void MeterPanel::update()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        feed (in[(size_t) ch],  sn::gainToDb (proc.inPeak[(size_t) ch].take()),  false);
        feed (out[(size_t) ch], sn::gainToDb (proc.outPeak[(size_t) ch].take()), false);
    }
    feed (comp, proc.compGr.take(), true);
    feed (lim,  proc.limGr.take(),  true);

    lufsM = proc.loudness.momentary.load();
    lufsS = proc.loudness.shortTerm.load();
    lufsI = proc.loudness.integrated.load();
    repaint (barsArea.getUnion (lufsArea));
}

void MeterPanel::drawBar (Graphics& g, Rectangle<float> r, const Bar& b, bool isGr) const
{
    g.setColour (Theme::track.withAlpha (0.6f));
    g.fillRoundedRectangle (r, 2.0f);

    if (isGr)
    {
        // 0…20 dB зверху вниз
        const float frac = jlimit (0.0f, 1.0f, b.value / 20.0f);
        if (frac > 0.0f)
        {
            g.setColour (Theme::gr);
            g.fillRoundedRectangle (r.withHeight (r.getHeight() * frac), 2.0f);
        }
        return;
    }

    auto toFrac = [] (float db) { return jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f); };
    const float frac = toFrac (b.value);
    if (frac > 0.0f)
    {
        auto fill = r.withTrimmedTop (r.getHeight() * (1.0f - frac));
        ColourGradient grad (Theme::hot, r.getX(), r.getY(), Theme::good, r.getX(), r.getBottom(), false);
        grad.addColour (1.0 - (double) toFrac (-9.0f), Theme::warn);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fill, 2.0f);
    }

    const float hy = r.getBottom() - r.getHeight() * toFrac (b.hold);
    if (b.hold > -48.0f)
    {
        g.setColour (b.hold > -0.1f ? Theme::hot : Theme::text.withAlpha (0.8f));
        g.fillRect (r.getX(), hy, r.getWidth(), 1.5f);
    }
}

void MeterPanel::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (Theme::card);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (Theme::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

    g.setColour (Theme::text);
    g.setFont (FontOptions (12.0f, Font::bold));
    g.drawText ("METERS", 16, 10, 120, 18, Justification::centredLeft);
    g.setColour (Theme::border);
    g.fillRect (12.0f, 36.0f, r.getWidth() - 24.0f, 1.0f);

    // Смуги: IN L/R, GR COMP/LIM, OUT L/R
    auto bars = barsArea.toFloat();
    const auto labelsH = 14.0f;
    auto meterR = bars.withTrimmedBottom (labelsH);
    const float groupW = meterR.getWidth() / 3.0f;
    const float barW = 10.0f, gap = 4.0f;

    auto group = [&] (int idx, const Bar& a, const Bar& b, bool isGr, const String& name)
    {
        const float cx = meterR.getX() + groupW * ((float) idx + 0.5f);
        drawBar (g, { cx - barW - gap * 0.5f, meterR.getY(), barW, meterR.getHeight() }, a, isGr);
        drawBar (g, { cx + gap * 0.5f,        meterR.getY(), barW, meterR.getHeight() }, b, isGr);
        g.setColour (Theme::muted);
        g.setFont (FontOptions (9.5f, Font::bold));
        g.drawText (name, Rectangle<float> (cx - groupW * 0.5f, meterR.getBottom() + 2.0f, groupW, labelsH - 2.0f),
                    Justification::centred);
    };
    group (0, in[0], in[1], false, "IN");
    group (1, comp, lim, true, "GR C / L");
    group (2, out[0], out[1], false, "OUT");

    // Числове значення виходу
    g.setColour (Theme::muted);
    g.setFont (FontOptions (9.5f));
    const float outDb = jmax (out[0].hold, out[1].hold);
    g.drawText (outDb > -99.0f ? String (outDb, 1) : String ("-inf"),
                Rectangle<float> (meterR.getRight() - groupW, meterR.getY() - 14.0f, groupW, 12.0f), Justification::centred);
    g.drawText (lim.hold > 0.05f ? "-" + String (lim.hold, 1) : String ("0.0"),
                Rectangle<float> (meterR.getX() + groupW, meterR.getY() - 14.0f, groupW, 12.0f), Justification::centred);

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

    r.removeFromTop (14);
    barsArea = r;
}

//==============================================================================
MainContent::MainContent (SpacenerdMasterProcessor& p)
    : meters (p),
      proc (p),
      eq    (p.apvts, "EQ",         ParamIDs::eqOn,    4),
      comp  (p.apvts, "COMPRESSOR", ParamIDs::compOn,  4),
      sat   (p.apvts, "SATURATION", ParamIDs::satOn,   2),
      width (p.apvts, "STEREO",     ParamIDs::widthOn, 2),
      lim   (p.apvts, "LIMITER",    ParamIDs::limOn,   2)
{
    auto& s = p.apvts;
    auto k = [&s] (const char* id, const char* name, bool bipolar = false)
    {
        return std::make_unique<Knob> (s, id, name, bipolar);
    };

    eq.add (k (ParamIDs::hpfFreq,  "Low Cut"));
    eq.add (k (ParamIDs::lowFreq,  "Low"));
    eq.add (k (ParamIDs::midFreq,  "Mid"));
    eq.add (k (ParamIDs::highFreq, "High"));
    eq.add (k (ParamIDs::midQ,     "Mid Q"));
    eq.add (k (ParamIDs::lowGain,  "Low Gain",  true));
    eq.add (k (ParamIDs::midGain,  "Mid Gain",  true));
    eq.add (k (ParamIDs::highGain, "High Gain", true));

    comp.add (k (ParamIDs::threshold, "Threshold"));
    comp.add (k (ParamIDs::ratio,     "Ratio"));
    comp.add (k (ParamIDs::attack,    "Attack"));
    comp.add (k (ParamIDs::release,   "Release"));
    comp.add (k (ParamIDs::knee,      "Knee"));
    comp.add (k (ParamIDs::scHpf,     "SC Filter"));
    comp.add (k (ParamIDs::makeup,    "Makeup"));
    comp.add (k (ParamIDs::compMix,   "Mix"));

    sat.add (k (ParamIDs::drive,  "Drive"));
    sat.add (k (ParamIDs::satMix, "Mix"));

    width.add (k (ParamIDs::width,    "Width", true));
    width.add (k (ParamIDs::monoBass, "Mono Bass"));

    lim.add (k (ParamIDs::limGain, "Gain"));
    lim.add (k (ParamIDs::ceiling, "Ceiling"));
    lim.add (k (ParamIDs::limRel,  "Release"));

    for (int i = 0; i < p.getNumPrograms(); ++i)
        presetBox.addItem (p.getProgramName (i), i + 1);
    presetBox.setTooltip ("Factory presets");
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx >= 0 && idx != proc.getCurrentProgram())
            proc.setCurrentProgram (idx);
    };
    syncPreset();
    addAndMakeVisible (presetBox);

    for (auto* c : std::initializer_list<Component*> { &eq, &comp, &sat, &width, &lim, &meters })
        addAndMakeVisible (c);
}

void MainContent::syncPreset()
{
    const int id = proc.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != id)
        presetBox.setSelectedId (id, dontSendNotification);
}

void MainContent::paint (Graphics& g)
{
    g.fillAll (Theme::bg);

    // Шапка: м'яке світіння за логотипом
    g.setGradientFill (ColourGradient (Theme::accent.withAlpha (0.14f), 120.0f, 28.0f,
                                       Colours::transparentBlack, 520.0f, 28.0f, true));
    g.fillRect (0, 0, getWidth(), 56);

    g.setColour (Theme::text);
    g.setFont (FontOptions (19.0f, Font::bold));
    g.drawText ("SPACENERD", 20, 14, 130, 28, Justification::centredLeft);
    g.setColour (Theme::accent);
    g.setFont (FontOptions (19.0f));
    g.drawText ("MASTER", 136, 14, 120, 28, Justification::centredLeft);

    g.setColour (Theme::muted);
    g.setFont (FontOptions (11.0f));
    g.drawText ("EQ  >  COMP  >  SAT  >  STEREO  >  LIMIT     4x oversampled",
                getWidth() - 440, 14, 420, 28, Justification::centredRight);

    g.setColour (Theme::muted);
    g.setFont (FontOptions (10.0f, Font::bold));
    g.drawText ("PRESET", 240, 14, 56, 28, Justification::centredRight);
}

void MainContent::resized()
{
    presetBox.setBounds (300, 14, 240, 28);

    auto r = getLocalBounds().withTrimmedTop (56).reduced (16, 0).withTrimmedBottom (16);
    constexpr int gap = 10;

    eq.setBounds   (r.removeFromLeft (280)); r.removeFromLeft (gap);
    comp.setBounds (r.removeFromLeft (280)); r.removeFromLeft (gap);

    auto col = r.removeFromLeft (152); r.removeFromLeft (gap);
    sat.setBounds (col.removeFromTop ((col.getHeight() - gap) / 2));
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
    const float scale = (float) getWidth() / (float) baseW;
    content.setTransform (AffineTransform::scale (scale));
}
