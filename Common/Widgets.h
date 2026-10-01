#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "LookAndFeel.h"

namespace snui
{
using APVTS = juce::AudioProcessorValueTreeState;

//==============================================================================
/** Ручка з підписом і значенням. Подвійний клік — значення за замовчуванням,
    Shift + тягнути — точне налаштування. */
class Knob final : public juce::Component
{
public:
    Knob (APVTS& state, const juce::String& paramId, const juce::String& title,
          bool bipolar = false, juce::Colour colour = Theme::accent);
    void resized() override;
    /** Підказка при наведенні: що робить ручка (а не лише її назва). */
    Knob& help (const juce::String& text);

private:
    juce::Label label;
    juce::Slider slider;
    APVTS::SliderAttachment attachment;
};

//==============================================================================
class PowerButton final : public juce::ToggleButton
{
public:
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

//==============================================================================
/** Текстова кнопка-перемикач («пігулка») для bool-параметра. */
class PillToggle final : public juce::ToggleButton
{
public:
    PillToggle (APVTS& state, const juce::String& paramId, const juce::String& text,
                juce::Colour onColour = Theme::accent);
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    juce::Colour onColour;
    APVTS::ButtonAttachment attachment;
};

//==============================================================================
/** Сегментний перемикач для choice-параметра (напр. TUBE | TAPE | SOFT). */
class Segmented final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Segmented (APVTS& state, const juce::String& paramId, const juce::String& title = {});
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> segmentArea() const;
    int segmentAt (juce::Point<float>) const;

    juce::String title;
    juce::StringArray items;
    int selected = 0, hover = -1;
    juce::ParameterAttachment attachment;
};

//==============================================================================
/** Випадний список для choice-параметра з підписом. */
class LabeledCombo final : public juce::Component, public juce::SettableTooltipClient
{
public:
    LabeledCombo (APVTS& state, const juce::String& paramId, const juce::String& title);
    void resized() override;

private:
    juce::Label label;
    juce::ComboBox box;
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
};

//==============================================================================
/** Картка модуля: заголовок, кнопка живлення, сітка елементів. */
class Section final : public juce::Component
{
public:
    Section (APVTS& state, const juce::String& title, const juce::String& powerParamId, int columns);

    /** span — скільки колонок займає елемент; rowHeight — висота рядка (0 = як у ручки). */
    juce::Component& add (std::unique_ptr<juce::Component> c, int span = 1, int rowHeight = 0);
    Knob& knob (APVTS& state, const char* id, const char* name, bool bipolar = false);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void updateAlpha();

    struct Item { std::unique_ptr<juce::Component> c; int span, height; };
    juce::String title;
    int columns;
    PowerButton power;
    std::unique_ptr<APVTS::ButtonAttachment> powerAttachment;
    std::vector<Item> items;
};

//==============================================================================
/** Метр: пік + утримання; або індикатор зменшення підсилення (GR). */
struct MeterBar
{
    float value = -100.0f, hold = -100.0f;
    int holdFrames = 0;

    void feed (float db, bool isGr);
    void draw (juce::Graphics&, juce::Rectangle<float>, bool isGr, float grRangeDb = 20.0f) const;
};

/** Картка з фоном і заголовком (для власних панелей). */
void drawCard (juce::Graphics&, juce::Rectangle<float>, const juce::String& title, bool active = true);

/** Шапка плагіна: логотип і підзаголовок. */
void drawHeader (juce::Graphics&, int width, const juce::String& product, const juce::String& subtitle);

//==============================================================================
/** Вибір заводського пресету (синхронізується з програмою процесора). */
class PresetBox final : public juce::ComboBox
{
public:
    explicit PresetBox (juce::AudioProcessor&);
    void sync();
    /** Власне меню: пресет застосовується завжди, навіть якщо вибрати той самий повторно. */
    void showPopup() override;

private:
    juce::AudioProcessor& proc;
};
//==============================================================================
/** Розмір вікна плагіна: вміщується в екран (із запасом на заголовок хоста), можна тягнути кутом,
    обраний масштаб запам'ятовується в проєкті. */
void setupEditorSize (juce::AudioProcessorEditor&, juce::ValueTree state, int baseW, int baseH);
void rememberEditorScale (juce::ValueTree state, int width, int baseW);
} // namespace snui
