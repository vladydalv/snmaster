#include "AlbumView.h"

using namespace juce;
using namespace snui;

AlbumSong AlbumView::currentSong (SpacenerdMasterProcessor& p, const String& name)
{
    AlbumSong s;
    s.name = name;
    s.lufs = p.analysis.outLufs();
    s.tp = std::max (p.analysis.truePeakDb(), sn::gainToDb (p.truePeakMax.load()));
    s.tone = an::normalise (p.analysis.predicted());
    return s;
}

AlbumView::AlbumView (SpacenerdMasterProcessor& p) : proc (p)
{
    albumBox.setEditableText (true);
    albumBox.setTextWhenNothingSelected ("type an album name");
    albumBox.setTooltip ("Pick an album or type a new name");
    const auto names = Album::list();
    for (int i = 0; i < names.size(); ++i) albumBox.addItem (names[i], i + 1);
    albumBox.onChange = [this] { loadAlbum (albumBox.getText()); };

    songName.setTextToShowWhenEmpty ("song name", Theme::muted);
    songName.setFont (FontOptions (14.0f));
    songName.setText (proc.apvts.state.getProperty ("albumSong").toString(), false);
    songName.onTextChange = [this] { proc.apvts.state.setProperty ("albumSong", songName.getText().trim(), nullptr); };

    saveButton.setTooltip ("Store this song's loudness, density and tone in the album (play at least 20 s of it first)");
    saveButton.onClick = [this]
    {
        if (album.name.isEmpty() || songName.getText().trim().isEmpty()) return;
        album.put (currentSong (proc, songName.getText().trim()));
        album.save();
        relayout();
    };

    for (auto* c : std::initializer_list<Component*> { &albumBox, &songName, &saveButton }) addAndMakeVisible (c);
    const auto last = proc.apvts.state.getProperty ("album").toString();
    albumBox.setText (last.isNotEmpty() ? last : (names.isEmpty() ? String() : names[0]), sendNotificationSync);
    if (albumBox.getText().isEmpty()) relayout();
    startTimerHz (2);
}

void AlbumView::loadAlbum (const String& n)
{
    if (n.trim().isEmpty()) return;
    album.load (n);
    proc.apvts.state.setProperty ("album", album.name, nullptr);
    if (songName.getText().isEmpty()) songName.setText ("Song " + String ((int) album.songs.size() + 1), false);
    relayout();
}

void AlbumView::relayout()
{
    setSize (640, top + rowH * ((int) album.songs.size() + 1) + 150);
    repaint();
}

void AlbumView::resized()
{
    albumBox.setBounds (90, 14, 240, 28);
    songName.setBounds (90, 54, 240, 28);
    saveButton.setBounds (342, 54, 160, 28);
}

void AlbumView::timerCallback()
{
    saveButton.setEnabled (album.name.isNotEmpty() && proc.analysis.outMeasuredSeconds() >= 20.0 && proc.analysis.outLufs() > -70.0f);
    repaint (0, top, getWidth(), getHeight() - top);
}

void AlbumView::paint (Graphics& g)
{
    g.fillAll (Theme::card);
    g.setColour (Theme::muted);
    g.setFont (FontOptions (11.0f, Font::bold));
    g.drawText ("ALBUM", 16, 14, 70, 28, Justification::centredLeft);
    g.drawText ("THIS SONG", 16, 54, 74, 28, Justification::centredLeft);

    // Таблиця
    const int cols[] { 16, 222, 284, 352, 420, 484, 548 };
    const char* heads[] { "SONG", "LUFS", "VS ALBUM", "PLR", "LOWS", "MIDS", "HIGHS" };
    g.setFont (FontOptions (10.5f, Font::bold));
    for (int c = 0; c < 7; ++c) g.drawText (heads[c], cols[c], top - 18, 70, 16, Justification::centredLeft);

    auto diffCell = [&] (float v, float warn, int x, int y)
    {
        g.setColour (std::abs (v) > warn * 2.0f ? Theme::hot : std::abs (v) > warn ? Theme::warn : Theme::text);
        g.drawText ((v >= 0 ? "+" : "") + String (v, 1), x, y, 66, rowH, Justification::centredLeft);
    };

    removeRects.clear();
    int y = top;
    auto row = [&] (const AlbumSong& s, bool live)
    {
        const auto d = album.compare (s);
        g.setColour (live ? Theme::accent.withAlpha (0.12f) : Colours::transparentBlack);
        g.fillRoundedRectangle (8.0f, (float) y + 1.0f, (float) getWidth() - 16.0f, (float) rowH - 2.0f, 6.0f);
        g.setFont (FontOptions (13.0f, live ? Font::bold : Font::plain));
        g.setColour (live ? Theme::accent : Theme::text);
        g.drawText (live ? "now: " + (s.name.isEmpty() ? String ("this song") : s.name) : s.name, cols[0], y, 200, rowH, Justification::centredLeft, true);
        g.setColour (Theme::text);
        g.drawText (s.lufs > -70.0f ? String (s.lufs, 1) : String ("--"), cols[1], y, 66, rowH, Justification::centredLeft);
        g.drawText (s.lufs > -70.0f ? String (s.plr(), 1) : String ("--"), cols[3], y, 66, rowH, Justification::centredLeft);
        if (d.valid && s.lufs > -70.0f)
        {
            diffCell (d.lufs, 1.0f, cols[2], y);
            diffCell (d.lows, 1.5f, cols[4], y); diffCell (d.mids, 1.5f, cols[5], y); diffCell (d.highs, 1.5f, cols[6], y);
        }
        if (! live)
        {
            const Rectangle<float> x ((float) getWidth() - 34.0f, (float) y + 5.0f, 16.0f, 16.0f);
            removeRects.push_back (x);
            g.setColour (Theme::muted);
            const auto c = x.reduced (4.0f);
            g.drawLine (c.getX(), c.getY(), c.getRight(), c.getBottom(), 1.4f);
            g.drawLine (c.getRight(), c.getY(), c.getX(), c.getBottom(), 1.4f);
        }
        y += rowH;
    };
    for (const auto& s : album.songs) row (s, false);
    const auto now = currentSong (proc, songName.getText().trim());
    row (now, true);

    // Поради для поточної пісні
    y += 10;
    g.setFont (FontOptions (12.5f));
    if (album.name.isEmpty())
    {
        g.setColour (Theme::muted);
        g.drawFittedText ("Type an album name, play a finished song through Master, name it and SAVE. Then load the next song: "
                          "this panel shows where it differs from the rest (LOWS / MIDS / HIGHS in dB vs the album average).",
                          16, y, getWidth() - 32, 60, Justification::topLeft, 3);
        return;
    }
    if (album.songs.empty() || (album.songs.size() == 1 && album.songs[0].name.equalsIgnoreCase (now.name)))
    {
        g.setColour (Theme::muted);
        g.drawText ("Save at least two songs to compare them.", 16, y, getWidth() - 32, 20, Justification::centredLeft);
        return;
    }
    if (now.lufs <= -70.0f)
    {
        g.setColour (Theme::muted);
        g.drawText ("Play this song to compare it with the album.", 16, y, getWidth() - 32, 20, Justification::centredLeft);
        return;
    }
    const auto tips = Album::advice (album.compare (now));
    if (tips.isEmpty())
    {
        g.setColour (Theme::good);
        g.drawText ("This song sits right in the album.", 16, y, getWidth() - 32, 20, Justification::centredLeft);
    }
    for (const auto& t : tips)
    {
        g.setColour (Theme::warn);
        g.fillEllipse (18.0f, (float) y + 6.0f, 7.0f, 7.0f);
        g.setColour (Theme::text);
        g.drawText (t, 32, y, getWidth() - 48, 20, Justification::centredLeft, true);
        y += 22;
    }
}

void AlbumView::mouseDown (const MouseEvent& e)
{
    for (size_t i = 0; i < removeRects.size() && i < album.songs.size(); ++i)
        if (removeRects[i].contains (e.position))
        {
            album.remove (album.songs[i].name);
            album.save();
            relayout();
            return;
        }
}
