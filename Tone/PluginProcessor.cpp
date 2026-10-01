#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace sn;
using namespace ToneIDs;

SpacenerdToneProcessor::SpacenerdToneProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createToneLayout())
{
    for (auto* param : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            params.add (ranged->getParameterID(), apvts.getRawParameterValue (ranged->getParameterID()));
}

bool SpacenerdToneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    // Моно → стерео теж дозволено (моно-доріжка з гітарою/вокалом)
    const auto in = layouts.getMainInputChannelSet();
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void SpacenerdToneProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    maxBlock = std::max (1, samplesPerBlock);
    const int numCh = std::min (2, getTotalNumOutputChannels());
    const double osFs = fs * (1 << kOsOrder);

    transient.prepare (fs);
    tube.prepare (osFs, numCh);
    tape.prepare (osFs, numCh);
    exciter.prepare (osFs, numCh);
    wow.prepare (fs, numCh);
    deEsser.prepare (fs, numCh);

    using OS = juce::dsp::Oversampling<float>;
    oversampler = std::make_unique<OS> ((size_t) numCh, (size_t) kOsOrder, OS::filterHalfBandFIREquiripple, true, true);
    oversampler->initProcessing ((size_t) samplesPerBlock);

    latency = (int) std::round (oversampler->getLatencyInSamples()) + wow.getLatency();
    setLatencySamples (latency);

    dryDelay.setSize (numCh, latency + 1);
    dryDelay.clear();
    dryBlock.setSize (numCh, samplesPerBlock);
    dryPos = 0;

    scratch.setSize (numCh, samplesPerBlock * (1 << kOsOrder));
    firstBlock = true;
    inGainSm.reset (fs, 0.03);  outGainSm.reset (fs, 0.03);
    mixSm.reset (fs, 0.03);     wowSm.reset (fs, 0.1);
    inGainSm.setCurrentAndTargetValue (dbToGain (p (inGain)));
    outGainSm.setCurrentAndTargetValue (dbToGain (p (outGain)));
    mixSm.setCurrentAndTargetValue (p (mix) * 0.01f);
    wowSm.setCurrentAndTargetValue (on (tapeOn) ? p (tapeWow) * 0.01f : 0.0f);
}

void SpacenerdToneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Моно-вхід на стерео-виході: дублюємо канал
    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());

    const int total = buffer.getNumSamples();
    for (int start = 0; start < total; start += maxBlock)
    {
        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                                        start, std::min (maxBlock, total - start));
        processChunk (chunk);
    }
}

void SpacenerdToneProcessor::processChunk (juce::AudioBuffer<float>& buffer)
{
    const int numCh = std::min ({ buffer.getNumChannels(), getTotalNumOutputChannels(), 2 });
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);

    // --- Вхід
    inGainSm.setTargetValue (dbToGain (p (inGain)));
    inGainSm.applyGain (buffer, n);
    for (int ch = 0; ch < numCh; ++ch)
        inPeak[(size_t) ch].push (buffer.getMagnitude (ch, 0, n));

    // --- Сухий сигнал із затримкою, рівною латентності мокрого тракту
    const int dlen = dryDelay.getNumSamples();
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* src = buffer.getReadPointer (ch);
        float* line = dryDelay.getWritePointer (ch);
        float* dst = dryBlock.getWritePointer (ch);
        int pos = dryPos;
        for (int i = 0; i < n; ++i)
        {
            line[pos] = src[i];
            const int rp = pos + 1 == dlen ? 0 : pos + 1;   // найстаріший семпл (затримка dlen-1 = latency)
            dst[i] = line[rp];
            pos = rp;
        }
    }
    dryPos = (dryPos + n) % dlen;

    float* const* data = buffer.getArrayOfWritePointers();

    // Модулі вмикаються/вимикаються плавно (≈ 20 мс), ручки драйву згладжені (≈ 50 мс)
    const float step = std::min (1.0f, (float) n / (float) (0.02 * fs));
    const float kSm = std::min (1.0f, (float) n / (float) (0.05 * fs));
    if (firstBlock)
    {
        trMix = on (trOn) ? 1.0f : 0.0f; tubeMixF = on (tubeOn) ? 1.0f : 0.0f; tapeMixF = on (tapeOn) ? 1.0f : 0.0f;
        excMixF = on (excOn) ? 1.0f : 0.0f; dsMixF = on (dsOn) ? 1.0f : 0.0f;
        tubeDriveSm = p (tubeDrive); tubeWetSm = p (tubeMix) * 0.01f; tapeDriveSm = p (tapeDrive);
        firstBlock = false;
    }
    tubeDriveSm += (p (tubeDrive) - tubeDriveSm) * kSm;
    tubeWetSm   += (p (tubeMix) * 0.01f - tubeWetSm) * kSm;
    tapeDriveSm += (p (tapeDrive) - tapeDriveSm) * kSm;

    // --- Транзієнти (до сатурації: як атака/сустейн інструмента)
    sn::runFaded (trMix, on (trOn), data, numCh, n, step, scratch, [&] (bool fresh)
    {
        juce::ignoreUnused (fresh);
        transient.process (data, numCh, n, p (trAttack), p (trSustain));
    });

    // --- 4x: лампа → плівка → ексайтер
    {
        juce::dsp::AudioBlock<float> block (data, (size_t) numCh, (size_t) n);
        auto up = oversampler->processSamplesUp (block);
        const int on_ = (int) up.getNumSamples();
        float* os[2] = { up.getChannelPointer (0), numCh > 1 ? up.getChannelPointer (1) : nullptr };

        sn::runFaded (tubeMixF, on (tubeOn), os, numCh, on_, step, scratch, [&] (bool fresh)
        {
            if (fresh) tube.reset();
            tube.process (up, tubeDriveSm, p (tubeBias), tubeWetSm);
        });
        sn::runFaded (tapeMixF, on (tapeOn), os, numCh, on_, step, scratch, [&] (bool fresh)
        {
            if (fresh) tape.reset();
            tape.process (up, tapeDriveSm, (int) p (tapeSpeed), 1.0f, p (tapeBias));
        });
        sn::runFaded (excMixF, on (excOn), os, numCh, on_, step, scratch, [&] (bool)
        {
            exciter.process (up, p (excFreq), p (excAmount));
        });

        oversampler->processSamplesDown (block);
    }

    // --- Детонація (завжди в тракті, щоб латентність не змінювалась)
    wowSm.setTargetValue (on (tapeOn) ? p (tapeWow) * 0.01f : 0.0f);
    {
        const float amt = wowSm.getCurrentValue();
        wowSm.skip (n);
        wow.process (buffer, numCh, amt);
    }

    // --- Де-есер (після сатурації, яка підсилює сибілянти)
    float dsGr = 0.0f;
    sn::runFaded (dsMixF, on (dsOn), data, numCh, n, step, scratch, [&] (bool fresh)
    {
        if (fresh) deEsser.reset();
        dsGr = deEsser.process (data, numCh, n, p (dsFreq), p (dsSens), p (dsRange), on (dsListen));
    });
    deEssGr.push (dsGr);

    // --- Mix: сухий/мокрий, вирівняні за часом
    mixSm.setTargetValue (p (mix) * 0.01f);
    for (int i = 0; i < n; ++i)
    {
        const float m = mixSm.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
            data[ch][i] = m * data[ch][i] + (1.0f - m) * dryBlock.getSample (ch, i);
    }

    // --- Вихід
    outGainSm.setTargetValue (dbToGain (p (outGain)));
    outGainSm.applyGain (buffer, n);
    for (int ch = 0; ch < numCh; ++ch)
        outPeak[(size_t) ch].push (buffer.getMagnitude (ch, 0, n));
}

void SpacenerdToneProcessor::setCurrentProgram (int index)
{
    const auto& presets = getTonePresets();
    if (index < 0 || index >= (int) presets.size()) return;

    const auto& preset = presets[(size_t) index];
    for (auto* param : getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
        if (ranged == nullptr || ranged->getParameterID() == dsListen) continue;

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
}

const juce::String SpacenerdToneProcessor::getProgramName (int index)
{
    const auto& presets = getTonePresets();
    return index >= 0 && index < (int) presets.size() ? juce::String (presets[(size_t) index].name) : juce::String();
}

void SpacenerdToneProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SpacenerdToneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentPreset.store ((int) apvts.state.getProperty ("preset", 0));
        }
}

juce::AudioProcessorEditor* SpacenerdToneProcessor::createEditor()
{
    return new SpacenerdToneEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpacenerdToneProcessor();
}
