#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace sn;
using namespace StompIDs;

SpacenerdStompProcessor::SpacenerdStompProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createStompLayout())
{
    for (auto* param : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            params[ranged->getParameterID()] = apvts.getRawParameterValue (ranged->getParameterID());
}

bool SpacenerdStompProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void SpacenerdStompProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    maxBlock = std::max (1, samplesPerBlock);
    const int numCh = std::min (2, getTotalNumOutputChannels());
    const double osFs = fs * (1 << kOsOrder);

    pedal.prepare (osFs, numCh);
    mod.prepare (fs);
    echo.prepare (fs);

    using OS = juce::dsp::Oversampling<float>;
    oversampler = std::make_unique<OS> ((size_t) numCh, (size_t) kOsOrder, OS::filterHalfBandFIREquiripple, true, true);
    oversampler->initProcessing ((size_t) maxBlock);
    latency = (int) std::round (oversampler->getLatencyInSamples());
    setLatencySamples (latency);

    bassSplit.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    bassSplit.prepare ({ fs, (juce::uint32) maxBlock, 2 });
    bassSplit.setCutoffFrequency (std::max (45.0f, p (cleanBass)));

    lowDelay.setSize (2, latency + 1);
    lowDelay.clear();
    lowBlock.setSize (2, maxBlock);
    onsetBlock.setSize (1, maxBlock);
    lowPos = 0;

    outGainSm.reset (fs, 0.03);
    outGainSm.setCurrentAndTargetValue (dbToGain (p (outGain)));
    first = true;
}

float SpacenerdStompProcessor::syncedRate (double bpm) const
{
    static constexpr float perBeat[] { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };    // Free, 1/4, 1/8, 1/8T, 1/16
    const int s = juce::jlimit (0, 4, (int) p (modSync));
    return s == 0 || bpm <= 0.0 ? p (rate) : (float) (bpm / 60.0) * perBeat[s];
}

float SpacenerdStompProcessor::echoTimeMs (double bpm) const
{
    static constexpr float beats[] { 0.0f, 1.0f, 0.75f, 0.5f, 0.25f };      // Free, 1/4, 1/8., 1/8, 1/16
    const int s = juce::jlimit (0, 4, (int) p (echoSync));
    if (s == 0 || bpm <= 0.0) return p (echoTime);
    return juce::jlimit (40.0f, 2400.0f, (float) (60000.0 / bpm) * beats[s]);
}

void SpacenerdStompProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());

    // Темп і позиція хоста для sync
    double bpm = 0.0;
    std::optional<double> ppq;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (pos->getIsPlaying()) if (auto q = pos->getPpqPosition()) ppq = *q;
        }
    bpmView.store ((float) bpm);

    // Фаза модуляції прив'язана до сітки, коли транспорт іде
    const int s = juce::jlimit (0, 4, (int) p (modSync));
    if (s > 0 && ppq.has_value())
    {
        static constexpr double perBeat[] { 0.0, 1.0, 2.0, 3.0, 4.0 };
        mod.setPhase (*ppq * perBeat[s]);
    }

    const int total = buffer.getNumSamples();
    for (int start = 0; start < total; start += maxBlock)
    {
        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                                        start, std::min (maxBlock, total - start));
        processChunk (chunk);
    }
}

void SpacenerdStompProcessor::processChunk (juce::AudioBuffer<float>& buffer)
{
    const int numCh = std::min ({ buffer.getNumChannels(), getTotalNumOutputChannels(), 2 });
    const int n = buffer.getNumSamples();
    if (n == 0) return;
    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);

    double bpm = bpmView.load();
    float* const* data = buffer.getArrayOfWritePointers();

    for (int ch = 0; ch < numCh; ++ch)
        inPeak[(size_t) ch].push (buffer.getMagnitude (ch, 0, n));

    // Сигнал для детектора атак (Rise) — до педалі
    {
        auto* o = onsetBlock.getWritePointer (0);
        for (int i = 0; i < n; ++i)
            o[i] = numCh > 1 ? 0.5f * (data[0][i] + data[1][i]) : data[0][i];
    }

    // Цілі
    const bool driveActive = on (driveOn);
    const float bassHz = p (cleanBass);
    const st::Pedal::Settings target { p (circuit), p (gain), p (tone), p (battery), p (level) };
    const float bassT = driveActive && bassHz >= 45.0f ? 1.0f : 0.0f;
    const float driveT = driveActive ? 1.0f : 0.0f;
    if (first) { sm = target; bassMix = bassT; driveMix = driveT; first = false; }
    bassSplit.setCutoffFrequency (std::max (45.0f, bassHz));

    // --- Clean Bass: низ обходить педаль, решта йде в дисторшн (LR4 low-pass + комплементарний верх)
    const float subA = 1.0f - (float) std::exp (-(double) kSub / (0.03 * fs));
    {
        const int dlen = lowDelay.getNumSamples();
        float bm = bassMix;
        const float bmStep = 1.0f - (float) std::exp (-1.0 / (0.02 * fs));
        for (int i = 0; i < n; ++i)
        {
            bm += bmStep * (bassT - bm);
            for (int ch = 0; ch < numCh; ++ch)
            {
                // Комплементарний розподіл: педаль отримує x − низ, чиста гілка — низ; сума = x (прозоро при bm = 0)
                const float lo = bassSplit.processSample (ch, data[ch][i]);
                data[ch][i] -= bm * lo;
                // Лінія затримки на латентність
                float* line = lowDelay.getWritePointer (ch);
                const int wp = (lowPos + i) % dlen;
                line[wp] = bm * lo;
                lowBlock.setSample (ch, i, line[(wp + 1) % dlen]);
            }
        }
        bassMix = std::abs (bm - bassT) < 1e-4f ? bassT : bm;
        lowPos = (lowPos + n) % dlen;
    }

    // --- 4x: педаль (зі згладжуванням налаштувань між під-блоками)
    {
        juce::dsp::AudioBlock<float> block (data, (size_t) numCh, (size_t) n);
        auto up = oversampler->processSamplesUp (block);
        const int factor = 1 << kOsOrder;

        if (driveMix > 0.0f || driveT > 0.0f)
        {
            float dryBuf[2][kSub * 4];
            for (int start = 0; start < n; start += kSub)
            {
                const int len = std::min (kSub, n - start);
                auto sub = up.getSubBlock ((size_t) (start * factor), (size_t) (len * factor));

                auto smooth = [subA] (float& v, float t, float eps) { v += subA * (t - v); if (std::abs (t - v) < eps) v = t; };
                smooth (sm.circuit, target.circuit, 0.002f);
                smooth (sm.gainPct, target.gainPct, 0.05f);
                smooth (sm.tonePct, target.tonePct, 0.05f);
                smooth (sm.batteryPct, target.batteryPct, 0.05f);
                smooth (sm.levelDb, target.levelDb, 0.01f);

                const float d0 = driveMix;
                smooth (driveMix, driveT, 0.001f);
                const float d1 = driveMix;

                if (d0 >= 1.0f && d1 >= 1.0f)
                {
                    pedal.process (sub, sm);
                    continue;
                }
                // Плавне вмикання/вимикання педалі без клацань
                const int ns = (int) sub.getNumSamples();
                const auto nch = std::min<size_t> (2, sub.getNumChannels());
                for (size_t ch = 0; ch < nch; ++ch)
                    std::copy_n (sub.getChannelPointer (ch), ns, dryBuf[ch]);
                pedal.process (sub, sm);
                for (size_t ch = 0; ch < nch; ++ch)
                {
                    auto* w = sub.getChannelPointer (ch);
                    for (int i = 0; i < ns; ++i)
                    {
                        const float m = d0 + (d1 - d0) * (float) i / (float) ns;
                        w[i] = m * w[i] + (1.0f - m) * dryBuf[ch][i];
                    }
                }
            }
            if (driveMix <= 0.0f) pedal.reset();
        }

        oversampler->processSamplesDown (block);
    }

    for (int ch = 0; ch < numCh; ++ch)
        buffer.addFrom (ch, 0, lowBlock, ch, 0, n);

    // --- Модуляція
    {
        const bool modActive = on (modOn);
        const int mode = (int) p (modMode);
        if (modActive || (mode != st::Modulator::vibrato && mod.getDepth() > 0.001f))
            mod.process (data, numCh, n, mode, syncedRate (bpm), modActive ? p (depth) : 0.0f, p (shape), p (rise),
                         onsetBlock.getReadPointer (0));
        lfoView.store (modActive ? mod.getLfo() : 0.0f);
    }

    // --- Ехо (з «хвостами» після вимикання)
    {
        const bool echoActive = on (echoOn);
        if (echoActive || echo.getTailLevel() > 1.0e-5f)
            echo.process (data, numCh, n, echoTimeMs (bpm), p (feedback), p (echoTone), p (echoMix), p (wear), echoActive);
        else
            echo.reset();
    }

    // --- Вихід
    outGainSm.setTargetValue (dbToGain (p (outGain)));
    outGainSm.applyGain (buffer, n);
    for (int ch = 0; ch < numCh; ++ch)
        outPeak[(size_t) ch].push (buffer.getMagnitude (ch, 0, n));
}

void SpacenerdStompProcessor::setCurrentProgram (int index)
{
    const auto& presets = getStompPresets();
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
}

const juce::String SpacenerdStompProcessor::getProgramName (int index)
{
    const auto& presets = getStompPresets();
    return index >= 0 && index < (int) presets.size() ? juce::String (presets[(size_t) index].name) : juce::String();
}

void SpacenerdStompProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SpacenerdStompProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentPreset.store ((int) apvts.state.getProperty ("preset", 0));
        }
}

juce::AudioProcessorEditor* SpacenerdStompProcessor::createEditor()
{
    return new SpacenerdStompEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpacenerdStompProcessor();
}
