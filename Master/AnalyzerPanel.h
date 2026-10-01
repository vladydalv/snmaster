#pragma once

#include "PluginProcessor.h"
#include "../Common/Widgets.h"
#include "../Common/Analysis.h"

/** Аналізатор треку: слухає ВЕСЬ трек (від RESET), а не останні секунди.
    Крива YOUR TRACK = середній спектр усього треку на вході Master + поточна АЧХ еквалайзера Master:
    стабільна і миттєво реагує на ручки EQ. Оцінка — загальний звук треку: на що схожий, характер,
    гучність і динаміка з поточними налаштуваннями, три головні поради. */
class AnalyzerPanel final : public juce::Component
{
public:
    explicit AnalyzerPanel (SpacenerdMasterProcessor&);
    void tick();                        // ~30 Гц з таймера редактора (лише перемальовування: аналіз — у процесорі)
    void paint (juce::Graphics&) override;
    void resized() override;

    // Для тестів
    void applyAssist();
    void resetListening() { proc.analysis.reset(); }
    const an::Report& getReport() const { return proc.analysis.report; }
    an::Spectrum mixSpectrum() const { return proc.analysis.predicted(); }
    double listenedSeconds() const { return proc.analysis.inAn.songSeconds(); }

private:
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float>);

    SpacenerdMasterProcessor& proc;
    TrackAnalysis& ta;
    juce::String assistMessage;
    int assistMessageFrames = 0;
    int refineSteps = 0;         // після ASSIST: підлаштувати драйв лімітера за фактичною гучністю

    snui::Segmented genreSel;
    snui::LabeledCombo decadeSel, loudSel;
    juce::TextButton resetButton { "RESET" }, assistButton { "ASSIST" }, albumButton { "ALBUM..." };
    juce::Rectangle<int> graphArea, verdictArea;
};
