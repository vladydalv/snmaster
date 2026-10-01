#include "PluginEditor.h"

using namespace juce;
using namespace snui;
using namespace ToneIDs;

//==============================================================================
ToneOutputPanel::ToneOutputPanel (SpacenerdToneProcessor& p)
    : proc (p),
      inKnob  (p.apvts, inGain,  "Input",  true, Theme::accent2),
      mixKnob (p.apvts, mix,     "Mix",    false, Theme::accent2),
      outKnob (p.apvts, outGain, "Output", true, Theme::accent2)
{
    addAndMakeVisible (inKnob);
    addAndMakeVisible (mixKnob);
    addAndMakeVisible (outKnob);
}

void ToneOutputPanel::update()
{
    for (size_t ch = 0; ch < 2; ++ch)
    {
        in[ch].feed  (sn::gainToDb (proc.inPeak[ch].take()),  false);
        out[ch].feed (sn::gainToDb (proc.outPeak[ch].take()), false);
    }
    ds.feed (proc.deEssGr.take(), true);
    repaint (barsArea.expanded (0, 18));
}

void ToneOutputPanel::paint (Graphics& g)
{
    drawCard (g, getLocalBounds().toFloat(), "OUTPUT");

    auto meterR = barsArea.toFloat().withTrimmedBottom (14.0f);
    const float groupW = meterR.getWidth() / 3.0f;
    constexpr float barW = 10.0f, gap = 4.0f;

    auto label = [&] (int idx, const String& name)
    {
        const float cx = meterR.getX() + groupW * ((float) idx + 0.5f);
        g.setColour (Theme::muted);
        g.setFont (FontOptions (9.5f, Font::bold));
        g.drawText (name, Rectangle<float> (cx - groupW * 0.5f, meterR.getBottom() + 2.0f, groupW, 12.0f), Justification::centred);
        return cx;
    };

    float cx = label (0, "IN");
    in[0].draw (g, { cx - barW - gap * 0.5f, meterR.getY(), barW, meterR.getHeight() }, false);
    in[1].draw (g, { cx + gap * 0.5f,        meterR.getY(), barW, meterR.getHeight() }, false);

    cx = label (1, "DE-ESS");
    ds.draw (g, { cx - barW * 0.5f, meterR.getY(), barW, meterR.getHeight() }, true, 12.0f);
    g.setColour (Theme::muted);
    g.setFont (FontOptions (9.5f, Font::bold));
    g.drawText (ds.hold > 0.05f ? "-" + String (ds.hold, 1) : String ("0.0"),
                Rectangle<float> (cx - groupW * 0.5f, meterR.getY() - 15.0f, groupW, 12.0f), Justification::centred);

    cx = label (2, "OUT");
    out[0].draw (g, { cx - barW - gap * 0.5f, meterR.getY(), barW, meterR.getHeight() }, false);
    out[1].draw (g, { cx + gap * 0.5f,        meterR.getY(), barW, meterR.getHeight() }, false);
}

void ToneOutputPanel::resized()
{
    auto r = getLocalBounds().withTrimmedTop (44).reduced (10, 8);
    auto knobs = r.removeFromBottom (100);
    const int kw = knobs.getWidth() / 3;
    inKnob.setBounds (knobs.removeFromLeft (kw));
    mixKnob.setBounds (knobs.removeFromLeft (kw));
    outKnob.setBounds (knobs);
    r.removeFromTop (18);
    r.removeFromBottom (10);
    barsArea = r;
}

//==============================================================================
ToneContent::ToneContent (SpacenerdToneProcessor& p)
    : presetBox (p),
      tube      (p.apvts, "TUBE",       tubeOn, 3),
      tape      (p.apvts, "TAPE",       tapeOn, 3),
      exciter   (p.apvts, "EXCITER",    excOn,  2),
      transient (p.apvts, "TRANSIENT",  trOn,   2),
      deEss     (p.apvts, "DE-ESSER",   dsOn,   4),
      output (p)
{
    auto& s = p.apvts;

    tube.knob (s, tubeDrive, "Drive").help ("tube saturation; level stays the same");
    tube.knob (s, tubeBias,  "Bias").help ("tube operating point: higher = more even harmonics, warmer and fatter");
    tube.knob (s, tubeMix,   "Mix").help ("blend with the clean signal");

    tape.add (std::make_unique<Segmented> (s, tapeSpeed, "Speed, ips"), 3, 44);
    tape.knob (s, tapeDrive, "Drive").help ("tape compression and saturation");
    tape.knob (s, tapeBias,  "Bias").help ("tape bias: low = under-biased, gritty and broken-up; high = cleaner, softer, darker");
    tape.knob (s, tapeWow,   "Wow / Flutter").help ("pitch drift of a worn tape machine");

    exciter.knob (s, excFreq,   "Freq").help ("harmonics are generated above this frequency");
    exciter.knob (s, excAmount, "Amount").help ("how much new top-end harmonics are added (presence and air)");

    transient.knob (s, trAttack,  "Attack",  true).help ("+ = sharper pick/stick attack, - = softer");
    transient.knob (s, trSustain, "Sustain", true).help ("+ = longer ring and room, - = tighter, drier");

    deEss.knob (s, dsFreq,  "Freq").help ("where the S sounds start (5-8 kHz for vocals)");
    deEss.knob (s, dsSens,  "Sensitivity").help ("higher = reacts to softer S sounds");
    deEss.knob (s, dsRange, "Range").help ("maximum reduction of S sounds");
    auto listen = std::make_unique<PillToggle> (s, dsListen, "LISTEN", Theme::gr);
    listen->setTooltip ("Hear only what the de-esser detects");
    deEss.add (std::move (listen), 1, 24);

    for (auto* c : std::initializer_list<Component*> { &presetBox, &tube, &tape, &exciter, &transient, &deEss, &output })
        addAndMakeVisible (c);
}

void ToneContent::paint (Graphics& g)
{
    g.fillAll (Theme::bg);
    drawHeader (g, getWidth(), "TONE", "TRANSIENT > TUBE > TAPE > EXCITER > DE-ESS   |   4x oversampled");
}

void ToneContent::resized()
{
    presetBox.setBounds (300, 14, 280, 28);

    auto r = getLocalBounds().withTrimmedTop (56).reduced (16, 0).withTrimmedBottom (16);
    constexpr int gap = 10;

    output.setBounds (r.removeFromRight (230));
    r.removeFromRight (gap);

    auto top = r.removeFromTop (220);
    r.removeFromTop (gap);
    auto bottom = r;

    // Порядок як у ланцюгу: TRANSIENT > TUBE > TAPE, далі EXCITER > DE-ESS
    transient.setBounds (top.removeFromLeft (230)); top.removeFromLeft (gap);
    tube.setBounds (top.removeFromLeft (330));      top.removeFromLeft (gap);
    tape.setBounds (top);

    exciter.setBounds (bottom.removeFromLeft (230)); bottom.removeFromLeft (gap);
    deEss.setBounds (bottom);
}

//==============================================================================
SpacenerdToneEditor::SpacenerdToneEditor (SpacenerdToneProcessor& p)
    : AudioProcessorEditor (p), content (p)
{
    addAndMakeVisible (content);
    setLookAndFeel (&lnf);
    content.setBounds (0, 0, baseW, baseH);

    snui::setupEditorSize (*this, p.apvts.state, baseW, baseH);
    startTimerHz (30);
}

SpacenerdToneEditor::~SpacenerdToneEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SpacenerdToneEditor::paint (Graphics& g) { g.fillAll (Theme::bg); }

void SpacenerdToneEditor::resized()
{
    content.setTransform (AffineTransform::scale ((float) getWidth() / (float) baseW));
    snui::rememberEditorScale (static_cast<SpacenerdToneProcessor&> (processor).apvts.state, getWidth(), baseW);
}
