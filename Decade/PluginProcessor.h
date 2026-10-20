#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "Presets.h"
#include "DecadeDSP.h"

/** SN Decade: звук барабанів обраного року. Карта рік × кімната, стійка з 6 модулів (порядок змінюється), носій. */
class SpacenerdDecadeProcessor final : public juce::AudioProcessor
{
public:
    SpacenerdDecadeProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SN Decade"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return (int) getDecadePresets().size(); }
    int getCurrentProgram() override { return currentPreset.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Порядок модулів стійки (перетягування в інтерфейсі). */
    std::array<int, dec::kNumModules> getOrder() const;
    void setOrder (const std::array<int, dec::kNumModules>&);

    /** Що зараз робить епоха (для інтерфейсу). */
    dec::Resolved resolve (float year, float room) const;
    int currentFormat() const;
    int currentRoomType() const;

    juce::AudioProcessorValueTreeState apvts;
    std::array<sn::AtomicMax, 2> inPeak, outPeak;
    std::atomic<float> squashGr { 0.0f }, gritView { 0.0f }, roomTail { 0.0f }, loudDiffDb { 0.0f };

private:
    float p (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    bool on (const char* id) const { return p (id) > 0.5f; }
    void processChunk (juce::AudioBuffer<float>&);

    std::atomic<int> currentPreset { 0 };
    std::array<std::atomic<int>, dec::kNumModules> order {};
    double fs = 48000.0;
    int maxBlock = 512, latency = 0;

    dec::ToneMod tone; dec::GritMod grit; dec::FilterMod filter; dec::SquashMod squash; dec::HitMod hit; dec::RoomMod roomMod;
    dec::FormatMod fmt;
    juce::AudioBuffer<float> dryLine, dryBlock;
    int dryPos = 0;
    float yearSm = 1972.0f, roomSm = 40.0f, mixSm = 1.0f, inSm = 1.0f, outSm = 1.0f;
    bool first = true;
    double inLoud = 0.0, outLoud = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpacenerdDecadeProcessor)
};
