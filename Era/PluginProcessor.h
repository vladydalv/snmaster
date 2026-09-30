#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "EraModel.h"
#include "../Master/MasterDSP.h"

class SpacenerdEraProcessor final : public juce::AudioProcessor
{
public:
    SpacenerdEraProcessor();
    ~SpacenerdEraProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Spacenerd Era"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPreset.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Для інтерфейсу
    static constexpr int kBands = 12;
    std::array<std::atomic<float>, kBands> bandLevel {};   // рівні смуг (лінійні) для пікселів
    std::array<sn::AtomicMax, 2> outPeak;
    sn::AtomicMax compGr, limGr;
    sn::LoudnessMeter outLoudness, inLoudness;
    std::atomic<float> matchDb { 0.0f }, pushDb { 0.0f };

private:
    float p (const char* id) const { return params.at (id)->load (std::memory_order_relaxed); }
    void processChunk (juce::AudioBuffer<float>&);
    void updateFilters (const era::Settings& s, bool force);
    void measureTruePeak (const juce::AudioBuffer<float>&, int numCh, int n);

    std::map<juce::String, std::atomic<float>*> params;
    std::atomic<int> currentPreset { 0 };
    int maxBlock = 512;
    double fs = 44100.0, osFs = 176400.0;
    static constexpr int kOsOrder = 2;

    era::Settings cur {};
    bool curValid = false;

    std::array<std::array<sn::Biquad, 5>, 2> eq;           // low cut, low shelf, mid, high shelf, top LP
    std::array<float, 11> eqState {};

    sn::Compressor comp;
    sn::TubeStage tube;
    sn::TapeStage tape;
    sn::Limiter limiter;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler, tpDetector;
    std::vector<float> peakBuf;
    juce::dsp::LinkwitzRileyFilter<float> sideHpf;
    sn::LoudnessMeter preLimLoudness, postLimLoudness;

    std::array<sn::Biquad, kBands> bands;
    std::array<float, kBands> bandEnv {};
    float bandRel = 0.999f;

    float programMs = 0.0f, programCoef = 0.999f;          // рівень програми для авто-порогу
    float pushState = 0.0f, matchState = 0.0f;

    juce::AudioBuffer<float> dryDelay, dryBlock;
    int dryPos = 0;

    juce::SmoothedValue<float> yearSm, intensitySm, mixSm;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> pushSm, outGainSm, matchSm;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpacenerdEraProcessor)
};
