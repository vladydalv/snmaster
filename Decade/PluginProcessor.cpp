#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace DecIDs;

SpacenerdDecadeProcessor::SpacenerdDecadeProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createDecadeLayout())
{
    for (int i = 0; i < dec::kNumModules; ++i) order[(size_t) i] = i;
    prepareToPlay (48000.0, 512);
}

bool SpacenerdDecadeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    const auto in = layouts.getMainInputChannelSet();
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void SpacenerdDecadeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    maxBlock = std::max (1, samplesPerBlock);
    tone.prepare (fs); grit.prepare (fs, 2, maxBlock); filter.prepare (fs); squash.prepare (fs); hit.prepare (fs); roomMod.prepare (fs);
    fmt.prepare (fs);
    latency = grit.latency() + fmt.latency();
    setLatencySamples (latency);
    dryLine.setSize (2, latency + 1); dryLine.clear();
    dryBlock.setSize (2, maxBlock);
    dryPos = 0;
    first = true;
}

std::array<int, dec::kNumModules> SpacenerdDecadeProcessor::getOrder() const
{
    std::array<int, dec::kNumModules> r {};
    for (int i = 0; i < dec::kNumModules; ++i) r[(size_t) i] = order[(size_t) i].load();
    return r;
}

void SpacenerdDecadeProcessor::setOrder (const std::array<int, dec::kNumModules>& o)
{
    // Лише перестановка 0…5
    std::array<bool, dec::kNumModules> seen {};
    for (int m : o) { if (m < 0 || m >= dec::kNumModules || seen[(size_t) m]) return; seen[(size_t) m] = true; }
    juce::StringArray s;
    for (int i = 0; i < dec::kNumModules; ++i) { order[(size_t) i] = o[(size_t) i]; s.add (juce::String (o[(size_t) i])); }
    apvts.state.setProperty ("order", s.joinIntoString (","), nullptr);
}

int SpacenerdDecadeProcessor::currentFormat() const
{
    const int f = (int) p (format);
    return f > 0 ? f - 1 : dec::formatForYear (p (year));
}

int SpacenerdDecadeProcessor::currentRoomType() const
{
    const int r = (int) p (roomType);
    return r > 0 ? r - 1 : dec::forYear (p (year)).roomType;
}

dec::Resolved SpacenerdDecadeProcessor::resolve (float yr, float roomAxis) const
{
    using namespace dec;
    const auto e = forYear (yr);
    const float a = p (auth) * 0.01f;
    Resolved r;
    r.lowDb = a * e.lowDb + p (toneLow);
    r.lowMidDb = a * e.lowMidDb + p (toneLowMid);
    r.presDb = a * e.presDb + p (tonePres);
    r.topDb = a * e.topDb + p (toneTop);

    const float base = a * e.drive, t = p (gritDrive) * 0.01f;
    r.drive = juce::jlimit (0.0f, 1.0f, t >= 0.0f ? base + t * (1.0f - base) : base * (1.0f + t));
    const int gc = (int) p (gritChar);
    r.grit = gc > 0 ? gc - 1 : e.grit;

    r.hpHz = 20.0f * std::pow (e.hpHz / 20.0f, a) * std::exp2 (p (filterHp));
    r.lpHz = 20000.0f * std::pow (e.lpHz / 20000.0f, a) * std::exp2 (p (filterLp));
    r.reso = p (filterReso) * 0.01f;

    const float sa = p (squashAmt) * 0.01f, sp = p (squashSpeed) * 0.01f;
    const float ratioBase = 1.0f + a * (e.ratio - 1.0f);
    r.ratio = std::max (1.0f, 1.0f + (ratioBase - 1.0f) * (1.0f + sa) + std::max (0.0f, sa) * 1.5f);
    r.thrDb = std::max (0.0f, a * e.thrDb * (1.0f + 0.5f * sa) + std::max (0.0f, sa) * 4.0f);
    r.attackMs = e.attackMs * std::exp2 (-1.5f * sp);
    r.releaseMs = e.releaseMs * std::exp2 (-1.5f * sp);
    r.compMix = juce::jlimit (0.0f, 1.0f, a * e.compMix * (1.0f + 0.5f * sa) + std::max (0.0f, sa) * 0.3f);

    r.hitAttack = juce::jlimit (-100.0f, 100.0f, a * e.hitAttack + 60.0f * p (hitAttack) * 0.01f);
    r.hitSustain = juce::jlimit (-100.0f, 100.0f, a * e.hitSustain + 60.0f * p (hitSustain) * 0.01f);

    // Кімната: тип за епохою або вибраний; вісь Room — від сухого до повного простору цього типу
    const int rt = (int) p (roomType);
    Space sp2 = rt > 0 ? roomPreset (rt - 1) : e.space;
    const float ax = juce::jlimit (0.0f, 1.0f, roomAxis * 0.01f);
    const float rEff = e.roomFloor + (1.0f - e.roomFloor) * ax;
    r.space = sp2;
    r.roomType = rt > 0 ? rt - 1 : e.roomType;
    // Size: більша кімната — більші відстані, пізніші відбиття і довший хвіст (як у справжньому приміщенні)
    const float sizeMul = std::exp2 (0.8f * p (roomSize) * 0.01f);
    r.space.size = lerpF (0.35f, sp2.size, rEff) * sizeMul;
    r.space.decay = lerpF (0.25f, sp2.decay, rEff) * std::pow (sizeMul, 0.7f) * std::exp2 (1.2f * p (roomDecay) * 0.01f);
    r.space.predelayMs = sp2.predelayMs * sizeMul;
    // Рівень простору (енергія кімнати відносно сухого): ~-17 дБ на 25 %, ~-12 дБ на 50 %, ~-2 дБ на 100 %
    r.roomWet = rEff < 0.01f ? 0.0f : a * sn::dbToGain (lerpF (-16.0f, 5.5f, rEff));

    r.width = 1.0f - a * (1.0f - e.width);
    return r;
}

void SpacenerdDecadeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    for (int start = 0; start < total; start += maxBlock)
    {
        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, std::min (maxBlock, total - start));
        processChunk (chunk);
    }
}

void SpacenerdDecadeProcessor::processChunk (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    if (n == 0) return;
    const int inCh = std::min (2, getTotalNumInputChannels());
    const int numCh = std::min (2, buffer.getNumChannels());
    if (inCh == 1 && numCh == 2) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);

    // Плавні рік і кімната (рух пальцем по карті не клацає)
    const float a = std::min (1.0f, (float) n / (float) (0.06 * fs));
    if (first) { yearSm = p (year); roomSm = p (room); mixSm = p (mix) * 0.01f; inSm = sn::dbToGain (p (inGain)); outSm = sn::dbToGain (p (outGain)); first = false; }
    yearSm += a * (p (year) - yearSm);
    roomSm += a * (p (room) - roomSm);

    // Вхід
    const float in1 = sn::dbToGain (p (inGain));
    for (int ch = 0; ch < numCh; ++ch) buffer.applyGainRamp (ch, 0, n, inSm, in1);
    inSm = in1;
    for (int ch = 0; ch < numCh; ++ch) inPeak[(size_t) ch].push (buffer.getMagnitude (ch, 0, n));
    double eIn = 0.0;
    for (int ch = 0; ch < numCh; ++ch) { const float r = buffer.getRMSLevel (ch, 0, n); eIn += r * r; }

    // Сухий сигнал із затримкою, як у мокрого
    const int dl = dryLine.getNumSamples();
    for (int ch = 0; ch < numCh; ++ch)
    {
        float* line = dryLine.getWritePointer (ch);
        float* dst = dryBlock.getWritePointer (ch);
        const float* src = buffer.getReadPointer (ch);
        int pos = dryPos;
        for (int i = 0; i < n; ++i)
        {
            dst[i] = line[(size_t) ((pos + 1) % dl)];
            line[(size_t) pos] = src[i];
            pos = (pos + 1) % dl;
        }
    }
    dryPos = (dryPos + n) % dl;

    // Стійка в обраному порядку
    const auto r = resolve (yearSm, roomSm);
    for (int k = 0; k < dec::kNumModules; ++k)
    {
        switch (order[(size_t) k].load())
        {
            case dec::ModTone:   if (on (toneOn)) tone.process (buffer, numCh, r); break;
            case dec::ModGrit:   if (on (gritOn)) grit.process (buffer, numCh, r); else { dec::Resolved z = r; z.drive = 0.0f; z.grit = dec::Tape; grit.process (buffer, numCh, z); } break;   // латентність стала
            case dec::ModFilter: if (on (filterOn)) filter.process (buffer, numCh, r); break;
            case dec::ModSquash: if (on (squashOn)) squash.process (buffer, numCh, r); else squash.gr = 0.0f; break;
            case dec::ModHit:    if (on (hitOn)) hit.process (buffer, numCh, r); break;
            case dec::ModRoom:   if (on (roomOn)) roomMod.process (buffer, numCh, r); else roomMod.tailView = 0.0f; break;
            default: break;
        }
    }

    // Ширина (1950-ті — моно)
    if (numCh == 2 && r.width < 0.999f)
    {
        float* L = buffer.getWritePointer (0); float* R = buffer.getWritePointer (1);
        for (int i = 0; i < n; ++i) { const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]) * r.width; L[i] = m + s; R[i] = m - s; }
    }

    // Носій — після стійки
    fmt.process (buffer, numCh, currentFormat(), p (fmtAmount) * 0.01f, p (fmtAge) * 0.01f);

    // Mix і вихід
    const float mix1 = p (mix) * 0.01f, out1 = sn::dbToGain (p (outGain));
    for (int ch = 0; ch < numCh; ++ch)
    {
        float* d = buffer.getWritePointer (ch);
        const float* dr = dryBlock.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) i / (float) n;
            const float m = mixSm + (mix1 - mixSm) * t, g = outSm + (out1 - outSm) * t;
            d[i] = (dr[i] * (1.0f - m) + d[i] * m) * g;
        }
    }
    mixSm = mix1; outSm = out1;

    double eOut = 0.0;
    for (int ch = 0; ch < numCh; ++ch) { outPeak[(size_t) ch].push (buffer.getMagnitude (ch, 0, n)); const float rr = buffer.getRMSLevel (ch, 0, n); eOut += rr * rr; }
    const float k = std::min (1.0f, (float) n / (float) (2.0 * fs));
    inLoud += k * (eIn - inLoud); outLoud += k * (eOut - outLoud);
    loudDiffDb = inLoud > 1.0e-10 ? (float) (10.0 * std::log10 (std::max (outLoud, 1.0e-12) / inLoud)) : 0.0f;
    squashGr = squash.gr; gritView = grit.level; roomTail = roomMod.tailView;
}

void SpacenerdDecadeProcessor::setCurrentProgram (int index)
{
    const auto& presets = getDecadePresets();
    if (index < 0 || index >= (int) presets.size()) return;
    const auto& preset = presets[(size_t) index];
    for (auto* param : getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
        if (ranged == nullptr) continue;
        const auto id = ranged->getParameterID();
        if (id == DecIDs::inGain || id == DecIDs::outGain || id == DecIDs::mix) continue;   // рівні користувача не чіпаємо
        float value = ranged->getDefaultValue();
        if (id == DecIDs::year) value = ranged->convertTo0to1 (preset.year);
        if (id == DecIDs::room) value = ranged->convertTo0to1 (preset.room);
        for (const auto& [pid, v] : preset.values)
            if (id == pid) value = ranged->convertTo0to1 (v);
        ranged->beginChangeGesture();
        ranged->setValueNotifyingHost (value);
        ranged->endChangeGesture();
    }
    currentPreset.store (index);
    apvts.state.setProperty ("preset", index, nullptr);
}

const juce::String SpacenerdDecadeProcessor::getProgramName (int index)
{
    const auto& presets = getDecadePresets();
    return index >= 0 && index < (int) presets.size() ? juce::String (presets[(size_t) index].name) : juce::String();
}

void SpacenerdDecadeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SpacenerdDecadeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentPreset.store ((int) apvts.state.getProperty ("preset", 0));
            juce::StringArray s; s.addTokens (apvts.state.getProperty ("order").toString(), ",", {});
            if (s.size() == dec::kNumModules)
            {
                std::array<int, dec::kNumModules> o {};
                for (int i = 0; i < dec::kNumModules; ++i) o[(size_t) i] = s[i].getIntValue();
                setOrder (o);
            }
        }
}

juce::AudioProcessorEditor* SpacenerdDecadeProcessor::createEditor()
{
    return new SpacenerdDecadeEditor (*this);
}

#ifndef SN_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpacenerdDecadeProcessor();
}
#endif
