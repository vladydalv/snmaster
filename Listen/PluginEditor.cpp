#include "PluginEditor.h"

using namespace juce;
using namespace snui;

//==============================================================================
InstPicker::InstPicker (SpacenerdListenProcessor& p)
    : proc (p),
      att (*p.apvts.getParameter (ListenIDs::instrument), [this] (float v) { selected = roundToInt (v); repaint(); })
{
    att.sendInitialUpdate();
    setTooltip ("What's on this track. AUTO guesses from the sound (the dotted outline); click an icon if it guessed wrong.");
}

Rectangle<float> InstPicker::cell (int i) const
{
    const float w = (float) getWidth() / 9.0f;
    return { w * (float) i, 0.0f, w, (float) getHeight() };
}

int InstPicker::cellAt (Point<float> p) const
{
    return jlimit (0, 8, (int) (p.x / ((float) getWidth() / 9.0f)));
}

void InstPicker::paint (Graphics& g)
{
    for (int i = 0; i < 9; ++i)
    {
        auto c = cell (i).reduced (3.0f);
        const bool sel = i == selected, hov = i == hover, guessed = selected == 0 && i - 1 == guess;
        g.setColour (sel ? Theme::accent.withAlpha (0.22f) : hov ? Theme::track.withAlpha (0.8f) : Theme::card);
        g.fillRoundedRectangle (c, 8.0f);
        if (sel)
        {
            g.setColour (Theme::accent);
            g.drawRoundedRectangle (c, 8.0f, 1.5f);
        }
        else if (guessed)
        {
            Path outline; outline.addRoundedRectangle (c.reduced (0.75f), 8.0f);
            Path dashed;
            const float dashes[] { 4.0f, 3.0f };
            PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (Theme::accent2);
            g.fillPath (dashed);
        }
        else
        {
            g.setColour (Theme::border);
            g.drawRoundedRectangle (c, 8.0f, 1.0f);
        }

        const auto col = sel ? Theme::text : guessed ? Theme::accent2 : Theme::muted;
        auto iconR = c.withTrimmedBottom (18.0f).reduced (10.0f, 6.0f);
        if (i == 0)
        {
            g.setColour (col);
            g.setFont (FontOptions (15.0f, Font::bold));
            g.drawText ("AUTO", iconR, Justification::centred);
        }
        else
            drawInstrument (g, i - 1, iconR, col, 1.5f);
        g.setColour (col);
        g.setFont (FontOptions (10.0f, Font::bold));
        g.drawText (i == 0 ? String ("GUESS") : String (mix::instName (i - 1)).toUpperCase(), c.removeFromBottom (18.0f).translated (0.0f, -3.0f),
                    Justification::centred);
    }
}

void InstPicker::mouseDown (const MouseEvent& e)
{
    att.setValueAsCompleteGesture ((float) cellAt (e.position));
}

void InstPicker::mouseMove (const MouseEvent& e)
{
    const int h = cellAt (e.position);
    if (h != hover) { hover = h; repaint(); }
}

void InstPicker::mouseExit (const MouseEvent&) { hover = -1; repaint(); }

//==============================================================================
ListenContent::ListenContent (SpacenerdListenProcessor& p) : proc (p), picker (p)
{
    nameLabel.setEditable (false, true, false);
    nameLabel.setJustificationType (Justification::centred);
    nameLabel.setFont (FontOptions (17.0f, Font::bold));
    nameLabel.setColour (Label::textColourId, Theme::text);
    nameLabel.setColour (Label::outlineWhenEditingColourId, Theme::accent);
    nameLabel.setTooltip ("Track name shown in SN Master. Double-click to rename.");
    nameLabel.onTextChange = [this] { proc.setUserName (nameLabel.getText()); };

    resetButton.setTooltip ("Forget what was heard and listen again (RESET in SN Master resets all tracks)");
    resetButton.onClick = [this] { proc.resetStats(); };

    adviceView.setViewedComponent (&advice, false);
    adviceView.setScrollBarsShown (true, false);
    adviceView.setScrollBarThickness (6);

    for (auto* c : std::initializer_list<Component*> { &picker, &nameLabel, &resetButton, &adviceView })
        addAndMakeVisible (c);
}

void ListenContent::tick()
{
    level = std::max (sn::gainToDb (proc.peakView.exchange (0.0f)), level - 1.5f);
    const auto& f = proc.features;
    picker.setGuess (f.valid ? f.autoInst : -1);
    if (! nameLabel.isBeingEdited() && nameLabel.getText() != proc.displayName())
        nameLabel.setText (proc.displayName(), dontSendNotification);
    advice.set (proc.verdict);
    const int w = adviceView.getWidth() - 10;
    advice.setSize (w, advice.preferredHeight (w));
    repaint (statusArea);
    repaint (footerArea);
    repaint (0, 0, getWidth(), 56);
}

void ListenContent::paint (Graphics& g)
{
    g.fillAll (Theme::bg);
    g.setGradientFill (ColourGradient (Theme::accent.withAlpha (0.14f), 120.0f, 28.0f, Colours::transparentBlack, 520.0f, 28.0f, true));
    g.fillRect (0, 0, getWidth(), 56);
    g.setColour (Theme::text);
    g.setFont (FontOptions (19.0f, Font::bold));
    g.drawText ("SPACENERD", 20, 14, 130, 28, Justification::centredLeft);
    g.setColour (Theme::accent);
    g.setFont (FontOptions (19.0f));
    g.drawText ("LISTEN", 136, 14, 120, 28, Justification::centredLeft);

    // Зв'язок із Master
    const bool conn = proc.masterConnected;
    g.setColour (conn ? Theme::good : Theme::muted);
    g.fillEllipse ((float) getWidth() - 236.0f, 24.0f, 8.0f, 8.0f);
    g.setFont (FontOptions (11.5f));
    g.drawText (conn ? "connected to SN Master: full mix advice" : "no SN Master on the output: track-only checks",
                getWidth() - 224, 14, 208, 28, Justification::centredLeft);

    // Ліва картка: інструмент і статус
    auto left = statusArea.toFloat();
    drawCard (g, left, "TRACK");
    const auto& v = proc.verdict;
    const auto col = statusColour (v.status);
    const float d = 96.0f;
    const auto ring = Rectangle<float> (left.getCentreX() - d * 0.5f, left.getY() + 50.0f, d, d);
    g.setColour (col.withAlpha (0.12f));
    g.fillEllipse (ring);
    g.setColour (col);
    g.drawEllipse (ring.reduced (2.0f), 3.0f);
    drawInstrument (g, proc.features.inst, ring.reduced (24.0f), Theme::text, 2.0f);

    g.setFont (FontOptions (12.0f, Font::bold));
    g.drawText (statusWord (v.status).toUpperCase(), Rectangle<float> (left.getX(), left.getY() + 184.0f, left.getWidth(), 16.0f), Justification::centred);
    g.setColour (Theme::muted);
    g.setFont (FontOptions (11.5f));
    const auto& f = proc.features;
    const String guessText = f.manual ? String ("set by you: ") + mix::instName (f.inst)
                           : f.valid ? String ("auto: ") + mix::instName (f.autoInst) + (f.autoConf < 0.55f ? " (not sure)" : "")
                                     : String ("auto: listening...");
    g.drawText (guessText, Rectangle<float> (left.getX(), left.getY() + 200.0f, left.getWidth(), 16.0f), Justification::centred);

    // Підсумок цифрами (дрібно)
    g.setColour (Theme::border);
    g.fillRect (left.getX() + 12.0f, left.getY() + 226.0f, left.getWidth() - 24.0f, 1.0f);
    auto foot = Rectangle<float> (left.getX() + 16.0f, left.getY() + 232.0f, left.getWidth() - 32.0f, 70.0f);
    g.setFont (FontOptions (11.0f));
    auto line = [&] (const String& k, const String& val)
    {
        auto row = foot.removeFromTop (16.0f);
        g.setColour (Theme::muted); g.drawText (k, row, Justification::centredLeft);
        g.setColour (Theme::text);  g.drawText (val, row, Justification::centredRight);
    };
    line ("Loudness", f.lufs > -90.0f ? String (f.lufs, 1) + " LUFS" : String ("--"));
    line ("Peak", f.peakDb > -90.0f ? String (f.peakDb, 1) + " dBFS" : String ("--"));
    line ("Tuning", f.tuneFrames >= 40.0f ? (f.tuneCents >= 0 ? "+" : "") + String (roundToInt (f.tuneCents)) + " cents" : String ("--"));
    line ("Heard", String ((int) f.seconds / 60) + ":" + String ((int) f.seconds % 60).paddedLeft ('0', 2));

    // Права картка: поради
    drawCard (g, adviceView.getBounds().toFloat().withTrimmedTop (-44.0f).expanded (14.0f, 0.0f).withTrimmedBottom (-14.0f), "WHAT TO DO");
}

void ListenContent::resized()
{
    picker.setBounds (16, 64, getWidth() - 32, 78);
    const int top = 154;
    statusArea = { 16, top, 224, getHeight() - top - 16 };
    footerArea = statusArea;
    nameLabel.setBounds (statusArea.getX() + 10, statusArea.getY() + 152, statusArea.getWidth() - 20, 26);
    resetButton.setBounds (statusArea.getRight() - 72, statusArea.getY() + 8, 60, 22);
    adviceView.setBounds (statusArea.getRight() + 26, top + 44, getWidth() - statusArea.getRight() - 26 - 30, getHeight() - top - 44 - 30);
}

//==============================================================================
SpacenerdListenEditor::SpacenerdListenEditor (SpacenerdListenProcessor& p)
    : AudioProcessorEditor (p), content (p)
{
    addAndMakeVisible (content);
    setLookAndFeel (&lnf);
    content.setBounds (0, 0, baseW, baseH);
    setResizable (true, true);
    setResizeLimits (baseW * 3 / 4, baseH * 3 / 4, baseW * 2, baseH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) baseW / (double) baseH);
    setSize (baseW, baseH);
    content.tick();
    startTimerHz (15);
}

SpacenerdListenEditor::~SpacenerdListenEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SpacenerdListenEditor::resized()
{
    content.setTransform (AffineTransform::scale ((float) getWidth() / (float) baseW));
}
