#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Theme
{
    inline const juce::Colour bg       { 0xff0f1115 };
    inline const juce::Colour card     { 0xff171a21 };
    inline const juce::Colour border   { 0xff242834 };
    inline const juce::Colour track    { 0xff2a2f3b };
    inline const juce::Colour knobTop  { 0xff262a34 };
    inline const juce::Colour knobBot  { 0xff1a1d24 };
    inline const juce::Colour text     { 0xffe6e9ef };
    inline const juce::Colour muted    { 0xff7d8596 };
    inline const juce::Colour accent   { 0xff8b7cff };
    inline const juce::Colour accent2  { 0xff5ad1e6 };
    inline const juce::Colour gr       { 0xfff5a524 };
    inline const juce::Colour good     { 0xff4ade80 };
    inline const juce::Colour warn     { 0xfffacc15 };
    inline const juce::Colour hot      { 0xfff87171 };
}

class SpacenerdLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SpacenerdLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, Theme::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, Theme::accent.withAlpha (0.35f));
        setColour (juce::Label::textColourId, Theme::text);
        setColour (juce::TextEditor::textColourId, Theme::text);
        setColour (juce::TextEditor::backgroundColourId, Theme::card);
        setColour (juce::TextEditor::highlightColourId, Theme::accent.withAlpha (0.35f));
        setColour (juce::CaretComponent::caretColourId, Theme::accent);
        setColour (juce::TextButton::buttonColourId, Theme::track);
        setColour (juce::TextButton::textColourOffId, Theme::muted);
        setColour (juce::TextButton::textColourOnId, Theme::text);
        setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        using namespace juce;
        const auto bounds = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
        const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const float arcR = radius - 3.0f;
        const float lineW = 3.5f;
        const bool enabled = s.isEnabled();
        const auto accent = s.findColour (Slider::rotarySliderFillColourId);

        // Трек
        Path trackArc;
        trackArc.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (Theme::track);
        g.strokePath (trackArc, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));

        // Значення (біполярні ручки: від центру)
        const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
        const float angle = startAngle + pos * (endAngle - startAngle);
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

        if (std::abs (angle - from) > 0.001f)
        {
            Path valueArc;
            valueArc.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, jmin (from, angle), jmax (from, angle), true);
            g.setColour (enabled ? accent : Theme::muted);
            g.strokePath (valueArc, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));
        }

        // Корпус
        const float bodyR = arcR - 7.0f;
        const auto body = Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c);
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillEllipse (body.translated (0.0f, 2.0f));
        g.setGradientFill (ColourGradient (Theme::knobTop, c.x, body.getY(), Theme::knobBot, c.x, body.getBottom(), false));
        g.fillEllipse (body);
        g.setColour (Colours::white.withAlpha (0.06f));
        g.drawEllipse (body.reduced (0.5f), 1.0f);

        // Вказівник
        const Point<float> p1 = c.getPointOnCircumference (bodyR * 0.35f, angle);
        const Point<float> p2 = c.getPointOnCircumference (bodyR * 0.85f, angle);
        g.setColour (enabled ? Theme::text : Theme::muted);
        g.drawLine ({ p1, p2 }, 2.2f);

        if (s.isMouseOverOrDragging() && enabled)
        {
            g.setColour (accent.withAlpha (0.12f));
            g.fillEllipse (body.expanded (3.0f));
        }
    }

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (juce::FontOptions (11.5f));
        l->setJustificationType (juce::Justification::centred);
        return l;
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (Theme::track.brighter (down ? 0.2f : over ? 0.1f : 0.0f));
        g.fillRoundedRectangle (r, 5.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::FontOptions (11.0f, juce::Font::bold); }
};
