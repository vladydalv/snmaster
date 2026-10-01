#include "MixPanel.h"

using namespace juce;
using namespace snui;

namespace
{
/** Вміст спливаючого вікна з порадами. */
class DetailsView final : public Component
{
public:
    DetailsView (const String& t, const String& s, int inst, bool overall, const mix::Verdict& v)
        : title (t), sub (s), instrument (inst), isOverall (overall), status (v.status)
    {
        list.set (v);
        addAndMakeVisible (list);
        const int w = 420;
        list.setBounds (20, 74, w - 40, list.preferredHeight (w - 40));
        setSize (w, list.getBottom() + 18);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Theme::card);
        const auto col = statusColour (status);
        const Rectangle<float> ring (20.0f, 16.0f, 44.0f, 44.0f);
        g.setColour (col.withAlpha (0.14f)); g.fillEllipse (ring);
        g.setColour (col); g.drawEllipse (ring.reduced (1.5f), 2.0f);
        if (isOverall)
        {
            g.setColour (Theme::text);
            g.setFont (FontOptions (11.0f, Font::bold));
            g.drawText ("MIX", ring, Justification::centred);
        }
        else drawInstrument (g, instrument, ring.reduced (11.0f), Theme::text, 1.6f);
        g.setColour (Theme::text);
        g.setFont (FontOptions (16.0f, Font::bold));
        g.drawText (title, 76, 16, getWidth() - 96, 22, Justification::centredLeft, true);
        g.setColour (col);
        g.setFont (FontOptions (12.0f, Font::bold));
        g.drawText (sub, 76, 38, getWidth() - 96, 18, Justification::centredLeft, true);
    }

private:
    String title, sub;
    int instrument;
    bool isOverall;
    int status;
    AdviceList list;
};
}

//==============================================================================
void MixPanel::Card::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool hov = isMouseOver();
    g.setColour (hov ? Theme::track.withAlpha (0.9f) : Theme::bg.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 9.0f);
    const auto col = statusColour (v.status);
    g.setColour (hov ? col.withAlpha (0.6f) : Theme::border);
    g.drawRoundedRectangle (r, 9.0f, 1.0f);

    const float d = r.getHeight() - 16.0f;
    const Rectangle<float> ring (r.getX() + 8.0f, r.getCentreY() - d * 0.5f, d, d);
    g.setColour (col.withAlpha (0.14f)); g.fillEllipse (ring);
    g.setColour (col); g.drawEllipse (ring.reduced (1.5f), 2.2f);
    if (overall)
    {
        g.setColour (Theme::text);
        g.setFont (FontOptions (12.0f, Font::bold));
        g.drawText ("MIX", ring, Justification::centred);
    }
    else drawInstrument (g, inst, ring.reduced (d * 0.24f), Theme::text, 1.5f);

    auto t = r.withLeft (ring.getRight() + 10.0f).reduced (0.0f, 8.0f);
    g.setColour (Theme::text);
    g.setFont (FontOptions (13.0f, Font::bold));
    g.drawText (name, t.removeFromTop (t.getHeight() * 0.5f), Justification::bottomLeft, true);
    g.setColour (v.status < 0 ? Theme::muted : col);
    g.setFont (FontOptions (11.0f));
    g.drawText (sub, t, Justification::topLeft, true);
}

void MixPanel::Card::mouseDown (const MouseEvent&) { owner.showDetails (*this); }

//==============================================================================
MixPanel::MixPanel (SpacenerdMasterProcessor& p) : proc (p)
{
    overallCard.overall = true;
    overallCard.name = "Whole mix";
    strip.setViewedComponent (&stripContent, false);
    strip.setScrollBarsShown (false, true);
    strip.setScrollBarThickness (5);
    addAndMakeVisible (overallCard);
    addAndMakeVisible (strip);
    tick();
}

static String subFor (const mix::Verdict& v)
{
    if (v.status < 0) return "listening...";
    int probs = 0, checks = 0;
    for (int i = 0; i < v.numItems; ++i) { probs += v.items[i].sev >= 2; checks += v.items[i].sev == 1; }
    if (probs + checks == 0) return "sounds right";
    StringArray parts;
    if (probs > 0) parts.add (String (probs) + (probs == 1 ? " problem" : " problems"));
    if (checks > 0) parts.add (String (checks) + " to check");
    return parts.joinIntoString (", ");
}

void MixPanel::tick()
{
    const auto& w = proc.mixWatch;
    String key;
    for (auto& t : w.tracks)
        key << t.slot << ":" << t.inst << ":" << t.name << ":" << t.v.status << ":" << t.v.numItems << ":" << String::fromUTF8 (t.v.items[0].title) << "|";
    key << "o" << w.overall.status << w.overall.numItems << String::fromUTF8 (w.overall.items[0].title);

    if (key == shownKey) return;
    shownKey = key;

    overallCard.v = w.overall;
    overallCard.sub = w.tracks.empty() ? String ("no SN Listen yet") : subFor (w.overall);
    overallCard.setTooltip (w.tracks.empty() ? String ("Put SN Listen on your tracks to get mix advice here.") : verdictText (w.overall));
    overallCard.repaint();

    while (cards.size() > w.tracks.size()) { stripContent.removeChildComponent (cards.back().get()); cards.pop_back(); }
    while (cards.size() < w.tracks.size()) { cards.push_back (std::make_unique<Card> (*this)); stripContent.addAndMakeVisible (*cards.back()); }
    for (size_t i = 0; i < cards.size(); ++i)
    {
        auto& c = *cards[i];
        const auto& t = w.tracks[i];
        c.name = t.name.isNotEmpty() ? t.name : String (mix::instName (t.inst));
        c.inst = t.inst;
        c.v = t.v;
        c.sub = subFor (t.v);
        c.setTooltip (c.name + " (" + mix::instName (t.inst) + ")\n\n" + verdictText (t.v));
        c.repaint();
    }
    layoutCards();
    repaint();
}

void MixPanel::layoutCards()
{
    constexpr int cw = 168, gap = 8;
    const int h = strip.getHeight() - 7;
    stripContent.setSize (std::max (strip.getWidth(), (int) cards.size() * (cw + gap)), h);
    for (size_t i = 0; i < cards.size(); ++i) cards[i]->setBounds ((int) i * (cw + gap), 0, cw, h);
}

void MixPanel::showDetails (Card& c)
{
    auto view = std::make_unique<DetailsView> (c.overall ? String ("Whole mix") : c.name, c.overall ? c.sub : String (mix::instName (c.inst)) + "  -  " + c.sub,
                                               c.inst, c.overall, c.v);
    auto* top = getTopLevelComponent();
    CallOutBox::launchAsynchronously (std::move (view), top->getLocalArea (&c, c.getLocalBounds()), top);
}

void MixPanel::paint (Graphics& g)
{
    drawCard (g, getLocalBounds().toFloat(), "MIX  (SN Listen on your tracks)");
    if (proc.mixWatch.tracks.empty())
    {
        auto r = strip.getBounds().toFloat();
        float x = r.getX() + 6.0f;
        for (int i = 0; i < mix::Other; ++i)
        {
            drawInstrument (g, i, { x, r.getY() + 12.0f, 30.0f, 30.0f }, Theme::muted.withAlpha (0.55f), 1.3f);
            x += 40.0f;
        }
        g.setColour (Theme::muted);
        g.setFont (FontOptions (12.5f));
        g.drawFittedText ("Put SN Listen on each track (last plugin in the chain). Tracks show up here green / yellow / red, "
                          "with what to fix for the chosen genre. Hover for tips, click for details.",
                          Rectangle<int> ((int) x + 16, (int) r.getY() + 4, (int) (r.getRight() - x - 20), (int) r.getHeight() - 10),
                          Justification::centredLeft, 3);
    }
}

void MixPanel::resized()
{
    auto r = getLocalBounds().withTrimmedTop (44).reduced (12, 0).withTrimmedBottom (10);
    overallCard.setBounds (r.removeFromLeft (220));
    r.removeFromLeft (12);
    strip.setBounds (r);
    layoutCards();
}
