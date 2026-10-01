#include <juce_audio_processors/juce_audio_processors.h>
#include "../Listen/PluginProcessor.h"
#include "../Listen/PluginEditor.h"
#include "../Master/PluginProcessor.h"
#include "../Master/PluginEditor.h"
#include <iostream>

using namespace juce;

static int failures = 0;
static void check (bool ok, const String& what) { std::cout << (ok ? "[ OK ] " : "[FAIL] ") << what << std::endl; if (! ok) ++failures; }

static constexpr double fs = 48000.0;
static constexpr double twoPi = MathConstants<double>::twoPi;

struct Head final : AudioPlayHead
{
    int64 pos = 0;
    bool playing = true;
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i; i.setIsPlaying (playing); i.setTimeInSamples (pos); return i;
    }
};

//==============================================================================
// Синтетичні інструменти (моно → стерео)
static AudioBuffer<float> makeKick (double sec, uint32 seed)
{
    AudioBuffer<float> b (2, (int) (sec * fs)); b.clear();
    Random rnd ((int64) seed);
    for (double t0 = 0.0; t0 < sec - 0.4; t0 += rnd.nextBool() ? 0.5 : 0.25)
    {
        double ph = 0.0;
        const int s0 = (int) (t0 * fs);
        for (int i = 0; i < (int) (0.35 * fs) && s0 + i < b.getNumSamples(); ++i)
        {
            const double t = i / fs;
            ph += twoPi * (50.0 + 90.0 * std::exp (-t * 30.0)) / fs;
            const float v = (float) (0.8 * std::exp (-t * 9.0) * std::sin (ph) + (i < 96 ? 0.25 * (rnd.nextFloat() * 2 - 1) : 0.0));
            b.addSample (0, s0 + i, v); b.addSample (1, s0 + i, v);
        }
    }
    return b;
}

static AudioBuffer<float> makeBass (double sec, uint32 seed)
{
    AudioBuffer<float> b (2, (int) (sec * fs)); b.clear();
    Random rnd ((int64) seed);
    const double notes[] { 41.2, 55.0, 73.42, 49.0, 61.74 };
    sn::Biquad lp; lp.setLowPass (fs, 600.0, 0.7);
    double ph = 0.0;
    int i = 0;
    while (i < b.getNumSamples())
    {
        const double hz = notes[rnd.nextInt (5)];
        const int len = (int) ((rnd.nextBool() ? 0.5 : 0.75) * fs);
        const bool rest = rnd.nextInt (5) == 0;
        for (int k = 0; k < len && i < b.getNumSamples(); ++k, ++i)
        {
            ph += hz / fs; ph -= std::floor (ph);
            const double env = rest ? 0.0 : std::min (1.0, k / 200.0) * std::exp (-k / fs * 1.2);
            const float v = lp.process ((float) (0.35 * env * (2.0 * ph - 1.0)));
            b.setSample (0, i, v); b.setSample (1, i, v);
        }
    }
    return b;
}

/** Карплус-Стронг з легким драйвом; cents — розстрій усього інструмента. */
static AudioBuffer<float> makeGuitar (double sec, uint32 seed, double cents, float humAmp = 0.0f)
{
    AudioBuffer<float> b (2, (int) (sec * fs)); b.clear();
    Random rnd ((int64) seed);
    const double notes[] { 164.81, 196.0, 220.0, 246.94, 293.66, 329.63 };
    const double k = std::pow (2.0, cents / 1200.0);
    int i = 0;
    while (i < b.getNumSamples())
    {
        const double hz = notes[rnd.nextInt (6)] * k;
        const double len = hz > 0 ? fs / hz : 100.0;
        std::vector<float> line ((size_t) std::ceil (len) + 2);
        for (auto& x : line) x = rnd.nextFloat() * 2.0f - 1.0f;
        const int noteLen = (int) (0.6 * fs), gap = (int) (0.25 * fs);
        double rp = 0.0;
        float prev = 0.0f;
        for (int n = 0; n < noteLen && i < b.getNumSamples(); ++n, ++i)
        {
            // Дробова затримка: лінійна інтерполяція
            const double idx = rp; const int i0 = (int) idx % (int) line.size();
            const float s = line[(size_t) i0];
            const float y = 0.996f * 0.5f * (s + prev);
            prev = s;
            line[(size_t) i0] = y;
            rp += 1.0; if (rp >= len) rp -= len;
            const float v = (float) std::tanh (2.5 * y) * 0.4f;
            b.setSample (0, i, v); b.setSample (1, i, v);
        }
        i += gap;
    }
    if (humAmp > 0.0f)
        for (int n = 0; n < b.getNumSamples(); ++n)
        {
            const double t = n / fs;
            const float h = humAmp * (float) (std::sin (twoPi * 50 * t) + 0.6 * std::sin (twoPi * 100 * t) + 0.4 * std::sin (twoPi * 150 * t));
            b.addSample (0, n, h); b.addSample (1, n, h);
        }
    return b;
}

static AudioBuffer<float> makeVocal (double sec, uint32 seed)
{
    AudioBuffer<float> b (2, (int) (sec * fs)); b.clear();
    Random rnd ((int64) seed);
    sn::Biquad f1, f2, hp;
    f1.setBandPass (fs, 700.0, 3.0); f2.setBandPass (fs, 2400.0, 4.0); hp.setHighPass (fs, 5000.0);
    double ph = 0.0, vib = 0.0;
    int i = 0;
    const double notes[] { 196.0, 220.0, 246.94, 261.63 };
    while (i < b.getNumSamples())
    {
        const double base = notes[rnd.nextInt (4)];
        const int len = (int) (0.9 * fs);
        for (int k = 0; k < len && i < b.getNumSamples(); ++k, ++i)
        {
            vib += twoPi * 5.5 / fs;
            const double hz = base * std::pow (2.0, 35.0 * std::sin (vib) / 1200.0);
            ph += hz / fs; ph -= std::floor (ph);
            const double env = std::min (1.0, k / 2000.0) * std::min (1.0, (len - k) / 2000.0);
            const float src = (float) (std::pow (std::sin (MathConstants<double>::pi * ph), 8.0) - 0.27);   // імпульсне джерело
            float v = 0.6f * (f1.process (src) + 0.7f * f2.process (src)) + 0.2f * src;
            if (k < 2400) v += 0.08f * hp.process (rnd.nextFloat() * 2 - 1);      // приголосні
            v *= (float) env * 0.5f;
            b.setSample (0, i, v); b.setSample (1, i, v);
        }
        i += (int) (0.2 * fs);
    }
    return b;
}

static AudioBuffer<float> makeOverheads (double sec, uint32 seed)
{
    AudioBuffer<float> b (2, (int) (sec * fs)); b.clear();
    Random rnd ((int64) seed);
    sn::Biquad hpL, hpR; hpL.setHighPass (fs, 4000.0); hpR.setHighPass (fs, 4000.0);
    for (double t0 = 0.0; t0 < sec - 0.3; t0 += 0.25)
    {
        const int s0 = (int) (t0 * fs);
        for (int i = 0; i < (int) (0.25 * fs) && s0 + i < b.getNumSamples(); ++i)
        {
            const float env = (float) std::exp (-(i / fs) * 14.0);
            b.setSample (0, s0 + i, 0.5f * env * hpL.process (rnd.nextFloat() * 2 - 1));
            b.setSample (1, s0 + i, 0.5f * env * hpR.process (rnd.nextFloat() * 2 - 1));
        }
    }
    return b;
}

/** Уся установка: бочка на 1 і 3, малий на 2 і 4, хай-хет вісімками (120 BPM). */
static AudioBuffer<float> makeKit (double sec, uint32 seed, float kickG, float snareG, float hatG)
{
    AudioBuffer<float> b (2, (int) (sec * fs)); b.clear();
    Random rnd ((int64) seed);
    sn::Biquad hpS, hpH; hpS.setHighPass (fs, 1000.0); hpH.setHighPass (fs, 7000.0);
    auto add = [&] (int s0, int len, auto gen)
    {
        for (int i = 0; i < len && s0 + i < b.getNumSamples(); ++i) { const float v = 0.7f * gen (i, i / fs); b.addSample (0, s0 + i, v); b.addSample (1, s0 + i, v); }
    };
    for (int step = 0; step * 0.25 < sec - 0.5; ++step)
    {
        const int s0 = (int) (step * 0.25 * fs);
        const float hv = hatG * (step % 2 == 0 ? 1.0f : 0.6f);
        add (s0, (int) (0.08 * fs), [&] (int, double t) { return hv * (float) std::exp (-t * 60.0) * hpH.process (rnd.nextFloat() * 2 - 1); });
        if (step % 4 == 0)
        {
            double ph = 0.0;
            add (s0, (int) (0.35 * fs), [&] (int i, double t) {
                ph += twoPi * (50.0 + 90.0 * std::exp (-t * 30.0)) / fs;
                return kickG * (float) (std::exp (-t * 9.0) * std::sin (ph) + (i < 96 ? 0.25 * (rnd.nextFloat() * 2 - 1) : 0.0)); });
        }
        if (step % 4 == 2)
            add (s0, (int) (0.25 * fs), [&] (int, double t) {
                return snareG * (float) (0.6 * std::exp (-t * 25.0) * std::sin (twoPi * 185.0 * t) + 0.7 * std::exp (-t * 14.0) * hpS.process (rnd.nextFloat() * 2 - 1)); });
    }
    return b;
}

/** Прогнати сигнал через SN Listen (з таймлайном хоста). */
static void feed (SpacenerdListenProcessor& p, Head& head, const AudioBuffer<float>& src, int start, int n)
{
    MidiBuffer midi;
    constexpr int block = 480;
    for (int pos = start; pos < start + n; pos += block)
    {
        const int len = std::min (block, start + n - pos);
        AudioBuffer<float> b (2, len);
        for (int ch = 0; ch < 2; ++ch) b.copyFrom (ch, 0, src, ch, pos, len);
        head.pos = pos;
        p.processBlock (b, midi);
        if ((pos / block) % 10 == 0) p.update();
    }
}

static std::unique_ptr<SpacenerdListenProcessor> listenTo (const AudioBuffer<float>& src, Head& head)
{
    auto p = std::make_unique<SpacenerdListenProcessor>();
    p->setPlayHead (&head);
    p->setRateAndBufferSizeDetails (fs, 480);
    p->prepareToPlay (fs, 480);
    feed (*p, head, src, 0, src.getNumSamples());
    for (int i = 0; i < 6; ++i) p->update();
    return p;
}

int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI init;
    auto busFile = File::getSpecialLocation (File::tempDirectory).getChildFile ("sn_mixbus_test.bin");
    busFile.deleteFile();
    mix::Bus::fileOverride() = busFile;
    const double sec = 20.0;

    // 1. Тип інструмента вгадується
    {
        Head head;
        struct Case { const char* name; AudioBuffer<float> sig; int expect; };
        std::vector<Case> cases;
        cases.push_back ({ "kick", makeKick (sec, 1), mix::Kick });
        cases.push_back ({ "bass", makeBass (sec, 2), mix::Bass });
        cases.push_back ({ "guitar", makeGuitar (sec, 3, 0.0), mix::Guitar });
        cases.push_back ({ "vocal", makeVocal (sec, 4), mix::Vocal });
        cases.push_back ({ "overheads", makeOverheads (sec, 5), mix::Drums });
        for (auto& c : cases)
        {
            const auto t0 = Time::getMillisecondCounterHiRes();
            auto p = listenTo (c.sig, head);
            const double cpu = (Time::getMillisecondCounterHiRes() - t0) / (sec * 1000.0) * 100.0;
            if (c.expect == mix::Kick) check (cpu < 3.0, "CPU per track: " + String (cpu, 2) + " % of one core");
            const auto& f = p->features;
            check (f.valid && f.autoInst == c.expect, String ("auto-detect ") + c.name + " -> " + mix::instName (f.autoInst)
                   + "  (crest " + String (f.crestDb, 1) + ", pitched " + String (f.pitchedFrac, 2) + ", spread " + String (f.pitchSpread, 0)
                   + ", median " + String (f.medianHz, 0) + " Hz)");
        }
    }

    // 1b. Уся установка однією доріжкою: розпізнається і баланс усередині
    {
        Head head;
        auto items = [] (const SpacenerdListenProcessor& p)
        {
            StringArray t;
            for (int i = 0; i < p.verdict.numItems; ++i) t.add (String::fromUTF8 (p.verdict.items[i].title));
            return t;
        };
        auto ok = listenTo (makeKit (sec, 21, 0.8f, 0.6f, 0.12f), head);
        const auto& f = ok->features;
        std::cout << "  kit: kick " << f.kitKickDb << " (" << f.kitKickHits << " hits), snare " << f.kitSnareDb << " (" << f.kitSnareHits
                  << "), cymbals " << f.kitCymDb << " | advice: " << items (*ok).joinIntoString ("; ") << std::endl;
        check (f.autoInst == mix::Drums, String ("full kit detected as ") + mix::instName (f.autoInst));
        const auto okItems = items (*ok);
        check (! okItems.joinIntoString ("|").contains ("buried") && ! okItems.joinIntoString ("|").contains ("over the snare"), "balanced kit: no balance complaints");
        auto quietKick = listenTo (makeKit (sec, 22, 0.2f, 0.6f, 0.12f), head);
        check (items (*quietKick).contains ("Kick buried in the kit"), "kick -12 dB in the kit -> 'Kick buried' (" + items (*quietKick).joinIntoString ("; ") + ")");
        auto loudHats = listenTo (makeKit (sec, 23, 0.8f, 0.6f, 0.9f), head);
        std::cout << "  loud hats: inst " << mix::instName (loudHats->features.autoInst) << ", kick " << loudHats->features.kitKickDb << " (" << loudHats->features.kitKickHits
                  << "), snare " << loudHats->features.kitSnareDb << " (" << loudHats->features.kitSnareHits << "), cym " << loudHats->features.kitCymDb << std::endl;
        check (items (*loudHats).contains ("Cymbals over the snare"), "loud hats -> 'Cymbals over the snare' (" + items (*loudHats).joinIntoString ("; ") + ")");
    }

    // 2. Стрій і гул
    {
        Head head;
        auto sharp = listenTo (makeGuitar (sec, 7, 20.0), head);
        check (std::abs (sharp->features.tuneCents - 20.0f) <= 4.0f && sharp->features.tuneFrames >= 40,
               "tuning +20 cents detected: " + String (sharp->features.tuneCents, 0) + " (" + String ((int) sharp->features.tuneFrames) + " notes)");
        auto clean = listenTo (makeGuitar (sec, 8, 0.0), head);
        auto humming = listenTo (makeGuitar (sec, 8, 0.0, 0.003f), head);
        check (humming->features.hum >= 0.5f && std::abs (humming->features.humHz - 50.0f) < 1.0f && clean->features.hum < 0.2f,
               "mains hum: " + String (humming->features.hum, 2) + " at " + String (humming->features.humHz, 0) + " Hz, clean " + String (clean->features.hum, 2));
        bool humAdvice = false;
        for (int i = 0; i < humming->verdict.numItems; ++i) humAdvice |= String (humming->verdict.items[i].title).contains ("hum");
        check (humAdvice, "hum advice shown on the track");
    }

    // 3. Мікс: Master оцінює фейдери, баланс і пише висновки доріжкам
    {
        Head head;
        const auto kick = makeKick (sec, 11), bass = makeBass (sec, 12), gtr = makeGuitar (sec, 13, 0.0);
        const float gK = 0.5f, gB = 1.0f, gG = 0.25f;
        AudioBuffer<float> mixBuf (2, kick.getNumSamples());
        mixBuf.clear();
        for (int ch = 0; ch < 2; ++ch)
        {
            mixBuf.addFrom (ch, 0, kick, ch, 0, mixBuf.getNumSamples(), gK);
            mixBuf.addFrom (ch, 0, bass, ch, 0, mixBuf.getNumSamples(), gB);
            mixBuf.addFrom (ch, 0, gtr, ch, 0, mixBuf.getNumSamples(), gG);
        }

        SpacenerdListenProcessor lk, lb, lg;
        SpacenerdMasterProcessor master;
        for (auto* p : { &lk, &lb, &lg }) { p->setPlayHead (&head); p->setRateAndBufferSizeDetails (fs, 480); p->prepareToPlay (fs, 480); }
        master.setPlayHead (&head); master.setRateAndBufferSizeDetails (fs, 480); master.prepareToPlay (fs, 480);
        lg.apvts.getParameter (ListenIDs::instrument)->setValueNotifyingHost (lg.apvts.getParameter (ListenIDs::instrument)->convertTo0to1 (1.0f + mix::Guitar));
        lg.setUserName ("Fuzz Gtr");

        MidiBuffer midi;
        constexpr int block = 480;
        for (int pos = 0; pos < mixBuf.getNumSamples(); pos += block)
        {
            head.pos = pos;
            const AudioBuffer<float>* srcs[] { &kick, &bass, &gtr };
            SpacenerdListenProcessor* ls[] { &lk, &lb, &lg };
            for (int k = 0; k < 3; ++k)
            {
                AudioBuffer<float> b (2, block);
                for (int ch = 0; ch < 2; ++ch) b.copyFrom (ch, 0, *srcs[k], ch, pos, block);
                ls[k]->processBlock (b, midi);
                if ((pos / block) % 10 == 0) ls[k]->update();
            }
            AudioBuffer<float> m (2, block);
            for (int ch = 0; ch < 2; ++ch) m.copyFrom (ch, 0, mixBuf, ch, pos, block);
            master.processBlock (m, midi);
        }
        for (auto* p : { &lk, &lb, &lg }) for (int i = 0; i < 6; ++i) p->update();
        master.apvts.getParameter (ParamIDs::targetGenre)->setValueNotifyingHost (master.apvts.getParameter (ParamIDs::targetGenre)->convertTo0to1 (1.0f));
        master.mixWatch.refreshNow();
        master.mixWatch.tick (1);

        const auto& w = master.mixWatch;
        check (w.tracks.size() == 3 && w.timelineOk, "Master sees " + String ((int) w.tracks.size()) + " listened tracks");
        auto fader = [&] (int inst) { for (auto& t : w.tracks) if (t.inst == inst) return t.faderDb; return 99.0f; };
        const float fk = fader (mix::Kick), fb = fader (mix::Bass), fg = fader (mix::Guitar);
        check (std::abs (fk + 6.0f) < 1.5f && std::abs (fb) < 1.5f && std::abs (fg + 12.0f) < 1.5f,
               "fader estimates: kick " + String (fk, 1) + " (-6), bass " + String (fb, 1) + " (0), guitar " + String (fg, 1) + " (-12) dB; mix covered "
               + String (roundToInt (w.explained * 100)) + "%");
        bool quietGtr = false;
        for (int i = 0; i < w.overall.numItems; ++i) quietGtr |= String (w.overall.items[i].title).startsWith ("Guitar") && String (w.overall.items[i].title).contains ("quiet");
        check (quietGtr, "balance: guitar flagged too quiet for stoner");
        std::cout << "  overall:\n" << snui::verdictText (w.overall).replace ("\n\n", "\n") << std::endl;

        lg.update();
        for (int i = 0; i < 5; ++i) lg.update();
        check (lg.masterConnected && std::abs (lg.verdict.faderDb - fg) < 0.01f, "Listen shows Master's verdict (connected)");
        bool named = false;
        for (auto& t : w.tracks) named |= t.name == "Fuzz Gtr";
        check (named, "track name reaches Master");

        if (argc > 1)
        {
            const File dir (argv[1]);
            {
                SpacenerdListenEditor ed (lg);
                ed.setSize (SpacenerdListenEditor::baseW, SpacenerdListenEditor::baseH);
                auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.5f);
                FileOutputStream os (dir.getChildFile ("listen.png")); os.setPosition (0); os.truncate();
                PNGImageFormat().writeImageToStream (img, os);
            }
            {
                std::unique_ptr<AudioProcessorEditor> ed (master.createEditor());
                auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
                FileOutputStream os (dir.getChildFile ("master_mix.png")); os.setPosition (0); os.truncate();
                PNGImageFormat().writeImageToStream (img, os);
            }
            std::cout << "Snapshots written" << std::endl;
        }
    }

    busFile.deleteFile();
    std::cout << (failures == 0 ? "ALL PASSED" : String (failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
