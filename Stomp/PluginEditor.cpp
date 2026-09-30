#include "PluginEditor.h"

using namespace juce;
using namespace snui;
using namespace StompIDs;

static const char* const circuitNames[] { "'65 GERMANIUM", "'70 BRITISH", "'73 TRIANGLE", "'81 OP-AMP", "MODERN" };
static const char* const circuitHints[] { "round, fat, cleans up with volume", "two tube-like stages, chewy mids",
                                          "scooped wall, endless sustain", "tight diode clip, grunge bite", "tight lows, mids forward, gated" };

//==============================================================================
StompPad::StompPad (SpacenerdStompProcessor& p)
    : proc (p),
      circAtt (*p.apvts.getParameter (circuit), [this] (float v) { circVal = v; repaint(); }, nullptr),
      gainAtt (*p.apvts.getParameter (gain),    [this] (float v) { gainVal = v; repaint(); }, nullptr)
{
    circAtt.sendInitialUpdate();
    gainAtt.sendInitialUpdate();
    for (auto& f : flicker) f = rng.nextFloat();
    setMouseCursor (MouseCursor::CrosshairCursor);
    setTooltip ("Drag: left-right = circuit (morphs smoothly between eras), up-down = gain. Double-click: reset.");
}

Colour StompPad::circuitColour (float x)
{
    static constexpr uint32 c[] { 0xfff5a524, 0xfff97316, 0xffec4899, 0xff8b7cff, 0xff5ad1e6 };
    x = jlimit (0.0f, 4.0f, x);
    const int i = std::min (3, (int) x);
    return Colour (c[i]).interpolatedWith (Colour (c[i + 1]), x - (float) i);
}

Rectangle<float> StompPad::grid() const
{
    return getLocalBounds().toFloat().withTrimmedTop (62.0f).withTrimmedBottom (16.0f).withTrimmedLeft (40.0f).withTrimmedRight (18.0f);
}

void StompPad::update (float outLevel)
{
    const float norm = jlimit (0.0f, 1.0f, (sn::gainToDb (outLevel) + 50.0f) / 44.0f);
    level = norm > level ? norm : level * 0.85f + norm * 0.15f;
    const float bat = proc.apvts.getRawParameterValue (battery)->load() * 0.01f;
    const int changes = 60 + (int) (140.0f * bat);          // сіла батарейка — пікселі «іскрять» частіше
    for (int i = 0; i < changes; ++i)
        flicker[(size_t) rng.nextInt ((int) flicker.size())] = rng.nextFloat();
    repaint();
}

void StompPad::paint (Graphics& g)
{
    const bool driveActive = proc.apvts.getRawParameterValue (driveOn)->load() > 0.5f;
    drawCard (g, getLocalBounds().toFloat(), "CIRCUIT MORPH", driveActive);

    const int idx = jlimit (0, 4, roundToInt (circVal));
    const bool between = std::abs (circVal - (float) idx) > 0.08f;
    g.setColour (circuitColour (circVal));
    g.setFont (FontOptions (15.0f, Font::bold));
    const String name = between ? String (circuitNames[(int) circVal]) + "  >  " + circuitNames[std::min (4, (int) circVal + 1)]
                                : String (circuitNames[idx]);
    g.drawText (name, 170, 8, 420, 24, Justification::centredLeft);
    g.setColour (Theme::muted);
    g.setFont (FontOptions (12.0f));
    g.drawText ((between ? String ("morphing") : String (circuitHints[idx])) + "   |   gain " + String (roundToInt (gainVal)) + " %",
                getWidth() - 360, 8, 342, 24, Justification::centredRight);

    const auto gr = grid();
    const float cw = gr.getWidth() / (float) cols, rh = gr.getHeight() / (float) rows;
    const float lfo = proc.lfoView.load();
    const float pulse = 1.0f + 0.35f * lfo;

    // Підписи зон
    g.setFont (FontOptions (9.5f, Font::bold));
    for (int z = 0; z < 5; ++z)
    {
        const float x = gr.getX() + ((float) z / 4.0f) * (gr.getWidth() - cw) + cw * 0.5f;
        g.setColour (circuitColour ((float) z).withAlpha (z == idx && ! between ? 1.0f : 0.55f));
        g.drawText (circuitNames[z], Rectangle<float> (x - 60.0f, gr.getY() - 20.0f, 120.0f, 14.0f), Justification::centred);
    }

    // Пікселі
    const float gainRow = gainVal * 0.01f * (float) (rows - 1);
    const float cursorCol = circVal / 4.0f * (float) (cols - 1);
    for (int c = 0; c < cols; ++c)
    {
        const float cx = (float) c / (float) (cols - 1) * 4.0f;
        const auto col = circuitColour (cx);
        const float dx = ((float) c - cursorCol) / 6.0f;
        const float prox = std::exp (-dx * dx);
        for (int r = 0; r < rows; ++r)
        {
            const bool under = (float) r <= gainRow;
            const float f = flicker[(size_t) (c * rows + r)];
            float a = 0.05f + (under ? 0.08f + 0.30f * prox : 0.02f)
                    + level * (0.2f + 0.6f * prox) * (0.35f + 0.65f * f) * (under ? 1.0f : 0.35f) * pulse;
            if (! driveActive) a *= 0.45f;
            a = jlimit (0.03f, 1.0f, a);
            const auto px = Rectangle<float> (gr.getX() + (float) c * cw, gr.getBottom() - (float) (r + 1) * rh, cw, rh).reduced (1.0f);
            g.setColour (col.withAlpha (a));
            g.fillRoundedRectangle (px, 1.5f);
        }
    }

    // Курсор
    const float px = gr.getX() + (cursorCol + 0.5f) * cw;
    const float py = gr.getBottom() - (gainRow + 0.5f) * rh;
    g.setColour (Colours::white.withAlpha (0.18f));
    g.drawVerticalLine (roundToInt (px), gr.getY(), gr.getBottom());
    g.drawHorizontalLine (roundToInt (py), gr.getX(), gr.getRight());
    g.setColour (circuitColour (circVal).withAlpha (0.35f));
    g.fillEllipse (Rectangle<float> (26.0f, 26.0f).withCentre ({ px, py }));
    g.setColour (Colours::white);
    g.drawEllipse (Rectangle<float> (14.0f, 14.0f).withCentre ({ px, py }), 2.0f);

    Graphics::ScopedSaveState save (g);
    g.setColour (Theme::muted);
    g.setFont (FontOptions (10.0f, Font::bold));
    g.addTransform (AffineTransform::rotation (-MathConstants<float>::halfPi, 18.0f, gr.getCentreY()));
    g.drawText ("GAIN", Rectangle<float> (18.0f - 60.0f, gr.getCentreY() - 7.0f, 120.0f, 14.0f), Justification::centred);
}

void StompPad::setFromPoint (Point<float> p)
{
    const auto gr = grid();
    const float cw = gr.getWidth() / (float) cols;
    float x = (p.x - gr.getX() - cw * 0.5f) / (gr.getWidth() - cw) * 4.0f;
    x = jlimit (0.0f, 4.0f, x);
    // Легке «прилипання» до класичних схем
    const float nearest = std::round (x);
    if (std::abs (x - nearest) < 0.06f) x = nearest;
    circAtt.setValueAsPartOfGesture (x);
    gainAtt.setValueAsPartOfGesture (jlimit (0.0f, 100.0f, (gr.getBottom() - p.y) / gr.getHeight() * 100.0f));
}

void StompPad::mouseDown (const MouseEvent& e)
{
    dragging = true;
    circAtt.beginGesture();
    gainAtt.beginGesture();
    setFromPoint (e.position);
}

void StompPad::mouseDrag (const MouseEvent& e) { if (dragging) setFromPoint (e.position); }

void StompPad::mouseUp (const MouseEvent&)
{
    if (! dragging) return;
    circAtt.endGesture();
    gainAtt.endGesture();
    dragging = false;
}

void StompPad::mouseDoubleClick (const MouseEvent&)
{
    circAtt.setValueAsCompleteGesture (2.0f);
    gainAtt.setValueAsCompleteGesture (60.0f);
}

//==============================================================================
OutputPanel::OutputPanel (SpacenerdStompProcessor& p)
    : proc (p), outKnob (p.apvts, outGain, "Output", true, Theme::accent2)
{
    addAndMakeVisible (outKnob);
}

float OutputPanel::update()
{
    float peak = 0.0f;
    for (size_t ch = 0; ch < 2; ++ch)
    {
        in[ch].feed (sn::gainToDb (proc.inPeak[ch].take()), false);
        const float o = proc.outPeak[ch].take();
        peak = std::max (peak, o);
        out[ch].feed (sn::gainToDb (o), false);
    }
    repaint (barsArea.expanded (0, 18));
    return peak;
}

void OutputPanel::paint (Graphics& g)
{
    drawCard (g, getLocalBounds().toFloat(), "OUTPUT");
    auto meterR = barsArea.toFloat().withTrimmedBottom (14.0f);
    const float groupW = meterR.getWidth() / 2.0f;
    constexpr float barW = 10.0f, gap = 4.0f;
    auto group = [&] (int i, const String& name, const std::array<MeterBar, 2>& m)
    {
        const float cx = meterR.getX() + groupW * ((float) i + 0.5f);
        m[0].draw (g, { cx - barW - gap * 0.5f, meterR.getY(), barW, meterR.getHeight() }, false);
        m[1].draw (g, { cx + gap * 0.5f,        meterR.getY(), barW, meterR.getHeight() }, false);
        g.setColour (Theme::muted);
        g.setFont (FontOptions (9.5f, Font::bold));
        g.drawText (name, Rectangle<float> (cx - groupW * 0.5f, meterR.getBottom() + 2.0f, groupW, 12.0f), Justification::centred);
    };
    group (0, "IN", in);
    group (1, "OUT", out);
}

void OutputPanel::resized()
{
    auto r = getLocalBounds().withTrimmedTop (44).reduced (10, 8);
    outKnob.setBounds (r.removeFromBottom (100));
    r.removeFromBottom (8);
    barsArea = r;
}

//==============================================================================
void TrackView::update()
{
    const float hz = proc.trackedHz.load();
    if (hz > 0.0f) { shownHz = hz; holdFrames = 20; }
    else if (holdFrames > 0 && --holdFrames == 0) shownHz = 0.0f;
    repaint();
}

void TrackView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Theme::muted);
    g.setFont (FontOptions (10.0f, Font::bold));
    g.drawText ("TRACKING", r.removeFromTop (14.0f), Justification::centred);
    auto box = r.reduced (4.0f, 2.0f);
    g.setColour (Theme::track);
    g.fillRoundedRectangle (box, 6.0f);
    String txt ("--");
    if (shownHz > 0.0f)
    {
        static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int midi = roundToInt (69.0f + 12.0f * std::log2 (shownHz / 440.0f));
        txt = String (names[(midi % 12 + 12) % 12]) + String (midi / 12 - 1);
    }
    g.setColour (shownHz > 0.0f ? Theme::good : Theme::muted);
    g.setFont (FontOptions (14.0f, Font::bold));
    g.drawText (txt, box, Justification::centred);
}

//==============================================================================
StompContent::StompContent (SpacenerdStompProcessor& p)
    : proc (p),
      presetBox (p),
      pad (p),
      drive   (p.apvts, "DRIVE", driveOn, 3),
      octSec  (p.apvts, "OCTAVE", octOn, 7),
      modSec  (p.apvts, "MODULATION", modOn, 8),
      echoSec (p.apvts, "TAPE ECHO", echoOn, 5),
      output (p)
{
    auto& s = p.apvts;

    drive.knob (s, circuit, "Circuit");
    drive.knob (s, gain, "Gain");
    drive.knob (s, tone, "Tone", true);
    drive.knob (s, battery, "Battery");
    drive.knob (s, cleanBass, "Clean Bass");
    drive.knob (s, level, "Level", true);

    octSec.add (std::make_unique<Segmented> (s, octEngine, "Engine"), 3, 44);
    octSec.add (std::make_unique<Segmented> (s, octPos, "Position"), 2, 44);
    trackView = static_cast<TrackView*> (&octSec.add (std::make_unique<TrackView> (p), 2, 44));
    octSec.knob (s, sub1, "Sub -1");
    octSec.knob (s, sub2, "Sub -2");
    octSec.knob (s, octUp, "Up +1");
    octSec.knob (s, octDry, "Dry");
    octSec.knob (s, octTone, "Tone");
    octSec.knob (s, bloom, "Bloom");
    octSec.knob (s, wobble, "Wobble");

    // 8 колонок: режими ширші (назви не обрізаються), ручки — по 2 колонки
    modSec.add (std::make_unique<Segmented> (s, modMode, "Mode"), 5, 44);
    modSec.add (std::make_unique<Segmented> (s, modSync, "Sync"), 3, 44);
    modSec.add (std::make_unique<Knob> (s, rate, "Rate"), 2);
    modSec.add (std::make_unique<Knob> (s, depth, "Depth"), 2);
    modSec.add (std::make_unique<Knob> (s, shape, "Shape"), 2);
    modSec.add (std::make_unique<Knob> (s, rise, "Rise"), 2);

    echoSec.add (std::make_unique<Segmented> (s, echoSync, "Sync"), 5, 44);
    echoSec.knob (s, echoTime, "Time");
    echoSec.knob (s, feedback, "Repeats");
    echoSec.knob (s, echoTone, "Tone");
    echoSec.knob (s, wear, "Wear");
    echoSec.knob (s, echoMix, "Mix");

    for (auto* c : std::initializer_list<Component*> { &presetBox, &pad, &drive, &octSec, &modSec, &echoSec, &output })
        addAndMakeVisible (c);
}

void StompContent::tick()
{
    pad.update (output.update());
    if (trackView != nullptr) trackView->update();
    presetBox.sync();
}

void StompContent::paint (Graphics& g)
{
    g.fillAll (Theme::bg);
    drawHeader (g, getWidth(), "STOMP", "OCTAVE > CIRCUIT MORPH > MODULATION > TAPE ECHO   |   4x oversampled");
}

void StompContent::resized()
{
    presetBox.setBounds (300, 14, 280, 28);
    auto r = getLocalBounds().withTrimmedTop (56).reduced (16, 0).withTrimmedBottom (16);
    constexpr int gap = 10;

    auto top = r.removeFromTop (300);
    r.removeFromTop (gap);
    drive.setBounds (top.removeFromRight (360));
    top.removeFromRight (gap);
    pad.setBounds (top);

    output.setBounds (r.removeFromRight (180));
    r.removeFromRight (gap);
    octSec.setBounds (r.removeFromTop (212));
    r.removeFromTop (gap);
    const int half = (r.getWidth() - gap) / 2;
    modSec.setBounds (r.removeFromLeft (half));
    r.removeFromLeft (gap);
    echoSec.setBounds (r);
}

//==============================================================================
SpacenerdStompEditor::SpacenerdStompEditor (SpacenerdStompProcessor& p)
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

SpacenerdStompEditor::~SpacenerdStompEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SpacenerdStompEditor::paint (Graphics& g) { g.fillAll (Theme::bg); }

void SpacenerdStompEditor::resized()
{
    content.setTransform (AffineTransform::scale ((float) getWidth() / (float) baseW));
}
