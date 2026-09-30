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
    void tick();                        // ~30 Гц з таймера редактора
    void paint (juce::Graphics&) override;
    void resized() override;

    // Для тестів
    void feedForTest();
    void applyAssist();
    void resetListening();
    const an::Report& getReport() const { return report; }
    an::Spectrum mixSpectrum() const { return predicted(); }
    double listenedSeconds() const { return inAn.songSeconds(); }

private:
    float targetYear() const;
    int targetGenreIdx() const;
    an::Spectrum eqResponse() const;            // АЧХ еквалайзера Master на смугах аналізатора
    an::Spectrum predicted() const;             // весь трек (вхід) + EQ
    void updateReport();
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float>);
    bool dynamicsChanged();

    SpacenerdMasterProcessor& proc;
    an::Analyzer inAn, outAn;
    an::Report report;
    juce::String soundsLike, character;
    int frameCounter = 0;
    double fsUsed = 0.0;
    juce::String assistMessage;
    int assistMessageFrames = 0;

    // Гучність і піки на виході — з поточними налаштуваннями динаміки (скидається, коли їх змінюють)
    double outLoudE = 0.0, inLoudE = 0.0;
    int outLoudN = 0, inLoudN = 0;
    float tpSince = 0.0f, tickPeak = 0.0f;
    std::array<float, 10> dynSnapshot {};
    int settleTicks = 0;

    snui::Segmented genreSel;
    snui::LabeledCombo decadeSel;
    juce::TextButton resetButton { "RESET" }, assistButton { "ASSIST" };
    juce::Rectangle<int> graphArea, verdictArea;
};
