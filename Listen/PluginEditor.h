#pragma once

#include "PluginProcessor.h"
#include "../Common/Widgets.h"
#include "../Common/InstrumentIcons.h"

/** Вибір інструмента: AUTO або одна з іконок. В AUTO підсвічено, що вгадав аналіз. */
class InstPicker final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit InstPicker (SpacenerdListenProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void setGuess (int inst) { if (inst != guess) { guess = inst; repaint(); } }

private:
    int cellAt (juce::Point<float>) const;
    juce::Rectangle<float> cell (int i) const;
    SpacenerdListenProcessor& proc;
    juce::ParameterAttachment att;
    int selected = 0, hover = -1, guess = -1;
};

/** Застосовані виправлення: «таблетки» з × (прибрати) і перемикач усіх FIX (порівняти до/після). */
class FixStrip final : public juce::Component
{
public:
    explicit FixStrip (SpacenerdListenProcessor&);
    void update();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    SpacenerdListenProcessor& proc;
    snui::PillToggle onButton;
    std::vector<mix::Fix> fixes;
    std::vector<juce::Rectangle<float>> chips, crosses;
    juce::Point<float> hover;
};

class ListenContent final : public juce::Component
{
public:
    explicit ListenContent (SpacenerdListenProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

private:
    SpacenerdListenProcessor& proc;
    InstPicker picker;
    juce::Label nameLabel;
    juce::TextButton resetButton { "RESET" };
    juce::Viewport adviceView;
    snui::AdviceList advice;
    FixStrip fixStrip;
    juce::Rectangle<int> statusArea, footerArea;
    float level = 0.0f;
    int shownStatus = -2, shownInst = -1;
};

class SpacenerdListenEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpacenerdListenEditor (SpacenerdListenProcessor&);
    ~SpacenerdListenEditor() override;
    void paint (juce::Graphics& g) override { g.fillAll (Theme::bg); }
    void resized() override;

    static constexpr int baseW = 720, baseH = 540;

private:
    void timerCallback() override { content.tick(); }
    SpacenerdLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 500 };
    ListenContent content;
};
