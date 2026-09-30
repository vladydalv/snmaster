#include "PluginEditor.h"

using namespace juce;
using namespace snui;
using namespace FbIDs;
using snui::MeterBar;

static String noteName (float hz)
{
    if (hz <= 0.0f) return "--";
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int midi = roundToInt (69.0f + 12.0f * std::log2 (hz / 440.0f));
    return String (names[((midi % 12) + 12) % 12]) + String (midi / 12 - 1);
}

//==============================================================================
FeedbackStatusPanel::FeedbackStatusPanel (SpacenerdFeedbackProcessor& p)
    : proc (p), outKnob (p.apvts, outGain, "Output", true, Theme::accent2)
{
    addAndMakeVisible (outKnob);
}

void FeedbackStatusPanel::update()
{
    noteHz    = proc.engine.detectedHz.load();
    targetHz  = proc.engine.targetHz.load();
    bloom     = proc.engine.bloomLevel.load();
    progress  = proc.engine.sustainProgress.load();
    inDb      = proc.engine.inputDb.load();
    listening = proc.engine.listening.load();
    mode      = (int) proc.apvts.getRawParameterValue (trigger)->load();
    holdOn    = proc.apvts.getRawParameterValue (hold)->load() > 0.5f;
    out.feed (sn::gainToDb (proc.outPeak.take()), false);
    repaint();
}

void FeedbackStatusPanel::paint (Graphics& g)
{
    drawCard (g, getLocalBounds().toFloat(), "STATUS");
    auto r = infoArea.toFloat();

    auto labelled = [&] (const String& tag, const String& big, const String& small, Colour c, Colour smallColour)
    {
        auto line = r.removeFromTop (52.0f);
        g.setColour (Theme::muted);
        g.setFont (FontOptions (10.0f, Font::bold));
        g.drawText (tag, line.removeFromTop (14.0f), Justification::centredLeft);
        g.setColour (c);
        g.setFont (FontOptions (26.0f, Font::bold));
        g.drawText (big, line.removeFromLeft (64.0f), Justification::centredLeft);
        g.setColour (smallColour);
        g.setFont (FontOptions (12.0f));
        g.drawText (small, line, Justification::centredLeft);
    };

    // Струна: що чує плагін
    const bool quiet = inDb < -55.0f;
    const String stringInfo = ! listening ? (quiet ? String ("play a note") : String ("no clear pitch"))
                                          : String (noteHz, 1) + " Hz";
    labelled ("STRING", listening ? noteName (noteHz) : String ("--"), stringInfo, Theme::text, Theme::muted);

    // Фідбек: що відбувається і чого він чекає
    const bool active = bloom > 0.01f;
    String state;
    Colour stateColour = Theme::muted;
    if (active)                       { state = String (targetHz, 1) + " Hz"; stateColour = Theme::muted; }
    else if (mode == 1 && ! holdOn)   { state = "press HOLD"; stateColour = Theme::gr; }
    else if (progress > 0.0f)         { state = "sustain the note..."; stateColour = Theme::accent2; }
    else if (listening)               { state = "waiting"; }
    else                              { state = "--"; }
    labelled ("FEEDBACK", active ? noteName (targetHz) : String ("--"), state, Theme::accent, stateColour);

    // Смуга: відлік до зриву (Auto) або наростання фідбеку
    auto bigBar = [&] (const String& tag, float value, Colour a, Colour b)
    {
        g.setColour (Theme::muted);
        g.setFont (FontOptions (10.0f, Font::bold));
        g.drawText (tag, r.removeFromTop (14.0f), Justification::centredLeft);
        auto bar = r.removeFromTop (10.0f);
        g.setColour (Theme::track.withAlpha (0.7f));
        g.fillRoundedRectangle (bar, 5.0f);
        if (value > 0.001f)
        {
            g.setGradientFill (ColourGradient (a, bar.getX(), 0.0f, b, bar.getRight(), 0.0f, false));
            g.fillRoundedRectangle (bar.withWidth (jmax (10.0f, bar.getWidth() * jmin (1.0f, value))), 5.0f);
        }
        r.removeFromTop (10.0f);
    };
    if (active || progress <= 0.0f) bigBar ("BLOOM", bloom, Theme::accent, Theme::gr);
    else                            bigBar ("SUSTAIN", progress, Theme::accent2, Theme::accent);

    // Рівні входу й виходу
    auto small = [&] (const String& tag, float db, bool warnLow)
    {
        auto line = r.removeFromTop (14.0f);
        g.setColour (warnLow ? Theme::gr : Theme::muted);
        g.setFont (FontOptions (10.0f, Font::bold));
        g.drawText (tag, line.removeFromLeft (70.0f), Justification::centredLeft);
        g.drawText (db > -99.0f ? String (roundToInt (db)) + " dB" : String ("-inf"), line, Justification::centredRight);
        auto m = r.removeFromTop (6.0f);
        g.setColour (Theme::track.withAlpha (0.7f));
        g.fillRoundedRectangle (m, 3.0f);
        const float frac = jlimit (0.0f, 1.0f, (db + 72.0f) / 72.0f);
        g.setColour (warnLow ? Theme::gr : (db > -1.0f ? Theme::hot : Theme::good));
        g.fillRoundedRectangle (m.withWidth (m.getWidth() * frac), 3.0f);
        r.removeFromTop (6.0f);
    };
    small (quiet && inDb > -99.0f ? "IN (LOW)" : "IN", inDb, quiet && inDb > -99.0f);
    small ("OUT", out.value, false);
}

void FeedbackStatusPanel::resized()
{
    auto r = getLocalBounds().withTrimmedTop (44).reduced (16, 8);
    outKnob.setBounds (r.removeFromRight (84).withSizeKeepingCentre (84, 104));
    r.removeFromRight (8);
    infoArea = r;
}

//==============================================================================
FeedbackContent::FeedbackContent (SpacenerdFeedbackProcessor& p)
    : presetBox (p),
      trig (p.apvts, "TRIGGER",  {}, 2),
      harm (p.apvts, "HARMONIC", {}, 2),
      cab  (p.apvts, "CABINET",  {}, 3),
      status (p)
{
    auto& s = p.apvts;

    trig.add (std::make_unique<Segmented> (s, trigger), 2, 34);
    auto holdBtn = std::make_unique<PillToggle> (s, hold, "HOLD", Theme::gr);
    holdBtn->setTooltip ("Step up to the cabinet: starts feedback on the ringing note (automate it in Logic)");
    trig.add (std::move (holdBtn), 2, 34);
    trig.add (std::make_unique<Knob> (s, delay, "Delay"), 2);

    harm.add (std::make_unique<Segmented> (s, harmonic), 2, 34);
    harm.knob (s, tone,  "Tone");
    harm.knob (s, drift, "Drift");

    cab.knob (s, distance, "Distance");
    cab.knob (s, amount,   "Amount");
    cab.knob (s, morph,    "Morph");

    for (auto* c : std::initializer_list<Component*> { &presetBox, &trig, &harm, &cab, &status })
        addAndMakeVisible (c);
}

void FeedbackContent::paint (Graphics& g)
{
    g.fillAll (Theme::bg);
    drawHeader (g, getWidth(), "FEEDBACK", "insert before the amp sim");
}

void FeedbackContent::resized()
{
    presetBox.setBounds (300, 14, 240, 28);
    auto r = getLocalBounds().withTrimmedTop (56).reduced (16, 0).withTrimmedBottom (16);
    constexpr int gap = 10;
    trig.setBounds (r.removeFromLeft (220)); r.removeFromLeft (gap);
    harm.setBounds (r.removeFromLeft (260)); r.removeFromLeft (gap);
    cab.setBounds  (r.removeFromLeft (270)); r.removeFromLeft (gap);
    status.setBounds (r);
}

//==============================================================================
SpacenerdFeedbackEditor::SpacenerdFeedbackEditor (SpacenerdFeedbackProcessor& p)
    : AudioProcessorEditor (p), content (p)
{
    addAndMakeVisible (content);
    setLookAndFeel (&lnf);
    content.setBounds (0, 0, baseW, baseH);
    setResizable (true, true);
    setResizeLimits (baseW * 3 / 4, baseH * 3 / 4, baseW * 2, baseH * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseW / (double) baseH);
    setSize (baseW, baseH);
    startTimerHz (30);
}

SpacenerdFeedbackEditor::~SpacenerdFeedbackEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SpacenerdFeedbackEditor::paint (Graphics& g) { g.fillAll (Theme::bg); }

void SpacenerdFeedbackEditor::resized()
{
    content.setTransform (AffineTransform::scale ((float) getWidth() / (float) baseW));
}
