#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace sn;

SpacenerdMasterProcessor::SpacenerdMasterProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createParameterLayout())
{
    for (auto* param : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            params[ranged->getParameterID()] = apvts.getRawParameterValue (ranged->getParameterID());
}

bool SpacenerdMasterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SpacenerdMasterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    maxBlock = std::max (1, samplesPerBlock);
    const int numCh = getTotalNumOutputChannels();

    for (auto& chain : eq) for (auto& f : chain) f.reset();
    eqCache.fill (std::numeric_limits<float>::quiet_NaN());
    updateEq();

    comp.prepare (fs, numCh);

    sideHpf.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    sideHpf.prepare ({ fs, (juce::uint32) samplesPerBlock, 1 });
    sideHpf.setCutoffFrequency (100.0f);

    using OS = juce::dsp::Oversampling<float>;
    oversampler = std::make_unique<OS> ((size_t) numCh, (size_t) kOsOrder, OS::filterHalfBandFIREquiripple, true, true);
    oversampler->initProcessing ((size_t) samplesPerBlock);

    tpDetector = std::make_unique<OS> ((size_t) numCh, (size_t) kOsOrder, OS::filterHalfBandFIREquiripple, true, false);
    tpDetector->initProcessing ((size_t) samplesPerBlock);
    peakBuf.assign ((size_t) samplesPerBlock, 0.0f);

    // Вимірюємо затримку детектора імпульсом (лише шлях "вгору")
    int detectorDelay = 0;
    {
        constexpr int factor = 1 << kOsOrder;
        juce::AudioBuffer<float> probe (numCh, samplesPerBlock);
        long long osIndex = 0, found = -1;
        float best = 0.0f;
        for (int blockIdx = 0; blockIdx < 64 && osIndex < 16384; ++blockIdx)
        {
            probe.clear();
            if (blockIdx == 0) probe.setSample (0, 0, 1.0f);
            juce::dsp::AudioBlock<float> b (probe);
            auto up = tpDetector->processSamplesUp (b);
            for (size_t i = 0; i < up.getNumSamples(); ++i, ++osIndex)
                if (std::abs (up.getSample (0, (int) i)) > best) { best = std::abs (up.getSample (0, (int) i)); found = osIndex; }
        }
        detectorDelay = (int) std::round ((double) std::max (0LL, found) / factor);
        tpDetector->reset();
    }

    // Lookahead ≈ 1.5 мс
    const int lookahead = std::max (4, (int) std::round (0.0015 * fs));
    limiter.prepare (fs, numCh, lookahead, detectorDelay);

    setLatencySamples ((int) std::round (oversampler->getLatencyInSamples()) + limiter.getLatency());

    loudness.prepare (fs, numCh);

    for (auto* sm : { &inGainSm, &outGainSm, &limGainSm })
        sm->reset (fs, 0.03);
    widthSm.reset (fs, 0.03);

    inGainSm.setCurrentAndTargetValue  (dbToGain (p (ParamIDs::inGain)));
    outGainSm.setCurrentAndTargetValue (dbToGain (p (ParamIDs::outGain)));
    limGainSm.setCurrentAndTargetValue (dbToGain (on (ParamIDs::limOn) ? p (ParamIDs::limGain) : 0.0f));
    widthSm.setCurrentAndTargetValue   (p (ParamIDs::width) * 0.01f);

}

void SpacenerdMasterProcessor::updateEq()
{
    const std::array<float, 9> v {
        p (ParamIDs::hpfFreq),
        p (ParamIDs::lowGain),  p (ParamIDs::lowFreq),
        p (ParamIDs::midGain),  p (ParamIDs::midFreq), p (ParamIDs::midQ),
        p (ParamIDs::highGain), p (ParamIDs::highFreq),
        (float) fs };

    if (v == eqCache) return;
    eqCache = v;

    const auto nyq = (float) fs * 0.45f;
    std::array<Coeffs::Ptr, kEqBands> c {
        Coeffs::makeHighPass  (fs, std::min (v[0], nyq)),
        Coeffs::makeLowShelf  (fs, std::min (v[2], nyq), 0.707f, dbToGain (v[1])),
        Coeffs::makePeakFilter(fs, std::min (v[4], nyq), v[5],   dbToGain (v[3])),
        Coeffs::makeHighShelf (fs, std::min (v[7], nyq), 0.707f, dbToGain (v[6])) };

    for (auto& chain : eq)
        for (int b = 0; b < kEqBands; ++b)
            chain[(size_t) b].coefficients = c[(size_t) b];
}

void SpacenerdMasterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Якщо хост дав блок більший за оголошений — обробляємо частинами
    const int total = buffer.getNumSamples();
    for (int start = 0; start < total; start += maxBlock)
    {
        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                                        start, std::min (maxBlock, total - start));
        processChunk (chunk);
    }
}

void SpacenerdMasterProcessor::processChunk (juce::AudioBuffer<float>& buffer)
{
    const int numCh = std::min ({ buffer.getNumChannels(), getTotalNumOutputChannels(), 2 });
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);

    // --- Вхід
    inGainSm.setTargetValue (dbToGain (p (ParamIDs::inGain)));
    inGainSm.applyGain (buffer, n);

    for (int ch = 0; ch < std::min (numCh, 2); ++ch)
        inPeak[(size_t) ch].push (buffer.getMagnitude (ch, 0, n));

    // --- EQ
    if (on (ParamIDs::eqOn))
    {
        updateEq();
        const bool hpfActive = p (ParamIDs::hpfFreq) >= 15.0f;

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            auto& chain = eq[(size_t) ch];
            for (int i = 0; i < n; ++i)
            {
                float x = d[i];
                if (hpfActive) x = chain[0].processSample (x);
                x = chain[1].processSample (x);
                x = chain[2].processSample (x);
                x = chain[3].processSample (x);
                d[i] = x;
            }
        }
    }

    // --- Компресор
    if (on (ParamIDs::compOn))
    {
        const Compressor::Settings s {
            p (ParamIDs::threshold), p (ParamIDs::ratio), p (ParamIDs::attack), p (ParamIDs::release),
            p (ParamIDs::knee), p (ParamIDs::scHpf), p (ParamIDs::makeup), p (ParamIDs::compMix) * 0.01f };
        compGr.push (comp.process (buffer, s));
    }

    // --- Ширина стерео (M/S) і моно-баси
    if (numCh == 2 && on (ParamIDs::widthOn))
    {
        widthSm.setTargetValue (p (ParamIDs::width) * 0.01f);
        const float monoHz = p (ParamIDs::monoBass);
        const bool mono = monoHz >= 20.0f;
        if (mono) sideHpf.setCutoffFrequency (monoHz);

        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (l[i] + r[i]);
            float s = 0.5f * (l[i] - r[i]) * widthSm.getNextValue();
            if (mono) s = sideHpf.processSample (0, s);
            l[i] = m + s;
            r[i] = m - s;
        }
    }

    // --- Сатурація з 4x оверсемплінгом (завжди в тракті, щоб затримка не змінювалась)
    {
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) numCh, (size_t) n);
        auto up = oversampler->processSamplesUp (block);
        if (on (ParamIDs::satOn))
            Saturator::process (up, p (ParamIDs::drive), p (ParamIDs::satMix) * 0.01f);
        oversampler->processSamplesDown (block);
    }

    // --- Лімітер: драйв, детекція true peak, lookahead
    const bool limActive = on (ParamIDs::limOn);
    limGainSm.setTargetValue (dbToGain (limActive ? p (ParamIDs::limGain) : 0.0f));
    limGainSm.applyGain (buffer, n);
    {
        juce::dsp::AudioBlock<const float> block (buffer.getArrayOfReadPointers(), (size_t) numCh, (size_t) n);
        auto up = tpDetector->processSamplesUp (block);
        constexpr int factor = 1 << kOsOrder;
        for (int i = 0; i < n; ++i)
        {
            float pk = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const auto* d = up.getChannelPointer ((size_t) ch) + i * factor;
                for (int k = 0; k < factor; ++k) pk = std::max (pk, std::abs (d[k]));
            }
            peakBuf[(size_t) i] = pk;
        }

        const float minG = limiter.process (buffer.getArrayOfWritePointers(), numCh, n, peakBuf.data(),
                                            dbToGain (p (ParamIDs::ceiling)), p (ParamIDs::limRel), limActive);
        limGr.push (-gainToDb (minG));
    }

    // --- Вихід
    outGainSm.setTargetValue (dbToGain (p (ParamIDs::outGain)));
    outGainSm.applyGain (buffer, n);

    for (int ch = 0; ch < std::min (numCh, 2); ++ch)
        outPeak[(size_t) ch].push (buffer.getMagnitude (ch, 0, n));

    loudness.process (buffer);
}

void SpacenerdMasterProcessor::setCurrentProgram (int index)
{
    const auto& presets = getFactoryPresets();
    if (index < 0 || index >= (int) presets.size()) return;

    const auto& preset = presets[(size_t) index];
    for (auto* param : getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
        if (ranged == nullptr) continue;

        float value = ranged->getDefaultValue();
        for (const auto& [id, v] : preset.values)
            if (ranged->getParameterID() == id)
                value = ranged->convertTo0to1 (v);

        ranged->beginChangeGesture();
        ranged->setValueNotifyingHost (value);
        ranged->endChangeGesture();
    }

    currentPreset.store (index);
    apvts.state.setProperty ("preset", index, nullptr);
    loudness.requestReset();
}

const juce::String SpacenerdMasterProcessor::getProgramName (int index)
{
    const auto& presets = getFactoryPresets();
    return index >= 0 && index < (int) presets.size() ? juce::String (presets[(size_t) index].name) : juce::String();
}

void SpacenerdMasterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SpacenerdMasterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentPreset.store ((int) apvts.state.getProperty ("preset", 0));
        }
}

juce::AudioProcessorEditor* SpacenerdMasterProcessor::createEditor()
{
    return new SpacenerdMasterEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpacenerdMasterProcessor();
}
