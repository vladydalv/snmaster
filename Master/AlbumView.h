#pragma once

#include "PluginProcessor.h"
#include "Album.h"
#include "../Common/Widgets.h"

/** ALBUM: зберегти «відбиток» пісні й порівняти з рештою альбому (гучність, щільність, низ/середина/верх). */
class AlbumView final : public juce::Component, private juce::Timer
{
public:
    explicit AlbumView (SpacenerdMasterProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    /** Відбиток поточної пісні (те, що Master чує зараз, з усім ланцюгом). */
    static AlbumSong currentSong (SpacenerdMasterProcessor&, const juce::String& name);

private:
    void timerCallback() override;
    void loadAlbum (const juce::String&);
    void relayout();

    SpacenerdMasterProcessor& proc;
    Album album;
    juce::ComboBox albumBox;
    juce::TextEditor songName;
    juce::TextButton saveButton { "SAVE THIS SONG" };
    std::vector<juce::Rectangle<float>> removeRects;
    static constexpr int rowH = 26, top = 104;
};
