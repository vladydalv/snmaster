#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "Presets.h"
#include "SynthDSP.h"
#include "SynthFX.h"

/** SN Synth: аналоговий полі-синт (баси, ліди, пади) + фізична модель баритон-саксофона. */
class SpacenerdSynthProcessor final : public juce::AudioProcessor
{
public:
    SpacenerdSynthProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SN Synth"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return (int) getSynthPresets().size(); }
    int getCurrentProgram() override { return currentPreset.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    sn::AtomicMax outPeak[2];
    sn::StereoFifo scope;
    std::atomic<int> activeVoices { 0 };

private:
    float p (const char* id) const { return apvts.getRawParameterValue (id)->load (std::memory_order_relaxed); }
    void readParams();
    void handleMidi (const juce::MidiMessage&);
    void noteOn (int note, float vel);
    void noteOff (int note);
    void allOff();
    void render (float* L, float* R, int n);
    void runArp (int n);

    double fs = 48000.0;
    int maxBlock = 512;
    syn::Global g;
    std::array<syn::Voice, syn::kMaxVoices> voices;
    syn::Decimator decim;
    syn::Lfo lfo[2];
    float vibPh = 0.0f;
    syn::Rng rng;
    std::vector<float> osL, osR;
    uint64_t ageCounter = 0;
    float lastPitch = -1.0f;
    std::vector<int> monoStack;
    bool sustain = false;
    std::array<bool, 128> sustained {};
    int lastEngine = -1, lastMode = -1;

    // Арпеджіатор
    std::vector<int> arpHeld, arpOrder;
    int arpIdx = 0, arpDir = 1, arpNote = -1, arpOffIn = 0;
    double arpPhase = 0.0, lastPpq = -1.0;
    bool arpWasOn = false;
    std::array<float, 128> heldVel {};

    // Ефекти
    syn::Chorus chorus;
    syn::Delay delay;
    syn::Reverb reverb;
    double bpm = 120.0, ppq = 0.0; bool playing = false;

    std::atomic<int> currentPreset { 0 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpacenerdSynthProcessor)
};
