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
            params.add (ranged->getParameterID(), apvts.getRawParameterValue (ranged->getParameterID()));
    startTimerHz (30);
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
    const int numCh = std::min (2, getTotalNumOutputChannels());
    constexpr int factor = 1 << kOsOrder;
    osFs = fs * factor;

    for (auto& chain : eq) for (auto& f : chain) f.reset();
    updateEq (true);

    comp.prepare (osFs, numCh);
    tube.prepare (osFs, numCh, kSatRef);
    tape.prepare (osFs, numCh, kSatRef);

    sideHpf.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    sideHpf.prepare ({ fs, (juce::uint32) samplesPerBlock, 1 });
    sideHpf.setCutoffFrequency (100.0f);

    using OS = juce::dsp::Oversampling<float>;
    oversampler = std::make_unique<OS> ((size_t) numCh, (size_t) kOsOrder, OS::filterHalfBandFIREquiripple, true, true);
    oversampler->initProcessing ((size_t) samplesPerBlock);
    tpDetector = std::make_unique<OS> ((size_t) numCh, (size_t) kOsOrder, OS::filterHalfBandFIREquiripple, true, false);
    tpDetector->initProcessing ((size_t) samplesPerBlock);
    tpOut = std::make_unique<OS> ((size_t) numCh, (size_t) kOsOrder, OS::filterHalfBandFIREquiripple, false, false);
    tpOut->initProcessing ((size_t) samplesPerBlock);
    peakBuf.assign ((size_t) samplesPerBlock, 0.0f);
    outTpBuf.assign ((size_t) samplesPerBlock, 0.0f);

    // Затримка детектора true peak (лише шлях "вгору"), вимірюємо імпульсом
    int detectorDelay = 0;
    {
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

    // Lookahead 4 мс: менше спотворень на басу, ніж у коротких лімітерах
    const int lookahead = std::max (4, (int) std::round (0.004 * fs));
    limiter.prepare (fs, numCh, lookahead, detectorDelay);
    limiter.setLowFreqAware (true);

    setLatencySamples ((int) std::round (oversampler->getLatencyInSamples()) + limiter.getLatency());

    loudness.prepare (fs, numCh);
    inLoudness.prepare (fs, numCh);
    analysis.prepare (fs);
    osDry.setSize (numCh, samplesPerBlock * factor);
    firstBlock = true;
    clipPrevX.fill (0.0f); clipPrevF.fill (0.0f);

    for (auto* sm : { &inGainSm, &outGainSm, &limGainSm, &matchSm })
        sm->reset (fs, 0.03);
    widthSm.reset (fs, 0.03);

    inGainSm.setCurrentAndTargetValue  (dbToGain (p (ParamIDs::inGain)));
    outGainSm.setCurrentAndTargetValue (dbToGain (p (ParamIDs::outGain)));
    limGainSm.setCurrentAndTargetValue (dbToGain (on (ParamIDs::limOn) ? p (ParamIDs::limGain) : 0.0f));
    matchSm.setCurrentAndTargetValue (1.0f);
    widthSm.setCurrentAndTargetValue (p (ParamIDs::width) * 0.01f);
    matchState = 0.0f;
    matchDb.store (0.0f);
}

void SpacenerdMasterProcessor::updateEq (bool force)
{
    eqTarget = { p (ParamIDs::hpfFreq),
                 p (ParamIDs::lowGain),  p (ParamIDs::lowFreq),
                 p (ParamIDs::midGain),  p (ParamIDs::midFreq), p (ParamIDs::midQ),
                 p (ParamIDs::highGain), p (ParamIDs::highFreq) };

    bool changed = force;
    for (size_t i = 0; i < eqCur.size(); ++i)
    {
        const float t = eqTarget[i];
        float& c = eqCur[i];
        if (force) { c = t; continue; }
        const float d = t - c;
        if (std::abs (d) > 1.0e-4f * (1.0f + std::abs (t))) { c += d * 0.06f; changed = true; }
        else if (c != t) { c = t; changed = true; }
    }
    if (! changed) return;

    sn::Biquad hp, lo, mid, hi;
    if (eqCur[0] >= 15.0f) hp.setHighPass (osFs, eqCur[0]); else hp.setBypass();
    lo.setLowShelf  (osFs, eqCur[2], 0.707, eqCur[1]);
    mid.setPeak     (osFs, eqCur[4], eqCur[5], eqCur[3]);
    hi.setHighShelf (osFs, eqCur[7], 0.707, eqCur[6]);

    for (auto& chain : eq)
    {
        chain[0].copyCoeffs (hp); chain[1].copyCoeffs (lo);
        chain[2].copyCoeffs (mid); chain[3].copyCoeffs (hi);
    }
}

template <typename Fn>
void SpacenerdMasterProcessor::runFaded (float& mix, bool target, float* const* os, int numCh, int n, Fn&& fn)
{
    const float t = target ? 1.0f : 0.0f;
    if (mix <= 0.0f && ! target) return;
    const float m0 = mix;
    const float step = std::min (1.0f, (float) (n / (1 << kOsOrder)) / (float) (0.02 * fs));   // ≈ 20 мс
    const float m1 = std::abs (t - mix) <= step ? t : mix + (t > mix ? step : -step);
    mix = m1;
    if (m0 >= 1.0f && m1 >= 1.0f) { fn (false); return; }

    for (int ch = 0; ch < numCh; ++ch) std::copy_n (os[ch], n, osDry.getWritePointer (ch));
    fn (m0 <= 0.0f);
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* dry = osDry.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
        {
            const float k = m0 + (m1 - m0) * (float) i / (float) n;
            os[ch][i] = k * os[ch][i] + (1.0f - k) * dry[i];
        }
    }
}

/** М'який кліпер T·tanh(x/T) з антиаліасингом ADAA-1: y = (F(x) − F(x₁)) / (x − x₁), F = T²·ln cosh(x/T). */
float SpacenerdMasterProcessor::clipSample (float x, float& prevX, float& prevF, float T) const noexcept
{
    auto F = [T] (float v)
    {
        const float a = std::abs (v / T);
        // ln cosh(a) без переповнення: a + ln(1 + e^(−2a)) − ln 2
        return T * T * (a + std::log1p (std::exp (-2.0f * a)) - 0.69314718f);
    };
    const float fx = F (x);
    const float dx = x - prevX;
    float y;
    if (std::abs (dx) < 1.0e-5f) y = T * std::tanh (0.5f * (x + prevX) / T);
    else y = (fx - prevF) / dx;
    prevX = x; prevF = fx;
    return y;
}

void SpacenerdMasterProcessor::measureTruePeak (juce::dsp::Oversampling<float>& os, const juce::AudioBuffer<float>& buffer,
                                                int numCh, int n, float* dest)
{
    juce::dsp::AudioBlock<const float> block (buffer.getArrayOfReadPointers(), (size_t) numCh, (size_t) n);
    auto up = os.processSamplesUp (block);
    constexpr int factor = 1 << kOsOrder;
    for (int i = 0; i < n; ++i)
    {
        float pk = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const auto* d = up.getChannelPointer ((size_t) ch) + i * factor;
            for (int k = 0; k < factor; ++k) pk = std::max (pk, std::abs (d[k]));
        }
        dest[i] = pk;
    }
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

    const bool match = on (ParamIDs::gainMatch);
    inLoudness.process (buffer);   // гучність оригіналу (для Gain Match і Assist)

    // --- Вхід
    inGainSm.setTargetValue (dbToGain (p (ParamIDs::inGain)));
    inGainSm.applyGain (buffer, n);

    for (int ch = 0; ch < numCh; ++ch)
        inPeak[(size_t) ch].push (buffer.getMagnitude (ch, 0, n));
    inFifo.push (buffer.getReadPointer (0), numCh > 1 ? buffer.getReadPointer (1) : nullptr, n);

    // Повільно згладжені ручки (≈ 50 мс): без сходинок при автоматизації
    const float kSm = std::min (1.0f, (float) n / (float) (0.05 * fs));
    auto smooth = [&] (float& v, float t) { v += (t - v) * kSm; };
    if (firstBlock)
    {
        makeupSm = p (ParamIDs::makeup); compWetSm = p (ParamIDs::compMix) * 0.01f; driveSm = p (ParamIDs::drive);
        satWetSm = p (ParamIDs::satMix) * 0.01f; ceilingSm = p (ParamIDs::ceiling); clipSm = p (ParamIDs::clip) * 0.01f;
        eqMix = on (ParamIDs::eqOn) ? 1.0f : 0.0f; compMixF = on (ParamIDs::compOn) ? 1.0f : 0.0f;
        satMixF = on (ParamIDs::satOn) ? 1.0f : 0.0f; widthMix = on (ParamIDs::widthOn) ? 1.0f : 0.0f;
        limMix = on (ParamIDs::limOn) ? 1.0f : 0.0f;
        firstBlock = false;
    }
    smooth (makeupSm, p (ParamIDs::makeup)); smooth (compWetSm, p (ParamIDs::compMix) * 0.01f);
    smooth (driveSm, p (ParamIDs::drive));   smooth (satWetSm, p (ParamIDs::satMix) * 0.01f);
    smooth (ceilingSm, p (ParamIDs::ceiling)); smooth (clipSm, p (ParamIDs::clip) * 0.01f);

    // --- 4x: EQ → компресор → сатурація (кожен модуль вмикається/вимикається плавно)
    {
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) numCh, (size_t) n);
        auto up = oversampler->processSamplesUp (block);
        const int on_ = (int) up.getNumSamples();
        float* osData[2] = { up.getChannelPointer (0), numCh > 1 ? up.getChannelPointer (1) : nullptr };

        runFaded (eqMix, on (ParamIDs::eqOn), osData, numCh, on_, [&] (bool fresh)
        {
            if (fresh) { for (auto& chain : eq) for (auto& f : chain) f.reset(); }
            constexpr int sub = 32;
            for (int start = 0; start < on_; start += sub)
            {
                updateEq (false);
                const int len = std::min (sub, on_ - start);
                for (int ch = 0; ch < numCh; ++ch)
                {
                    auto& chain = eq[(size_t) ch];
                    auto* d = osData[ch] + start;
                    for (int i = 0; i < len; ++i)
                    {
                        float x = d[i];
                        for (auto& f : chain) x = f.process (x);
                        d[i] = x;
                    }
                }
            }
        });

        float gr = 0.0f;
        runFaded (compMixF, on (ParamIDs::compOn), osData, numCh, on_, [&] (bool fresh)
        {
            if (fresh) comp.reset();
            const Compressor::Settings cs {
                p (ParamIDs::threshold), p (ParamIDs::ratio), p (ParamIDs::attack), p (ParamIDs::release),
                p (ParamIDs::knee), p (ParamIDs::scHpf), makeupSm, compWetSm, on (ParamIDs::compAuto) };
            gr = comp.process (osData, numCh, on_, cs);
        });
        compGr.push (gr * compMixF);

        runFaded (satMixF, on (ParamIDs::satOn), osData, numCh, on_, [&] (bool fresh)
        {
            if (fresh) { tube.reset(); tape.reset(); }
            switch ((int) p (ParamIDs::satType))
            {
                // Усі типи відкалібровані на -6 dBFS: перемикання не стрибає гучністю
                case 0:  tube.process (up, driveSm, 40.0f, satWetSm); break;
                case 1:  tape.process (up, driveSm, sn::TapeStage::ips30, satWetSm); break;
                default: sn::SoftClip::process (up, driveSm, satWetSm, kSatRef); break;
            }
        });

        oversampler->processSamplesDown (block);
    }

    // --- Ширина стерео (M/S) і моно-баси (плавне вмикання)
    {
        const float wT = on (ParamIDs::widthOn) ? 1.0f : 0.0f;
        const float w0 = widthMix;
        widthMix = std::abs (wT - widthMix) < kSm ? wT : widthMix + (wT > widthMix ? kSm : -kSm);
        if (numCh == 2 && (w0 > 0.0f || wT > 0.0f))
        {
            if (w0 <= 0.0f) sideHpf.reset();
            widthSm.setTargetValue (p (ParamIDs::width) * 0.01f);
            const float monoHz = p (ParamIDs::monoBass);
            const bool mono = monoHz >= 20.0f;
            if (mono) sideHpf.setCutoffFrequency (monoHz);

            auto* l = buffer.getWritePointer (0);
            auto* r = buffer.getWritePointer (1);
            for (int i = 0; i < n; ++i)
            {
                const float m = 0.5f * (l[i] + r[i]);
                const float sIn = 0.5f * (l[i] - r[i]);
                float s = sIn * widthSm.getNextValue();
                if (mono) s = sideHpf.processSample (0, s);
                const float k = w0 + (widthMix - w0) * (float) i / (float) n;
                const float sOut = k * s + (1.0f - k) * sIn;
                l[i] = m + sOut;
                r[i] = m - sOut;
            }
        }
    }

    // --- Лімітер: драйв → м'який кліпер (зрізає короткі піки, лімітеру менше роботи) → true-peak lookahead
    const bool limActive = on (ParamIDs::limOn);
    const float lT = limActive ? 1.0f : 0.0f, l0 = limMix;
    limMix = std::abs (lT - limMix) < kSm ? lT : limMix + (lT > limMix ? kSm : -kSm);
    limGainSm.setTargetValue (dbToGain (limActive ? p (ParamIDs::limGain) : 0.0f));
    limGainSm.applyGain (buffer, n);

    const float ceilLin = dbToGain (ceilingSm);
    if (clipSm > 0.001f && limMix > 0.0f)
    {
        // Поріг: від +3 дБ над стелею (Clip 0 %) до −2 дБ під нею (Clip 100 %)
        const float T = ceilLin * dbToGain (3.0f - 5.0f * clipSm);
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                const float y = clipSample (d[i], clipPrevX[(size_t) ch], clipPrevF[(size_t) ch], T);
                d[i] = limMix * y + (1.0f - limMix) * d[i];
            }
        }
    }
    else { clipPrevX.fill (0.0f); clipPrevF.fill (0.0f); }

    measureTruePeak (*tpDetector, buffer, numCh, n, peakBuf.data());
    const float minG = limiter.process (buffer.getArrayOfWritePointers(), numCh, n, peakBuf.data(),
                                        ceilLin, p (ParamIDs::limRel), l0, limMix);
    limGr.push (-gainToDb (minG));

    // --- Вихід
    outGainSm.setTargetValue (dbToGain (p (ParamIDs::outGain)));
    outGainSm.applyGain (buffer, n);

    for (int ch = 0; ch < numCh; ++ch)
        outPeak[(size_t) ch].push (buffer.getMagnitude (ch, 0, n));

    measureTruePeak (*tpOut, buffer, numCh, n, outTpBuf.data());
    float tp = tpMaxReset.exchange (false) ? 0.0f : truePeakMax.load();
    for (int i = 0; i < n; ++i) tp = std::max (tp, outTpBuf[(size_t) i]);
    truePeakMax.store (tp);

    loudness.process (buffer);
    outFifo.push (buffer.getReadPointer (0), numCh > 1 ? buffer.getReadPointer (1) : nullptr, n);

    // --- Gain Match: прибирає різницю гучності для чесного порівняння з Bypass (метри її не бачать)
    float target = 0.0f;
    if (match)
    {
        const float li = inLoudness.shortTerm.load(), lo = loudness.shortTerm.load();
        if (li > -70.0f && lo > -70.0f)
            target = juce::jlimit (-30.0f, 0.0f, li - lo);
        else
            target = matchState;
    }
    matchState += (target - matchState) * std::min (1.0f, (float) n / (float) (0.4 * fs));
    matchDb.store (match ? matchState : 0.0f);
    matchSm.setTargetValue (dbToGain (matchState));
    matchSm.applyGain (buffer, n);
}

void SpacenerdMasterProcessor::setCurrentProgram (int index)
{
    const auto& presets = getFactoryPresets();
    if (index < 0 || index >= (int) presets.size()) return;

    const auto& preset = presets[(size_t) index];
    for (auto* param : getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
        const auto pid = ranged != nullptr ? ranged->getParameterID() : juce::String();
        if (ranged == nullptr || pid == ParamIDs::gainMatch || pid == ParamIDs::targetGenre || pid == ParamIDs::targetDecade
            || pid == ParamIDs::loudTarget)
            continue;   // моніторинг і ціль аналізатора — не частина звуку пресету

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
    resetMeters();
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
