#pragma once

#include "PluginProcessor.h"
#include "../Common/Widgets.h"
#include "../Common/InstrumentIcons.h"

/** Панель MIX: доріжки з SN Listen як іконки зі світлофором; наведення — підказка, клік — усі поради. */
class MixPanel final : public juce::Component
{
public:
    explicit MixPanel (SpacenerdMasterProcessor&);
    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Card final : public juce::Component, public juce::SettableTooltipClient
    {
        Card (MixPanel& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }
        MixPanel& owner;
        juce::String name;
        int inst = mix::Other;
        bool overall = false;
        mix::Verdict v;
        juce::String sub;
    };

    void showDetails (Card&);
    void layoutCards();

    SpacenerdMasterProcessor& proc;
    Card overallCard { *this };
    juce::Viewport strip;
    juce::Component stripContent;
    std::vector<std::unique_ptr<Card>> cards;
    juce::String shownKey;
};
