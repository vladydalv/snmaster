#pragma once

#include "PluginProcessor.h"
#include "../Common/Widgets.h"

/** Карта: по горизонталі рік (1950 → зараз), по вертикалі кімната (сухо → величезно). Точки — пресети. */
class DecadeMap final : public juce::Component, public juce::TooltipClient
{
public:
    explicit DecadeMap (SpacenerdDecadeProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    juce::String getTooltip() override;
    void update();

    static juce::Colour decadeColour (float year);

private:
    juce::Rectangle<float> area() const;
    juce::Point<float> toPoint (float year, float room) const;
    int presetAt (juce::Point<float>) const;
    void setFrom (juce::Point<float>);

    SpacenerdDecadeProcessor& proc;
    juce::ParameterAttachment yearAtt, roomAtt;
    float yearVal = 1972.0f, roomVal = 40.0f;
    bool dragging = false;
    int hoverPreset = -1;
    float tail = 0.0f;
};

/** Модуль стійки: ручка-«грип» для перетягування, вимикач, ручки поправок, жива цифра. */
class ModuleCard final : public juce::Component
{
public:
    ModuleCard (SpacenerdDecadeProcessor&, int module);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void update();

    const int module;
    std::function<void (ModuleCard&, const juce::MouseEvent&, int phase)> onDrag;   // 0 почати, 1 тягнути, 2 відпустити

private:
    juce::Rectangle<int> grip() const { return { 8, 8, 26, 24 }; }
    juce::String liveText() const;

    SpacenerdDecadeProcessor& proc;
    snui::PowerButton power;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAtt;
    std::vector<std::unique_ptr<juce::Component>> controls;
    juce::String live;
};

/** Стійка з 6 модулів у порядку обробки; перетягни модуль за грип, щоб змінити порядок. */
class RackView final : public juce::Component
{
public:
    explicit RackView (SpacenerdDecadeProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void update();

private:
    void layout (bool animate);
    void handleDrag (ModuleCard&, const juce::MouseEvent&, int phase);
    juce::Rectangle<int> slot (int i) const;

    SpacenerdDecadeProcessor& proc;
    std::vector<std::unique_ptr<ModuleCard>> cards;
    std::array<int, dec::kNumModules> order {};
    ModuleCard* dragged = nullptr;
    int dragOffset = 0;
};

class DecadeContent final : public juce::Component
{
public:
    explicit DecadeContent (SpacenerdDecadeProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

private:
    SpacenerdDecadeProcessor& proc;
    snui::PresetBox presetBox;
    DecadeMap map;
    RackView rack;
    snui::LabeledCombo roomTypeBox, formatBox;
    snui::Knob fmtAmountKnob, fmtAgeKnob, authKnob, mixKnob, inKnob, outKnob;
    std::array<snui::MeterBar, 2> inMeters, outMeters;
    juce::Rectangle<int> infoArea, ioArea, meterArea;
    juce::String shownInfo;
};

class SpacenerdDecadeEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpacenerdDecadeEditor (SpacenerdDecadeProcessor&);
    ~SpacenerdDecadeEditor() override;
    void paint (juce::Graphics& g) override { g.fillAll (Theme::bg); }
    void resized() override;
    void refresh() { content.tick(); }
    static constexpr int baseW = 1240, baseH = 730;

private:
    void timerCallback() override { content.tick(); }
    SpacenerdLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 500 };
    DecadeContent content;
};
