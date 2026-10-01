#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MixBus.h"
#include "LookAndFeel.h"

/** Лінійні іконки інструментів (векторні, масштабуються під будь-який розмір). */
namespace snui
{
inline juce::Path instrumentPath (int inst)
{
    using juce::Path;
    Path p;
    switch (inst)
    {
        case mix::Kick:     // бочка спереду: обід, пластик, ніжки
            p.addEllipse (0.14f, 0.12f, 0.72f, 0.72f);
            p.addEllipse (0.36f, 0.34f, 0.28f, 0.28f);
            p.startNewSubPath (0.24f, 0.78f); p.lineTo (0.14f, 0.94f);
            p.startNewSubPath (0.76f, 0.78f); p.lineTo (0.86f, 0.94f);
            break;
        case mix::Snare:    // малий збоку + палички
            p.addEllipse (0.12f, 0.42f, 0.76f, 0.2f);
            p.startNewSubPath (0.12f, 0.52f); p.lineTo (0.12f, 0.78f);
            p.addArc (0.12f, 0.68f, 0.76f, 0.2f, juce::MathConstants<float>::pi * 0.5f, juce::MathConstants<float>::pi * 1.5f, false);
            p.startNewSubPath (0.88f, 0.52f); p.lineTo (0.88f, 0.78f);
            p.startNewSubPath (0.26f, 0.08f); p.lineTo (0.52f, 0.46f);
            p.startNewSubPath (0.78f, 0.08f); p.lineTo (0.56f, 0.46f);
            break;
        case mix::Drums:    // тарілка на стійці
            p.addEllipse (0.08f, 0.26f, 0.84f, 0.14f);
            p.addEllipse (0.44f, 0.27f, 0.12f, 0.06f);
            p.startNewSubPath (0.5f, 0.4f); p.lineTo (0.5f, 0.82f);
            p.startNewSubPath (0.5f, 0.82f); p.lineTo (0.3f, 0.94f);
            p.startNewSubPath (0.5f, 0.82f); p.lineTo (0.7f, 0.94f);
            p.startNewSubPath (0.5f, 0.82f); p.lineTo (0.5f, 0.94f);
            break;
        case mix::Bass:     // бас: менший корпус, довгий гриф, 4 кілки
        case mix::Guitar:   // електрогітара: «вісімка», звукознімачі, 6 кілків
        {
            const bool bass = inst == mix::Bass;
            // Корпус одним контуром (без внутрішніх дуг)
            Path body;
            body.startNewSubPath (0.5f, 0.52f);
            body.cubicTo (0.62f, 0.52f, 0.68f, 0.58f, 0.64f, 0.67f);
            body.cubicTo (0.62f, 0.71f, 0.76f, 0.75f, 0.74f, 0.86f);
            body.cubicTo (0.72f, 0.97f, 0.6f, 1.0f, 0.5f, 1.0f);
            body.cubicTo (0.4f, 1.0f, 0.28f, 0.97f, 0.26f, 0.86f);
            body.cubicTo (0.24f, 0.75f, 0.38f, 0.71f, 0.36f, 0.67f);
            body.cubicTo (0.32f, 0.58f, 0.38f, 0.52f, 0.5f, 0.52f);
            body.closeSubPath();
            if (bass) body.applyTransform (juce::AffineTransform::scale (0.84f, 0.84f, 0.5f, 1.0f));
            p.addPath (body);
            const float bodyTop = bass ? 0.6f : 0.52f, neckTop = bass ? 0.1f : 0.2f;
            p.addRoundedRectangle (0.475f, neckTop, 0.05f, bodyTop - neckTop + 0.04f, 0.015f);
            p.addRoundedRectangle (0.455f, neckTop - 0.11f, 0.09f, 0.12f, 0.03f);
            const int pegs = bass ? 2 : 3;
            for (int i = 0; i < pegs; ++i)
            {
                const float y = neckTop - 0.085f + (float) i * (0.1f / (float) pegs);
                p.startNewSubPath (0.455f, y); p.lineTo (0.41f, y);
                p.startNewSubPath (0.545f, y); p.lineTo (0.59f, y);
            }
            // Звукознімачі й струнотримач
            static constexpr float gtrLines[] { 0.7f, 0.79f, 0.9f }, bassLines[] { 0.79f, 0.9f };
            for (int i = 0; i < (bass ? 2 : 3); ++i)
            {
                const float y = bass ? bassLines[i] : gtrLines[i];
                p.startNewSubPath (0.43f, y); p.lineTo (0.57f, y);
            }
            // Нахил, як на класичних іконках: так інструмент заповнює квадрат
            {
                const auto rot = juce::AffineTransform::rotation (0.75f, 0.5f, 0.55f);
                p.applyTransform (rot);
                const auto bb = p.getBounds();       // вписати в квадрат з полями
                const float k = 0.92f / std::max (bb.getWidth(), bb.getHeight());
                p.applyTransform (juce::AffineTransform::translation (-bb.getCentreX(), -bb.getCentreY()).scaled (k).translated (0.5f, 0.5f));
            }
            break;
        }
        case mix::Vocal:    // мікрофон
            p.addRoundedRectangle (0.36f, 0.06f, 0.28f, 0.44f, 0.14f);
            p.startNewSubPath (0.36f, 0.24f); p.lineTo (0.64f, 0.24f);
            p.startNewSubPath (0.36f, 0.33f); p.lineTo (0.64f, 0.33f);
            p.addArc (0.24f, 0.18f, 0.52f, 0.5f, juce::MathConstants<float>::pi * 0.5f, juce::MathConstants<float>::pi * 1.5f, true);
            p.startNewSubPath (0.5f, 0.68f); p.lineTo (0.5f, 0.86f);
            p.startNewSubPath (0.32f, 0.92f); p.lineTo (0.68f, 0.92f);
            break;
        case mix::Keys:     // клавіатура
        {
            p.addRoundedRectangle (0.06f, 0.26f, 0.88f, 0.5f, 0.05f);
            for (int i = 1; i < 6; ++i) { const float x = 0.06f + 0.88f * (float) i / 6.0f; p.startNewSubPath (x, 0.56f); p.lineTo (x, 0.76f); }
            for (int i : { 1, 2, 4, 5 }) { const float x = 0.06f + 0.88f * (float) i / 6.0f; p.addRectangle (x - 0.035f, 0.26f, 0.07f, 0.3f); }
            break;
        }
        default:            // хвиля
            p.startNewSubPath (0.06f, 0.5f);
            for (int i = 1; i <= 40; ++i)
            {
                const float x = (float) i / 40.0f;
                const float env = std::sin (juce::MathConstants<float>::pi * x);
                p.lineTo (0.06f + 0.88f * x, 0.5f - 0.32f * env * std::sin (x * juce::MathConstants<float>::twoPi * 3.0f));
            }
            break;
    }
    return p;
}

/** Іконка в квадраті r. */
inline void drawInstrument (juce::Graphics& g, int inst, juce::Rectangle<float> r, juce::Colour c, float stroke = 1.6f)
{
    const float s = std::min (r.getWidth(), r.getHeight());
    const auto sq = r.withSizeKeepingCentre (s, s);
    auto p = instrumentPath (inst);
    p.applyTransform (juce::AffineTransform::scale (s).translated (sq.getX(), sq.getY()));
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

inline juce::Colour statusColour (int status)
{
    return status < 0 ? Theme::muted : status == 0 ? Theme::good : status == 1 ? Theme::warn : Theme::hot;
}

inline juce::String statusWord (int status)
{
    return status < 0 ? "listening" : status == 0 ? "good" : status == 1 ? "check" : "problem";
}

/** Текст порад для підказки. */
inline juce::String verdictText (const mix::Verdict& v)
{
    juce::String s;
    for (int i = 0; i < v.numItems; ++i)
    {
        if (i > 0) s << "\n\n";
        s << (v.items[i].sev >= 2 ? "[!] " : v.items[i].sev == 1 ? "[~] " : "") << juce::String::fromUTF8 (v.items[i].title) << ": "
          << juce::String::fromUTF8 (v.items[i].text);
    }
    return s.isEmpty() ? juce::String ("All good.") : s;
}

//==============================================================================
/** Список порад: кольорова позначка, заголовок, пояснення (з переносом рядків). */
class AdviceList final : public juce::Component
{
public:
    void set (const mix::Verdict& v)
    {
        if (std::memcmp (&v, &shown, sizeof (mix::Verdict)) == 0) return;
        shown = v;
        repaint();
    }

    int preferredHeight (int width) const
    {
        int h = 0;
        for (int i = 0; i < shown.numItems; ++i) h += itemHeight (i, width) + 10;
        return std::max (h, 40);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace juce;
        float y = 0.0f;
        const float w = (float) getWidth();
        if (shown.numItems == 0)
        {
            g.setColour (Theme::good);
            g.setFont (FontOptions (14.0f, Font::bold));
            g.drawText ("Sounds right: nothing to fix here.", getLocalBounds(), Justification::topLeft);
            return;
        }
        for (int i = 0; i < shown.numItems; ++i)
        {
            const auto& it = shown.items[i];
            const int h = itemHeight (i, getWidth());
            g.setColour (it.sev >= 2 ? Theme::hot : it.sev == 1 ? Theme::warn : Theme::accent2);
            g.fillEllipse (2.0f, y + 5.0f, 8.0f, 8.0f);
            g.setColour (Theme::text);
            g.setFont (FontOptions (13.5f, Font::bold));
            g.drawText (String::fromUTF8 (it.title), Rectangle<float> (18.0f, y, w - 18.0f, 18.0f), Justification::centredLeft, true);
            AttributedString a;
            a.append (String::fromUTF8 (it.text), FontOptions (12.5f), Theme::muted);
            a.setWordWrap (AttributedString::byWord);
            TextLayout tl; tl.createLayout (a, w - 18.0f);
            tl.draw (g, Rectangle<float> (18.0f, y + 20.0f, w - 18.0f, (float) h - 20.0f));
            y += (float) h + 10.0f;
        }
    }

private:
    int itemHeight (int i, int width) const
    {
        using namespace juce;
        AttributedString a;
        a.append (String::fromUTF8 (shown.items[i].text), FontOptions (12.5f), Theme::muted);
        a.setWordWrap (AttributedString::byWord);
        TextLayout tl; tl.createLayout (a, (float) width - 18.0f);
        return 20 + (int) std::ceil (tl.getHeight());
    }

    mix::Verdict shown;
};
} // namespace snui
