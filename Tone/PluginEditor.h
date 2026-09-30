#pragma once

#include "PluginProcessor.h"
#include "../Common/Widgets.h"

class ToneOutputPanel final : public juce::Component
{
public:
    explicit ToneOutputPanel (SpacenerdToneProcessor&);
    void update();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SpacenerdToneProcessor& proc;
    std::array<snui::MeterBar, 2> in, out;
    snui::MeterBar ds;
    juce::Rectangle<int> barsArea;
    snui::Knob inKnob, mixKnob, outKnob;
};

class ToneContent final : public juce::Component
{
public:
    explicit ToneContent (SpacenerdToneProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() { output.update(); presetBox.sync(); }

private:
    snui::PresetBox presetBox;
    snui::Section tube, tape, exciter, transient, deEss;
    ToneOutputPanel output;
};

class SpacenerdToneEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpacenerdToneEditor (SpacenerdToneProcessor&);
    ~SpacenerdToneEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int baseW = 1166, baseH = 486;

private:
    void timerCallback() override { content.tick(); }

    SpacenerdLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    ToneContent content;
};
