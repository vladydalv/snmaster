#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "StompDSP.h"
#include "Presets.h"

class SpacenerdStompProcessor final : public juce::AudioProcessor
{
public:
    SpacenerdStompProcessor();
    ~SpacenerdStompProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Spacenerd Stomp"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return (int) getStompPresets().size(); }
    int getCurrentProgram() override { return currentPreset.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    int getLatency() const noexcept { return latency; }

    juce::AudioProcessorValueTreeState apvts;
    std::array<sn::AtomicMax, 2> inPeak, outPeak;
    std::atomic<float> lfoView { 0.0f }, bpmView { 0.0f }, trackedHz { 0.0f };

private:
    float p (const char* id) const noexcept { return params.get (id); }
    bool on (const char* id) const { return p (id) > 0.5f; }
    void processChunk (juce::AudioBuffer<float>&);
    float syncedRate (double bpm) const;
    float echoTimeMs (double bpm) const;
    void runOctaver (float* const* data, int numCh, int n, double bpm, float* subClean = nullptr);

    sn::ParamCache params;
    std::atomic<int> currentPreset { 0 };
    int maxBlock = 512;

    st::Pedal pedal;
    st::Octaver octaver;
    st::Modulator mod;
    st::Echo echo;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    juce::dsp::LinkwitzRileyFilter<float> bassSplit;

    // Чистий низ (Clean Bass) затримується на латентність оверсемплінгу, щоб скластися в фазі
    juce::AudioBuffer<float> lowDelay, lowBlock, onsetBlock, subBlock, subDelay;
    int lowPos = 0, subPos = 0, latency = 0;

    // Згладжені налаштування педалі (оновлюються кожні kSub семплів)
    st::Pedal::Settings sm {};
    float bassMix = 0.0f, driveMix = 1.0f, modMix = 0.0f;
    juce::AudioBuffer<float> scratch;
    double wobbleHz (double bpm) const;
    bool first = true;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outGainSm;

    double fs = 44100.0;
    static constexpr int kOsOrder = 2, kSub = 32;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpacenerdStompProcessor)
};
