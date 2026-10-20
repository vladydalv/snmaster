#include "PluginEditor.h"

using namespace juce;
using namespace snui;
using namespace DecIDs;

namespace
{
constexpr float kY0 = 1950.0f, kY1 = 2025.0f;
String hzShort (float hz) { return hz >= 1000.0f ? String (hz / 1000.0f, hz >= 10000.0f ? 0 : 1) + "k" : String (roundToInt (hz)); }
}

//==============================================================================
Colour DecadeMap::decadeColour (float year)
{
    static const std::pair<float, uint32> stops[] {
        { 1950, 0xffc9a46a }, { 1960, 0xffe38b4a }, { 1970, 0xffd9b13b }, { 1980, 0xffff4fa3 },
        { 1990, 0xff3fc1b0 }, { 2000, 0xff4f8cff }, { 2010, 0xff8b7cff }, { 2025, 0xffb28cff } };
    year = jlimit (kY0, kY1, year);
    for (size_t i = 0; i + 1 < std::size (stops); ++i)
        if (year <= stops[i + 1].first)
        {
            const float t = (year - stops[i].first) / (stops[i + 1].first - stops[i].first);
            return Colour (stops[i].second).interpolatedWith (Colour (stops[i + 1].second), t);
        }
    return Colour (stops[std::size (stops) - 1].second);
}

DecadeMap::DecadeMap (SpacenerdDecadeProcessor& p)
    : proc (p),
      yearAtt (*p.apvts.getParameter (year), [this] (float v) { yearVal = v; repaint(); }),
      roomAtt (*p.apvts.getParameter (room), [this] (float v) { roomVal = v; repaint(); })
{
    yearAtt.sendInitialUpdate();
    roomAtt.sendInitialUpdate();
}

Rectangle<float> DecadeMap::area() const
{
    return getLocalBounds().toFloat().withTrimmedTop (44.0f).withTrimmedLeft (46.0f).withTrimmedRight (16.0f).withTrimmedBottom (30.0f);
}

Point<float> DecadeMap::toPoint (float y, float r) const
{
    const auto a = area();
    return { a.getX() + (y - kY0) / (kY1 - kY0) * a.getWidth(), a.getBottom() - r * 0.01f * a.getHeight() };
}

int DecadeMap::presetAt (Point<float> pt) const
{
    const auto& ps = getDecadePresets();
    for (int i = 1; i < (int) ps.size(); ++i)
        if (toPoint (ps[(size_t) i].year, ps[(size_t) i].room).getDistanceFrom (pt) < 6.0f) return i;
    return -1;
}

void DecadeMap::setFrom (Point<float> pt)
{
    const auto a = area();
    yearAtt.setValueAsPartOfGesture (jlimit (kY0, kY1, kY0 + (pt.x - a.getX()) / a.getWidth() * (kY1 - kY0)));
    roomAtt.setValueAsPartOfGesture (jlimit (0.0f, 100.0f, (a.getBottom() - pt.y) / a.getHeight() * 100.0f));
}

void DecadeMap::mouseDown (const MouseEvent& e)
{
    const int pr = presetAt (e.position);
    if (pr > 0 && toPoint (yearVal, roomVal).getDistanceFrom (e.position) > 10.0f)
    {
        proc.setCurrentProgram (pr);
        return;
    }
    dragging = true;
    yearAtt.beginGesture(); roomAtt.beginGesture();
    setFrom (e.position);
}

void DecadeMap::mouseDrag (const MouseEvent& e) { if (dragging) setFrom (e.position); }

void DecadeMap::mouseUp (const MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    yearAtt.endGesture(); roomAtt.endGesture();
}

void DecadeMap::mouseMove (const MouseEvent& e)
{
    const int h = presetAt (e.position);
    if (h != hoverPreset) { hoverPreset = h; repaint(); }
    setMouseCursor (h > 0 ? MouseCursor::PointingHandCursor : MouseCursor::CrosshairCursor);
}

String DecadeMap::getTooltip()
{
    if (hoverPreset > 0) return "Preset: " + String (getDecadePresets()[(size_t) hoverPreset].name) + " (click to load)";
    return "Drag the puck: left-right = year, up-down = room (dry and close at the bottom, huge at the top). Dots are presets.";
}

void DecadeMap::update()
{
    const float t = proc.roomTail.load();
    if (std::abs (t - tail) > 0.0005f) { tail = std::max (t, tail * 0.85f); repaint(); }
}

void DecadeMap::paint (Graphics& g)
{
    drawCard (g, getLocalBounds().toFloat(), "DRUMS THROUGH THE DECADES");
    const auto a = area();

    // Колонки десятиліть
    for (int d = 1950; d < 2030; d += 10)
    {
        const float x0 = toPoint ((float) d, 0).x, x1 = toPoint ((float) std::min (d + 10, 2025), 0).x;
        if (x1 <= x0) continue;
        const auto col = decadeColour ((float) d + 5.0f);
        ColourGradient grad (col.withAlpha (0.20f), x0, a.getY(), col.withAlpha (0.05f), x0, a.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRect (Rectangle<float>::leftTopRightBottom (x0, a.getY(), x1, a.getBottom()));
        g.setColour (Theme::bg.withAlpha (0.6f));
        g.drawVerticalLine ((int) x0, a.getY(), a.getBottom());
        g.setColour (col.withAlpha (0.9f));
        g.setFont (FontOptions (12.0f, Font::bold));
        g.drawText ("'" + String (d % 100).paddedLeft ('0', 2) + "s", Rectangle<float> (x0, a.getBottom() + 6.0f, x1 - x0, 16.0f), Justification::centred);
    }
    // Рівні кімнати
    g.setColour (Theme::border.withAlpha (0.8f));
    for (float r : { 25.0f, 50.0f, 75.0f })
    {
        const float y = toPoint (kY0, r).y;
        const float dashes[] { 3.0f, 5.0f };
        g.drawDashedLine ({ a.getX(), y, a.getRight(), y }, dashes, 2, 1.0f);
    }
    g.setColour (Theme::muted);
    g.setFont (FontOptions (10.0f, Font::bold));
    g.drawText ("HUGE", (int) a.getX() - 44, (int) a.getY() - 2, 40, 14, Justification::centredRight);
    g.drawText ("ROOM", (int) a.getX() - 44, (int) a.getCentreY() - 7, 40, 14, Justification::centredRight);
    g.drawText ("DRY", (int) a.getX() - 44, (int) a.getBottom() - 12, 40, 14, Justification::centredRight);

    // Пресети
    const auto& ps = getDecadePresets();
    for (int i = 1; i < (int) ps.size(); ++i)
    {
        const auto pt = toPoint (ps[(size_t) i].year, ps[(size_t) i].room);
        const bool hov = i == hoverPreset, cur = i == proc.getCurrentProgram();
        const float rad = hov ? 5.0f : 3.0f;
        g.setColour (decadeColour (ps[(size_t) i].year).withAlpha (hov || cur ? 1.0f : 0.65f));
        g.fillEllipse (pt.x - rad, pt.y - rad, rad * 2, rad * 2);
        if (hov)
        {
            g.setColour (Theme::text);
            g.setFont (FontOptions (11.5f, Font::bold));
            g.drawText (ps[(size_t) i].name, Rectangle<float> (pt.x + 8, pt.y - 9, 180, 18), Justification::centredLeft);
        }
    }

    // Шайба
    const auto pt = toPoint (yearVal, roomVal);
    const auto col = decadeColour (yearVal);
    const float pulse = 18.0f + jlimit (0.0f, 40.0f, tail * 900.0f);
    g.setColour (col.withAlpha (0.18f));
    g.fillEllipse (pt.x - pulse, pt.y - pulse, pulse * 2, pulse * 2);
    g.setColour (col);
    g.fillEllipse (pt.x - 9, pt.y - 9, 18, 18);
    g.setColour (Colours::white);
    g.drawEllipse (pt.x - 9, pt.y - 9, 18, 18, 2.0f);
    g.setFont (FontOptions (14.0f, Font::bold));
    g.drawText (String (roundToInt (yearVal)), Rectangle<float> (pt.x - 40, pt.y - 34, 80, 18), Justification::centred);
}

//==============================================================================
ModuleCard::ModuleCard (SpacenerdDecadeProcessor& p, int m) : module (m), proc (p)
{
    auto& s = p.apvts;
    static const char* onIds[] { toneOn, gritOn, filterOn, squashOn, hitOn, roomOn };
    powerAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (s, onIds[m], power);
    power.setTooltip ("On / Off");
    power.onStateChange = [this] { for (auto& c : controls) c->setAlpha (power.getToggleState() ? 1.0f : 0.38f); repaint(); };
    addAndMakeVisible (power);

    auto knob = [&] (const char* id, const char* name, const char* help)
    {
        auto k = std::make_unique<Knob> (s, id, name, true);
        k->help (help);
        addAndMakeVisible (*k);
        controls.push_back (std::move (k));
    };
    switch (m)
    {
        case dec::ModTone:
            knob (toneLow, "Low", "80 Hz shelf, relative to the era (0 = as the era)");
            knob (toneLowMid, "Low-mid", "400 Hz: box and body");
            knob (tonePres, "Presence", "4 kHz: snap and stick attack");
            knob (toneTop, "Top", "10 kHz shelf: cymbals and air");
            break;
        case dec::ModGrit:
        {
            knob (gritDrive, "Drive", "more or less saturation than the era");
            auto c = std::make_unique<LabeledCombo> (s, gritChar, "Character");
            c->setTooltip ("Auto follows the era: tape, tube, sampler (12-bit, 26 kHz) or hard clip");
            addAndMakeVisible (*c);
            controls.push_back (std::move (c));
            break;
        }
        case dec::ModFilter:
            knob (filterHp, "High-pass", "move the era's low cut, in octaves");
            knob (filterLp, "Low-pass", "move the era's top roll-off, in octaves");
            controls.push_back (std::make_unique<Knob> (s, filterReso, "Resonance", false));
            static_cast<Knob&> (*controls.back()).help ("peak at the filter edges");
            addAndMakeVisible (*controls.back());
            break;
        case dec::ModSquash:
            knob (squashAmt, "Amount", "more or less compression than the era");
            knob (squashSpeed, "Speed", "faster (+) or slower (-) attack and release");
            break;
        case dec::ModHit:
            knob (hitAttack, "Attack", "punch of each hit");
            knob (hitSustain, "Sustain", "ring and body after the hit");
            break;
        case dec::ModRoom:
            knob (roomSize, "Size", "bigger or smaller space than the map says");
            knob (roomDecay, "Decay", "longer or shorter tail");
            break;
        default: break;
    }
    power.onStateChange();
}

void ModuleCard::resized()
{
    power.setBounds (getWidth() - 40, 8, 28, 28);
    auto r = getLocalBounds().withTrimmedTop (44).reduced (8, 0).withTrimmedBottom (28);
    const int n = (int) controls.size();
    if (module == dec::ModTone)
    {
        const int w = r.getWidth() / 2, h = r.getHeight() / 2;
        for (int i = 0; i < 4; ++i) controls[(size_t) i]->setBounds (r.getX() + (i % 2) * w, r.getY() + (i / 2) * h, w, h);
    }
    else if (module == dec::ModGrit)
    {
        controls[0]->setBounds (r.removeFromTop (r.getHeight() * 3 / 5).reduced (10, 0));
        controls[1]->setBounds (r.reduced (4, 6));
    }
    else if (module == dec::ModFilter)
    {
        const int w = r.getWidth() / 2, h = r.getHeight() / 2;
        controls[0]->setBounds (r.getX(), r.getY(), w, h);
        controls[1]->setBounds (r.getX() + w, r.getY(), w, h);
        controls[2]->setBounds (r.getX() + w / 2, r.getY() + h, w, h);
    }
    else
    {
        const int h = r.getHeight() / std::max (1, n);
        for (int i = 0; i < n; ++i) controls[(size_t) i]->setBounds (r.getX() + 12, r.getY() + i * h, r.getWidth() - 24, h);
    }
}

String ModuleCard::liveText() const
{
    const auto r = proc.resolve (proc.apvts.getRawParameterValue (year)->load(), proc.apvts.getRawParameterValue (room)->load());
    auto sdb = [] (float v) { return (v >= 0 ? "+" : "") + String (v, 1); };
    switch (module)
    {
        case dec::ModTone:   return sdb (r.lowDb) + " " + sdb (r.lowMidDb) + " " + sdb (r.presDb) + " " + sdb (r.topDb);
        case dec::ModGrit:   return String (dec::gritName (r.grit)) + "  " + String (roundToInt (r.drive * 100.0f)) + " %";
        case dec::ModFilter: return hzShort (r.hpHz) + " Hz - " + hzShort (r.lpHz) + " Hz";
        case dec::ModSquash: return r.ratio > 1.01f ? String (r.ratio, 1) + ":1   GR " + String (proc.squashGr.load(), 1) + " dB" : String ("off in this era");
        case dec::ModHit:    return "attack " + sdb (r.hitAttack) + "  sustain " + sdb (r.hitSustain);
        case dec::ModRoom:   return r.roomWet > 0.0f ? String (dec::roomName (proc.currentRoomType())) + "  " + String (r.space.decay, 1) + " s" : String ("dry");
        default: return {};
    }
}

void ModuleCard::update()
{
    const auto t = liveText();
    if (t != live) { live = t; repaint (0, getHeight() - 28, getWidth(), 28); }
}

void ModuleCard::paint (Graphics& g)
{
    drawCard (g, getLocalBounds().toFloat(), "      " + String (dec::moduleName (module)), power.getToggleState());
    // Грип (6 крапок)
    const auto gr = grip().toFloat();
    g.setColour (isMouseOver() && gr.contains (getMouseXYRelative().toFloat()) ? Theme::accent : Theme::muted);
    for (int row = 0; row < 3; ++row)
        for (int c = 0; c < 2; ++c)
            g.fillEllipse (gr.getX() + 7.0f + c * 6.0f, gr.getY() + 6.0f + row * 5.0f, 2.6f, 2.6f);
    g.setColour (power.getToggleState() ? Theme::accent2 : Theme::muted);
    g.setFont (FontOptions (10.5f, Font::bold));
    g.drawText (live, 8, getHeight() - 26, getWidth() - 16, 18, Justification::centred, true);
}

void ModuleCard::mouseMove (const MouseEvent& e)
{
    setMouseCursor (grip().contains (e.getPosition()) ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor);
    repaint (grip());
}

void ModuleCard::mouseDown (const MouseEvent& e) { if (grip().contains (e.getPosition()) && onDrag) onDrag (*this, e, 0); }
void ModuleCard::mouseDrag (const MouseEvent& e) { if (onDrag) onDrag (*this, e, 1); }
void ModuleCard::mouseUp (const MouseEvent& e)   { if (onDrag) onDrag (*this, e, 2); }

//==============================================================================
RackView::RackView (SpacenerdDecadeProcessor& p) : proc (p)
{
    for (int m = 0; m < dec::kNumModules; ++m)
    {
        cards.push_back (std::make_unique<ModuleCard> (p, m));
        cards.back()->onDrag = [this] (ModuleCard& c, const MouseEvent& e, int ph) { handleDrag (c, e, ph); };
        addAndMakeVisible (*cards.back());
    }
    order = proc.getOrder();
}

Rectangle<int> RackView::slot (int i) const
{
    constexpr int gap = 8;
    const int w = (getWidth() - gap * (dec::kNumModules - 1)) / dec::kNumModules;
    return { i * (w + gap), 0, w, getHeight() };
}

void RackView::resized() { layout (false); }

void RackView::layout (bool animate)
{
    for (int i = 0; i < dec::kNumModules; ++i)
    {
        auto* c = cards[(size_t) order[(size_t) i]].get();
        if (c == dragged) continue;
        if (animate) Desktop::getInstance().getAnimator().animateComponent (c, slot (i), 1.0f, 120, false, 1.0, 1.0);
        else c->setBounds (slot (i));
    }
}

void RackView::handleDrag (ModuleCard& c, const MouseEvent& e, int phase)
{
    const auto pos = e.getEventRelativeTo (this).getPosition();
    if (phase == 0)
    {
        dragged = &c;
        dragOffset = pos.x - c.getX();
        c.toFront (false);
        return;
    }
    if (dragged != &c) return;
    const int w = slot (0).getWidth();
    const int x = jlimit (0, getWidth() - w, pos.x - dragOffset);
    c.setTopLeftPosition (x, 0);
    // Куди вставити: за центром картки
    const int target = jlimit (0, dec::kNumModules - 1, (x + w / 2) / (w + 8));
    std::vector<int> rest;
    for (int m : order) if (m != c.module) rest.push_back (m);
    rest.insert (rest.begin() + target, c.module);
    for (int i = 0; i < dec::kNumModules; ++i) order[(size_t) i] = rest[(size_t) i];
    if (phase == 1) { layout (true); return; }
    dragged = nullptr;
    proc.setOrder (order);
    layout (true);
}

void RackView::paint (Graphics& g)
{
    // Стрілки між модулями: напрямок обробки
    g.setColour (Theme::muted.withAlpha (0.6f));
    for (int i = 0; i + 1 < dec::kNumModules; ++i)
    {
        const auto s = slot (i);
        const float x = (float) s.getRight() + 4.0f, y = 22.0f;
        Path arrow; arrow.addTriangle (x - 3, y - 4, x - 3, y + 4, x + 3, y);
        g.fillPath (arrow);
    }
}

void RackView::update()
{
    for (auto& c : cards) c->update();
    if (dragged == nullptr && proc.getOrder() != order) { order = proc.getOrder(); layout (true); }
}

//==============================================================================
DecadeContent::DecadeContent (SpacenerdDecadeProcessor& p)
    : proc (p), presetBox (p), map (p), rack (p),
      roomTypeBox (p.apvts, roomType, "Room type"),
      formatBox (p.apvts, format, "Heard on"),
      fmtAmountKnob (p.apvts, fmtAmount, "Amount", false, Theme::accent2),
      fmtAgeKnob (p.apvts, fmtAge, "Age", false, Theme::accent2),
      authKnob (p.apvts, auth, "Authenticity"),
      mixKnob (p.apvts, mix, "Mix"),
      inKnob (p.apvts, inGain, "Input", true, Theme::accent2),
      outKnob (p.apvts, outGain, "Output", true, Theme::accent2)
{
    roomTypeBox.setTooltip ("Auto: the space of the chosen year. Or pick one: studio, live room, chamber, hall, garage, gated");
    formatBox.setTooltip ("How the listener heard it, after the rack. Auto follows the year: acetate/tube radio, vinyl, cassette, CD, MP3, stream");
    fmtAmountKnob.help ("from clean to worn");
    fmtAgeKnob.help ("how old and played-to-death the medium is (crackle, hiss, dull top)");
    authKnob.help ("0 = rack neutral (flat EQ, no drive, no room), 100 = fully the era");
    mixKnob.help ("dry / processed (latency-compensated)");
    for (auto* c : std::initializer_list<Component*> { &presetBox, &map, &rack, &roomTypeBox, &formatBox, &fmtAmountKnob, &fmtAgeKnob,
                                                       &authKnob, &mixKnob, &inKnob, &outKnob })
        addAndMakeVisible (c);
}

void DecadeContent::tick()
{
    presetBox.sync();
    map.update();
    rack.update();
    for (size_t ch = 0; ch < 2; ++ch)
    {
        inMeters[ch].feed (sn::gainToDb (proc.inPeak[ch].take()), false);
        outMeters[ch].feed (sn::gainToDb (proc.outPeak[ch].take()), false);
    }
    repaint (meterArea);
    const float y = proc.apvts.getRawParameterValue (year)->load();
    const auto info = String (roundToInt (y)) + String (proc.currentFormat()) + String (proc.currentRoomType()) + String (roundToInt (proc.apvts.getRawParameterValue (room)->load()));
    if (info != shownInfo) { shownInfo = info; repaint (infoArea); }
}

void DecadeContent::paint (Graphics& g)
{
    g.fillAll (Theme::bg);
    drawHeader (g, getWidth(), "DECADE", "the year's drum sound: drag the puck, shape the rack");

    // THE SOUND
    drawCard (g, infoArea.toFloat(), "THE SOUND");
    const float y = proc.apvts.getRawParameterValue (year)->load();
    const auto e = dec::forYear (y);
    auto r = infoArea.reduced (18, 0).withTrimmedTop (44);
    g.setColour (DecadeMap::decadeColour (y));
    g.setFont (FontOptions (40.0f, Font::bold));
    g.drawText (String (roundToInt (y)), r.removeFromTop (44), Justification::centredLeft);
    g.setColour (Theme::text);
    g.setFont (FontOptions (13.0f));
    g.drawFittedText (String (e.blurb), r.removeFromTop (36), Justification::topLeft, 2, 1.0f);
    g.setColour (Theme::muted);
    g.setFont (FontOptions (12.0f));
    const int fmtChoice = (int) proc.apvts.getRawParameterValue (format)->load();
    const int rtChoice = (int) proc.apvts.getRawParameterValue (roomType)->load();
    g.drawText ("Space: " + String (dec::roomName (proc.currentRoomType())) + (rtChoice == 0 ? " (era)" : ""), r.removeFromTop (17), Justification::centredLeft);
    g.drawText ("Heard on: " + String (dec::formatName (proc.currentFormat())) + (fmtChoice == 0 ? " (follows the year)" : " (locked)"), r.removeFromTop (17), Justification::centredLeft);

    // OUTPUT
    drawCard (g, ioArea.toFloat(), "OUTPUT");
    auto m = meterArea.toFloat();
    auto bar = [&] (const String& name, const std::array<MeterBar, 2>& mb, Rectangle<float> row)
    {
        g.setColour (Theme::muted);
        g.setFont (FontOptions (10.0f, Font::bold));
        g.drawText (name, row.removeFromLeft (32.0f), Justification::centredLeft);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto b = row.removeFromTop (row.getHeight() / (2 - ch)).reduced (0, 1.5f);
            g.setColour (Theme::track); g.fillRoundedRectangle (b, 2.0f);
            const float v = jlimit (0.0f, 1.0f, (mb[(size_t) ch].value + 60.0f) / 60.0f);
            g.setColour (mb[(size_t) ch].value > -1.0f ? Theme::hot : Theme::good.interpolatedWith (Theme::warn, v));
            g.fillRoundedRectangle (b.withWidth (b.getWidth() * v), 2.0f);
        }
    };
    bar ("IN", inMeters, m.removeFromTop (16));
    m.removeFromTop (6);
    bar ("OUT", outMeters, m.removeFromTop (16));
    const float ld = proc.loudDiffDb.load();
    g.setColour (Theme::text);
    g.setFont (FontOptions (11.0f));
    g.drawText ("output is " + (std::abs (ld) < 0.5f ? String ("as loud as the input") : String (std::abs (ld), 1) + " dB " + (ld > 0 ? "louder" : "quieter")),
                m.removeFromTop (20), Justification::centredLeft);
}

void DecadeContent::resized()
{
    presetBox.setBounds (300, 14, 260, 28);
    auto r = getLocalBounds().withTrimmedTop (60).reduced (16, 0).withTrimmedBottom (16);
    auto top = r.removeFromTop (300);
    infoArea = top.removeFromRight (412);
    top.removeFromRight (12);
    map.setBounds (top);
    {
        auto i = infoArea.reduced (18, 0).withTrimmedTop (44 + 44 + 36 + 34 + 8).withTrimmedBottom (12);
        auto left = i.removeFromLeft (196);
        roomTypeBox.setBounds (left.removeFromTop (44));
        left.removeFromTop (6);
        formatBox.setBounds (left.removeFromTop (44));
        i.removeFromLeft (8);
        fmtAmountKnob.setBounds (i.removeFromLeft (i.getWidth() / 2));
        fmtAgeKnob.setBounds (i);
    }
    r.removeFromTop (12);
    ioArea = r.removeFromRight (220);
    r.removeFromRight (12);
    rack.setBounds (r);
    {
        auto i = ioArea.reduced (10, 0).withTrimmedTop (44);
        meterArea = i.removeFromBottom (66).reduced (6, 0);
        const int w = i.getWidth() / 2, h = std::min (110, i.getHeight() / 2);
        authKnob.setBounds (i.getX(), i.getY(), w, h);
        mixKnob.setBounds (i.getX() + w, i.getY(), w, h);
        inKnob.setBounds (i.getX(), i.getY() + h, w, h);
        outKnob.setBounds (i.getX() + w, i.getY() + h, w, h);
    }
}

//==============================================================================
SpacenerdDecadeEditor::SpacenerdDecadeEditor (SpacenerdDecadeProcessor& p)
    : AudioProcessorEditor (p), content (p)
{
    addAndMakeVisible (content);
    setLookAndFeel (&lnf);
    content.setBounds (0, 0, baseW, baseH);
    snui::setupEditorSize (*this, p.apvts.state, baseW, baseH);
    startTimerHz (30);
}

SpacenerdDecadeEditor::~SpacenerdDecadeEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SpacenerdDecadeEditor::resized()
{
    content.setTransform (AffineTransform::scale ((float) getWidth() / (float) baseW));
    snui::rememberEditorScale (static_cast<SpacenerdDecadeProcessor&> (processor).apvts.state, getWidth(), baseW);
}
