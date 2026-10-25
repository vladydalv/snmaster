#include "PluginEditor.h"

using namespace juce;
using namespace SynIDs;

namespace synui
{
//==============================================================================
Scope::Scope (SpacenerdSynthProcessor& p) : proc (p) { ring.assign (8192, 0.0f); }

void Scope::update()
{
    proc.scope.pull ([this] (const float* l, const float* r, int n)
    {
        for (int i = 0; i < n; ++i) { ring[(size_t) wpos] = 0.5f * (l[i] + r[i]); wpos = (wpos + 1) % (int) ring.size(); }
    });
    // Синхронізація: останній фронт через нуль угору у вікні
    const int sz = (int) ring.size(), span = (int) shown.size() * 2;
    int start = (wpos - span - 1 + sz) % sz;
    for (int k = 0; k < sz - span - 2; ++k)
    {
        const int i = (wpos - span - 2 - k + 2 * sz) % sz, j = (i + 1) % sz;
        if (ring[(size_t) i] <= 0.0f && ring[(size_t) j] > 0.0f) { start = j; break; }
        if (k > 4000) break;
    }
    for (size_t i = 0; i < shown.size(); ++i) shown[i] = ring[(size_t) ((start + 2 * (int) i) % sz)];
    for (int c = 0; c < 2; ++c) meters[c].feed (sn::gainToDb (proc.outPeak[c].take()), false);
    repaint();
}

void Scope::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    snui::drawCard (g, r, "OUTPUT");
    auto area = r.reduced (16.0f).withTrimmedTop (32.0f);
    auto meterArea = area.removeFromRight (18.0f);
    area.removeFromRight (10.0f);

    // Сітка
    g.setColour (Theme::border.withAlpha (0.6f));
    for (int k = 1; k < 4; ++k) g.fillRect (area.getX(), area.getY() + area.getHeight() * (float) k / 4.0f, area.getWidth(), 1.0f);
    for (int k = 1; k < 6; ++k) g.fillRect (area.getX() + area.getWidth() * (float) k / 6.0f, area.getY(), 1.0f, area.getHeight());

    float pk = 0.0f; for (auto v : shown) pk = std::max (pk, std::abs (v));
    const float scale = 0.45f / std::max (0.15f, pk);
    Path wave;
    for (size_t i = 0; i < shown.size(); ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / (float) (shown.size() - 1);
        const float y = area.getCentreY() - shown[i] * scale * area.getHeight();
        if (i == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y);
    }
    g.setColour (colour.withAlpha (0.18f));
    g.strokePath (wave, PathStrokeType (6.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (colour);
    g.strokePath (wave, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));

    const float mw = (meterArea.getWidth() - 4.0f) * 0.5f;
    meters[0].draw (g, meterArea.withWidth (mw), false);
    meters[1].draw (g, meterArea.withTrimmedLeft (mw + 4.0f), false);

    g.setColour (Theme::muted);
    g.setFont (FontOptions (11.0f));
    const int v = proc.activeVoices.load();
    g.drawText (caption + "   " + String (v) + (v == 1 ? " voice" : " voices"), getLocalBounds().reduced (16, 0).withTrimmedTop (10).withHeight (18), Justification::centredRight);
}

//==============================================================================
EnvGraph::EnvGraph (AudioProcessorValueTreeState& s, const char* a, const char* d, const char* su, const char* r, Colour c)
    : state (s), ids { a, d, su, r }, colour (c)
{
    for (auto& id : ids) state.addParameterListener (id, this);
}
EnvGraph::~EnvGraph() { for (auto& id : ids) state.removeParameterListener (id, this); }

void EnvGraph::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (4.0f, 2.0f);
    g.setColour (Theme::bg.withAlpha (0.6f));
    g.fillRoundedRectangle (r, 6.0f);
    r = r.reduced (8.0f, 6.0f);
    auto val = [this] (int k) { return state.getRawParameterValue (ids[k])->load(); };
    // Час — у корені (і коротке, і довге видно)
    const float a = std::sqrt (val (0) / 10000.0f), d = std::sqrt (val (1) / 20000.0f), s = val (2) * 0.01f, rl = std::sqrt (val (3) / 20000.0f);
    const float hold = 0.18f, total = a + d + hold + rl + 1.0e-3f;
    const float W = r.getWidth(), H = r.getHeight(), x0 = r.getX(), yB = r.getBottom();
    const float xa = x0 + W * a / total, xd = xa + W * d / total, xs = xd + W * hold / total, xr = x0 + W;
    Path p;
    p.startNewSubPath (x0, yB);
    p.quadraticTo (x0 + (xa - x0) * 0.4f, yB - H * 0.9f, xa, r.getY());
    p.quadraticTo (xa + (xd - xa) * 0.25f, yB - H * s, xd, yB - H * s);
    p.lineTo (xs, yB - H * s);
    p.quadraticTo (xs + (xr - xs) * 0.25f, yB, xr, yB);
    Path fill (p); fill.lineTo (x0, yB); fill.closeSubPath();
    g.setGradientFill (ColourGradient (colour.withAlpha (0.28f), 0.0f, r.getY(), colour.withAlpha (0.02f), 0.0f, yB, false));
    g.fillPath (fill);
    g.setColour (colour);
    g.strokePath (p, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
}

//==============================================================================
SyncRate::SyncRate (AudioProcessorValueTreeState& s, const char* rate, const char* div, const char* sync)
    : state (s), syncId (sync), knob (s, rate, "Rate"), combo (s, div, "Rate")
{
    addAndMakeVisible (knob);
    addChildComponent (combo);
    update();
}
void SyncRate::resized() { knob.setBounds (getLocalBounds()); combo.setBounds (getLocalBounds().withSizeKeepingCentre (getWidth(), 46)); }
void SyncRate::update()
{
    const bool sy = state.getRawParameterValue (syncId)->load() > 0.5f;
    knob.setVisible (! sy); combo.setVisible (sy);
}

//==============================================================================
void BipolarBar::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f, 2.0f);
    g.setColour (Theme::track);
    g.fillRoundedRectangle (r, 5.0f);
    const float v = (float) getValue() / 100.0f, cx = r.getCentreX();
    const float x1 = cx + v * r.getWidth() * 0.5f;
    if (std::abs (v) > 0.001f)
    {
        g.setColour ((v > 0 ? Theme::accent : Theme::accent2).withAlpha (isMouseOverOrDragging() ? 0.85f : 0.65f));
        g.fillRoundedRectangle (Rectangle<float> (std::min (cx, x1), r.getY(), std::abs (x1 - cx), r.getHeight()), 4.0f);
    }
    g.setColour (Theme::border.brighter (0.4f));
    g.fillRect (cx - 0.5f, r.getY() + 3.0f, 1.0f, r.getHeight() - 6.0f);
    g.setColour (std::abs (v) > 0.001f ? Theme::text : Theme::muted);
    g.setFont (FontOptions (11.0f));
    g.drawText ((v > 0.0005f ? "+" : "") + String (roundToInt (v * 100.0f)) + " %", r.reduced (8.0f, 0.0f), v < -0.001f ? Justification::centredRight : Justification::centredLeft);
}

ModMatrix::ModMatrix (AudioProcessorValueTreeState& s)
{
    for (int k = 0; k < 4; ++k)
    {
        auto& r = rows[(size_t) k];
        r.src.addItemList (syn::srcNames(), 1);
        r.dst.addItemList (syn::dstNames(), 1);
        r.amt.setSliderStyle (Slider::LinearHorizontal);
        r.amt.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        r.amt.setVelocityModeParameters (0.25, 1, 0.0, true, ModifierKeys::shiftModifier);
        r.amt.setTooltip ("Amount (double-click: 0)");
        r.amt.setDoubleClickReturnValue (true, 0.0);
        for (Component* c : std::initializer_list<Component*> { &r.src, &r.dst, &r.amt }) addAndMakeVisible (c);
        r.aSrc = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (s, mSrc[k], r.src);
        r.aDst = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (s, mDst[k], r.dst);
        r.aAmt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, mAmt[k], r.amt);
    }
}
void ModMatrix::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (16);
    const int h = r.getHeight() / 4;
    for (auto& row : rows)
    {
        auto line = r.removeFromTop (h).reduced (0, 3);
        const int w = line.getWidth();
        row.src.setBounds (line.removeFromLeft (w * 32 / 100));
        line.removeFromLeft (6);
        row.dst.setBounds (line.removeFromLeft (w * 36 / 100));
        line.removeFromLeft (6);
        row.amt.setBounds (line);
    }
}
void ModMatrix::paint (Graphics& g)
{
    g.setColour (Theme::muted);
    g.setFont (FontOptions (10.0f, Font::bold));
    const int w = getWidth();
    g.drawText ("SOURCE", 0, 0, w * 32 / 100, 14, Justification::centredLeft);
    g.drawText ("TARGET", w * 32 / 100 + 6, 0, w * 36 / 100, 14, Justification::centredLeft);
    g.drawText ("AMOUNT", w * 68 / 100 + 12, 0, w * 32 / 100 - 12, 14, Justification::centredLeft);
}

//==============================================================================
Tabs::Tabs (StringArray n, std::function<void (int)> cb) : names (std::move (n)), onChange (std::move (cb)) { setRepaintsOnMouseActivity (true); }
void Tabs::paint (Graphics& g)
{
    const float w = (float) getWidth() / (float) names.size();
    for (int i = 0; i < names.size(); ++i)
    {
        auto r = Rectangle<float> (w * (float) i, 0.0f, w, (float) getHeight()).reduced (3.0f, 2.0f);
        const bool on = i == current;
        g.setColour (on ? Theme::accent.withAlpha (0.2f) : Theme::card);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (on ? Theme::accent : Theme::border);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (on ? Theme::text : Theme::muted);
        g.setFont (FontOptions (11.0f, Font::bold));
        g.drawText (names[i], r, Justification::centred);
    }
}
void Tabs::mouseDown (const MouseEvent& e)
{
    current = jlimit (0, names.size() - 1, (int) (e.position.x / ((float) getWidth() / (float) names.size())));
    repaint();
    if (onChange) onChange (current);
}
} // namespace synui

//==============================================================================
namespace
{
snui::Knob& K (snui::Section& s, AudioProcessorValueTreeState& st, const char* id, const char* name, const char* help,
               int h = 92, bool bip = false, Colour c = Theme::accent)
{
    auto& k = static_cast<snui::Knob&> (s.add (std::make_unique<snui::Knob> (st, id, name, bip, c), 1, h));
    if (help != nullptr) k.help (help);
    return k;
}
std::unique_ptr<Label> note (const String& text)
{
    auto l = std::make_unique<Label>();
    l->setText (text, dontSendNotification);
    l->setFont (FontOptions (12.0f));
    l->setColour (Label::textColourId, Theme::muted);
    l->setJustificationType (Justification::centredLeft);
    l->setMinimumHorizontalScale (1.0f);
    return l;
}
}

SynthContent::SynthContent (SpacenerdSynthProcessor& p)
    : proc (p), presetBox (p),
      engineSel (p.apvts, engine), modeSel (p.apvts, mode),
      oscA (p.apvts, "OSC A", {}, 4), oscB (p.apvts, "OSC B", {}, 4), mixer (p.apvts, "SUB / NOISE / FM", {}, 3),
      sax (p.apvts, "BARITONE SAX", {}, 6), bowls (p.apvts, "BOWLS & BELLS   modal synthesis", {}, 5),
      scope (p),
      filter (p.apvts, "FILTER", {}, 6), filterEnv (p.apvts, "FILTER ENVELOPE", {}, 4), drive (p.apvts, "DRIVE", dOn, 4),
      amp (p.apvts, "AMP", {}, 4), perform (p.apvts, "PLAY", {}, 3),
      tabs ({ "MODULATION", "ARPEGGIATOR", "EFFECTS" }, [this] (int t) { showTab (t); }),
      lfo1 (p.apvts, "LFO 1", {}, 2), lfo2 (p.apvts, "LFO 2", {}, 2), matrixCard (p.apvts, "MOD MATRIX", {}, 2),
      arp (p.apvts, "ARPEGGIATOR", arpOn, 4), chorus (p.apvts, "CHORUS", chOn, 3), delay (p.apvts, "DELAY", dlOn, 2), reverb (p.apvts, "REVERB", rvOn, 2)
{
    auto& st = p.apvts;
    auto v = [&st] (const char* id) { return st.getRawParameterValue (id)->load(); };
    auto rule = [this] (Component& c, std::function<bool()> f) { relevance.push_back ({ &c, std::move (f) }); };
    engineSel.setTooltip ("Analog: oscillators and filter.  Sax: physical model of a baritone sax.  Bowls: singing bowls, bells, chimes");
    modeSel.setTooltip ("Poly: chords.  Mono: one note, retriggers.  Legato: one note, smooth slides between held notes");

    // Осцилятори
    for (int o = 0; o < 2; ++o)
    {
        auto& s = o == 0 ? oscA : oscB;
        const bool a = o == 0;
        const char* lvId = a ? aLevel : bLevel; const char* uniId = a ? aUni : bUni; const char* wvId = a ? aWave : bWave;
        auto heard = [v, lvId, a] { return v (lvId) > 0.05f || (! a && v (fm) > 0.05f); };
        auto unison = [v, uniId, wvId, heard] { return heard() && (v (uniId) > 1.5f || (int) v (wvId) == syn::WSuper); };
        rule (s.add (std::make_unique<snui::Segmented> (st, wvId), 4, 30), heard);
        rule (K (s, st, a ? aOct : bOct, "Octave", nullptr), heard);
        rule (K (s, st, a ? aSemi : bSemi, "Semi", nullptr, 92, true), heard);
        rule (K (s, st, a ? aFine : bFine, "Fine", "detune in cents", 92, true), heard);
        K (s, st, lvId, "Level", nullptr);
        rule (K (s, st, a ? aShape : bShape, "Shape", "saw: hollow second saw; square: pulse width; triangle: wavefold; sine: drive; supersaw: side voices"), heard);
        rule (K (s, st, uniId, "Unison", "stacked voices for width and thickness"), heard);
        rule (K (s, st, a ? aDetune : bDetune, "Detune", "spread between unison voices (works with Unison 2+ or Super)"), unison);
        rule (K (s, st, a ? aSpread : bSpread, "Stereo", "how wide the unison voices are panned (works with Unison 2+ or Super)"), unison);
    }
    rule (mixer.add (std::make_unique<snui::Segmented> (st, subOct), 3, 28), [v] { return v (subLevel) > 0.05f; });
    rule (mixer.add (std::make_unique<snui::Segmented> (st, subWave), 3, 28), [v] { return v (subLevel) > 0.05f; });
    K (mixer, st, subLevel, "Sub", "clean sine/square one or two octaves down: weight under the bass");
    K (mixer, st, noise, "Noise", "breath, air, snare-like attack");
    K (mixer, st, fm, "FM", "osc B modulates osc A: metallic, aggressive");
    rule (mixer.add (std::make_unique<snui::PillToggle> (st, bSync, "HARD SYNC  B TO A"), 3, 26), [v] { return v (bLevel) > 0.05f; });

    // Саксофон
    const Colour saxC = Theme::gr, bowlC = Theme::accent2;
    K (sax, st, saxBreath, "Breath", "how hard you blow: soft and airy to loud and raspy", 104, false, saxC);
    K (sax, st, saxReed, "Tone", "darker, softer reed to brighter, edgier", 104, false, saxC);
    K (sax, st, saxGrowl, "Growl", "singing into the horn: rough, dirty tone", 104, false, saxC);
    K (sax, st, saxAir, "Air", "breath noise around the tone", 104, false, saxC);
    K (sax, st, saxBody, "Body", "resonance of the big baritone body", 104, false, saxC);
    K (sax, st, saxScoop, "Scoop", "pitch bends up into each note, like a player does", 104, false, saxC);
    saxNote = static_cast<Label*> (&sax.add (note ({}), 6, 40));

    // Чаші
    bowls.add (std::make_unique<snui::Segmented> (st, bowlType), 5, 30);
    K (bowls, st, bowlDecay, "Ring", "how long it rings: a short ping to almost a minute", 104, false, bowlC);
    K (bowls, st, bowlHard, "Mallet", "soft felt mallet (warm) to hard stick (bright attack)", 104, false, bowlC);
    K (bowls, st, bowlRub, "Rub", "rubbing the rim while the key is held: the bowl sings without end", 104, false, bowlC);
    K (bowls, st, bowlBeat, "Beating", "the slow wobble of real bowls (two close tones beating)", 104, false, bowlC);
    K (bowls, st, bowlBright, "Bright", "dark and round to shimmering high partials", 104, false, bowlC);
    bowls.add (note ("Tip: long Release in AMP and the Reverb with Shimmer make the bowls float."), 5, 30);

    // Фільтр і драйв
    filter.add (std::make_unique<snui::Segmented> (st, fType), 6, 30);
    K (filter, st, cutoff, "Cutoff", nullptr);
    K (filter, st, reso, "Reso", "resonance; MS-20 screams, Ladder sings");
    K (filter, st, fDrive, "Drive", "push the filter: fatter, more growl");
    K (filter, st, fEnv, "Env", "how far the filter envelope opens the filter", 92, true);
    K (filter, st, fKey, "Key", "higher notes open the filter more");
    K (filter, st, fVel, "Velocity", "harder playing opens the filter");
    filterEnv.add (std::make_unique<synui::EnvGraph> (st, fA, fD, fS, fR, Theme::accent2), 4, 60);
    rule (K (filterEnv, st, fA, "Attack", nullptr, 92, false, Theme::accent2), [v] { return std::abs (v (fEnv)) > 0.5f; });
    rule (K (filterEnv, st, fD, "Decay", nullptr, 92, false, Theme::accent2), [v] { return std::abs (v (fEnv)) > 0.5f; });
    rule (K (filterEnv, st, fS, "Sustain", nullptr, 92, false, Theme::accent2), [v] { return std::abs (v (fEnv)) > 0.5f; });
    rule (K (filterEnv, st, fR, "Release", nullptr, 92, false, Theme::accent2), [v] { return std::abs (v (fEnv)) > 0.5f; });
    drive.add (std::make_unique<snui::Segmented> (st, dType), 4, 30);
    K (drive, st, dAmount, "Amount", "warm: tube; fuzz: hard and buzzy; fold: metallic; rectify: octave fuzz", 92, false, Theme::hot);
    rule (K (drive, st, dTone, "Tone", "top end of the distortion", 92, false, Theme::hot), [v] { return v (dAmount) > 0.05f; });
    rule (K (drive, st, dLow, "Clean Low", "below this the bass stays clean: dirty but still deep and powerful", 92, false, Theme::hot), [v] { return v (dAmount) > 0.05f; });
    rule (K (drive, st, dMix, "Mix", nullptr, 92, false, Theme::hot), [v] { return v (dAmount) > 0.05f; });

    // Підсилювач і гра
    amp.add (std::make_unique<synui::EnvGraph> (st, aA, aD, aS, aR, Theme::accent), 4, 40);
    K (amp, st, aA, "Attack", nullptr, 88);
    K (amp, st, aD, "Decay", nullptr, 88);
    K (amp, st, aS, "Sustain", nullptr, 88);
    K (amp, st, aR, "Release", nullptr, 88);
    K (amp, st, ampVel, "Velocity", "how much playing softer makes it quieter", 88);
    K (amp, st, volume, "Volume", nullptr, 88);
    K (perform, st, glide, "Glide", "slide between notes (in Mono / Legato, or from the last note in Poly)");
    K (perform, st, bend, "Bend", "pitch bend range");
    K (perform, st, drift, "Drift", "slow living movement of pitch and tone, like an old analog");
    rule (K (perform, st, vibRate, "Vib Rate", nullptr), [v] { return v (vibWheel) > 0.5f || v (vibAuto) > 0.5f || v (vibAT) > 0.5f; });
    K (perform, st, vibWheel, "Vib Wheel", "vibrato depth from the mod wheel");
    K (perform, st, vibAuto, "Auto Vib", "vibrato fades in by itself after each note starts");

    // Модуляція
    for (int k = 0; k < 2; ++k)
    {
        auto& s = k == 0 ? lfo1 : lfo2;
        s.add (std::make_unique<snui::LabeledCombo> (st, k == 0 ? l1Shape : l2Shape, "Shape"), 1, 46);
        s.add (std::make_unique<snui::LabeledCombo> (st, k == 0 ? l1Dst : l2Dst, "Target"), 1, 46);
        auto* r = static_cast<synui::SyncRate*> (&s.add (std::make_unique<synui::SyncRate> (st, k == 0 ? l1Rate : l2Rate, k == 0 ? l1Div : l2Div, k == 0 ? l1Sync : l2Sync), 1, 88));
        K (s, st, k == 0 ? l1Amt : l2Amt, "Depth", "how much this LFO moves its target (more routings: MOD MATRIX)", 88, true);
        s.add (std::make_unique<snui::PillToggle> (st, k == 0 ? l1Sync : l2Sync, "SYNC TO TEMPO"), 2, 24);
        (k == 0 ? rate1 : rate2) = r;
    }
    matrixCard.add (std::make_unique<synui::ModMatrix> (st), 2, 136);
    K (matrixCard, st, mA, "Env Attack", "Mod Env: a second envelope for the matrix", 80);
    K (matrixCard, st, mD, "Env Decay", nullptr, 80);

    arp.add (std::make_unique<snui::Segmented> (st, arpMode), 4, 30);
    arp.add (std::make_unique<snui::LabeledCombo> (st, arpRate, "Rate"), 2, 46);
    K (arp, st, arpOct, "Octaves", nullptr);
    K (arp, st, arpGate, "Gate", "note length: short and plucky to legato");

    K (chorus, st, chRate, "Rate", nullptr);
    K (chorus, st, chDepth, "Depth", nullptr);
    K (chorus, st, chMix, "Mix", nullptr);
    delay.add (std::make_unique<snui::LabeledCombo> (st, dlTime, "Time"), 2, 46);
    K (delay, st, dlFb, "Feedback", nullptr);
    K (delay, st, dlMix, "Mix", nullptr);
    delay.add (std::make_unique<snui::PillToggle> (st, dlPing, "PING-PONG"), 2, 24);
    K (reverb, st, rvSize, "Size", "small room to a 30-second cathedral");
    K (reverb, st, rvMix, "Mix", nullptr);
    K (reverb, st, rvShimmer, "Shimmer", "octave-up glow rising in the tail");
    reverb.add (std::make_unique<snui::PillToggle> (st, rvFreeze, "FREEZE", Theme::accent2), 1, 26);

    for (Component* c : std::initializer_list<Component*> { &presetBox, &engineSel, &modeSel, &oscA, &oscB, &mixer, &scope,
                                                           &filter, &filterEnv, &drive, &amp, &perform, &tabs })
        addAndMakeVisible (c);
    for (Component* c : std::initializer_list<Component*> { &sax, &bowls, &lfo1, &lfo2, &matrixCard, &arp, &chorus, &delay, &reverb })
        addChildComponent (c);
    showTab (0);
    tick();
}

void SynthContent::showTab (int t)
{
    tabs.current = t; tabs.repaint();
    for (Component* c : std::initializer_list<Component*> { &lfo1, &lfo2, &matrixCard }) c->setVisible (t == 0);
    arp.setVisible (t == 1);
    for (Component* c : std::initializer_list<Component*> { &chorus, &delay, &reverb }) c->setVisible (t == 2);
}

void SynthContent::layoutEngine()
{
    const int e = (int) proc.apvts.getRawParameterValue (engine)->load();
    oscA.setVisible (e == syn::EngAnalog); oscB.setVisible (e == syn::EngAnalog); mixer.setVisible (e == syn::EngAnalog);
    sax.setVisible (e == syn::EngSax);
    bowls.setVisible (e == syn::EngBowls);
    scope.colour = e == syn::EngSax ? Theme::gr : e == syn::EngBowls ? Theme::accent2 : Theme::accent;
    scope.caption = e == syn::EngSax ? "Sax" : e == syn::EngBowls ? "Bowls" : "Analog";
    shownEngine = e;
}

void SynthContent::resized()
{
    presetBox.setBounds (300, 14, 260, 28);
    engineSel.setBounds (580, 13, 270, 30);
    modeSel.setBounds (866, 13, 240, 30);

    sourceArea = { 16, 64, 900, 280 };
    oscA.setBounds (16, 64, 304, 280);
    oscB.setBounds (328, 64, 304, 280);
    mixer.setBounds (640, 64, 276, 280);
    sax.setBounds (sourceArea);
    bowls.setBounds (sourceArea);
    scope.setBounds (924, 64, 380, 280);

    filter.setBounds (16, 352, 520, 226);
    filterEnv.setBounds (544, 352, 330, 226);
    drive.setBounds (882, 352, 422, 226);

    amp.setBounds (16, 586, 262, 292);
    perform.setBounds (286, 586, 298, 292);
    tabArea = { 592, 586, 712, 292 };
    tabs.setBounds (tabArea.getX(), tabArea.getY(), tabArea.getWidth(), 30);
    const int py = tabArea.getY() + 36, ph = tabArea.getHeight() - 36;
    lfo1.setBounds (592, py, 172, ph);
    lfo2.setBounds (772, py, 172, ph);
    matrixCard.setBounds (952, py, 352, ph);
    arp.setBounds (592, py, 712, ph);
    chorus.setBounds (592, py, 228, ph);
    delay.setBounds (828, py, 228, ph);
    reverb.setBounds (1064, py, 240, ph);
}

void SynthContent::tick()
{
    presetBox.sync();
    const int e = (int) proc.apvts.getRawParameterValue (engine)->load();
    if (e != shownEngine) layoutEngine();
    if (saxNote != nullptr)
    {
        const auto t = syn::SaxLibrary::get().ready()
            ? String ("Real 1926 Conn baritone (Karoryfer Bear Sax). Overlap notes in Legato to slide. Velocity + Breath = how hard you blow, Mod wheel = growl, Aftertouch = more breath.")
            : String ("Sax samples not found: playing the built-in model. Overlap notes in Legato to slide, velocity = how hard you blow.");
        if (saxNote->getText() != t) saxNote->setText (t, dontSendNotification);
    }
    for (auto& [c, f] : relevance)
    {
        const bool on = f();
        const float base = c->getParentComponent() == &drive && proc.apvts.getRawParameterValue (dOn)->load() < 0.5f ? 0.38f : 1.0f;
        const float a = base * (on ? 1.0f : 0.35f);
        if (std::abs (c->getAlpha() - a) > 0.01f) c->setAlpha (a);
    }
    if (rate1 != nullptr) rate1->update();
    if (rate2 != nullptr) rate2->update();
    scope.update();
}

void SynthContent::paint (Graphics& g)
{
    g.fillAll (Theme::bg);
    snui::drawHeader (g, getWidth(), "SYNTH", "");
}

//==============================================================================
SpacenerdSynthEditor::SpacenerdSynthEditor (SpacenerdSynthProcessor& p)
    : AudioProcessorEditor (p), content (p)
{
    addAndMakeVisible (content);
    setLookAndFeel (&lnf);
    content.setBounds (0, 0, baseW, baseH);
    snui::setupEditorSize (*this, p.apvts.state, baseW, baseH);
    startTimerHz (30);
}

SpacenerdSynthEditor::~SpacenerdSynthEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SpacenerdSynthEditor::resized()
{
    content.setTransform (AffineTransform::scale ((float) getWidth() / (float) baseW));
    snui::rememberEditorScale (static_cast<SpacenerdSynthProcessor&> (processor).apvts.state, getWidth(), baseW);
}
