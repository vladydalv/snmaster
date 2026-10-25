#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "../Common/SNCommon.h"
#include "SaxModel.h"

/** Семпловий баритон-саксофон: справжній Conn 1926 (бібліотека Karoryfer «Bear Sax», роялті-фрі).
    Як у професійних семплерів: два динамічні шари з циклами (p / f), плавний перехід між ними за силою подиху,
    скриптове легато (нова нота стартує після атаки з глісандо від попередньої, стара затухає),
    шуми клапанів на атаці й відпусканні, субтон на тихій грі, гроул, повітря. */
namespace syn
{
class SaxLibrary
{
public:
    struct Sample { juce::AudioBuffer<float> buf; int loopStart = -1, loopEnd = -1; double rate = 44100.0; };
    struct Ref { int sample = -1; int keycenter = 60; float gain = 1.0f; int offset = 0; };
    struct Key { Ref p, f; std::vector<Ref> growl, sub, attackNoise, releaseNoise; };

    static constexpr int kLo = 36, kHi = 68;

    static SaxLibrary& get() { static SaxLibrary lib; return lib; }
    bool ready() const { return isReady.load (std::memory_order_acquire); }

    const Key& key (int note) const { return keys[(size_t) juce::jlimit (kLo, kHi, note) - kLo]; }
    const Sample& sample (int i) const { return samples[(size_t) i]; }
    const Ref& breath() const { return breathRef; }

    /** Знайти теку бібліотеки: усередині бандла плагіна, в Application Support або за змінною SN_SAX_DIR. */
    static juce::File findRoot()
    {
        juce::Array<juce::File> c;
        if (auto* env = std::getenv ("SN_SAX_DIR")) c.add (juce::File (juce::String::fromUTF8 (env)));
        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        c.add (exe.getParentDirectory().getParentDirectory().getChildFile ("Resources").getChildFile ("BearSax"));
        c.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support/Spacenerd/BearSax"));
        c.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Spacenerd/BearSax"));
        for (auto& f : c) if (f.getChildFile ("Programs/legato/dynfade_legato_first_map.sfz").existsAsFile()) return f;
        return {};
    }

private:
    SaxLibrary() { loader = std::thread ([this] { load(); }); }
    ~SaxLibrary() { if (loader.joinable()) loader.join(); }

    using Region = std::map<juce::String, juce::String>;

    /** Мінімальний розбір SFZ: <group>/<region>, ключ=значення, коментарі //. */
    static std::vector<Region> parse (const juce::File& f)
    {
        std::vector<Region> out;
        Region group, cur;
        bool inRegion = false;
        auto flush = [&] { if (inRegion) { Region r = group; for (auto& [k, v] : cur) r[k] = v; out.push_back (r); } cur.clear(); };
        juce::StringArray lines; lines.addLines (f.loadFileAsString());
        for (auto line : lines)
        {
            line = line.upToFirstOccurrenceOf ("//", false, false).trim();
            if (line.isEmpty()) continue;
            juce::StringArray tok; tok.addTokens (line, " \t", "");
            for (auto& t : tok)
            {
                if (t == "<group>") { flush(); inRegion = false; group.clear(); continue; }
                if (t == "<region>") { flush(); inRegion = true; continue; }
                if (t.startsWithChar ('<')) { flush(); inRegion = false; continue; }
                const int eq = t.indexOfChar ('=');
                if (eq > 0) (inRegion ? cur : group)[t.substring (0, eq)] = t.substring (eq + 1);
            }
        }
        flush();
        return out;
    }

    int loadSample (const juce::File& dir, const juce::String& rel)
    {
        const auto name = rel.replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false);
        if (auto it = index.find (name); it != index.end()) return it->second;
        const auto file = dir.getChildFile ("Samples").getChildFile (name);
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));
        if (r == nullptr) return -1;
        Sample s;
        s.rate = r->sampleRate;
        s.buf.setSize (1, (int) r->lengthInSamples);
        r->read (&s.buf, 0, (int) r->lengthInSamples, 0, true, false);
        const auto& md = r->metadataValues;
        if (md.getValue ("NumSampleLoops", "0").getIntValue() > 0)
        {
            s.loopStart = md.getValue ("Loop0Start", "-1").getIntValue();
            s.loopEnd = md.getValue ("Loop0End", "-1").getIntValue();
            if (s.loopEnd <= s.loopStart + 64 || s.loopEnd >= s.buf.getNumSamples()) s.loopStart = s.loopEnd = -1;
        }
        samples.push_back (std::move (s));
        const int idx = (int) samples.size() - 1;
        index[name] = idx;
        return idx;
    }

    void load()
    {
        formats.registerBasicFormats();
        const auto root = findRoot();
        if (! root.isDirectory()) return;
        const auto dir = root;
        const auto prog = root.getChildFile ("Programs/legato");
        keys.resize ((size_t) (kHi - kLo + 1));
        auto keyRange = [] (const Region& r, int& lo, int& hi)
        {
            lo = r.count ("lokey") ? r.at ("lokey").getIntValue() : (r.count ("key") ? r.at ("key").getIntValue() : 0);
            hi = r.count ("hikey") ? r.at ("hikey").getIntValue() : lo;
        };
        auto makeRef = [&] (const Region& r)
        {
            Ref ref;
            ref.sample = r.count ("sample") ? loadSample (dir, r.at ("sample")) : -1;
            ref.keycenter = r.count ("pitch_keycenter") ? r.at ("pitch_keycenter").getIntValue() : 60;
            ref.gain = r.count ("volume") ? sn::dbToGain (r.at ("volume").getFloatValue()) : 1.0f;
            ref.offset = r.count ("offset") ? r.at ("offset").getIntValue() : 0;
            return ref;
        };
        for (auto& r : parse (prog.getChildFile ("dynfade_legato_first_map.sfz")))
        {
            int lo, hi; keyRange (r, lo, hi);
            const bool loud = r.count ("xfin_locc1") > 0;
            for (int k = lo; k <= hi; ++k) if (k >= kLo && k <= kHi) (loud ? keys[(size_t) (k - kLo)].f : keys[(size_t) (k - kLo)].p) = makeRef (r);
        }
        auto addList = [&] (const char* file, std::function<std::vector<Ref>& (Key&, const Region&)> pick)
        {
            for (auto& r : parse (prog.getChildFile (file)))
            {
                int lo, hi; keyRange (r, lo, hi);
                const auto ref = makeRef (r);
                if (ref.sample < 0) continue;
                for (int k = lo; k <= hi; ++k) if (k >= kLo && k <= kHi) pick (keys[(size_t) (k - kLo)], r).push_back (ref);
            }
        };
        addList ("growl_map.sfz", [] (Key& k, const Region&) -> std::vector<Ref>& { return k.growl; });
        addList ("sub_map.sfz", [] (Key& k, const Region&) -> std::vector<Ref>& { return k.sub; });
        addList ("noises_map.sfz", [] (Key& k, const Region& r) -> std::vector<Ref>& { return r.count ("trigger") && r.at ("trigger") == "release" ? k.releaseNoise : k.attackNoise; });
        for (auto& r : parse (prog.getChildFile ("breath.sfz"))) { breathRef = makeRef (r); if (breathRef.sample >= 0) break; }

        // Перевірка: кожна клавіша має обидва шари сустейну
        for (auto& k : keys) if (k.p.sample < 0 || k.f.sample < 0) return;
        isReady.store (true, std::memory_order_release);
    }

    juce::AudioFormatManager formats;
    std::vector<Sample> samples;
    std::map<juce::String, int> index;
    std::vector<Key> keys;
    Ref breathRef;
    std::atomic<bool> isReady { false };
    std::thread loader;
};

//==============================================================================
class SampledSax
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        shelf.setHighShelf (fs, 2500.0, 0.7, 0.0); body.setPeak (fs, 220.0, 0.8, 0.0);
        reset();
    }
    void reset()
    {
        cur = {}; prev = {}; growl = {}; sub = {}; noise = {}; breathPos = 0.0; dyn = -1.0f; glideOff = 0.0f; scoopOff = 0.0f;
        shelf.reset(); body.reset(); designed = -1.0f;
    }

    void noteOn (int note, float vel, bool legato, const SaxParams& p, float glideMs)
    {
        auto& lib = SaxLibrary::get();
        if (! lib.ready()) return;
        const auto& k = lib.key (note);
        velocity = vel;
        if (legato && cur.on)
        {
            prev = cur; prev.fade = -1.0f / (0.06f * (float) fs);          // попередня нота затухає за 60 мс
            glideOff = (float) (lastNote - note);                         // глісандо від попередньої ноти
            glideK = 1.0f - std::exp (-1.0f / (0.001f * std::max (40.0f, glideMs) * 0.5f * (float) fs));
            start (cur, k, note, lib, k.f.offset > 0 ? k.f.offset : 1800); // легато: без атаки
            cur.level = 0.0f; cur.fade = 1.0f / (0.04f * (float) fs);
            scoopOff = -0.25f * p.scoop;
        }
        else
        {
            // Попередня нота ще дзвенить (хвіст відпускання) — коротко затухає, а не обривається
            if (cur.on) { prev = cur; prev.fade = -1.0f / (0.012f * (float) fs); } else prev.on = false;
            start (cur, k, note, lib, 0);
            cur.level = 1.0f; cur.fade = 0.0f;
            glideOff = 0.0f;
            scoopOff = -1.6f * p.scoop * (1.1f - 0.4f * vel);
            // Шум клапана на атаці, субтон на тихій грі, гроул
            if (! k.attackNoise.empty()) oneShot (noise, k.attackNoise[(size_t) (rr++ % k.attackNoise.size())], note, 0.5f + 1.5f * p.air);
            if (! k.sub.empty() && dynFor (vel, p) < 0.45f) oneShot (sub, k.sub[(size_t) (rr++ % k.sub.size())], note, 1.2f * (0.45f - dynFor (vel, p)) / 0.45f);
            if (! k.growl.empty() && p.growl > 0.05f) oneShot (growl, k.growl[(size_t) (rr++ % k.growl.size())], note, 1.3f * p.growl);
        }
        lastNote = note;
    }

    void noteOff (int note, const SaxParams& p)
    {
        auto& lib = SaxLibrary::get();
        if (! lib.ready()) return;
        const auto& k = lib.key (note);
        if (! k.releaseNoise.empty()) oneShot (noise, k.releaseNoise[(size_t) (rr++ % k.releaseNoise.size())], note, 0.4f + 1.2f * p.air);
    }

    /** Моно-вихід; pitchSemi — нота з бендом/вібрато; tail — поки звучать шуми після відпускання. */
    void render (float* out, int len, float pitchSemi, const SaxParams& p)
    {
        std::fill_n (out, len, 0.0f);
        auto& lib = SaxLibrary::get();
        if (! lib.ready()) return;
        if (std::abs (p.reed - designed) > 0.005f || std::abs (p.body - designedBody) > 0.005f)
        {
            designed = p.reed; designedBody = p.body;
            shelf.setHighShelf (fs, 2500.0, 0.7, (p.reed - 0.5f) * 14.0f);   // тростина: темніше ↔ яскравіше
            body.setPeak (fs, 220.0, 0.8, (p.body - 0.5f) * 10.0f);          // корпус: низ баритона
        }
        // Сила подиху → перехід між шарами p / f (рівна потужність, як xfade у SFZ)
        const float target = dynFor (velocity, p);
        if (dyn < 0.0f) dyn = target;
        dyn += (target - dyn) * (1.0f - std::exp (-(float) len / (0.03f * (float) fs)));
        const float t = juce::jlimit (0.0f, 1.0f, (dyn - 0.236f) / 0.764f);
        const float gP = (dyn < 0.236f ? std::sin (1.5708f * dyn / 0.236f) : std::cos (1.5708f * t)) * 1.0f;
        const float gF = std::sin (1.5708f * t);
        const float lvl = 0.35f + 0.65f * std::sqrt (dyn);

        scoopOff *= std::exp (-(float) len / (0.04f * (float) fs));
        glideOff *= std::pow (1.0f - glideK, (float) len);
        const float pitch = pitchSemi + scoopOff + glideOff;

        // Гроул: хрипіння й субгармоніка на сустейні (як живий музикант, що співає в інструмент)
        const float growlDepth = p.growl * 0.5f;
        const float subInc = 0.5f * 440.0f * std::exp2 ((pitch - 69.0f) / 12.0f) / (float) fs;

        for (int i = 0; i < len; ++i)
        {
            float s = 0.0f;
            s += play (cur, pitch, lib, gP, gF);
            if (prev.on) s += play (prev, pitch - glideOff + (float) (prev.note - lastNote), lib, gP, gF);
            s *= lvl;
            if (growlDepth > 0.0f)
            {
                growlPh += subInc; if (growlPh >= 1.0f) growlPh -= 1.0f;
                rough += 0.02f * ((rng.nextFloat() * 2.0f - 1.0f) - rough);
                s *= 1.0f + growlDepth * (0.5f * std::sin (6.2831853f * growlPh) + 2.5f * rough);
            }
            s += playOneShot (sub, pitch, lib) + playOneShot (growl, pitch, lib) + playOneShot (noise, (float) noise.note, lib) * 0.6f;
            // Повітря: петля дихання, гучніша на тихій грі
            const auto& br = lib.breath();
            if (br.sample >= 0 && p.air > 0.01f && cur.on)
            {
                const auto& bs = lib.sample (br.sample);
                const int n = bs.buf.getNumSamples();
                breathPos += bs.rate / fs; if (breathPos >= n - 2) breathPos = 0.0;
                s += bs.buf.getSample (0, (int) breathPos) * p.air * 0.25f * (1.2f - dyn) * cur.level;
            }
            out[i] = body.process (shelf.process (s)) * 2.2f;
        }
    }

private:
    struct Player
    {
        bool on = false;
        int note = 60, sp = -1, sf = -1, keycenter = 60;
        double pos = 0.0;
        float level = 1.0f, fade = 0.0f, gain = 1.0f;
    };

    static float dynFor (float vel, const SaxParams& p) { return juce::jlimit (0.03f, 1.0f, (0.25f + 0.75f * std::pow (vel, 0.8f)) * (0.35f + 0.85f * p.breath)); }

    void start (Player& pl, const SaxLibrary::Key& k, int note, const SaxLibrary& lib, int offset)
    {
        pl.on = true; pl.note = note; pl.sp = k.p.sample; pl.sf = k.f.sample; pl.keycenter = k.f.keycenter;
        pl.pos = std::min ((double) offset, (double) lib.sample (pl.sf).buf.getNumSamples() * 0.5);
    }

    void oneShot (Player& pl, const SaxLibrary::Ref& r, int note, float gain)
    {
        pl.on = true; pl.note = note; pl.sp = r.sample; pl.sf = -1; pl.keycenter = r.keycenter; pl.pos = 0.0; pl.gain = gain * r.gain; pl.level = 1.0f; pl.fade = 0.0f;
    }

    static inline float cubic (const float* d, int n, double pos)
    {
        const int i = (int) pos; const float a = (float) (pos - i);
        auto at = [&] (int k) { return d[juce::jlimit (0, n - 1, k)]; };
        const float x0 = at (i - 1), x1 = at (i), x2 = at (i + 1), x3 = at (i + 2);
        const float c1 = 0.5f * (x2 - x0), c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3, c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
        return ((c3 * a + c2) * a + c1) * a + x1;
    }

    float play (Player& pl, float pitch, const SaxLibrary& lib, float gP, float gF)
    {
        if (! pl.on) return 0.0f;
        const auto& sP = lib.sample (pl.sp);
        const auto& sF = lib.sample (pl.sf);
        const double rate = sF.rate / fs * std::exp2 ((pitch - (float) pl.keycenter) / 12.0f);
        const int n = std::min (sP.buf.getNumSamples(), sF.buf.getNumSamples());
        const float v = gP * cubic (sP.buf.getReadPointer (0), sP.buf.getNumSamples(), pl.pos) + gF * cubic (sF.buf.getReadPointer (0), sF.buf.getNumSamples(), pl.pos);
        pl.pos += rate;
        if (sF.loopEnd > 0) { if (pl.pos >= sF.loopEnd) pl.pos -= (sF.loopEnd - sF.loopStart); }
        else if (pl.pos >= n - 2) pl.on = false;
        pl.level = juce::jlimit (0.0f, 1.0f, pl.level + pl.fade);
        if (pl.fade < 0.0f && pl.level <= 0.0f) pl.on = false;
        return v * pl.level;
    }

    float playOneShot (Player& pl, float pitch, const SaxLibrary& lib)
    {
        if (! pl.on || pl.sp < 0) return 0.0f;
        const auto& s = lib.sample (pl.sp);
        const int n = s.buf.getNumSamples();
        const float v = cubic (s.buf.getReadPointer (0), n, pl.pos);
        pl.pos += s.rate / fs * std::exp2 ((pitch - (float) pl.keycenter) / 12.0f);
        if (pl.pos >= n - 2) pl.on = false;
        // м'який кінець, щоб one-shot не обривався
        const double left = n - pl.pos;
        const float tail = left < 0.03 * s.rate ? (float) (left / (0.03 * s.rate)) : 1.0f;
        return v * pl.gain * tail;
    }

    double fs = 96000.0, breathPos = 0.0;
    Player cur, prev, growl, sub, noise;
    float velocity = 0.8f, dyn = -1.0f, glideOff = 0.0f, glideK = 0.01f, scoopOff = 0.0f, growlPh = 0.0f, rough = 0.0f;
    float designed = -1.0f, designedBody = -1.0f;
    int lastNote = 60;
    unsigned rr = 0;
    sn::Biquad shelf, body;
    juce::Random rng { 11 };
};
} // namespace syn
