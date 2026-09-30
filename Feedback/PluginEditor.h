#pragma once

#include "PluginProcessor.h"
#include "../Common/Widgets.h"

class FeedbackStatusPanel final : public juce::Component
{
public:
    explicit FeedbackStatusPanel (SpacenerdFeedbackProcessor&);
    void update();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SpacenerdFeedbackProcessor& proc;
    float noteHz = 0.0f, targetHz = 0.0f, bloom = 0.0f;
    snui::MeterBar out;
    juce::Rectangle<int> infoArea;
    snui::Knob outKnob;
};

class FeedbackContent final : public juce::Component
{
public:
    explicit FeedbackContent (SpacenerdFeedbackProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() { status.update(); presetBox.sync(); }

private:
    snui::PresetBox presetBox;
    snui::Section trig, harm, cab;
    FeedbackStatusPanel status;
};

class SpacenerdFeedbackEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpacenerdFeedbackEditor (SpacenerdFeedbackProcessor&);
    ~SpacenerdFeedbackEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int baseW = 1066, baseH = 400;

private:
    void timerCallback() override { content.tick(); }

    SpacenerdLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    FeedbackContent content;
};
