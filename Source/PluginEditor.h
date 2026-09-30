#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "LookAndFeel.h"

//==============================================================================
class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
          const juce::String& title, bool bipolar = false, juce::Colour colour = Theme::accent);
    void resized() override;

private:
    juce::Label label;
    juce::Slider slider;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

//==============================================================================
class PowerButton final : public juce::ToggleButton
{
public:
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

//==============================================================================
class Section final : public juce::Component
{
public:
    Section (juce::AudioProcessorValueTreeState& state, const juce::String& title,
             const juce::String& powerParamId, int columns);

    Knob& add (std::unique_ptr<Knob> k);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void updateAlpha();

    juce::String title;
    int columns;
    PowerButton power;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
    std::vector<std::unique_ptr<Knob>> knobs;
};

//==============================================================================
class MeterPanel final : public juce::Component
{
public:
    MeterPanel (SpacenerdMasterProcessor&);
    void update();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Bar { float value = -100.0f, hold = -100.0f; int holdFrames = 0; };
    void drawBar (juce::Graphics&, juce::Rectangle<float>, const Bar&, bool isGr) const;
    static void feed (Bar&, float db, bool isGr);

    SpacenerdMasterProcessor& proc;
    std::array<Bar, 2> in, out;
    Bar comp, lim;
    float lufsM = -100.0f, lufsS = -100.0f, lufsI = -100.0f;

    juce::Rectangle<int> barsArea, lufsArea;
    juce::TextButton resetButton { "RESET" };
    Knob inKnob, outKnob;
};

//==============================================================================
class MainContent final : public juce::Component
{
public:
    explicit MainContent (SpacenerdMasterProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    MeterPanel meters;
    void syncPreset();

private:
    SpacenerdMasterProcessor& proc;
    juce::ComboBox presetBox;
    Section eq, comp, sat, width, lim;
};

//==============================================================================
class SpacenerdMasterEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpacenerdMasterEditor (SpacenerdMasterProcessor&);
    ~SpacenerdMasterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int baseW = 1146, baseH = 452;

private:
    void timerCallback() override { content.meters.update(); content.syncPreset(); }

    SpacenerdLookAndFeel lnf;
    MainContent content;
};
