#include "PluginEditor.h"

using namespace juce;
using namespace snui;
using namespace UnmaskIDs;

//==============================================================================
ClashDisplay::ClashDisplay (SpacenerdUnmaskProcessor& p) : proc (p)
{
    mainDb.fill (-120.0f); keyDb.fill (-120.0f);
    setTooltip ("Violet: this track. Cyan: the key (sidechain). The marker is where they clash; "
                "the notch shows how much is cut right now (only while the key plays).");
}

void ClashDisplay::update()
{
    for (int b = 0; b < an::kBands; ++b)
    {
        mainDb[(size_t) b] = proc.dsp.mainView[(size_t) b].load();
        keyDb[(size_t) b] = proc.dsp.keyView[(size_t) b].load();
    }
    gr = std::max (proc.dsp.grView.load(), gr * 0.85f);
    hz = proc.dsp.freqView.load();
    repaint();
}

float ClashDisplay::xFor (float f, Rectangle<float> r) const
{
    return r.getX() + r.getWidth() * (float) (std::log (f / 25.0) / std::log (18000.0 / 25.0));
}

void ClashDisplay::paint (Graphics& g)
{
    drawCard (g, getLocalBounds().toFloat(), "WHERE THEY CLASH");
    auto r = getLocalBounds().toFloat().withTrimmedTop (48.0f).reduced (18.0f, 12.0f).withTrimmedBottom (14.0f);

    // Діапазон пошуку
    const auto [lo, hi] = unmaskRange ((int) proc.apvts.getRawParameterValue (range)->load());
    const bool autoMode = proc.apvts.getRawParameterValue (mode)->load() < 0.5f;
    if (autoMode)
    {
        g.setColour (Theme::accent.withAlpha (0.06f));
        g.fillRect (Rectangle<float>::leftTopRightBottom (xFor (lo, r), r.getY(), xFor (hi, r), r.getBottom()));
    }

    // Сітка
    g.setFont (FontOptions (10.0f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xFor (f, r);
        g.setColour (Theme::border);
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (Theme::muted);
        g.drawText (f >= 1000.0f ? String ((int) (f / 1000.0f)) + "k" : String ((int) f), (int) x + 3, (int) r.getBottom() + 1, 40, 12, Justification::left);
    }

    // Нормування: кожен спектр відносно власного максимуму
    auto curve = [&] (const std::array<float, an::kBands>& db, Colour c)
    {
        const float mx = *std::max_element (db.begin(), db.end());
        if (mx < -100.0f) return;
        Path p;
        for (int b = 0; b < an::kBands; ++b)
        {
            const float v = jlimit (0.0f, 1.0f, 1.0f + (db[(size_t) b] - mx) / 48.0f);
            const Point<float> pt (xFor (an::bandHz[(size_t) b], r), r.getBottom() - v * r.getHeight() * 0.9f);
            if (b == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        Path fill (p);
        fill.lineTo (xFor (an::bandHz.back(), r), r.getBottom()); fill.lineTo (xFor (an::bandHz.front(), r), r.getBottom()); fill.closeSubPath();
        g.setColour (c.withAlpha (0.12f)); g.fillPath (fill);
        g.setColour (c); g.strokePath (p.createPathWithRoundedCorners (6.0f), PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
    };
    curve (mainDb, Theme::accent);
    curve (keyDb, Theme::accent2);

    // Частота конфлікту і поточний виріз
    const float x = xFor (hz, r);
    g.setColour (Theme::gr.withAlpha (0.8f));
    g.drawLine (x, r.getY(), x, r.getBottom(), 1.5f);
    const float depthPx = jlimit (0.0f, r.getHeight() * 0.5f, gr / 12.0f * r.getHeight() * 0.5f);
    if (depthPx > 0.5f)
    {
        Path notch;
        const float y0 = r.getY() + 30.0f;
        notch.startNewSubPath (x - 34.0f, y0);
        notch.quadraticTo (x - 10.0f, y0, x, y0 + depthPx);
        notch.quadraticTo (x + 10.0f, y0, x + 34.0f, y0);
        g.strokePath (notch, PathStrokeType (2.5f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    g.setColour (Theme::text);
    g.setFont (FontOptions (13.0f, Font::bold));
    const String label = (hz >= 1000.0f ? String (hz / 1000.0f, 2) + " kHz" : String (roundToInt (hz)) + " Hz") + "   -" + String (gr, 1) + " dB";
    g.drawText (label, (int) jlimit (r.getX(), r.getRight() - 160.0f, x + 8.0f), (int) r.getY() + 4, 160, 18, Justification::left);

    // Легенда
    g.setFont (FontOptions (11.0f, Font::bold));
    g.setColour (Theme::accent);  g.drawText ("THIS TRACK", getWidth() - 260, 14, 90, 18, Justification::right);
    g.setColour (Theme::accent2); g.drawText ("KEY (SIDECHAIN)", getWidth() - 160, 14, 140, 18, Justification::right);
}

//==============================================================================
UnmaskContent::UnmaskContent (SpacenerdUnmaskProcessor& p)
    : proc (p), presetBox (p), display (p), controls (p.apvts, "UNMASK", {}, 6)
{
    auto& s = p.apvts;
    auto m = std::make_unique<Segmented> (s, mode, "Mode");
    m->setTooltip ("Auto finds where this track and the key clash. Manual: you set the frequency.");
    controls.add (std::move (m), 2, 44);
    auto rg = std::make_unique<Segmented> (s, range, "Search range (Auto)");
    rg->setTooltip ("Where Auto looks: Lows for bass vs kick, Mids for guitars vs vocal, Full for anything.");
    controls.add (std::move (rg), 4, 44);
    freqKnob = &controls.knob (s, freq, "Frequency").help ("used in Manual mode (Auto shows what it found on the display)");
    controls.knob (s, depth, "Depth").help ("how deep the cut goes while the key plays");
    controls.knob (s, width, "Width (Q)").help ("higher = narrower cut");
    controls.knob (s, attack, "Attack").help ("how fast the cut follows the key (kick: 1-3 ms, vocal: ~10 ms)");
    controls.knob (s, release, "Release").help ("how fast this track comes back after the key");
    controls.knob (s, sens, "Sensitivity").help ("higher = quieter key notes already trigger the cut");
    auto d = std::make_unique<PillToggle> (s, delta, "LISTEN TO CUT", Theme::gr);
    d->setTooltip ("Hear only what is being removed (check you're not cutting too much). Turn off for the mix!");
    controls.add (std::move (d), 2, 30);

    for (auto* c : std::initializer_list<Component*> { &presetBox, &display, &controls })
        addAndMakeVisible (c);
}

void UnmaskContent::tick()
{
    display.update();
    freqKnob->setAlpha (proc.apvts.getRawParameterValue (mode)->load() < 0.5f ? 0.35f : 1.0f);   // в Auto частоту обирає плагін
    presetBox.sync();
    if (proc.sidechainConnected.load() != shownSc) { shownSc = ! shownSc; repaint (0, 0, getWidth(), 56); }
}

void UnmaskContent::paint (Graphics& g)
{
    g.fillAll (Theme::bg);
    drawHeader (g, getWidth(), "UNMASK", {});
    const bool key = proc.sidechainConnected.load() && proc.dsp.keyPresent.load();
    g.setColour (key ? Theme::good : Theme::warn);
    g.fillEllipse ((float) getWidth() - 330.0f, 24.0f, 8.0f, 8.0f);
    g.setFont (FontOptions (11.5f));
    g.drawText (key ? "key is playing into the sidechain" : "pick the key track in the plugin's Side Chain menu",
                getWidth() - 318, 14, 302, 28, Justification::centredLeft);
}

void UnmaskContent::resized()
{
    presetBox.setBounds (300, 14, 260, 28);
    auto r = getLocalBounds().withTrimmedTop (56).reduced (16, 0).withTrimmedBottom (16);
    display.setBounds (r.removeFromTop (270));
    r.removeFromTop (10);
    controls.setBounds (r);
}

//==============================================================================
SpacenerdUnmaskEditor::SpacenerdUnmaskEditor (SpacenerdUnmaskProcessor& p)
    : AudioProcessorEditor (p), content (p)
{
    addAndMakeVisible (content);
    setLookAndFeel (&lnf);
    content.setBounds (0, 0, baseW, baseH);
    setResizable (true, true);
    setResizeLimits (baseW * 3 / 4, baseH * 3 / 4, baseW * 2, baseH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) baseW / (double) baseH);
    setSize (baseW, baseH);
    startTimerHz (30);
}

SpacenerdUnmaskEditor::~SpacenerdUnmaskEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SpacenerdUnmaskEditor::resized()
{
    content.setTransform (AffineTransform::scale ((float) getWidth() / (float) baseW));
}
