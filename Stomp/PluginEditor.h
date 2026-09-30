#pragma once

#include "PluginProcessor.h"
#include "../Common/Widgets.h"

/** XY-дисплей схем: X — схема (морф між епохами), Y — гейн. Пікселі мерехтять від сигналу. */
class StompPad final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit StompPad (SpacenerdStompProcessor&);
    void update (float outLevel);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static constexpr int cols = 50, rows = 16;
    static juce::Colour circuitColour (float x);

private:
    juce::Rectangle<float> grid() const;
    void setFromPoint (juce::Point<float>);

    SpacenerdStompProcessor& proc;
    juce::ParameterAttachment circAtt, gainAtt;
    float circVal = 2.0f, gainVal = 60.0f, level = 0.0f;
    bool dragging = false;
    std::array<float, cols * rows> flicker {};
    juce::Random rng;
};

class OutputPanel final : public juce::Component
{
public:
    explicit OutputPanel (SpacenerdStompProcessor&);
    float update();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SpacenerdStompProcessor& proc;
    std::array<snui::MeterBar, 2> in, out;
    snui::Knob outKnob;
    juce::Rectangle<int> barsArea;
};

class StompContent final : public juce::Component
{
public:
    explicit StompContent (SpacenerdStompProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

private:
    SpacenerdStompProcessor& proc;
    snui::PresetBox presetBox;
    StompPad pad;
    snui::Section drive, modSec, echoSec;
    OutputPanel output;
};

class SpacenerdStompEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpacenerdStompEditor (SpacenerdStompProcessor&);
    ~SpacenerdStompEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int baseW = 1112, baseH = 612;
    void tickForTest() { content.tick(); }

private:
    void timerCallback() override { content.tick(); }

    SpacenerdLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    StompContent content;
};
