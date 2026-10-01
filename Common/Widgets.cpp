#include "Widgets.h"

using namespace juce;

namespace snui
{
//==============================================================================
Knob::Knob (APVTS& state, const String& paramId, const String& title, bool bipolar, Colour colour)
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
    // Shift + тягнути: точний режим
    slider.setVelocityModeParameters (0.25, 1, 0.0, true, ModifierKeys::shiftModifier);
    slider.getProperties().set ("bipolar", bipolar);

    if (auto* param = state.getParameter (paramId))
    {
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        slider.setTooltip (param->getName (64));
    }

    slider.setPopupMenuEnabled (false);
    addAndMakeVisible (slider);
}

Knob& Knob::help (const juce::String& text)
{
    slider.setTooltip (label.getText() + ": " + text);
    return *this;
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
PillToggle::PillToggle (APVTS& state, const String& paramId, const String& text, Colour c)
    : onColour (c), attachment (state, paramId, *this)
{
    setButtonText (text);
    if (auto* param = state.getParameter (paramId))
        setTooltip (param->getName (64));
}

void PillToggle::paintButton (Graphics& g, bool over, bool)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float rad = r.getHeight() * 0.5f;
    const bool onState = getToggleState();

    g.setColour (onState ? onColour.withAlpha (0.22f) : Theme::track.withAlpha (over ? 0.9f : 0.6f));
    g.fillRoundedRectangle (r, rad);
    g.setColour (onState ? onColour : Theme::border.brighter (over ? 0.3f : 0.1f));
    g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);

    g.setColour (onState ? Theme::text : Theme::muted);
    g.setFont (FontOptions (10.5f, Font::bold));
    g.drawText (getButtonText(), r, Justification::centred);
}

//==============================================================================
Segmented::Segmented (APVTS& state, const String& paramId, const String& t)
    : title (t),
      attachment (*state.getParameter (paramId), [this] (float v) { selected = roundToInt (v); repaint(); }, nullptr)
{
    if (auto* choice = dynamic_cast<AudioParameterChoice*> (state.getParameter (paramId)))
        items = choice->choices;
    attachment.sendInitialUpdate();
    setRepaintsOnMouseActivity (true);
}

Rectangle<float> Segmented::segmentArea() const
{
    auto r = getLocalBounds().toFloat();
    if (title.isNotEmpty()) r.removeFromTop (16.0f);
    const float h = jmin (26.0f, r.getHeight());
    return r.withSizeKeepingCentre (r.getWidth(), h).reduced (2.0f, 0.0f);
}

int Segmented::segmentAt (Point<float> p) const
{
    const auto r = segmentArea();
    if (! r.contains (p) || items.isEmpty()) return -1;
    return jlimit (0, items.size() - 1, (int) ((p.x - r.getX()) / (r.getWidth() / (float) items.size())));
}

void Segmented::paint (Graphics& g)
{
    if (title.isNotEmpty())
    {
        g.setColour (Theme::muted);
        g.setFont (FontOptions (10.0f, Font::bold));
        g.drawText (title.toUpperCase(), getLocalBounds().removeFromTop (14), Justification::centred);
    }

    const auto r = segmentArea();
    g.setColour (Theme::track.withAlpha (0.7f));
    g.fillRoundedRectangle (r, 6.0f);

    const float w = r.getWidth() / (float) jmax (1, items.size());
    for (int i = 0; i < items.size(); ++i)
    {
        auto seg = Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (2.0f);
        if (i == selected)
        {
            g.setColour (Theme::accent.withAlpha (0.28f));
            g.fillRoundedRectangle (seg, 5.0f);
            g.setColour (Theme::accent);
            g.drawRoundedRectangle (seg.reduced (0.5f), 5.0f, 1.0f);
        }
        else if (i == hover)
        {
            g.setColour (Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (seg, 5.0f);
        }
        g.setColour (i == selected ? Theme::text : Theme::muted);
        g.setFont (FontOptions (10.5f, Font::bold));
        g.drawText (items[i].toUpperCase(), seg, Justification::centred);
    }
}

void Segmented::mouseDown (const MouseEvent& e)
{
    const int i = segmentAt (e.position);
    if (i >= 0 && i != selected) attachment.setValueAsCompleteGesture ((float) i);
}

void Segmented::mouseMove (const MouseEvent& e) { const int h = segmentAt (e.position); if (h != hover) { hover = h; repaint(); } }
void Segmented::mouseExit (const MouseEvent&)   { hover = -1; repaint(); }

//==============================================================================
LabeledCombo::LabeledCombo (APVTS& state, const String& paramId, const String& title)
{
    label.setText (title.toUpperCase(), dontSendNotification);
    label.setFont (FontOptions (10.0f, Font::bold));
    label.setColour (Label::textColourId, Theme::muted);
    label.setJustificationType (Justification::centredLeft);
    addAndMakeVisible (label);

    if (auto* choice = dynamic_cast<AudioParameterChoice*> (state.getParameter (paramId)))
        box.addItemList (choice->choices, 1);
    addAndMakeVisible (box);
    attachment = std::make_unique<APVTS::ComboBoxAttachment> (state, paramId, box);
}

void LabeledCombo::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (14));
    box.setBounds (r.removeFromTop (26).reduced (2, 0));
}

//==============================================================================
Section::Section (APVTS& state, const String& t, const String& powerId, int cols)
    : title (t), columns (cols)
{
    if (powerId.isEmpty())
    {
        power.setToggleState (true, dontSendNotification);   // секція без вимикача
        return;
    }
    power.setTooltip ("On / Off");
    addAndMakeVisible (power);
    powerAttachment = std::make_unique<APVTS::ButtonAttachment> (state, powerId, power);
    power.onStateChange = [this] { updateAlpha(); };
}

Component& Section::add (std::unique_ptr<Component> c, int span, int rowHeight)
{
    addAndMakeVisible (*c);
    items.push_back ({ std::move (c), jlimit (1, columns, span), rowHeight });
    updateAlpha();
    return *items.back().c;
}

Knob& Section::knob (APVTS& state, const char* id, const char* name, bool bipolar)
{
    return static_cast<Knob&> (add (std::make_unique<Knob> (state, id, name, bipolar)));
}

void Section::updateAlpha()
{
    const float a = power.getToggleState() ? 1.0f : 0.38f;
    for (auto& it : items) it.c->setAlpha (a);
    repaint();
}

void Section::paint (Graphics& g)
{
    drawCard (g, getLocalBounds().toFloat(), title, power.getToggleState());
}

void Section::resized()
{
    power.setBounds (getWidth() - 36, 7, 24, 24);

    auto area = getLocalBounds().withTrimmedTop (40).reduced (12, 6);
    constexpr int knobH = 104;

    // Розкладка в рядки з урахуванням span
    struct Row { std::vector<size_t> idx; int height = 0; };
    std::vector<Row> rows;
    int used = columns;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (used + items[i].span > columns) { rows.push_back ({}); used = 0; }
        rows.back().idx.push_back (i);
        rows.back().height = jmax (rows.back().height, items[i].height > 0 ? items[i].height : knobH);
        used += items[i].span;
    }
    if (rows.empty()) return;

    int totalH = 0;
    for (auto& r : rows) totalH += r.height;
    if (totalH > area.getHeight())   // стискаємо пропорційно, якщо не влазить
        for (auto& r : rows) r.height = r.height * area.getHeight() / totalH;
    totalH = 0;
    for (auto& r : rows) totalH += r.height;

    const int gapY = jmax (0, (area.getHeight() - totalH) / ((int) rows.size() + 1));
    const int cellW = area.getWidth() / columns;
    int y = area.getY() + gapY;
    for (auto& r : rows)
    {
        int col = 0;
        for (auto i : r.idx)
        {
            const int w = cellW * items[i].span;
            const int h = items[i].height > 0 ? jmin (items[i].height, r.height) : r.height;
            items[i].c->setBounds (area.getX() + col * cellW, y + (r.height - h) / 2, w, h);
            col += items[i].span;
        }
        y += r.height + gapY;
    }
}

//==============================================================================
void MeterBar::feed (float db, bool isGr)
{
    // Миттєвий зліт, плавне падіння, утримання піку
    if (isGr)
        value = db > value ? db : value + (db - value) * 0.25f;
    else
        value = db > value ? db : jmax (db, value - 1.2f);

    if (value >= hold) { hold = value; holdFrames = 45; }
    else if (--holdFrames <= 0) hold = jmax (value, hold - 1.5f);
}

void MeterBar::draw (Graphics& g, Rectangle<float> r, bool isGr, float grRangeDb) const
{
    g.setColour (Theme::track.withAlpha (0.6f));
    g.fillRoundedRectangle (r, 2.0f);

    if (isGr)
    {
        const float frac = jlimit (0.0f, 1.0f, value / grRangeDb);
        if (frac > 0.0f)
        {
            g.setColour (Theme::gr);
            g.fillRoundedRectangle (r.withHeight (r.getHeight() * frac), 2.0f);
        }
        return;
    }

    auto toFrac = [] (float db) { return jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f); };
    const float frac = toFrac (value);
    if (frac > 0.0f)
    {
        auto fill = r.withTrimmedTop (r.getHeight() * (1.0f - frac));
        ColourGradient grad (Theme::hot, r.getX(), r.getY(), Theme::good, r.getX(), r.getBottom(), false);
        grad.addColour (1.0 - (double) toFrac (-9.0f), Theme::warn);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fill, 2.0f);
    }

    if (hold > -48.0f)
    {
        const float hy = r.getBottom() - r.getHeight() * toFrac (hold);
        g.setColour (hold > -0.1f ? Theme::hot : Theme::text.withAlpha (0.8f));
        g.fillRect (r.getX(), hy, r.getWidth(), 1.5f);
    }
}

//==============================================================================
void drawCard (Graphics& g, Rectangle<float> r, const String& title, bool active)
{
    g.setColour (Theme::card);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (Theme::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

    g.setColour (active ? Theme::text : Theme::muted);
    g.setFont (FontOptions (12.0f, Font::bold));
    g.drawText (title, (int) r.getX() + 16, (int) r.getY() + 10, (int) r.getWidth() - 60, 18, Justification::centredLeft);

    g.setColour (Theme::border);
    g.fillRect (r.getX() + 12.0f, r.getY() + 36.0f, r.getWidth() - 24.0f, 1.0f);
}

void drawHeader (Graphics& g, int width, const String& product, const String& subtitle)
{
    g.setGradientFill (ColourGradient (Theme::accent.withAlpha (0.14f), 120.0f, 28.0f,
                                       Colours::transparentBlack, 520.0f, 28.0f, true));
    g.fillRect (0, 0, width, 56);

    g.setColour (Theme::text);
    g.setFont (FontOptions (19.0f, Font::bold));
    g.drawText ("SPACENERD", 20, 14, 130, 28, Justification::centredLeft);
    g.setColour (Theme::accent);
    g.setFont (FontOptions (19.0f));
    g.drawText (product, 136, 14, 120, 28, Justification::centredLeft);

    g.setColour (Theme::muted);
    g.setFont (FontOptions (11.0f));
    g.drawText (subtitle, width - 440, 14, 420, 28, Justification::centredRight);

    g.setFont (FontOptions (10.0f, Font::bold));
    g.drawText ("PRESET", 240, 14, 56, 28, Justification::centredRight);
}

//==============================================================================
PresetBox::PresetBox (AudioProcessor& p) : proc (p)
{
    for (int i = 0; i < p.getNumPrograms(); ++i)
        addItem (p.getProgramName (i), i + 1);
    setTooltip ("Factory presets");
    onChange = [this]
    {
        const int idx = getSelectedId() - 1;
        if (idx >= 0 && idx != proc.getCurrentProgram())
            proc.setCurrentProgram (idx);
    };
    sync();
}

void PresetBox::showPopup()
{
    PopupMenu menu;
    const int current = proc.getCurrentProgram();
    for (int i = 0; i < proc.getNumPrograms(); ++i)
        menu.addItem (i + 1, proc.getProgramName (i), true, i == current);

    Component::SafePointer<PresetBox> safe (this);
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                        [safe] (int result)
                        {
                            if (safe == nullptr || result <= 0) return;
                            safe->proc.setCurrentProgram (result - 1);
                            safe->sync();
                        });
}

void PresetBox::sync()
{
    const int id = proc.getCurrentProgram() + 1;
    if (getSelectedId() != id)
        setSelectedId (id, dontSendNotification);
}
//==============================================================================
void setupEditorSize (AudioProcessorEditor& ed, ValueTree state, int baseW, int baseH)
{
    float fit = 1.0f;
    if (auto* d = Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = d->userArea;
        fit = std::min (((float) area.getHeight() - 150.0f) / (float) baseH, ((float) area.getWidth() - 60.0f) / (float) baseW);
    }
    fit = std::max (0.5f, fit);
    ed.setResizable (true, true);
    ed.setResizeLimits (baseW / 2, baseH / 2, roundToInt ((float) baseW * std::max (1.0f, fit)), roundToInt ((float) baseH * std::max (1.0f, fit)));
    if (auto* c = ed.getConstrainer())
        c->setFixedAspectRatio ((double) baseW / (double) baseH);
    float s = (float) state.getProperty ("uiScale", 0.0f);
    if (s <= 0.0f) s = std::min (1.0f, fit);        // перший раз: не більше екрана
    s = jlimit (0.5f, fit, s);
    ed.setSize (roundToInt ((float) baseW * s), roundToInt ((float) baseH * s));
}

void rememberEditorScale (ValueTree state, int width, int baseW)
{
    const float s = (float) width / (float) baseW;
    if (std::abs ((float) state.getProperty ("uiScale", 0.0f) - s) > 0.005f)
        state.setProperty ("uiScale", s, nullptr);
}
} // namespace snui
