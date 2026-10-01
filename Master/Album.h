#pragma once

#include <juce_core/juce_core.h>
#include "../Common/Analysis.h"

/** Альбом: «відбитки» пісень (гучність, динаміка, тембр), щоб пісні звучали як одна платівка.
    Зберігається у ~/Library/Application Support/Spacenerd/Albums/<назва>.json. */
struct AlbumSong
{
    juce::String name;
    float lufs = -100.0f, tp = -100.0f;
    an::Spectrum tone {};                // нормований тембр (форма, не гучність)
    float plr() const noexcept { return tp - lufs; }
};

class Album
{
public:
    static juce::File& dirOverride() { static juce::File f; return f; }   // для тестів

    static juce::File dir()
    {
        if (dirOverride() != juce::File()) return dirOverride();
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
                   .getChildFile ("Application Support")
#endif
                   .getChildFile ("Spacenerd").getChildFile ("Albums");
    }

    static juce::StringArray list()
    {
        juce::StringArray r;
        for (const auto& f : dir().findChildFiles (juce::File::findFiles, false, "*.json"))
            r.add (f.getFileNameWithoutExtension());
        r.sort (true);
        return r;
    }

    juce::String name;
    std::vector<AlbumSong> songs;

    void load (const juce::String& albumName)
    {
        name = albumName.trim();
        songs.clear();
        const auto v = juce::JSON::parse (file());
        if (auto* arr = v.getProperty ("songs", {}).getArray())
            for (const auto& s : *arr)
            {
                AlbumSong song;
                song.name = s.getProperty ("name", "").toString();
                song.lufs = (float) s.getProperty ("lufs", -100.0);
                song.tp = (float) s.getProperty ("tp", -100.0);
                if (auto* t = s.getProperty ("tone", {}).getArray())
                    for (int b = 0; b < an::kBands && b < t->size(); ++b) song.tone[(size_t) b] = (float) (*t)[b];
                if (song.name.isNotEmpty()) songs.push_back (song);
            }
    }

    bool save() const
    {
        if (name.isEmpty()) return false;
        juce::Array<juce::var> arr;
        for (const auto& s : songs)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("name", s.name);
            o->setProperty ("lufs", s.lufs);
            o->setProperty ("tp", s.tp);
            juce::Array<juce::var> t;
            for (auto v : s.tone) t.add (v);
            o->setProperty ("tone", t);
            arr.add (juce::var (o));
        }
        auto* root = new juce::DynamicObject();
        root->setProperty ("songs", arr);
        dir().createDirectory();
        return file().replaceWithText (juce::JSON::toString (juce::var (root)));
    }

    void put (const AlbumSong& s)
    {
        for (auto& x : songs) if (x.name.equalsIgnoreCase (s.name)) { x = s; return; }
        songs.push_back (s);
    }

    void remove (const juce::String& songName)
    {
        songs.erase (std::remove_if (songs.begin(), songs.end(), [&] (const AlbumSong& s) { return s.name.equalsIgnoreCase (songName); }), songs.end());
    }

    /** Відхилення пісні від середнього решти альбому. */
    struct Diff { bool valid = false; float lufs = 0, plr = 0, lows = 0, mids = 0, highs = 0; };

    Diff compare (const AlbumSong& s) const
    {
        Diff d;
        int n = 0;
        double lufsE = 0.0, plr = 0.0;
        an::Spectrum tone {};
        for (const auto& o : songs)
        {
            if (o.name.equalsIgnoreCase (s.name)) continue;
            lufsE += std::pow (10.0, o.lufs / 10.0); plr += o.plr();
            for (int b = 0; b < an::kBands; ++b) tone[(size_t) b] += o.tone[(size_t) b];
            ++n;
        }
        if (n == 0) return d;
        d.valid = true;
        d.lufs = s.lufs - (float) (10.0 * std::log10 (lufsE / n));
        d.plr = s.plr() - (float) (plr / n);
        for (auto& v : tone) v /= (float) n;
        auto region = [&] (float lo, float hi)
        {
            double sum = 0.0; int k = 0;
            for (int b = 0; b < an::kBands; ++b)
                if (an::bandHz[(size_t) b] >= lo && an::bandHz[(size_t) b] <= hi) { sum += s.tone[(size_t) b] - tone[(size_t) b]; ++k; }
            return (float) (sum / std::max (1, k));
        };
        d.lows = region (40, 160); d.mids = region (200, 2000); d.highs = region (3150, 12500);
        return d;
    }

    /** Поради, щоб пісня сіла в альбом (ручки Master). */
    static juce::StringArray advice (const Diff& d)
    {
        juce::StringArray r;
        if (! d.valid) return r;
        auto db = [] (float v) { return juce::String (std::abs (v), 1) + " dB"; };
        if (std::abs (d.lufs) > 1.0f)
            r.add (juce::String (d.lufs > 0 ? "Louder" : "Quieter") + " than the album by " + juce::String (std::abs (d.lufs), 1) + " LU  ->  LIMITER: DRIVE "
                   + (d.lufs > 0 ? "-" : "+") + juce::String (std::abs (d.lufs), 1) + " dB");
        if (std::abs (d.lows) > 1.5f)
            r.add (juce::String ("Lows ") + (d.lows > 0 ? "heavier" : "lighter") + " by " + db (d.lows) + "  ->  EQ: LOW GAIN " + (d.lows > 0 ? "-" : "+") + db (d.lows * 0.8f));
        if (std::abs (d.highs) > 1.5f)
            r.add (juce::String ("Highs ") + (d.highs > 0 ? "brighter" : "darker") + " by " + db (d.highs) + "  ->  EQ: HIGH GAIN " + (d.highs > 0 ? "-" : "+") + db (d.highs * 0.8f));
        if (std::abs (d.plr) > 2.0f)
            r.add (d.plr > 0 ? "More dynamic than the album (PLR +" + juce::String (d.plr, 1) + ")  ->  COMP: THRESHOLD - or LIMITER: DRIVE +"
                             : "Denser than the album (PLR " + juce::String (d.plr, 1) + ")  ->  LIMITER: DRIVE - or COMP: RATIO -");
        return r;
    }

private:
    juce::File file() const { return dir().getChildFile (juce::File::createLegalFileName (name) + ".json"); }
};
