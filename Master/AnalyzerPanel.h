#pragma once

#include "PluginProcessor.h"
#include "../Common/Widgets.h"
#include "../Common/Analysis.h"

/** Аналізатор міксу з вердиктами і Assist: слухає вхід/вихід Master, порівнює з ціллю жанру й епохи. */
class AnalyzerPanel final : public juce::Component
{
public:
    explicit AnalyzerPanel (SpacenerdMasterProcessor&);
    void tick();                        // ~30 Гц з таймера редактора
    void paint (juce::Graphics&) override;
    void resized() override;

    // Для тестів
    void feedForTest();
    void applyAssist();
    const an::Report& getReport() const { return report; }

private:
    float targetYear() const;
    int targetGenreIdx() const;
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float>);

    SpacenerdMasterProcessor& proc;
    an::Analyzer inAn, outAn;
    an::Report report;
    int frameCounter = 0;
    double fsUsed = 0.0;
    juce::String assistMessage;
    int assistMessageFrames = 0;
    std::vector<float> learnLufs;

    snui::Segmented genreSel;
    snui::LabeledCombo decadeSel;
    juce::TextButton learnButton { "LEARN" }, assistButton { "ASSIST" };
    juce::Rectangle<int> graphArea, verdictArea;
};
