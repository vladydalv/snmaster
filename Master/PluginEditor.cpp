#include "PluginEditor.h"

using namespace juce;
using namespace snui;

//==============================================================================
MeterPanel::MeterPanel (SpacenerdMasterProcessor& p)
    : proc (p),
      inKnob  (p.apvts, ParamIDs::inGain,  "Input",  true, Theme::accent2),
      outKnob (p.apvts, ParamIDs::outGain, "Output", false, Theme::accent2)
{
    resetButton.onClick = [this] { proc.resetMeters(); };
    resetButton.setTooltip ("Reset integrated loudness and true peak");
    addAndMakeVisible (resetButton);
    addAndMakeVisible (inKnob);
    addAndMakeVisible (outKnob);
}

void MeterPanel::update()
{
    for (size_t ch = 0; ch < 2; ++ch)
    {
        in[ch].feed  (sn::gainToDb (proc.inPeak[ch].take()),  false);
        out[ch].feed (sn::gainToDb (proc.outPeak[ch].take()), false);
    }
    comp.feed (proc.compGr.take(), true);
    lim.feed  (proc.limGr.take(),  true);

    lufsM = proc.loudness.momentary.load();
    lufsS = proc.loudness.shortTerm.load();
    lufsI = proc.loudness.integrated.load();
    tpMax = sn::gainToDb (proc.truePeakMax.load());
    repaint (barsArea.getUnion (lufsArea).expanded (0, 18));
}

void MeterPanel::paint (Graphics& g)
{
    drawCard (g, getLocalBounds().toFloat(), "METERS");

    auto bars = barsArea.toFloat();
    constexpr float labelsH = 14.0f;
    auto meterR = bars.withTrimmedBottom (labelsH);
    const float groupW = meterR.getWidth() / 3.0f;
    constexpr float barW = 10.0f, gap = 4.0f;

    auto group = [&] (int idx, const MeterBar& a, const MeterBar& b, bool isGr, const String& name)
    {
        const float cx = meterR.getX() + groupW * ((float) idx + 0.5f);
        a.draw (g, { cx - barW - gap * 0.5f, meterR.getY(), barW, meterR.getHeight() }, isGr);
        b.draw (g, { cx + gap * 0.5f,        meterR.getY(), barW, meterR.getHeight() }, isGr);
        g.setColour (Theme::muted);
        g.setFont (FontOptions (9.5f, Font::bold));
        g.drawText (name, Rectangle<float> (cx - groupW * 0.5f, meterR.getBottom() + 2.0f, groupW, labelsH - 2.0f),
                    Justification::centred);
    };
    group (0, in[0], in[1], false, "IN");
    group (1, comp, lim, true, "GR C / L");
    group (2, out[0], out[1], false, "OUT");

    // Числа над смугами: GR лімітера і true peak виходу
    g.setFont (FontOptions (9.5f, Font::bold));
    g.setColour (Theme::muted);
    g.drawText (lim.hold > 0.05f ? "-" + String (lim.hold, 1) : String ("0.0"),
                Rectangle<float> (meterR.getX() + groupW, meterR.getY() - 15.0f, groupW, 12.0f), Justification::centred);
    g.setColour (tpMax > -0.05f ? Theme::hot : Theme::muted);
    g.drawText (tpMax > -99.0f ? "TP " + String (tpMax, 1) : String ("TP --"),
                Rectangle<float> (meterR.getRight() - groupW - 6.0f, meterR.getY() - 15.0f, groupW + 12.0f, 12.0f),
                Justification::centred);

    // LUFS
    auto la = lufsArea.toFloat();
    auto row = [&] (const String& tag, float v, bool big)
    {
        auto line = la.removeFromTop (big ? 26.0f : 18.0f);
        g.setColour (Theme::muted);
        g.setFont (FontOptions (10.0f, Font::bold));
        g.drawText (tag, line.removeFromLeft (70.0f), Justification::centredLeft);
        g.setColour (big ? Theme::accent2 : Theme::text);
        g.setFont (FontOptions (big ? 20.0f : 13.0f, Font::bold));
        g.drawText (v > -99.0f ? String (v, 1) : String ("--"), line, Justification::centredRight);
    };
    row ("INTEGRATED", lufsI, true);
    row ("SHORT-TERM", lufsS, false);
    row ("MOMENTARY", lufsM, false);
}

void MeterPanel::resized()
{
    auto r = getLocalBounds().withTrimmedTop (44).reduced (14, 8);
    auto knobs = r.removeFromBottom (100);
    const int kw = knobs.getWidth() / 2;
    inKnob.setBounds (knobs.removeFromLeft (kw));
    outKnob.setBounds (knobs);

    r.removeFromBottom (6);
    auto lufs = r.removeFromBottom (86);
    resetButton.setBounds (lufs.removeFromBottom (20).removeFromRight (64));
    lufsArea = lufs.withTrimmedBottom (4);

    r.removeFromTop (18);
    barsArea = r;
}

//==============================================================================
MainContent::MainContent (SpacenerdMasterProcessor& p)
    : meters (p),
      analyzer (p),
      mixPanel (p),
      proc (p),
      presetBox (p),
      matchButton (p.apvts, ParamIDs::gainMatch, "GAIN MATCH", Theme::gr),
      abButton (p.apvts, ParamIDs::refAB, "A/B REF", Theme::accent2),
      listenSel (p.apvts, ParamIDs::monitor),
      eq    (p.apvts, "EQ",         ParamIDs::eqOn,    4),
      comp  (p.apvts, "COMPRESSOR", ParamIDs::compOn,  4),
      sat   (p.apvts, "SATURATION", ParamIDs::satOn,   2),
      width (p.apvts, "STEREO",     ParamIDs::widthOn, 2),
      lim   (p.apvts, "LIMITER",    ParamIDs::limOn,   2)
{
    auto& s = p.apvts;

    // Частоти над своїми підсиленнями
    eq.knob (s, ParamIDs::lowFreq,  "Low Freq").help ("corner of the low shelf");
    eq.knob (s, ParamIDs::midFreq,  "Mid Freq").help ("centre of the mid bell");
    eq.knob (s, ParamIDs::highFreq, "High Freq").help ("corner of the high shelf");
    eq.knob (s, ParamIDs::hpfFreq,  "Low Cut").help ("removes rumble below this frequency (Off = full range)");
    eq.knob (s, ParamIDs::lowGain,  "Low Gain",  true);
    eq.knob (s, ParamIDs::midGain,  "Mid Gain",  true);
    eq.knob (s, ParamIDs::highGain, "High Gain", true);
    eq.knob (s, ParamIDs::midQ,     "Mid Q").help ("width of the mid bell: low = broad, high = narrow");

    comp.knob (s, ParamIDs::threshold, "Threshold").help ("level where compression starts");
    comp.knob (s, ParamIDs::ratio,     "Ratio").help ("how strongly levels above the threshold are reduced; 2:1 is gentle glue");
    comp.knob (s, ParamIDs::attack,    "Attack").help ("longer = more drum punch passes through");
    comp.knob (s, ParamIDs::release,   "Release").help ("how fast it lets go; too short pumps, too long squashes");
    comp.knob (s, ParamIDs::knee,      "Knee").help ("softness of the compression onset");
    comp.knob (s, ParamIDs::scHpf,     "SC Filter").help ("bass below this does not trigger compression (less pumping from kick/bass)");
    comp.knob (s, ParamIDs::makeup,    "Makeup").help ("level added after compression");
    comp.knob (s, ParamIDs::compMix,   "Mix").help ("parallel compression: blend of dry and compressed");
    comp.add (std::make_unique<Component>(), 3, 26);
    comp.add (std::make_unique<PillToggle> (s, ParamIDs::compAuto, "AUTO REL"), 1, 24);

    sat.add (std::make_unique<Segmented> (s, ParamIDs::satType), 2, 34);
    sat.knob (s, ParamIDs::drive,  "Drive").help ("amount of harmonic colour; level stays the same");
    sat.knob (s, ParamIDs::satMix, "Mix");

    width.knob (s, ParamIDs::width,    "Width", true);
    width.knob (s, ParamIDs::monoBass, "Mono Bass").help ("bass below this frequency is made mono (tight, vinyl/club safe)");

    lim.knob (s, ParamIDs::limGain, "Drive").help ("pushes the mix into the limiter = louder. ASSIST sets it for the streaming target");
    lim.knob (s, ParamIDs::ceiling, "Ceiling").help ("maximum true peak; keep -1 dB for streaming");
    lim.knob (s, ParamIDs::clip,    "Clip").help ("soft clipper before the limiter: shaves short drum peaks so the limiter works less (more punch at the same loudness)");
    lim.knob (s, ParamIDs::limRel,  "Release").help ("limiter recovery time");

    matchButton.setTooltip ("Gain Match: output level follows input loudness, so Bypass compares at equal loudness. Turn off before bouncing.");
    abButton.setTooltip ("Hear your reference instead of your master, at the same loudness, from the same song position. Turn off before bouncing.");
    listenSel.setTooltip ("Check how the master translates: phone speaker, earbuds, car, mono. STREAM: as heard after Spotify-style loudness normalisation "
                          "for the 'Release for' target (turned down if louder; up only to -1 dBTP) with a lossy-like 16 kHz band limit. "
                          "Monitoring only: set back to Studio before bouncing!");
    refButton.setTooltip ("Load a record you want to sound like (WAV/AIFF/MP3/FLAC). It becomes the analyzer target and the A/B reference. Right-click: remove.");
    refButton.onClick = [this]
    {
        if (ModifierKeys::currentModifiers.isPopupMenu()) { proc.clearReference(); return; }
        chooser = std::make_unique<FileChooser> ("Reference track", File(), "*.wav;*.aif;*.aiff;*.mp3;*.m4a;*.flac");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [this] (const FileChooser& fc) { if (fc.getResult().existsAsFile()) { proc.loadReference (fc.getResult()); updateRefButton(); } });
    };
    proc.onReferenceChanged = [safe = Component::SafePointer<MainContent> (this)] { if (safe != nullptr) safe->updateRefButton(); };
    updateRefButton();

    for (auto* c : std::initializer_list<Component*> { &presetBox, &matchButton, &abButton, &refButton, &listenSel, &analyzer, &mixPanel, &eq, &comp, &sat, &width, &lim, &meters })
        addAndMakeVisible (c);
}

void MainContent::updateRefButton()
{
    if (proc.isLoadingReference()) refButton.setButtonText ("LOADING...");
    else if (auto* r = proc.reference()) refButton.setButtonText ("REF: " + r->name);
    else refButton.setButtonText (proc.refError.isNotEmpty() ? proc.refError : String ("LOAD REFERENCE..."));
    abButton.setEnabled (proc.reference() != nullptr);
}

void MainContent::tick()
{
    meters.update();
    analyzer.tick();
    mixPanel.tick();
    presetBox.sync();
    const float m = proc.matchDb.load();
    if (std::abs (m - shownMatch) > 0.05f) { shownMatch = m; repaint (0, 0, getWidth(), 56); }
    repaint (listenSel.getBounds().expanded (6, 14));
    if (proc.isLoadingReference() != (refButton.getButtonText() == "LOADING...")) updateRefButton();
}

void MainContent::paint (Graphics& g)
{
    g.fillAll (Theme::bg);
    drawHeader (g, getWidth(), "MASTER", {});
    // Попередження: режими прослуховування потрапляють і в експорт
    const bool listening = (int) proc.apvts.getRawParameterValue (ParamIDs::monitor)->load() != 0
                        || proc.apvts.getRawParameterValue (ParamIDs::refAB)->load() > 0.5f;
    if (listening)
    {
        g.setColour (Theme::hot.withAlpha (0.18f));
        g.fillRoundedRectangle (listenSel.getBounds().toFloat().expanded (4.0f, 3.0f), 8.0f);
    }
    if ((int) proc.apvts.getRawParameterValue (ParamIDs::monitor)->load() == 5)
    {
        const float sg = proc.streamGainDb.load();
        g.setColour (Theme::accent2);
        g.setFont (FontOptions (10.5f, Font::bold));
        g.drawText ("streaming plays this " + (std::abs (sg) < 0.05f ? String ("as is") : (sg > 0 ? "+" : "") + String (sg, 1) + " dB"),
                    listenSel.getRight() - 220, listenSel.getBottom() + 2, 220, 12, Justification::centredRight);
    }

    if (matchButton.getToggleState())
    {
        g.setColour (Theme::gr);
        g.setFont (FontOptions (12.0f, Font::bold));
        g.drawText (String (shownMatch, 1) + " dB", matchButton.getRight() + 8, 14, 70, 28, Justification::centredLeft);
    }
}

void MainContent::resized()
{
    presetBox.setBounds (300, 14, 170, 28);
    matchButton.setBounds (480, 16, 92, 24);
    refButton.setBounds (580, 15, 144, 26);
    abButton.setBounds (732, 16, 72, 24);
    listenSel.setBounds (814, 14, getWidth() - 830, 28);

    auto r = getLocalBounds().withTrimmedTop (56).reduced (16, 0).withTrimmedBottom (16);
    constexpr int gap = 10;
    analyzer.setBounds (r.removeFromTop (SpacenerdMasterEditor::analyzerH));
    r.removeFromTop (gap);
    mixPanel.setBounds (r.removeFromTop (SpacenerdMasterEditor::mixH));
    r.removeFromTop (gap);

    eq.setBounds   (r.removeFromLeft (280)); r.removeFromLeft (gap);
    comp.setBounds (r.removeFromLeft (280)); r.removeFromLeft (gap);

    auto col = r.removeFromLeft (172); r.removeFromLeft (gap);
    sat.setBounds (col.removeFromTop (224));
    col.removeFromTop (gap);
    width.setBounds (col);

    lim.setBounds (r.removeFromLeft (152)); r.removeFromLeft (gap);
    meters.setBounds (r);
}

//==============================================================================
SpacenerdMasterEditor::SpacenerdMasterEditor (SpacenerdMasterProcessor& p)
    : AudioProcessorEditor (p), content (p)
{
    addAndMakeVisible (content);
    setLookAndFeel (&lnf);          // після додавання вмісту, щоб оновились усі нащадки
    content.setBounds (0, 0, baseW, baseH);

    snui::setupEditorSize (*this, p.apvts.state, baseW, baseH);

    startTimerHz (30);
}

SpacenerdMasterEditor::~SpacenerdMasterEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SpacenerdMasterEditor::paint (Graphics& g) { g.fillAll (Theme::bg); }

void SpacenerdMasterEditor::resized()
{
    content.setTransform (AffineTransform::scale ((float) getWidth() / (float) baseW));
    snui::rememberEditorScale (static_cast<SpacenerdMasterProcessor&> (processor).apvts.state, getWidth(), baseW);
}
