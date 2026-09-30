#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "FeedbackDSP.h"
#include "Presets.h"

class SpacenerdFeedbackProcessor final : public juce::AudioProcessor
{
public:
    SpacenerdFeedbackProcessor();
    ~SpacenerdFeedbackProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Spacenerd Feedback"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return (int) getFeedbackPresets().size(); }
    int getCurrentProgram() override { return currentPreset.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    sn::FeedbackEngine engine;
    sn::AtomicMax outPeak;

private:
    float p (const char* id) const noexcept { return params.get (id); }

    sn::ParamCache params;
    std::atomic<int> currentPreset { 0 };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outGainSm;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpacenerdFeedbackProcessor)
};
