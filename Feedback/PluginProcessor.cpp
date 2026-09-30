#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace FbIDs;

SpacenerdFeedbackProcessor::SpacenerdFeedbackProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createFeedbackLayout())
{
    for (auto* param : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            params[ranged->getParameterID()] = apvts.getRawParameterValue (ranged->getParameterID());
}

bool SpacenerdFeedbackProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void SpacenerdFeedbackProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    outGainSm.reset (sampleRate, 0.03);
    outGainSm.setCurrentAndTargetValue (sn::dbToGain (p (outGain)));
    setLatencySamples (0);
}

void SpacenerdFeedbackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    const int numCh = std::min ({ buffer.getNumChannels(), getTotalNumOutputChannels(), 2 });

    sn::FeedbackEngine::Settings s;
    s.mode     = (int) p (trigger);
    s.hold     = p (hold) > 0.5f;
    s.delaySec = p (delay);
    s.harmonic = (int) p (harmonic);
    s.distance = p (distance) * 0.01f;
    s.amount   = p (amount) * 0.01f;
    s.morph    = p (morph) * 0.01f;
    s.toneHz   = p (tone);
    s.drift    = p (drift) * 0.01f;

    engine.process (buffer.getArrayOfWritePointers(), numCh, n, s);

    outGainSm.setTargetValue (sn::dbToGain (p (outGain)));
    outGainSm.applyGain (buffer, n);
    outPeak.push (buffer.getMagnitude (0, n));
}

void SpacenerdFeedbackProcessor::setCurrentProgram (int index)
{
    const auto& presets = getFeedbackPresets();
    if (index < 0 || index >= (int) presets.size()) return;

    for (auto* param : getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
        if (ranged == nullptr || ranged->getParameterID() == hold) continue;

        float value = ranged->getDefaultValue();
        for (const auto& [id, v] : presets[(size_t) index].values)
            if (ranged->getParameterID() == id)
                value = ranged->convertTo0to1 (v);

        ranged->beginChangeGesture();
        ranged->setValueNotifyingHost (value);
        ranged->endChangeGesture();
    }
    currentPreset.store (index);
    apvts.state.setProperty ("preset", index, nullptr);
}

const juce::String SpacenerdFeedbackProcessor::getProgramName (int index)
{
    const auto& presets = getFeedbackPresets();
    return index >= 0 && index < (int) presets.size() ? juce::String (presets[(size_t) index].name) : juce::String();
}

void SpacenerdFeedbackProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SpacenerdFeedbackProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentPreset.store ((int) apvts.state.getProperty ("preset", 0));
        }
}

juce::AudioProcessorEditor* SpacenerdFeedbackProcessor::createEditor()
{
    return new SpacenerdFeedbackEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpacenerdFeedbackProcessor();
}
