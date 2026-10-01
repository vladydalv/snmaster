#include "PluginProcessor.h"
#include "PluginEditor.h"

SpacenerdListenProcessor::SpacenerdListenProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createListenLayout())
{
    juce::Random rnd;
    while (id == 0) id = (uint64_t) rnd.nextInt64();
    if (bus.open())
    {
        slot = bus.claim (id);
        seenReset = bus.layout()->h.resetCounter.load();
    }
    maker.prepare (fs);
    loudness.prepare (fs, 2);
    startTimerHz (10);
}

SpacenerdListenProcessor::~SpacenerdListenProcessor()
{
    stopTimer();
    bus.release (slot, id);
}

bool SpacenerdListenProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SpacenerdListenProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    maker.prepare (fs);
    loudness.prepare (fs, 2);
}

void SpacenerdListenProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0) return;
    for (int ch = getTotalNumInputChannels(); ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);

    int64_t start = -1;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (pos->getIsPlaying())
                if (auto t = pos->getTimeInSamples()) start = std::max<int64_t> (0, *t);

    const float* l = buffer.getReadPointer (0);
    const float* r = buffer.getNumChannels() > 1 && getTotalNumInputChannels() > 1 ? buffer.getReadPointer (1) : nullptr;
    maker.process (l, r, n, start);
    loudness.process (buffer);
    float pk = buffer.getMagnitude (0, 0, n);
    if (r != nullptr) pk = std::max (pk, buffer.getMagnitude (1, 0, n));
    if (pk > peakView.load()) peakView = pk;
}

void SpacenerdListenProcessor::resetStats()
{
    resetPending = true;
}

void SpacenerdListenProcessor::update()
{
    // Скидання: кнопкою тут або з Master'а (RESET аналізатора)
    if (bus.isOpen())
    {
        const auto rc = bus.layout()->h.resetCounter.load();
        if (rc != seenReset) { seenReset = rc; resetPending = true; }
        if (slot < 0 || ! bus.ownsSlot (slot, id)) slot = bus.claim (id);
    }
    if (resetPending.exchange (false)) { stats.reset(); loudness.requestReset(); }

    const auto now = mix::nowMs();
    maker.pull ([this, now] (const mix::FrameStat& f)
    {
        stats.add (f);
        if (slot >= 0 && f.idx >= 0)
        {
            float g[mix::kGroups] {};
            for (int b = 0; b < an::kBands; ++b) g[mix::groupOfBand (b)] += f.bands[b];
            bus.writeFrame (slot, f.idx, g, now);
        }
    });

    if (++ticks % 5 != 0 && ticks > 1) return;    // підсумок — двічі на секунду

    auto f = stats.features (loudness.integrated.load());
    const int choice = (int) apvts.getRawParameterValue (ListenIDs::instrument)->load();
    f.manual = choice > 0 ? 1 : 0;
    f.inst = choice > 0 ? choice - 1 : f.autoInst;
    features = f;

    if (slot >= 0)
    {
        bus.heartbeat (slot);
        bus.writeFeatures (slot, displayName(), f);
    }
    masterConnected = bus.isOpen() && now - bus.layout()->h.masterHeartbeat.load() < 4000;

    mix::Verdict v;
    if (masterConnected && bus.readVerdict (slot, v)) verdict = v;
    else
    {
        std::vector<mix::Advice> adv;
        mix::trackChecks (f, adv, bus.isOpen() ? (int) bus.layout()->h.genre.load() : 1);
        verdict = mix::makeVerdict (adv, f.valid != 0);
    }
}

juce::String SpacenerdListenProcessor::displayName() const
{
    const auto user = apvts.state.getProperty ("trackName").toString();
    if (user.isNotEmpty()) return user;
    {
        const juce::ScopedLock sl (nameLock);
        if (hostName.isNotEmpty()) return hostName;
    }
    return mix::instName (features.inst);
}

void SpacenerdListenProcessor::setUserName (const juce::String& s)
{
    apvts.state.setProperty ("trackName", s.trim(), nullptr);
}

void SpacenerdListenProcessor::updateTrackProperties (const TrackProperties& p)
{
    if (p.name.has_value())
    {
        const juce::ScopedLock sl (nameLock);
        hostName = *p.name;
    }
}

void SpacenerdListenProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SpacenerdListenProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SpacenerdListenProcessor::createEditor()
{
    return new SpacenerdListenEditor (*this);
}

#ifndef SN_NO_PLUGIN_ENTRY      // тест збирає Listen і Master разом
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpacenerdListenProcessor();
}
#endif
