#pragma once

#include "PluginProcessor.h"
#include "../Common/Widgets.h"
#include "AnalyzerPanel.h"

//==============================================================================
class MeterPanel final : public juce::Component
{
public:
    explicit MeterPanel (SpacenerdMasterProcessor&);
    void update();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SpacenerdMasterProcessor& proc;
    std::array<snui::MeterBar, 2> in, out;
    snui::MeterBar comp, lim;
    float lufsM = -100.0f, lufsS = -100.0f, lufsI = -100.0f, tpMax = -100.0f;

    juce::Rectangle<int> barsArea, lufsArea;
    juce::TextButton resetButton { "RESET" };
    snui::Knob inKnob, outKnob;
};

//==============================================================================
class MainContent final : public juce::Component
{
public:
    explicit MainContent (SpacenerdMasterProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

    MeterPanel meters;
    AnalyzerPanel analyzer;

private:
    SpacenerdMasterProcessor& proc;
    snui::PresetBox presetBox;
    snui::PillToggle matchButton;
    float shownMatch = 0.0f;
    snui::Section eq, comp, sat, width, lim;
};

//==============================================================================
class SpacenerdMasterEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpacenerdMasterEditor (SpacenerdMasterProcessor&);
    ~SpacenerdMasterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int baseW = 1166, baseH = 806;
    static constexpr int analyzerH = 310;

private:
    void timerCallback() override { content.tick(); }

    SpacenerdLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    MainContent content;
};
