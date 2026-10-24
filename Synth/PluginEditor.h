#pragma once

#include "PluginProcessor.h"
#include "../Common/Widgets.h"

namespace synui
{
/** Осцилограф виходу (синхронізація по фронту) і стан голосів. */
class Scope final : public juce::Component
{
public:
    explicit Scope (SpacenerdSynthProcessor&);
    void update();
    void paint (juce::Graphics&) override;
    juce::Colour colour = Theme::accent;
    juce::String caption;

private:
    SpacenerdSynthProcessor& proc;
    std::vector<float> ring;
    int wpos = 0;
    std::array<float, 512> shown {};
    snui::MeterBar meters[2];
};

/** Графік обвідної ADSR (живий, за параметрами). */
class EnvGraph final : public juce::Component, private juce::AudioProcessorValueTreeState::Listener
{
public:
    EnvGraph (juce::AudioProcessorValueTreeState&, const char* a, const char* d, const char* s, const char* r, juce::Colour);
    ~EnvGraph() override;
    void paint (juce::Graphics&) override;

private:
    void parameterChanged (const juce::String&, float) override { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<EnvGraph> (this)] { if (sp) sp->repaint(); }); }
    juce::AudioProcessorValueTreeState& state;
    juce::StringArray ids;
    juce::Colour colour;
};

/** Ручка частоти LFO, що стає вибором долі такту при синхронізації з темпом. */
class SyncRate final : public juce::Component
{
public:
    SyncRate (juce::AudioProcessorValueTreeState&, const char* rate, const char* div, const char* sync);
    void resized() override;
    void update();

private:
    juce::AudioProcessorValueTreeState& state;
    juce::String syncId;
    snui::Knob knob;
    snui::LabeledCombo combo;
};

/** Біполярна смужка глибини (заповнення від центру). */
class BipolarBar final : public juce::Slider
{
public:
    void paint (juce::Graphics&) override;
};

/** Матриця модуляції: 4 рядки «джерело → ціль × глибина». */
class ModMatrix final : public juce::Component
{
public:
    explicit ModMatrix (juce::AudioProcessorValueTreeState&);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct Row
    {
        juce::ComboBox src, dst;
        BipolarBar amt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> aSrc, aDst;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aAmt;
    };
    std::array<Row, 4> rows;
};

/** Перемикач вкладок нижньої панелі. */
class Tabs final : public juce::Component
{
public:
    Tabs (juce::StringArray names, std::function<void (int)> onChange);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    int current = 0;

private:
    juce::StringArray names;
    std::function<void (int)> onChange;
};
} // namespace synui

class SynthContent final : public juce::Component
{
public:
    explicit SynthContent (SpacenerdSynthProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();
    void showTab (int);

private:
    void layoutEngine();

    SpacenerdSynthProcessor& proc;
    snui::PresetBox presetBox;
    snui::Segmented engineSel, modeSel;
    // Джерела звуку
    snui::Section oscA, oscB, mixer, sax, bowls;
    synui::Scope scope;
    // Тон
    snui::Section filter, filterEnv, drive;
    // Нижній ряд
    snui::Section amp, perform;
    synui::Tabs tabs;
    snui::Section lfo1, lfo2, matrixCard, arp, chorus, delay, reverb;
    synui::SyncRate* rate1 = nullptr;
    synui::SyncRate* rate2 = nullptr;
    int shownEngine = -1;
    juce::Rectangle<int> sourceArea, tabArea;
};

class SpacenerdSynthEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpacenerdSynthEditor (SpacenerdSynthProcessor&);
    ~SpacenerdSynthEditor() override;
    void paint (juce::Graphics& g) override { g.fillAll (Theme::bg); }
    void resized() override;
    static constexpr int baseW = 1320, baseH = 890;
    void refresh() { content.tick(); }
    void showTab (int t) { content.showTab (t); }

private:
    void timerCallback() override { content.tick(); }
    SpacenerdLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 500 };
    SynthContent content;
};
