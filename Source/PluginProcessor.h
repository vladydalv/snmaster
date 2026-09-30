#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "DSP.h"
#include "Presets.h"

class SpacenerdMasterProcessor final : public juce::AudioProcessor
{
public:
    SpacenerdMasterProcessor();
    ~SpacenerdMasterProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Spacenerd Master"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return (int) getFactoryPresets().size(); }
    int getCurrentProgram() override { return currentPreset.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Метри (читає UI)
    std::array<sn::AtomicMax, 2> inPeak, outPeak;
    sn::AtomicMax compGr, limGr;
    sn::LoudnessMeter loudness;

private:
    float p (const char* id) const { return params.at (id)->load (std::memory_order_relaxed); }
    bool on (const char* id) const { return p (id) > 0.5f; }
    void updateEq();
    std::atomic<int> currentPreset { 0 };
    void processChunk (juce::AudioBuffer<float>&);
    int maxBlock = 512;

    std::map<juce::String, std::atomic<float>*> params;

    using Filter = juce::dsp::IIR::Filter<float>;
    using Coeffs = juce::dsp::IIR::Coefficients<float>;
    static constexpr int kEqBands = 4;
    std::array<std::array<Filter, kEqBands>, 2> eq;     // [канал][смуга], максимум стерео
    std::array<float, 9> eqCache {};

    sn::Compressor comp;
    sn::Limiter limiter;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;   // сатурація
    std::unique_ptr<juce::dsp::Oversampling<float>> tpDetector;    // детектор true peak
    std::vector<float> peakBuf;
    juce::dsp::LinkwitzRileyFilter<float> sideHpf;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inGainSm, outGainSm, limGainSm;
    juce::SmoothedValue<float> widthSm;

    double fs = 44100.0;
    static constexpr int kOsOrder = 2; // 2^2 = 4x

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpacenerdMasterProcessor)
};
