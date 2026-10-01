#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "Presets.h"
#include "UnmaskDSP.h"

/** SN Unmask: ставиться на доріжку, яка має поступитися (бас, гітари), ключ — у сайдчейн (бочка, вокал). */
class SpacenerdUnmaskProcessor final : public juce::AudioProcessor
{
public:
    SpacenerdUnmaskProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SN Unmask"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return (int) getUnmaskPresets().size(); }
    int getCurrentProgram() override { return currentPreset.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    Unmasker dsp;
    std::atomic<bool> sidechainConnected { false };

private:
    float p (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    std::atomic<int> currentPreset { 0 };
    std::vector<float> keyBuf;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpacenerdUnmaskProcessor)
};
