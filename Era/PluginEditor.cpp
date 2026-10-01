#include "PluginEditor.h"

using namespace juce;
using namespace snui;
using namespace EraIDs;

//==============================================================================
EraPad::EraPad (SpacenerdEraProcessor& p)
    : proc (p),
      yearAtt (*p.apvts.getParameter (year),      [this] (float v) { yearVal = v; repaint(); }, nullptr),
      intAtt  (*p.apvts.getParameter (intensity), [this] (float v) { intVal = v;  repaint(); }, nullptr),
      lowAtt  (*p.apvts.getParameter (yearLow),   [this] (float v) { lowVal = v;  repaint(); }, nullptr),
      highAtt (*p.apvts.getParameter (yearHigh),  [this] (float v) { highVal = v; repaint(); }, nullptr),
      lowAmtAtt  (*p.apvts.getParameter (lowAmt),  [this] (float v) { lowAmtVal = v;  repaint(); }, nullptr),
      highAmtAtt (*p.apvts.getParameter (highAmt), [this] (float v) { highAmtVal = v; repaint(); }, nullptr)
{
    lowAmtAtt.sendInitialUpdate();
    highAmtAtt.sendInitialUpdate();
    yearAtt.sendInitialUpdate();
    intAtt.sendInitialUpdate();
    lowAtt.sendInitialUpdate();
    highAtt.sendInitialUpdate();
    for (auto& f : flicker) f = rng.nextFloat();
    setMouseCursor (MouseCursor::CrosshairCursor);
    setTooltip ("Drag: left-right = year, up-down = intensity. SPLIT: drag LOW (bass) and HIGH (top end) handles: left-right = decade, up-down = how strong. Double-click: reset.");
}

Colour EraPad::eraColour (float y)
{
    struct Stop { float year; uint32 argb; };
    static constexpr Stop stops[] { { 1960, 0xfff5a524 }, { 1972, 0xfff97316 }, { 1984, 0xffec4899 },
                                    { 1996, 0xff8b7cff }, { 2008, 0xff5ad1e6 }, { 2025, 0xff4ade80 } };
    for (size_t i = 0; i + 1 < std::size (stops); ++i)
        if (y <= stops[i + 1].year)
        {
            const float t = jlimit (0.0f, 1.0f, (y - stops[i].year) / (stops[i + 1].year - stops[i].year));
            return Colour (stops[i].argb).interpolatedWith (Colour (stops[i + 1].argb), t);
        }
    return Colour (stops[std::size (stops) - 1].argb);
}

Rectangle<float> EraPad::grid() const
{
    return getLocalBounds().toFloat().withTrimmedTop (44.0f).withTrimmedBottom (30.0f).withTrimmedLeft (40.0f).withTrimmedRight (18.0f);
}

void EraPad::update()
{
    for (size_t b = 0; b < bands.size(); ++b)
    {
        const float db = sn::gainToDb (proc.bandLevel[b].load());
        const float norm = jlimit (0.0f, 1.0f, (db + 60.0f) / 50.0f);
        bands[b] = norm > bands[b] ? norm : bands[b] * 0.85f + norm * 0.15f;
    }
    // Мерехтіння: частина пікселів щокадру отримує нову випадкову яскравість
    for (int i = 0; i < 90; ++i)
        flicker[(size_t) rng.nextInt ((int) flicker.size())] = rng.nextFloat();
    repaint();
}

void EraPad::paint (Graphics& g)
{
    drawCard (g, getLocalBounds().toFloat(), "ERA");

    const int genreIdx = (int) proc.apvts.getRawParameterValue (genre)->load();
    const auto s = era::applyIntensity (era::forYear (yearVal, genreIdx), intVal * 0.01f);

    // Опис поточної точки
    g.setColour (eraColour (yearVal));
    g.setFont (FontOptions (20.0f, Font::bold));
    g.drawText (String (roundToInt (yearVal)), 80, 8, 70, 24, Justification::centredLeft);
    g.setColour (Theme::muted);
    g.setFont (FontOptions (12.0f));
    g.drawText (era::describe (s), 150, 8, getWidth() - 170, 24, Justification::centredRight);

    const auto gr = grid();
    const float cw = gr.getWidth() / (float) cols, rh = gr.getHeight() / (float) rows;

    // Класичні роки жанру
    for (auto [y0, y1] : era::classicZones (genreIdx))
    {
        const float x0 = gr.getX() + (y0 - 1960.0f) * cw, x1 = gr.getX() + (y1 - 1960.0f + 1.0f) * cw;
        auto zone = Rectangle<float> (x0, gr.getY() - 4.0f, x1 - x0, gr.getHeight() + 8.0f);
        g.setColour (Theme::accent2.withAlpha (0.07f));
        g.fillRoundedRectangle (zone, 6.0f);
        g.setColour (Theme::accent2.withAlpha (0.35f));
        g.drawRoundedRectangle (zone, 6.0f, 1.0f);
        g.setFont (FontOptions (9.0f, Font::bold));
        g.drawText ("CLASSIC", zone.withHeight (12.0f).translated (0.0f, -14.0f), Justification::centred);
    }

    // Пікселі
    const float intRow = intVal * 0.01f * (float) (rows - 1);
    for (int c = 0; c < cols; ++c)
    {
        const float yr = 1960.0f + (float) c;
        const auto col = eraColour (yr);
        auto proxTo = [yr] (float target) { const float dx = yr - target; return std::exp (-dx * dx / (2.0f * 7.0f * 7.0f)); };
        const bool sp = splitOn();
        for (int r = 0; r < rows; ++r)
        {
            // У режимі Split: нижні ряди «світяться» біля року LOW (бас), верхні — біля HIGH (верх)
            const float prox = ! sp ? proxTo (yearVal)
                             : (r < rows / 3 ? proxTo (lowVal) : (r >= rows - rows / 3 ? proxTo (highVal) : proxTo (yearVal)));
            const float rowFrac = (float) r / (float) (rows - 1);
            const float band = bands[(size_t) jlimit (0, (int) bands.size() - 1, roundToInt (rowFrac * (float) (bands.size() - 1)))];
            const bool under = (float) r <= intRow;
            const float f = flicker[(size_t) (c * rows + r)];
            float a = 0.05f + (under ? 0.10f + 0.25f * prox : 0.02f)
                    + band * (0.25f + 0.55f * prox) * (0.4f + 0.6f * f) * (under ? 1.0f : 0.45f);
            a = jlimit (0.03f, 1.0f, a);
            const auto px = Rectangle<float> (gr.getX() + (float) c * cw, gr.getBottom() - (float) (r + 1) * rh, cw, rh).reduced (1.0f);
            g.setColour (col.withAlpha (a));
            g.fillRoundedRectangle (px, 1.5f);
        }
    }

    // Ручки LOW (бас) і HIGH (верх) у режимі Split
    if (splitOn())
    {
        auto handle = [&] (bool low)
        {
            const float yr = low ? lowVal : highVal;
            const auto c = eraColour (yr);
            const auto h = handleRect (low);
            const float x = h.getCentreX();
            // Пунктир від ручки до осі: висота = сила смуги
            const float y0 = h.getCentreY();
            const float y1 = gr.getBottom();
            Path line; line.startNewSubPath (x, y0); line.lineTo (x, y1);
            Path dashed; const float d[] { 4.0f, 3.0f };
            PathStrokeType (1.5f).createDashedStroke (dashed, line, d, 2);
            g.setColour (c.withAlpha (0.8f));
            g.fillPath (dashed);
            const bool active = drag == (low ? Drag::low : Drag::high);
            g.setColour (Theme::bg.withAlpha (0.85f));
            g.fillRoundedRectangle (h, 9.0f);
            g.setColour (c);
            g.drawRoundedRectangle (h, 9.0f, active ? 2.2f : 1.4f);
            g.setFont (FontOptions (10.0f, Font::bold));
            g.drawText ((low ? "LOW " : "HIGH ") + String (roundToInt (yr)) + " · " + String (roundToInt (low ? lowAmtVal : highAmtVal)) + "%", h, Justification::centred);
        };
        handle (true);
        handle (false);

        g.setColour (Theme::muted);
        g.setFont (FontOptions (9.0f, Font::bold));
        g.drawText ("TOP END", Rectangle<float> (gr.getRight() - 70.0f, gr.getY() - 14.0f, 70.0f, 12.0f), Justification::centredRight);
        g.drawText ("BASS", Rectangle<float> (gr.getRight() - 70.0f, gr.getBottom() + 2.0f, 70.0f, 12.0f), Justification::centredRight);
    }

    // Курсор
    const float cx = gr.getX() + (yearVal - 1960.0f + 0.5f) * cw;
    const float cy = gr.getBottom() - (intRow + 0.5f) * rh;
    g.setColour (Colours::white.withAlpha (0.18f));
    g.drawVerticalLine (roundToInt (cx), gr.getY(), gr.getBottom());
    g.drawHorizontalLine (roundToInt (cy), gr.getX(), gr.getRight());
    g.setColour (eraColour (yearVal).withAlpha (0.35f));
    g.fillEllipse (Rectangle<float> (26.0f, 26.0f).withCentre ({ cx, cy }));
    g.setColour (Colours::white);
    g.drawEllipse (Rectangle<float> (14.0f, 14.0f).withCentre ({ cx, cy }), 2.0f);

    // Осі
    g.setColour (Theme::muted);
    g.setFont (FontOptions (10.0f, Font::bold));
    for (int d = 1960; d <= 2020; d += 10)
    {
        const float x = gr.getX() + (float) (d - 1960) * cw;
        g.drawText (String (d), Rectangle<float> (x - 20.0f, gr.getBottom() + 8.0f, 40.0f, 14.0f), Justification::centred);
    }
    Graphics::ScopedSaveState save (g);
    g.addTransform (AffineTransform::rotation (-MathConstants<float>::halfPi, 18.0f, gr.getCentreY()));
    g.drawText ("INTENSITY", Rectangle<float> (18.0f - 60.0f, gr.getCentreY() - 7.0f, 120.0f, 14.0f), Justification::centred);
}

bool EraPad::splitOn() const { return proc.apvts.getRawParameterValue (split)->load() > 0.5f; }

Rectangle<float> EraPad::handleRect (bool low) const
{
    const auto gr = grid();
    const float cw = gr.getWidth() / (float) cols;
    const float x = gr.getX() + ((low ? lowVal : highVal) - 1960.0f + 0.5f) * cw;
    const float y = gr.getBottom() - (low ? lowAmtVal : highAmtVal) * 0.01f * gr.getHeight();
    return { x - 52.0f, y - 9.0f, 104.0f, 18.0f };
}

void EraPad::setFromPoint (Point<float> p)
{
    const auto gr = grid();
    const float y = jlimit (1960.0f, 2025.0f, 1960.0f + (p.x - gr.getX()) / gr.getWidth() * (float) cols - 0.5f);
    const float i = jlimit (0.0f, 100.0f, (gr.getBottom() - p.y) / gr.getHeight() * 100.0f);
    if (drag == Drag::low)  { lowAtt.setValueAsPartOfGesture (y);  lowAmtAtt.setValueAsPartOfGesture (i);  return; }
    if (drag == Drag::high) { highAtt.setValueAsPartOfGesture (y); highAmtAtt.setValueAsPartOfGesture (i); return; }
    yearAtt.setValueAsPartOfGesture (y);
    intAtt.setValueAsPartOfGesture (i);
}

void EraPad::mouseDown (const MouseEvent& e)
{
    drag = Drag::main;
    if (splitOn())
    {
        // Ручки мають пріоритет над основним курсором
        if (handleRect (true).expanded (6.0f).contains (e.position))       drag = Drag::low;
        else if (handleRect (false).expanded (6.0f).contains (e.position)) drag = Drag::high;
    }
    if (drag == Drag::low)       { lowAtt.beginGesture();  lowAmtAtt.beginGesture(); }
    else if (drag == Drag::high) { highAtt.beginGesture(); highAmtAtt.beginGesture(); }
    else                         { yearAtt.beginGesture(); intAtt.beginGesture(); }
    setFromPoint (e.position);
}

void EraPad::mouseDrag (const MouseEvent& e) { if (drag != Drag::none) setFromPoint (e.position); }

void EraPad::mouseUp (const MouseEvent&)
{
    if (drag == Drag::low)        { lowAtt.endGesture();  lowAmtAtt.endGesture(); }
    else if (drag == Drag::high)  { highAtt.endGesture(); highAmtAtt.endGesture(); }
    else if (drag == Drag::main)  { yearAtt.endGesture(); intAtt.endGesture(); }
    drag = Drag::none;
    repaint();
}

void EraPad::mouseDoubleClick (const MouseEvent& e)
{
    if (splitOn() && handleRect (true).expanded (6.0f).contains (e.position))
    { lowAtt.setValueAsCompleteGesture (1972.0f);  lowAmtAtt.setValueAsCompleteGesture (70.0f);  return; }
    if (splitOn() && handleRect (false).expanded (6.0f).contains (e.position))
    { highAtt.setValueAsCompleteGesture (2015.0f); highAmtAtt.setValueAsCompleteGesture (70.0f); return; }
    yearAtt.setValueAsCompleteGesture (1975.0f);
    intAtt.setValueAsCompleteGesture (60.0f);
}

//==============================================================================
EraContent::EraContent (SpacenerdEraProcessor& p)
    : proc (p),
      presetBox (p),
      matchButton (p.apvts, gainMatch, "GAIN MATCH", Theme::gr),
      pad (p),
      genreSel (p.apvts, genre),
      yearKnob (p.apvts, year, "Year"),
      intKnob  (p.apvts, intensity, "Intensity"),
      mixKnob  (p.apvts, mix, "Mix", false, Theme::accent2),
      outKnob  (p.apvts, outGain, "Output", true, Theme::accent2),
      lowKnob  (p.apvts, yearLow, "Low Year"),
      highKnob (p.apvts, yearHigh, "High Year"),
      splitButton (p.apvts, split, "SPLIT")
{
    splitButton.setTooltip ("Split: bass from one decade, top end from another");
    refButton.setTooltip ("Analyse a WAV/AIFF/MP3 of a record and move the cursor to its decade (for the chosen genre)");
    refButton.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Reference record", File(), "*.wav;*.aif;*.aiff;*.mp3;*.m4a;*.flac");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [this] (const FileChooser& fc) { if (fc.getResult().existsAsFile()) loadReference (fc.getResult()); });
    };
    matchButton.setTooltip ("Gain Match: compare with Bypass at equal loudness. Turn off before bouncing.");
    for (auto* c : std::initializer_list<Component*> { &presetBox, &matchButton, &pad, &genreSel, &yearKnob, &intKnob, &mixKnob, &outKnob,
                                                       &lowKnob, &highKnob, &splitButton, &refButton })
        addAndMakeVisible (c);
}

void EraContent::loadReference (const File& file)
{
    if (analysing.exchange (true)) return;
    refText = "Analysing " + file.getFileName() + "...";
    repaint();
    const int genreIdx = (int) proc.apvts.getRawParameterValue (genre)->load();
    Component::SafePointer<EraContent> safe (this);

    proc.refPool.addJob ([safe, file, genreIdx]
    {
        AudioFormatManager fm;
        fm.registerBasicFormats();
        String text;
        an::RefResult res;
        bool ok = false;
        if (std::unique_ptr<AudioFormatReader> reader (fm.createReaderFor (file)); reader != nullptr)
        {
            // До 60 с із середини треку
            const auto len = reader->lengthInSamples;
            const int64 want = std::min<int64> (len, (int64) (60.0 * reader->sampleRate));
            const int64 start = std::max<int64> (0, (len - want) / 2);
            AudioBuffer<float> buf ((int) std::min<unsigned int> (2u, reader->numChannels), (int) want);
            reader->read (&buf, 0, (int) want, start, true, true);
            res = an::matchReference (buf, reader->sampleRate, genreIdx);
            ok = true;
            text = file.getFileNameWithoutExtension() + ":  " + String (roundToInt (res.year)) + "  (" + String (res.match) + "% match, "
                 + String (res.lufs, 1) + " LUFS)";
        }
        else
            text = "Can't read " + file.getFileName();

        MessageManager::callAsync ([safe, text, res, ok]
        {
            if (safe == nullptr) return;
            safe->refText = text;
            if (ok)
            {
                auto set = [&] (const char* id, float v)
                {
                    auto* prm = safe->proc.apvts.getParameter (id);
                    prm->beginChangeGesture();
                    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
                    prm->endChangeGesture();
                };
                set (year, res.year);
                set (intensity, 80.0f);
            }
            safe->analysing = false;
            safe->repaint();
        });
    });
}

void EraContent::tick()
{
    const float a = proc.apvts.getRawParameterValue (split)->load() > 0.5f ? 1.0f : 0.35f;
    lowKnob.setAlpha (a); highKnob.setAlpha (a);
    pad.update();
    presetBox.sync();
    for (size_t ch = 0; ch < 2; ++ch) out[ch].feed (sn::gainToDb (proc.outPeak[ch].take()), false);
    comp.feed (proc.compGr.take(), true);
    lim.feed (proc.limGr.take(), true);
    lufs = proc.outLoudness.shortTerm.load();
    push = proc.pushDb.load();
    match = proc.matchDb.load();
    repaint (meterArea.expanded (4));
    repaint (0, 0, getWidth(), 56);
}

void EraContent::paint (Graphics& g)
{
    g.fillAll (Theme::bg);
    drawHeader (g, getWidth(), "ERA", "the sound of a decade, in one gesture");

    if (matchButton.getToggleState())
    {
        g.setColour (Theme::gr);
        g.setFont (FontOptions (12.0f, Font::bold));
        g.drawText (String (match, 1) + " dB", matchButton.getRight() + 8, 14, 70, 28, Justification::centredLeft);
    }

    // Нижній ряд: картки
    auto row = getLocalBounds().withTrimmedTop (56 + 300 + 10).reduced (16, 0).withTrimmedBottom (16).toFloat();
    auto genreCard = row.removeFromLeft (380.0f);
    drawCard (g, genreCard, "GENRE");
    g.setColour (Theme::muted);
    g.setFont (FontOptions (11.0f));
    g.drawFittedText (refText, genreCard.withTrimmedTop (132.0f).reduced (16.0f, 0.0f).withHeight (30.0f).toNearestInt(),
                      Justification::centredLeft, 2);
    row.removeFromLeft (10.0f);
    drawCard (g, row.removeFromLeft (500.0f), "CONTROLS");
    row.removeFromLeft (10.0f);
    drawCard (g, row, "OUTPUT");

    // Метри
    auto m = meterArea.toFloat();
    const float barW = 10.0f, gap = 4.0f;
    auto bars = m.removeFromLeft (80.0f);
    auto meterR = bars.withTrimmedBottom (14.0f);
    out[0].draw (g, { meterR.getX() + 6.0f, meterR.getY(), barW, meterR.getHeight() }, false);
    out[1].draw (g, { meterR.getX() + 6.0f + barW + gap, meterR.getY(), barW, meterR.getHeight() }, false);
    comp.draw (g, { meterR.getX() + 46.0f, meterR.getY(), barW, meterR.getHeight() }, true);
    lim.draw  (g, { meterR.getX() + 46.0f + barW + gap, meterR.getY(), barW, meterR.getHeight() }, true);
    g.setColour (Theme::muted);
    g.setFont (FontOptions (9.0f, Font::bold));
    g.drawText ("OUT", Rectangle<float> (meterR.getX(), meterR.getBottom() + 2.0f, 40.0f, 12.0f), Justification::centred);
    g.drawText ("GR", Rectangle<float> (meterR.getX() + 40.0f, meterR.getBottom() + 2.0f, 40.0f, 12.0f), Justification::centred);

    auto line = [&] (const String& tag, const String& v, Colour c, float size)
    {
        auto l = m.removeFromTop (size + 16.0f);
        g.setColour (Theme::muted);
        g.setFont (FontOptions (10.0f, Font::bold));
        g.drawText (tag, l.removeFromTop (14.0f), Justification::centredLeft);
        g.setColour (c);
        g.setFont (FontOptions (size, Font::bold));
        g.drawText (v, l, Justification::centredLeft);
    };
    m.removeFromLeft (10.0f);
    line ("LOUDNESS (S)", lufs > -99.0f ? String (lufs, 1) + " LUFS" : String ("--"), Theme::accent2, 18.0f);
    line ("ERA PUSH", "+" + String (push, 1) + " dB", Theme::text, 14.0f);
    line ("CEILING", "-1.0 dBTP", Theme::text, 14.0f);
}

void EraContent::resized()
{
    presetBox.setBounds (300, 14, 240, 28);
    matchButton.setBounds (556, 16, 96, 24);

    auto r = getLocalBounds().withTrimmedTop (56).reduced (16, 0).withTrimmedBottom (16);
    pad.setBounds (r.removeFromTop (300));
    r.removeFromTop (10);

    auto genreCard = r.removeFromLeft (380); r.removeFromLeft (10);
    auto gc = genreCard.withTrimmedTop (48).reduced (14, 0);
    genreSel.setBounds (gc.removeFromTop (34));
    gc.removeFromTop (10);
    refButton.setBounds (gc.removeFromTop (28).withWidth (190));

    auto ctrlCard = r.removeFromLeft (500); r.removeFromLeft (10);
    splitButton.setBounds (ctrlCard.getRight() - 84, ctrlCard.getY() + 8, 70, 22);
    auto ctrl = ctrlCard.withTrimmedTop (40).reduced (8, 4);
    const int kw = ctrl.getWidth() / 6;
    for (auto* k : std::initializer_list<Component*> { &yearKnob, &intKnob, &lowKnob, &highKnob, &mixKnob, &outKnob })
        k->setBounds (ctrl.removeFromLeft (kw).withSizeKeepingCentre (kw, 104));

    meterArea = r.withTrimmedTop (44).reduced (12, 10);
}

//==============================================================================
SpacenerdEraEditor::SpacenerdEraEditor (SpacenerdEraProcessor& p)
    : AudioProcessorEditor (p), content (p)
{
    addAndMakeVisible (content);
    setLookAndFeel (&lnf);
    content.setBounds (0, 0, baseW, baseH);
    snui::setupEditorSize (*this, p.apvts.state, baseW, baseH);
    startTimerHz (30);
}

SpacenerdEraEditor::~SpacenerdEraEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SpacenerdEraEditor::paint (Graphics& g) { g.fillAll (Theme::bg); }

void SpacenerdEraEditor::resized()
{
    content.setTransform (AffineTransform::scale ((float) getWidth() / (float) baseW));
    snui::rememberEditorScale (static_cast<SpacenerdEraProcessor&> (processor).apvts.state, getWidth(), baseW);
}
