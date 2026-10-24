#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace syn;

SpacenerdSynthProcessor::SpacenerdSynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createSynthLayout())
{
    prepareToPlay (48000.0, 512);
}

bool SpacenerdSynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void SpacenerdSynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    maxBlock = std::max (64, samplesPerBlock);
    g.fs2 = 2.0 * fs;
    for (auto& v : voices) v.prepare (g.fs2);
    decim.reset();
    osL.assign ((size_t) (2 * 64), 0.0f);
    osR.assign ((size_t) (2 * 64), 0.0f);
    chorus.prepare (fs);
    delay.prepare (fs);
    reverb.prepare (fs, std::max (maxBlock, 4096));
    monoStack.clear(); arpHeld.clear(); arpNote = -1;
    sustained.fill (false);
}

void SpacenerdSynthProcessor::readParams()
{
    using namespace SynIDs;
    g.engine = (int) p (engine);
    g.mode = (int) p (mode);
    g.glideMs = p (glide);
    const char* wv[2] { aWave, bWave }; const char* oc[2] { aOct, bOct }; const char* se[2] { aSemi, bSemi }; const char* fi[2] { aFine, bFine };
    const char* lv[2] { aLevel, bLevel }; const char* sh[2] { aShape, bShape }; const char* un[2] { aUni, bUni }; const char* de[2] { aDetune, bDetune }; const char* sp[2] { aSpread, bSpread };
    for (int o = 0; o < 2; ++o)
    {
        g.wave[o] = (int) p (wv[o]);
        g.pitchOff[o] = 12.0f * p (oc[o]) + p (se[o]) + 0.01f * p (fi[o]);
        const float l = p (lv[o]) * 0.01f;
        g.level[o] = l * l * 1.1f;
        g.shape[o] = p (sh[o]) * 0.01f;
        g.uni[o] = juce::jlimit (1, kMaxUni, (int) p (un[o]));
        g.detune[o] = p (de[o]) * 0.01f;
        g.spread[o] = p (sp[o]) * 0.01f;
    }
    g.sync = p (bSync) > 0.5f;
    g.fm = p (fm) * 0.01f;
    { const float s = p (subLevel) * 0.01f; g.subLevel = s * s; }
    g.subOct = (int) p (subOct); g.subWave = (int) p (subWave);
    { const float nz = p (noise) * 0.01f; g.noise = nz * nz; }

    g.sax.breath = p (saxBreath) * 0.01f; g.sax.reed = p (saxReed) * 0.01f; g.sax.growl = p (saxGrowl) * 0.01f;
    g.sax.air = p (saxAir) * 0.01f; g.sax.body = p (saxBody) * 0.01f; g.sax.scoop = p (saxScoop) * 0.01f;

    g.fType = (int) p (fType); g.cutoff = p (cutoff); g.reso = p (reso) * 0.01f; g.fDriveGain = sn::dbToGain (p (fDrive));
    g.fEnv = p (fEnv) * 0.01f; g.fKey = p (fKey) * 0.01f; g.fVel = p (fVel) * 0.01f;
    g.fA = p (fA); g.fD = p (fD); g.fS = p (fS) * 0.01f; g.fR = p (fR);
    g.aA = p (aA); g.aD = p (aD); g.aS = p (aS) * 0.01f; g.aR = p (aR); g.ampVel = p (ampVel) * 0.01f;
    g.dOn = p (dOn) > 0.5f; g.dType = (int) p (dType); g.dAmount = p (dAmount) * 0.01f; g.dTone = p (dTone); g.dLow = p (dLow); g.dMix = p (dMix) * 0.01f;
    g.mA = p (mA); g.mD = p (mD);
    for (int k = 0; k < 4; ++k) { g.mSrc[k] = (int) p (mSrc[k]); g.mDst[k] = (int) p (mDst[k]); g.mAmt[k] = p (mAmt[k]) * 0.01f; }
    g.vibAuto = p (vibAuto) * 0.01f;
    g.drift = p (drift) * 0.01f;
    g.bowl.type = (int) p (bowlType); g.bowl.decay = p (bowlDecay) * 0.01f; g.bowl.hard = p (bowlHard) * 0.01f;
    g.bowl.rub = p (bowlRub) * 0.01f; g.bowl.beat = p (bowlBeat) * 0.01f; g.bowl.bright = p (bowlBright) * 0.01f;
}

void SpacenerdSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = juce::jlimit (30.0, 300.0, *b);
            playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) ppq = *q;
        }

    readParams();
    if (g.engine != lastEngine || g.mode != lastMode)
    {
        for (auto& v : voices) v.reset();
        monoStack.clear();
        lastEngine = g.engine; lastMode = g.mode;
    }
    const bool arpOn = p (SynIDs::arpOn) > 0.5f;
    if (arpWasOn && ! arpOn) { if (arpNote >= 0) noteOff (arpNote); arpNote = -1; for (int nt : arpHeld) { juce::ignoreUnused (nt); } arpHeld.clear(); }
    arpWasOn = arpOn;

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    std::vector<float> tmpR;
    float* Rw = R;
    if (Rw == nullptr) { tmpR.assign ((size_t) n, 0.0f); Rw = tmpR.data(); }

    int pos = 0;
    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, n, meta.samplePosition);
        if (t > pos) render (L + pos, Rw + pos, t - pos);
        pos = std::max (pos, t);
        handleMidi (meta.getMessage());
    }
    if (pos < n) render (L + pos, Rw + pos, n - pos);

    // Ефекти
    using namespace SynIDs;
    if (p (chOn) > 0.5f) chorus.process (L, Rw, n, p (chRate), p (chDepth) * 0.01f, p (chMix) * 0.01f);
    if (p (dlOn) > 0.5f) delay.process (L, Rw, n, (float) (60.0 / bpm * delayBeats ((int) p (dlTime))), p (dlFb) * 0.01f, p (dlMix) * 0.01f, p (dlPing) > 0.5f);
    if (p (rvOn) > 0.5f)
        for (int s0 = 0; s0 < n; s0 += 4096)
            reverb.process (L + s0, Rw + s0, std::min (4096, n - s0), p (rvSize) * 0.01f, p (rvMix) * 0.01f, p (rvShimmer) * 0.01f, p (rvFreeze) > 0.5f);

    // Гучність і м'яке обмеження (без жорсткого кліпу на виході)
    const float vol = sn::dbToGain (p (volume));
    auto soft = [] (float x) { const float a = std::abs (x); return a < 0.9f ? x : std::copysign (0.9f + 0.1f * std::tanh ((a - 0.9f) * 10.0f), x); };
    float pkL = 0.0f, pkR = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        L[i] = soft (L[i] * vol); Rw[i] = soft (Rw[i] * vol);
        pkL = std::max (pkL, std::abs (L[i])); pkR = std::max (pkR, std::abs (Rw[i]));
    }
    if (R == nullptr) for (int i = 0; i < n; ++i) L[i] = 0.5f * (L[i] + Rw[i]);
    outPeak[0].push (pkL); outPeak[1].push (pkR);
    scope.push (L, Rw, n);
    int act = 0; for (auto& v : voices) act += v.active() ? 1 : 0;
    activeVoices.store (act);
}

void SpacenerdSynthProcessor::render (float* L, float* R, int n)
{
    for (int start = 0; start < n; start += 64)
    {
        const int len = std::min (64, n - start);
        if (p (SynIDs::arpOn) > 0.5f) runArp (len);
        const int n2 = 2 * len;
        std::fill_n (osL.data(), n2, 0.0f);
        std::fill_n (osR.data(), n2, 0.0f);

        for (int off = 0; off < n2; off += kSub)
        {
            const int sl = std::min (kSub, n2 - off);
            // LFO, вібрато (раз на підблок)
            const double beatsPerSample = bpm / 60.0 / g.fs2;
            const char* shp[2] { SynIDs::l1Shape, SynIDs::l2Shape }; const char* rt[2] { SynIDs::l1Rate, SynIDs::l2Rate };
            const char* sy[2] { SynIDs::l1Sync, SynIDs::l2Sync }; const char* dv[2] { SynIDs::l1Div, SynIDs::l2Div };
            for (int k = 0; k < 2; ++k)
            {
                const int shape = (int) p (shp[k]);
                if (p (sy[k]) > 0.5f)
                {
                    const double beats = divBeats ((int) p (dv[k]));
                    if (playing) lfo[k].sync (shape, (float) std::fmod (ppq / beats, 1.0), 0.3f);
                    else lfo[k].advance (shape, (float) (beatsPerSample * sl / beats), 0.3f);
                }
                else lfo[k].advance (shape, (float) (p (rt[k]) * sl / g.fs2), 0.3f);
                g.lfo[k] = lfo[k].out;
            }
            if (playing) ppq += beatsPerSample * sl;
            vibPh += (float) (p (SynIDs::vibRate) * sl / g.fs2); if (vibPh >= 1.0f) vibPh -= 1.0f;
            g.vib = std::sin (6.2831853f * vibPh);
            g.vibDepth = 0.6f * (g.wheel * p (SynIDs::vibWheel) * 0.01f + g.at * p (SynIDs::vibAT) * 0.01f);

            for (auto& v : voices)
                if (v.active()) v.render (osL.data() + off, osR.data() + off, sl, g);
        }
        decim.process (osL.data(), osR.data(), L + start, R + start, len);
    }
}

//==============================================================================
void SpacenerdSynthProcessor::handleMidi (const juce::MidiMessage& m)
{
    const bool arpOn = p (SynIDs::arpOn) > 0.5f;
    if (m.isNoteOn())
    {
        const int nt = m.getNoteNumber();
        const float vel = m.getFloatVelocity();
        if (arpOn)
        {
            if (std::find (arpHeld.begin(), arpHeld.end(), nt) == arpHeld.end()) arpHeld.push_back (nt);
            heldVel[(size_t) nt] = vel;
            if (arpHeld.size() == 1 && ! playing) arpPhase = 1.0;   // перша нота — одразу
        }
        else noteOn (nt, vel);
    }
    else if (m.isNoteOff())
    {
        const int nt = m.getNoteNumber();
        if (arpOn) arpHeld.erase (std::remove (arpHeld.begin(), arpHeld.end(), nt), arpHeld.end());
        else if (sustain) sustained[(size_t) nt] = true;
        else noteOff (nt);
    }
    else if (m.isPitchWheel()) g.bendSemi = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * p (SynIDs::bend);
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();
        const float v = (float) m.getControllerValue() / 127.0f;
        if (cc == 1) g.wheel = v;
        else if (cc == 64)
        {
            const bool on = v >= 0.5f;
            if (sustain && ! on)
                for (int nt = 0; nt < 128; ++nt)
                    if (sustained[(size_t) nt]) { sustained[(size_t) nt] = false; noteOff (nt); }
            sustain = on;
        }
        else if (cc == 120 || cc == 123) allOff();
    }
    else if (m.isChannelPressure()) g.at = (float) m.getChannelPressureValue() / 127.0f;
    else if (m.isAftertouch()) g.at = std::max (g.at, (float) m.getAfterTouchValue() / 127.0f);
    else if (m.isAllNotesOff() || m.isAllSoundOff()) allOff();
}

void SpacenerdSynthProcessor::noteOn (int nt, float vel)
{
    if (g.mode == ModePoly)
    {
        Voice* v = nullptr;
        for (auto& x : voices) if (x.active() && x.note == nt) { v = &x; break; }
        if (v == nullptr) for (auto& x : voices) if (! x.active()) { v = &x; break; }
        if (v == nullptr)   // крадіжка: найстаріший відпущений, інакше найстаріший
        {
            for (auto& x : voices) if (! x.held && (v == nullptr || x.age < v->age)) v = &x;
            if (v == nullptr) for (auto& x : voices) if (v == nullptr || x.age < v->age) v = &x;
        }
        v->start (nt, vel, g.glideMs > 0.5f ? lastPitch : -1.0f, true, false, g, rng);
        v->age = ++ageCounter;
    }
    else
    {
        auto& v = voices[0];
        const bool wasHeld = ! monoStack.empty();
        monoStack.erase (std::remove (monoStack.begin(), monoStack.end(), nt), monoStack.end());
        monoStack.push_back (nt);
        const bool legato = g.mode == ModeLegato && wasHeld && v.active();
        const float from = v.active() ? v.currentPitch() : lastPitch;
        const float glideFrom = (g.mode == ModeMono || legato) ? from : -1.0f;
        v.start (nt, vel, glideFrom, ! legato, legato, g, rng);
        v.age = ++ageCounter;
    }
    lastPitch = (float) nt;
}

void SpacenerdSynthProcessor::noteOff (int nt)
{
    if (g.mode == ModePoly)
    {
        for (auto& v : voices) if (v.held && v.note == nt) v.release (g);
        return;
    }
    auto& v = voices[0];
    monoStack.erase (std::remove (monoStack.begin(), monoStack.end(), nt), monoStack.end());
    if (v.note != nt) return;
    if (monoStack.empty()) { v.release (g); return; }
    const int back = monoStack.back();
    if (g.mode == ModeLegato) v.glideTo (back);
    else v.start (back, v.vel, v.currentPitch(), true, false, g, rng);
    lastPitch = (float) back;
}

void SpacenerdSynthProcessor::allOff()
{
    for (auto& v : voices) if (v.active()) v.release (g);
    monoStack.clear(); arpHeld.clear(); sustained.fill (false);
    if (arpNote >= 0) arpNote = -1;
}

void SpacenerdSynthProcessor::runArp (int n)
{
    using namespace SynIDs;
    const double stepBeats = divBeats ((int) p (arpRate));
    const double samplesPerStep = 60.0 / bpm * stepBeats * fs;

    if (arpNote >= 0) { arpOffIn -= n; if (arpOffIn <= 0) { noteOff (arpNote); arpNote = -1; } }
    if (arpHeld.empty()) { if (arpNote >= 0) { noteOff (arpNote); arpNote = -1; } arpIdx = 0; arpDir = 1; lastPpq = -1.0; return; }

    bool trigger = false;
    if (playing)
    {
        const double step = std::floor (ppq / stepBeats);
        if (step != lastPpq) { trigger = true; lastPpq = step; }
    }
    else
    {
        arpPhase += n / samplesPerStep;
        if (arpPhase >= 1.0) { trigger = true; arpPhase -= std::floor (arpPhase); }
    }
    if (! trigger) return;

    // Послідовність: утримані ноти × октави
    const int mode = (int) p (arpMode), octs = (int) p (arpOct);
    std::vector<int> base (arpHeld);
    if (mode != 4) std::sort (base.begin(), base.end());
    arpOrder.clear();
    for (int o = 0; o < octs; ++o) for (int nt : base) if (nt + 12 * o < 128) arpOrder.push_back (nt + 12 * o);
    const int cnt = (int) arpOrder.size();
    int idx = 0;
    switch (mode)
    {
        case 1: idx = cnt - 1 - (arpIdx % cnt); ++arpIdx; break;
        case 2:
        {
            if (cnt == 1) { idx = 0; break; }
            const int period = 2 * cnt - 2, k = arpIdx % period;
            idx = k < cnt ? k : period - k; ++arpIdx; break;
        }
        case 3: idx = (int) ((rng.next() * 0.5f + 0.5f) * (float) cnt) % cnt; break;
        default: idx = arpIdx % cnt; ++arpIdx; break;
    }
    if (arpNote >= 0) noteOff (arpNote);
    arpNote = arpOrder[(size_t) idx];
    const int src = base[(size_t) (idx % (int) base.size())];
    noteOn (arpNote, heldVel[(size_t) juce::jlimit (0, 127, src)]);
    arpOffIn = (int) (samplesPerStep * p (arpGate) * 0.01);
}

//==============================================================================
void SpacenerdSynthProcessor::setCurrentProgram (int index)
{
    const auto& presets = getSynthPresets();
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

const juce::String SpacenerdSynthProcessor::getProgramName (int index)
{
    const auto& presets = getSynthPresets();
    return index >= 0 && index < (int) presets.size() ? juce::String::fromUTF8 (presets[(size_t) index].name) : juce::String();
}

void SpacenerdSynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SpacenerdSynthProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentPreset.store ((int) apvts.state.getProperty ("preset", 0));
        }
}

juce::AudioProcessorEditor* SpacenerdSynthProcessor::createEditor()
{
    return new SpacenerdSynthEditor (*this);
}

#ifndef SN_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpacenerdSynthProcessor();
}
#endif
