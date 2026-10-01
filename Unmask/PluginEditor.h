#pragma once

#include "PluginProcessor.h"
#include "../Common/Widgets.h"

/** Спектри доріжки й ключа, знайдена частота конфлікту і скільки зараз вирізається. */
class ClashDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ClashDisplay (SpacenerdUnmaskProcessor&);
    void update();
    void paint (juce::Graphics&) override;

private:
    float xFor (float hz, juce::Rectangle<float> r) const;
    SpacenerdUnmaskProcessor& proc;
    std::array<float, an::kBands> mainDb {}, keyDb {};
    float gr = 0.0f, hz = 300.0f;
};

class UnmaskContent final : public juce::Component
{
public:
    explicit UnmaskContent (SpacenerdUnmaskProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

private:
    SpacenerdUnmaskProcessor& proc;
    snui::PresetBox presetBox;
    ClashDisplay display;
    snui::Section controls;
    bool shownSc = false;
    snui::Knob* freqKnob = nullptr;
};

class SpacenerdUnmaskEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpacenerdUnmaskEditor (SpacenerdUnmaskProcessor&);
    ~SpacenerdUnmaskEditor() override;
    void paint (juce::Graphics& g) override { g.fillAll (Theme::bg); }
    void resized() override;
    static constexpr int baseW = 900, baseH = 600;
    void refresh() { content.tick(); }

private:
    void timerCallback() override { content.tick(); }
    SpacenerdLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 500 };
    UnmaskContent content;
};
