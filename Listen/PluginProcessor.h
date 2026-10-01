#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "../Common/ListenAnalysis.h"
#include "../Common/MixAdvisor.h"

/** SN Listen: ставиться на доріжку, звук не змінює. Слухає інструмент і передає підсумок у SN Master. */
class SpacenerdListenProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    SpacenerdListenProcessor();
    ~SpacenerdListenProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SN Listen"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    void updateTrackProperties (const TrackProperties&) override;

    /** Обробити накопичене й оновити висновок (message thread; таймер робить це сам). */
    void update();
    void resetStats();

    juce::String displayName() const;
    void setUserName (const juce::String&);

    juce::AudioProcessorValueTreeState apvts;
    mix::Features features;           // message thread
    mix::Verdict verdict;
    bool masterConnected = false;
    int slotIndex() const noexcept { return slot; }
    std::atomic<float> peakView { 0.0f };

private:
    void timerCallback() override { update(); }

    mix::FrameMaker maker;
    mix::ListenStats stats;
    sn::LoudnessMeter loudness;
    mix::Bus bus;
    uint64_t id = 0;
    int slot = -1, ticks = 0;
    uint32_t seenReset = 0;
    std::atomic<bool> resetPending { false };
    juce::String hostName;   // назва доріжки від хоста (Logic передає її AU-плагінам)
    mutable juce::CriticalSection nameLock;
    double fs = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpacenerdListenProcessor)
};
