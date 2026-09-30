#pragma once

#include "PluginProcessor.h"
#include "../Common/Widgets.h"
#include "../Common/Analysis.h"
#include <juce_audio_formats/juce_audio_formats.h>

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
    juce::ParameterAttachment yearAtt, intAtt, lowAtt, highAtt, lowAmtAtt, highAmtAtt;
    float yearVal = 1975.0f, intVal = 60.0f, lowVal = 1972.0f, highVal = 2015.0f, lowAmtVal = 70.0f, highAmtVal = 70.0f;
    enum class Drag { none, main, low, high };
    Drag drag = Drag::none;
    bool splitOn() const;
    juce::Rectangle<float> handleRect (bool low) const;
    std::array<float, SpacenerdEraProcessor::kBands> bands {};
    std::array<float, cols * rows> flicker {};
    juce::Random rng;
};

class EraContent final : public juce::Component
{
public:
    explicit EraContent (SpacenerdEraProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();
    void loadReferenceForTest (const juce::File& f) { loadReference (f); }
    juce::String getRefText() const { return refText; }

private:
    SpacenerdEraProcessor& proc;
    snui::PresetBox presetBox;
    snui::PillToggle matchButton;
    EraPad pad;
    snui::Segmented genreSel;
    snui::Knob yearKnob, intKnob, mixKnob, outKnob, lowKnob, highKnob;
    snui::PillToggle splitButton;
    juce::TextButton refButton { "REFERENCE MATCH..." };
    juce::String refText { "Load a record you love: Era finds its decade" };
    std::unique_ptr<juce::FileChooser> chooser;
    std::atomic<bool> analysing { false };
    void loadReference (const juce::File&);
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

    static constexpr int baseW = 1166, baseH = 560;

private:
    void timerCallback() override { content.tick(); }

    SpacenerdLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    EraContent content;
};
