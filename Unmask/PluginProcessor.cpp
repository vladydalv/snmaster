#include "PluginProcessor.h"
#include "PluginEditor.h"

SpacenerdUnmaskProcessor::SpacenerdUnmaskProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createUnmaskLayout())
{
    dsp.prepare (48000.0);
    keyBuf.assign (4096, 0.0f);
}

bool SpacenerdUnmaskProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    if (layouts.getMainInputChannelSet() != out) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void SpacenerdUnmaskProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    dsp.prepare (sampleRate);
    keyBuf.assign ((size_t) std::max (4096, samplesPerBlock), 0.0f);
}

void SpacenerdUnmaskProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, true, 0);
    const int n = buffer.getNumSamples();
    const int numCh = std::min (2, main.getNumChannels());
    if (n == 0 || numCh == 0) return;

    const int scCh = getBusCount (true) > 1 && getBus (true, 1)->isEnabled() ? getBusBuffer (buffer, true, 1).getNumChannels() : 0;
    sidechainConnected.store (scCh > 0);

    const auto [lo, hi] = unmaskRange ((int) p (UnmaskIDs::range));
    const Unmasker::Settings s { p (UnmaskIDs::mode) < 0.5f, lo, hi, p (UnmaskIDs::freq), p (UnmaskIDs::depth), p (UnmaskIDs::width),
                                 p (UnmaskIDs::attack), p (UnmaskIDs::release), p (UnmaskIDs::sens), p (UnmaskIDs::delta) > 0.5f };

    const int chunk = (int) keyBuf.size();
    for (int start = 0; start < n; start += chunk)
    {
        const int len = std::min (chunk, n - start);
        const float* key = nullptr;
        if (scCh > 0)
        {
            auto sc = getBusBuffer (buffer, true, 1);
            for (int i = 0; i < len; ++i)
            {
                float k = 0.0f;
                for (int c = 0; c < scCh; ++c) k += sc.getSample (c, start + i);
                keyBuf[(size_t) i] = k / (float) scCh;
            }
            key = keyBuf.data();
        }
        float* ptrs[2] { main.getWritePointer (0, start), numCh > 1 ? main.getWritePointer (1, start) : nullptr };
        dsp.process (ptrs, numCh, key, len, s);
    }
}

void SpacenerdUnmaskProcessor::setCurrentProgram (int index)
{
    const auto& presets = getUnmaskPresets();
    if (index < 0 || index >= (int) presets.size()) return;
    const auto& preset = presets[(size_t) index];
    for (auto* param : getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
        if (ranged == nullptr) continue;
        float value = ranged->getDefaultValue();
        for (const auto& [id, v] : preset.values)
            if (ranged->getParameterID() == id) value = ranged->convertTo0to1 (v);
        ranged->beginChangeGesture();
        ranged->setValueNotifyingHost (value);
        ranged->endChangeGesture();
    }
    currentPreset.store (index);
    apvts.state.setProperty ("preset", index, nullptr);
}

const juce::String SpacenerdUnmaskProcessor::getProgramName (int index)
{
    const auto& presets = getUnmaskPresets();
    return index >= 0 && index < (int) presets.size() ? juce::String (presets[(size_t) index].name) : juce::String();
}

void SpacenerdUnmaskProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SpacenerdUnmaskProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentPreset.store ((int) apvts.state.getProperty ("preset", 0));
        }
}

juce::AudioProcessorEditor* SpacenerdUnmaskProcessor::createEditor()
{
    return new SpacenerdUnmaskEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpacenerdUnmaskProcessor();
}
