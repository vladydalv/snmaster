#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace sn;
using namespace EraIDs;

namespace
{
struct EraPreset { const char* name; float year, intensity; int genre; };
const EraPreset presets[]
{
    { "Init",               1975.0f, 60.0f, 0 },
    { "Protostoner '71",    1971.0f, 75.0f, 1 },
    { "Desert Rock '97",    1997.0f, 70.0f, 1 },
    { "Modern Stoner",      2018.0f, 70.0f, 1 },
    { "Psych '68",          1968.0f, 80.0f, 2 },
    { "Space Rock '73",     1973.0f, 75.0f, 3 },
    { "Grunge '92",         1992.0f, 75.0f, 4 },
    { "Loudness War '04",   2004.0f, 90.0f, 0 },
    { "Streaming Today",    2024.0f, 60.0f, 0 },
};
}

SpacenerdEraProcessor::SpacenerdEraProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createEraLayout())
{
    for (auto* param : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            params.add (ranged->getParameterID(), apvts.getRawParameterValue (ranged->getParameterID()));
    for (auto& b : bandLevel) b.store (0.0f);
}

bool SpacenerdEraProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SpacenerdEraProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    maxBlock = std::max (1, samplesPerBlock);
    const int numCh = std::min (2, getTotalNumOutputChannels());
    constexpr int factor = 1 << kOsOrder;
    osFs = fs * factor;

    comp.prepare (osFs, numCh);
    tube.prepare (osFs, numCh, 0.5f);
    tape.prepare (osFs, numCh, 0.5f);
    for (auto& ch : eq) for (auto& f : ch) f.reset();
    curValid = false;

    sideHpf.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    sideHpf.prepare ({ fs, (juce::uint32) samplesPerBlock, 1 });
    sideHpf.setCutoffFrequency (100.0f);

    using OS = juce::dsp::Oversampling<float>;
    oversampler = std::make_unique<OS> ((size_t) numCh, (size_t) kOsOrder, OS::filterHalfBandFIREquiripple, true, true);
    oversampler->initProcessing ((size_t) samplesPerBlock);
    tpDetector = std::make_unique<OS> ((size_t) numCh, (size_t) kOsOrder, OS::filterHalfBandFIREquiripple, true, false);
    tpDetector->initProcessing ((size_t) samplesPerBlock);
    peakBuf.assign ((size_t) samplesPerBlock, 0.0f);

    // Затримка детектора true peak (імпульсом)
    int detectorDelay = 0;
    {
        juce::AudioBuffer<float> probe (numCh, samplesPerBlock);
        long long osIndex = 0, found = -1; float best = 0.0f;
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
    limiter.prepare (fs, numCh, std::max (4, (int) std::round (0.004 * fs)), detectorDelay);

    const int latency = (int) std::round (oversampler->getLatencyInSamples()) + limiter.getLatency();
    setLatencySamples (latency);
    dryDelay.setSize (numCh, latency + 1); dryDelay.clear();
    dryBlock.setSize (numCh, samplesPerBlock);
    dryPos = 0;

    preLimLoudness.prepare (fs, numCh);
    postLimLoudness.prepare (fs, numCh);
    outLoudness.prepare (fs, numCh);
    inLoudness.prepare (fs, numCh);

    // Смуги для піксельного дисплея: 12 смуг від 60 Гц до 12 кГц
    for (int b = 0; b < kBands; ++b)
    {
        const double f = 60.0 * std::pow (200.0, (double) b / (kBands - 1));
        bands[(size_t) b].setBandPass (fs, f, 2.0);
        bands[(size_t) b].reset();
        bandEnv[(size_t) b] = 0.0f;
    }
    bandRel = (float) std::exp (-1.0 / (0.12 * fs));
    programCoef = (float) std::exp (-1.0 / (0.4 * fs));
    programMs = 0.0f; pushState = 0.0f; matchState = 0.0f;

    yearSm.reset (fs, 0.08);      yearSm.setCurrentAndTargetValue (p (year));
    yearLowSm.reset (fs, 0.08);   yearLowSm.setCurrentAndTargetValue (p (yearLow));
    yearHighSm.reset (fs, 0.08);  yearHighSm.setCurrentAndTargetValue (p (yearHigh));
    intensitySm.reset (fs, 0.08); intensitySm.setCurrentAndTargetValue (p (intensity) * 0.01f);
    mixSm.reset (fs, 0.03);       mixSm.setCurrentAndTargetValue (p (mix) * 0.01f);
    pushSm.reset (fs, 0.05);      pushSm.setCurrentAndTargetValue (1.0f);
    outGainSm.reset (fs, 0.03);   outGainSm.setCurrentAndTargetValue (dbToGain (p (outGain)));
    matchSm.reset (fs, 0.03);     matchSm.setCurrentAndTargetValue (1.0f);
}

void SpacenerdEraProcessor::updateFilters (const era::Settings& s, bool force)
{
    const std::array<float, 11> v { s.lowCutHz, s.lowDb, s.lowHz, s.midDb, s.midHz, s.midQ, s.highDb, s.highHz, s.topLpHz, 0, 0 };
    if (! force && v == eqState) return;
    eqState = v;

    sn::Biquad cut, lo, mid, hi, top;
    if (s.lowCutHz > 12.0f) cut.setHighPass (osFs, s.lowCutHz); else cut.setBypass();
    lo.setLowShelf  (osFs, s.lowHz, 0.707, s.lowDb);
    mid.setPeak     (osFs, s.midHz, s.midQ, s.midDb);
    hi.setHighShelf (osFs, s.highHz, 0.707, s.highDb);
    if (s.topLpHz < 21000.0f) top.setLowPass (osFs, s.topLpHz, 0.707); else top.setBypass();
    for (auto& ch : eq)
    {
        ch[0].copyCoeffs (cut); ch[1].copyCoeffs (lo); ch[2].copyCoeffs (mid);
        ch[3].copyCoeffs (hi);  ch[4].copyCoeffs (top);
    }
}

void SpacenerdEraProcessor::measureTruePeak (const juce::AudioBuffer<float>& buffer, int numCh, int n)
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
}

void SpacenerdEraProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    for (int start = 0; start < total; start += maxBlock)
    {
        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                                        start, std::min (maxBlock, total - start));
        processChunk (chunk);
    }
}

static void smoothSettings (era::Settings& c, const era::Settings& t, float a)
{
    auto s = [a] (float& x, float target) { x += (target - x) * a; };
    s (c.lowCutHz, t.lowCutHz); s (c.lowDb, t.lowDb); s (c.lowHz, t.lowHz);
    s (c.midDb, t.midDb); s (c.midHz, t.midHz); s (c.midQ, t.midQ);
    s (c.highDb, t.highDb); s (c.highHz, t.highHz); s (c.topLpHz, t.topLpHz);
    s (c.ratio, t.ratio); s (c.thrOffsetDb, t.thrOffsetDb); s (c.attackMs, t.attackMs); s (c.releaseMs, t.releaseMs);
    s (c.tube, t.tube); s (c.tape, t.tape); s (c.clip, t.clip);
    s (c.width, t.width); s (c.monoBassHz, t.monoBassHz); s (c.targetLufs, t.targetLufs);
    c.tapeSpeed = t.tapeSpeed;
}

void SpacenerdEraProcessor::processChunk (juce::AudioBuffer<float>& buffer)
{
    const int numCh = std::min ({ buffer.getNumChannels(), getTotalNumOutputChannels(), 2 });
    const int n = buffer.getNumSamples();
    if (n == 0) return;
    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
    float* const* data = buffer.getArrayOfWritePointers();

    const bool match = p (gainMatch) > 0.5f;
    if (match) inLoudness.process (buffer);
    preLimLoudness.process (buffer);   // гучність входу — від неї рахується «наскільки гучніше» для епохи

    // --- Сухий сигнал із затримкою для Mix
    const int dlen = dryDelay.getNumSamples();
    for (int ch = 0; ch < numCh; ++ch)
    {
        float* line = dryDelay.getWritePointer (ch);
        float* dst = dryBlock.getWritePointer (ch);
        int pos = dryPos;
        for (int i = 0; i < n; ++i)
        {
            line[pos] = data[ch][i];
            const int rp = pos + 1 == dlen ? 0 : pos + 1;
            dst[i] = line[rp];
            pos = rp;
        }
    }
    dryPos = (dryPos + n) % dlen;

    // --- Рівень програми (для авто-порогу компресора) і смуги для дисплея
    for (int i = 0; i < n; ++i)
    {
        float m = 0.0f;
        for (int ch = 0; ch < numCh; ++ch) m += data[ch][i];
        m /= (float) numCh;
        programMs = m * m + programCoef * (programMs - m * m);
        for (int b = 0; b < kBands; ++b)
        {
            const float v = std::abs (bands[(size_t) b].process (m));
            auto& e = bandEnv[(size_t) b];
            e = v > e ? v : v + bandRel * (e - v);
        }
    }
    for (int b = 0; b < kBands; ++b) bandLevel[(size_t) b].store (bandEnv[(size_t) b]);
    const float programDb = gainToDb (std::sqrt (programMs) * 1.41421356f);

    // --- Налаштування епохи
    yearSm.setTargetValue (p (year));
    intensitySm.setTargetValue (p (intensity) * 0.01f);
    const float yr = yearSm.skip (n), k = intensitySm.skip (n);
    const int gIdx = (int) p (genre);
    auto target = era::applyIntensity (era::forYear (yr, gIdx), k);
    if (p (split) > 0.5f)
    {
        // Низ (обрізання, low shelf, моно-бас) — з року LOW, верх (яскравість, завал) — з року HIGH.
        // Кожна смуга має власну інтенсивність (вертикаль ручок); різниця епох у смугах підсилена ×2,
        // інакше вона тонша за різницю всього ланцюга і на слух губиться.
        yearLowSm.setTargetValue (p (yearLow));
        yearHighSm.setTargetValue (p (yearHigh));
        const auto lo = era::applyIntensity (era::forYear (yearLowSm.skip (n), gIdx), p (lowAmt) * 0.01f);
        const auto hi = era::applyIntensity (era::forYear (yearHighSm.skip (n), gIdx), p (highAmt) * 0.01f);
        constexpr float bandEmphasis = 2.0f;
        target.lowCutHz = lo.lowCutHz; target.lowDb = lo.lowDb * bandEmphasis; target.lowHz = lo.lowHz; target.monoBassHz = lo.monoBassHz;
        target.highDb = hi.highDb * bandEmphasis; target.highHz = hi.highHz; target.topLpHz = hi.topLpHz;
    }
    else
    {
        yearLowSm.setCurrentAndTargetValue (p (yearLow));
        yearHighSm.setCurrentAndTargetValue (p (yearHigh));
    }
    if (! curValid) { cur = target; curValid = true; updateFilters (cur, true); speedSm = (float) target.tapeSpeed; }
    // Швидкість стрічки ковзає (~0.3 с), а не перемикається стрибком — без клацань
    speedSm += ((float) target.tapeSpeed - speedSm) * std::min (1.0f, (float) n / (float) (0.1 * getSampleRate()));
    if (std::abs ((float) target.tapeSpeed - speedSm) < 0.002f) speedSm = (float) target.tapeSpeed;

    // --- 4x: EQ → компресор → лампа → плівка → кліп
    {
        juce::dsp::AudioBlock<float> block (data, (size_t) numCh, (size_t) n);
        auto up = oversampler->processSamplesUp (block);
        const int on = (int) up.getNumSamples();
        float* osData[2] = { up.getChannelPointer (0), numCh > 1 ? up.getChannelPointer (1) : nullptr };

        constexpr int sub = 64;
        for (int start = 0; start < on; start += sub)
        {
            smoothSettings (cur, target, 0.08f);
            updateFilters (cur, false);
            const int len = std::min (sub, on - start);
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* d = osData[ch] + start;
                auto& chain = eq[(size_t) ch];
                for (int i = 0; i < len; ++i)
                {
                    float x = d[i];
                    for (auto& f : chain) x = f.process (x);
                    d[i] = x;
                }
            }
        }

        if (cur.ratio > 1.01f)
        {
            const Compressor::Settings cs {
                juce::jlimit (-60.0f, 0.0f, programDb + cur.thrOffsetDb), cur.ratio, cur.attackMs, cur.releaseMs,
                6.0f, 80.0f, 0.0f, 1.0f, true };
            compGr.push (comp.process (osData, numCh, on, cs));
        }
        if (cur.tube > 0.5f) tube.process (up, cur.tube, 35.0f, 1.0f);
        if (cur.tape > 0.5f) tape.process (up, cur.tape, speedSm, 1.0f);
        if (cur.clip > 0.5f) SoftClip::process (up, cur.clip, 1.0f, 0.5f);

        oversampler->processSamplesDown (block);
    }

    // --- Стерео
    if (numCh == 2 && (std::abs (cur.width - 100.0f) > 0.5f || cur.monoBassHz > 20.0f))
    {
        const float w = cur.width * 0.01f;
        const bool mono = cur.monoBassHz > 20.0f;
        if (mono) sideHpf.setCutoffFrequency (cur.monoBassHz);
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (data[0][i] + data[1][i]);
            float s = 0.5f * (data[0][i] - data[1][i]) * w;
            if (mono) s = sideHpf.processSample (0, s);
            data[0][i] = m + s; data[1][i] = m - s;
        }
    }

    // --- Гучність епохи: лімітер «дотягує» до типової гучності релізів того часу.
    // Регулятор зі зворотним зв'язком по гучності ПІСЛЯ лімітера (враховує, скільки він «з'їдає»).
    {
        pushSm.setTargetValue (dbToGain (pushState));
        pushSm.applyGain (buffer, n);
    }
    measureTruePeak (buffer, numCh, n);
    const float minG = limiter.process (data, numCh, n, peakBuf.data(), dbToGain (-1.0f), 60.0f, 1.0f, 1.0f);
    limGr.push (-gainToDb (minG));
    postLimLoudness.process (buffer);
    {
        const float inSt = preLimLoudness.shortTerm.load(), outSt = postLimLoudness.shortTerm.load();
        if (inSt > -60.0f && outSt > -60.0f)
        {
            const float targetEff = inSt + k * std::max (0.0f, cur.targetLufs - inSt);
            const float err = juce::jlimit (-3.0f, 3.0f, targetEff - outSt);
            pushState += err * 0.8f * (float) n / (float) fs;          // до ~2.4 дБ/с: плавно, без «дихання»
            pushState = juce::jlimit (0.0f, 24.0f * k, pushState);
        }
        pushDb.store (pushState);
    }

    // --- Mix
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
    for (int ch = 0; ch < numCh; ++ch) outPeak[(size_t) ch].push (buffer.getMagnitude (ch, 0, n));
    outLoudness.process (buffer);

    // --- Gain Match
    float mt = 0.0f;
    if (match)
    {
        const float li = inLoudness.shortTerm.load(), lo = outLoudness.shortTerm.load();
        mt = (li > -70.0f && lo > -70.0f) ? juce::jlimit (-30.0f, 6.0f, li - lo) : matchState;
    }
    matchState += (mt - matchState) * std::min (1.0f, (float) n / (float) (0.4 * fs));
    matchDb.store (match ? matchState : 0.0f);
    matchSm.setTargetValue (dbToGain (matchState));
    matchSm.applyGain (buffer, n);
}

int SpacenerdEraProcessor::getNumPrograms() { return (int) std::size (presets); }

void SpacenerdEraProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= getNumPrograms()) return;
    const auto& pr = presets[index];
    auto set = [this] (const char* id, float v)
    {
        auto* prm = apvts.getParameter (id);
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
        prm->endChangeGesture();
    };
    set (year, pr.year); set (intensity, pr.intensity); set (genre, (float) pr.genre);
    set (mix, 100.0f); set (outGain, 0.0f);
    set (split, 0.0f);                       // інакше Split перекриває низ/верх пресета
    currentPreset.store (index);
    apvts.state.setProperty ("preset", index, nullptr);
}

const juce::String SpacenerdEraProcessor::getProgramName (int index)
{
    return index >= 0 && index < getNumPrograms() ? juce::String (presets[index].name) : juce::String();
}

void SpacenerdEraProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SpacenerdEraProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentPreset.store ((int) apvts.state.getProperty ("preset", 0));
        }
}

juce::AudioProcessorEditor* SpacenerdEraProcessor::createEditor() { return new SpacenerdEraEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SpacenerdEraProcessor(); }
