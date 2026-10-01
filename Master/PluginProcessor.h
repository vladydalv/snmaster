#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "MasterDSP.h"
#include "Presets.h"
#include "TrackAnalysis.h"
#include "Reference.h"

class SpacenerdMasterProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    SpacenerdMasterProcessor();
    ~SpacenerdMasterProcessor() override { *alive = false; stopTimer(); pool.removeAllJobs (true, 10000); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SN Master"; }
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

    /** Скидає Integrated LUFS і максимум true peak. */
    void resetMeters() noexcept { loudness.requestReset(); inLoudness.requestReset(); tpMaxReset.store (true); }

    juce::AudioProcessorValueTreeState apvts;

    // Метри (читає UI)
    std::array<sn::AtomicMax, 2> inPeak, outPeak;
    sn::AtomicMax compGr, limGr;
    sn::LoudnessMeter loudness, inLoudness;
    std::atomic<float> truePeakMax { 0.0f };   // лінійний, від останнього скидання
    std::atomic<float> matchDb { 0.0f };        // поточна корекція Gain Match
    sn::StereoFifo inFifo, outFifo;             // для аналізатора
    TrackAnalysis analysis { apvts, inFifo, outFifo, loudness, inLoudness };   // живе тут: не губиться, коли вікно закрите

    // Референс: завантаження у фоні, A/B на однаковій гучності (лише message thread викликає ці методи)
    void loadReference (const juce::File&);
    void clearReference();
    void setReferenceTrack (std::unique_ptr<RefTrack>);   // message thread
    const RefTrack* reference() const noexcept { return ref.get(); }    // лише message thread
    bool isLoadingReference() const noexcept { return refLoading.load(); }
    juce::String refError;
    std::function<void()> onReferenceChanged;

private:
    void timerCallback() override { analysis.tick(); }
    float p (const char* id) const noexcept { return params.get (id); }
    /** Плавне вмикання/вимикання модуля в 4x-домені (без клацань). */
    template <typename Fn> void runFaded (float& mix, bool target, float* const* os, int numCh, int n, Fn&& fn);
    float clipSample (float x, float& prevX, float& prevF, float T) const noexcept;
    bool on (const char* id) const { return p (id) > 0.5f; }
    void updateEq (bool force);
    void processChunk (juce::AudioBuffer<float>&);
    void measureTruePeak (juce::dsp::Oversampling<float>&, const juce::AudioBuffer<float>&, int numCh, int n, float* dest);

    sn::ParamCache params;
    std::atomic<int> currentPreset { 0 };
    std::atomic<bool> tpMaxReset { false };
    int maxBlock = 512;

    // EQ в оверсемплованому домені: без стискання АЧХ біля Найквіста (cramping)
    static constexpr int kEqBands = 4;
    std::array<std::array<sn::Biquad, kEqBands>, 2> eq;
    std::array<float, 8> eqCur {}, eqTarget {};

    sn::Compressor comp;
    sn::TubeStage tube;
    sn::TapeStage tape;
    sn::Limiter limiter;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler, tpDetector, tpOut;
    std::vector<float> peakBuf, outTpBuf;
    juce::dsp::LinkwitzRileyFilter<float> sideHpf;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inGainSm, outGainSm, limGainSm, matchSm;
    juce::SmoothedValue<float> widthSm;
    float matchState = 0.0f;

    // Плавні перемикачі модулів (0…1) і повільно згладжені ручки (оновлюються щоблоку)
    float eqMix = 1.0f, compMixF = 1.0f, satMixF = 0.0f, widthMix = 1.0f, limMix = 1.0f;
    float makeupSm = 0.0f, compWetSm = 1.0f, driveSm = 25.0f, satWetSm = 1.0f, ceilingSm = -1.0f, clipSm = 0.0f;
    bool firstBlock = true;
    juce::AudioBuffer<float> osDry;
    // Кліпер (ADAA 1-го порядку): стан по каналах
    std::array<float, 2> clipPrevX {}, clipPrevF {};

    // Референс і монітори
    std::unique_ptr<RefTrack> ref;
    juce::SpinLock refLock;
    juce::ThreadPool pool { 1 };
    std::atomic<bool> refLoading { false };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    float refMix = 0.0f, refGainSm = 1.0f;
    juce::int64 hostPos = 0; bool hostPlaying = false;
    struct Monitor
    {
        int mode = 0, fadeIn = 0, fadeLen = 1;
        std::array<std::array<sn::Biquad, 4>, 2> f;
        void set (int m, double fs);
        void process (juce::AudioBuffer<float>&, int numCh, int n, double fs);
    } mon;
    void applyReference (juce::AudioBuffer<float>&, int numCh, int n);

    double fs = 44100.0, osFs = 176400.0;
    static constexpr int kOsOrder = 2; // 2^2 = 4x
    static constexpr float kSatRef = 0.5f; // -6 dBFS: опорний рівень сатурації на майстер-шині

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpacenerdMasterProcessor)
};
