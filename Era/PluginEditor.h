#pragma once

#include "PluginProcessor.h"
#include "../Common/Widgets.h"

/** Піксельний XY-дисплей: X — рік, Y — інтенсивність. Пікселі мерехтять від музики. */
class EraPad final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit EraPad (SpacenerdEraProcessor&);
    void update();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static constexpr int cols = 66, rows = 18;   // 1960…2025, по року на стовпчик

private:
    juce::Rectangle<float> grid() const;
    void setFromPoint (juce::Point<float>);
    static juce::Colour eraColour (float year);

    SpacenerdEraProcessor& proc;
    juce::ParameterAttachment yearAtt, intAtt;
    float yearVal = 1975.0f, intVal = 60.0f;
    std::array<float, SpacenerdEraProcessor::kBands> bands {};
    std::array<float, cols * rows> flicker {};
    juce::Random rng;
    bool dragging = false;
};

class EraContent final : public juce::Component
{
public:
    explicit EraContent (SpacenerdEraProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

private:
    SpacenerdEraProcessor& proc;
    snui::PresetBox presetBox;
    snui::PillToggle matchButton;
    EraPad pad;
    snui::Segmented genreSel;
    snui::Knob yearKnob, intKnob, mixKnob, outKnob;
    std::array<snui::MeterBar, 2> out;
    snui::MeterBar comp, lim;
    float lufs = -100.0f, push = 0.0f, match = 0.0f;
    juce::Rectangle<int> meterArea;
};

class SpacenerdEraEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpacenerdEraEditor (SpacenerdEraProcessor&);
    ~SpacenerdEraEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int baseW = 1066, baseH = 560;

private:
    void timerCallback() override { content.tick(); }

    SpacenerdLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    EraContent content;
};
