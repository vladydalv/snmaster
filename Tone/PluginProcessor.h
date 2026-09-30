#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "ToneDSP.h"
#include "Presets.h"

class SpacenerdToneProcessor final : public juce::AudioProcessor
{
public:
    SpacenerdToneProcessor();
    ~SpacenerdToneProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Spacenerd Tone"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return (int) getTonePresets().size(); }
    int getCurrentProgram() override { return currentPreset.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    std::array<sn::AtomicMax, 2> inPeak, outPeak;
    sn::AtomicMax deEssGr;

private:
    float p (const char* id) const noexcept { return params.get (id); }
    bool on (const char* id) const { return p (id) > 0.5f; }
    void processChunk (juce::AudioBuffer<float>&);

    sn::ParamCache params;
    std::atomic<int> currentPreset { 0 };
    int maxBlock = 512;

    sn::TransientShaper transient;
    sn::TubeStage tube;
    sn::TapeStage tape;
    sn::Exciter exciter;
    sn::WowFlutter wow;
    sn::DeEsser deEsser;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    // Сухий сигнал, затриманий на латентність — для Mix без гребінчастого фільтра
    juce::AudioBuffer<float> dryDelay, dryBlock;
    int dryPos = 0, latency = 0;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inGainSm, outGainSm;
    juce::SmoothedValue<float> mixSm, wowSm;
    float trMix = 0, tubeMixF = 0, tapeMixF = 0, excMixF = 0, dsMixF = 0;
    float tubeDriveSm = 0, tubeWetSm = 1, tapeDriveSm = 0;
    bool firstBlock = true;
    juce::AudioBuffer<float> scratch;

    double fs = 44100.0;
    static constexpr int kOsOrder = 2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpacenerdToneProcessor)
};
