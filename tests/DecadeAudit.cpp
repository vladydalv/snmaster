// Аудит SN Decade: кожен контрол робить те, що написано, і достатньо сильно.
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Decade/PluginProcessor.h"
#include <iostream>

using namespace juce;
static constexpr double fs = 48000.0, twoPi = MathConstants<double>::twoPi;
static int failures = 0;
static void check (bool ok, const String& what) { std::cout << (ok ? "[ OK ] " : "[FAIL] ") << what << std::endl; if (! ok) ++failures; }

static void set (SpacenerdDecadeProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

static AudioBuffer<float> kit (double sec)
{
    AudioBuffer<float> b (2, (int) ((sec + 1.5) * fs)); b.clear();
    Random rnd (11);
    sn::Biquad hpS, hpH; hpS.setHighPass (fs, 1000.0); hpH.setHighPass (fs, 7000.0);
    for (int step = 0; step * 0.25 < sec - 0.3; ++step)
    {
        const int s0 = (int) (step * 0.25 * fs);
        for (int i = 0; i < (int) (0.3 * fs) && s0 + i < b.getNumSamples(); ++i)
        {
            const double t = i / fs;
            float l = 0.0f, r = 0.0f;
            const float h = (i < (int) (0.08 * fs)) ? 0.08f * (float) std::exp (-t * 60.0) * hpH.process (rnd.nextFloat() * 2 - 1) : 0.0f;
            l += h; r += 0.7f * h;
            if (step % 4 == 0) { const float k = 0.5f * (float) (std::exp (-t * 9.0) * std::sin (twoPi * (50.0 * t + 1.5 * (1.0 - std::exp (-t * 30.0))))); l += k; r += k; }
            if (step % 4 == 2) { const float s = 0.35f * (float) (0.6 * std::exp (-t * 25.0) * std::sin (twoPi * 185.0 * t) + 0.7 * std::exp (-t * 14.0) * hpS.process (rnd.nextFloat() * 2 - 1)); l += 0.8f * s; r += s; }
            b.addSample (0, s0 + i, l); b.addSample (1, s0 + i, r);
        }
    }
    return b;
}

using Setup = std::function<void (SpacenerdDecadeProcessor&)>;
static AudioBuffer<float> render (const AudioBuffer<float>& in, Setup setup)
{
    SpacenerdDecadeProcessor p;
    set (p, DecIDs::format, 6.0f); set (p, DecIDs::fmtAmount, 0.0f);   // за замовчуванням без носія — перевіряємо стійку
    if (setup) setup (p);
    p.setRateAndBufferSizeDetails (fs, 512);
    p.prepareToPlay (fs, 512);
    AudioBuffer<float> out (in);
    MidiBuffer midi;
    for (int pos = 0; pos < out.getNumSamples(); pos += 512)
    {
        AudioBuffer<float> b (out.getArrayOfWritePointers(), 2, pos, std::min (512, out.getNumSamples() - pos));
        p.processBlock (b, midi);
    }
    return out;
}

static double energy (const AudioBuffer<float>& b, int from = 0, int to = -1)
{
    if (to < 0) to = b.getNumSamples();
    double e = 0.0;
    for (int ch = 0; ch < 2; ++ch) for (int i = from; i < to; ++i) e += (double) b.getSample (ch, i) * b.getSample (ch, i);
    return e;
}
static float db (double e) { return (float) (10.0 * std::log10 (e + 1e-30)); }

static float diffDb (const AudioBuffer<float>& a, const AudioBuffer<float>& b)
{
    double e = 0.0, r = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i) { const double d = a.getSample (ch, i) - b.getSample (ch, i); e += d * d; r += (double) b.getSample (ch, i) * b.getSample (ch, i); }
    return db (e / (r + 1e-30));
}

/** Рівень смуги (крута смуга: 4 біквади). */
static float bandDb (const AudioBuffer<float>& b, double lo, double hi)
{
    std::array<sn::Biquad, 4> f;
    f[0].setHighPass (fs, lo, 0.707); f[1].setHighPass (fs, lo, 0.707); f[2].setLowPass (fs, hi, 0.707); f[3].setLowPass (fs, hi, 0.707);
    double e = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i) { float y = b.getSample (0, i); for (auto& x : f) y = x.process (y); e += (double) y * y; }
    return db (e);
}

/** Пік / RMS у вікнах ударів (атака). */
static float crestDb (const AudioBuffer<float>& b)
{
    double pk = 0.0, e = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i) { const double v = b.getSample (0, i); pk = std::max (pk, std::abs (v)); e += v * v; }
    return db (pk * pk / (e / b.getNumSamples() + 1e-30));
}

static AudioBuffer<float> sine (double hz, float amp)
{
    AudioBuffer<float> b (2, (int) (1.5 * fs));
    for (int i = 0; i < b.getNumSamples(); ++i) { const float v = amp * (float) std::sin (twoPi * hz * i / fs); b.setSample (0, i, v); b.setSample (1, i, v); }
    return b;
}
static float tailRms (const AudioBuffer<float>& b) { return db (energy (b, (int) (0.8 * fs), (int) (1.4 * fs))); }
static Setup dryRack (Setup extra)
{
    return [extra] (SpacenerdDecadeProcessor& p) { set (p, DecIDs::room, 0.0f); set (p, DecIDs::roomOn, 0.0f); set (p, DecIDs::squashOn, 0.0f); set (p, DecIDs::hitOn, 0.0f); if (extra) extra (p); };
}

int main()
{
    ScopedJuceInitialiser_GUI init;
    const auto loop = kit (6.0);
    const int tailFrom = (int) (6.0 * fs), tailTo = (int) (7.0 * fs);

    if (std::getenv ("ROOMSWEEP"))
    {
        for (float er : { 0.0f, 0.7f }) for (float sz : { 0.55f, 0.7f, 0.85f, 1.0f, 1.2f })
        {
            dec::RoomEngine e; e.prepare (fs);
            dec::RoomEngine::Params prm; prm.type = 1; prm.size = sz; prm.rtMid = 1.0f; prm.dampHz = 7000; prm.er = er; prm.predelayMs = 12; prm.gate = 0; prm.diffusion = 0.7f;
            const auto in = kit (2.0);
            AudioBuffer<float> b (in);
            e.process (b.getWritePointer (0), b.getWritePointer (1), b.getNumSamples(), prm, 1.0f);
            for (int ch = 0; ch < 2; ++ch) b.addFrom (ch, 0, in, ch, 0, in.getNumSamples(), -1.0f);
            std::cout << "engine er " << er << " size " << sz << ": " << db (energy (b)) - db (energy (in)) << std::endl;
        }
        for (float yr : { 1965.0f, 1984.0f, 2012.0f })
        {
            const auto dry = render (loop, [yr] (auto& p) { set (p, DecIDs::year, yr); set (p, DecIDs::roomOn, 0.0f); set (p, DecIDs::squashOn, 0.0f); });
            String line = String ((int) yr) + ":";
            for (float rm = 10; rm <= 100; rm += 10)
            {
                const auto wet = render (loop, [yr, rm] (auto& p) { set (p, DecIDs::year, yr); set (p, DecIDs::room, rm); set (p, DecIDs::squashOn, 0.0f); });
                AudioBuffer<float> ro (wet);
                for (int ch = 0; ch < 2; ++ch) ro.addFrom (ch, 0, dry, ch, 0, dry.getNumSamples(), -1.0f);
                line << String (db (energy (ro)) - db (energy (dry)), 1).paddedLeft (' ', 7);
            }
            std::cout << line << std::endl;
        }
        return 0;
    }
    // A. Сила кімнати: енергія кімнати відносно сухого сигналу
    std::cout << "\nROOM LEVEL (room energy vs dry, dB)   room:   25     50     75    100" << std::endl;
    for (float yr : { 1958.0f, 1965.0f, 1972.0f, 1977.0f, 1984.0f, 1991.0f, 2012.0f })
    {
        const auto dry = render (loop, [yr] (auto& p) { set (p, DecIDs::year, yr); set (p, DecIDs::roomOn, 0.0f); set (p, DecIDs::squashOn, 0.0f); });
        String line = "  " + String ((int) yr) + "                                    ";
        for (float rm : { 25.0f, 50.0f, 75.0f, 100.0f })
        {
            const auto wet = render (loop, [yr, rm] (auto& p) { set (p, DecIDs::year, yr); set (p, DecIDs::room, rm); set (p, DecIDs::squashOn, 0.0f); });
            AudioBuffer<float> roomOnly (wet);
            for (int ch = 0; ch < 2; ++ch) roomOnly.addFrom (ch, 0, dry, ch, 0, dry.getNumSamples(), -1.0f);
            line << String (db (energy (roomOnly)) - db (energy (dry)), 1).paddedLeft (' ', 7);
        }
        std::cout << line << std::endl;
    }

    // B. Кожен контрол: зміна звуку (дельта до стандарту) і цільова міра
    const auto ref = render (loop, {});
    std::cout << "\nCONTROLS (difference vs default, and the specific effect)" << std::endl;
    auto audit = [&] (const char* id, float lo, float hi, std::function<float (const AudioBuffer<float>&)> metric, const char* metricName, float minChange)
    {
        const auto a = render (loop, [=] (auto& p) { set (p, id, lo); });
        const auto b = render (loop, [=] (auto& p) { set (p, id, hi); });
        const float m0 = metric (a), m1 = metric (b);
        const float dA = diffDb (a, ref), dB = diffDb (b, ref);
        check (std::abs (m1 - m0) >= minChange && std::max (dA, dB) > -40.0f,
               String (id).paddedRight (' ', 12) + " " + String (lo) + " .. " + String (hi) + ": " + metricName + " " + String (m0, 1) + " -> " + String (m1, 1)
               + "   (sound change " + String (dA, 0) + " / " + String (dB, 0) + " dB)");
    };
    using B = const AudioBuffer<float>&;
    // Tone і Grit — на синусах (точна АЧХ / гармоніки)
    for (auto [id, hz, need] : { std::tuple { DecIDs::toneLow, 50.0, 16.0f }, { DecIDs::toneLowMid, 400.0, 20.0f }, { DecIDs::tonePres, 4000.0, 20.0f }, { DecIDs::toneTop, 14000.0, 18.0f } })
    {
        const auto s = sine (hz, 0.05f);
        const float g0 = tailRms (render (s, dryRack ([id] (auto& p) { set (p, id, -12.0f); set (p, DecIDs::gritOn, 0.0f); set (p, DecIDs::filterOn, 0.0f); })));
        const float g1 = tailRms (render (s, dryRack ([id] (auto& p) { set (p, id, 12.0f); set (p, DecIDs::gritOn, 0.0f); set (p, DecIDs::filterOn, 0.0f); })));
        check (g1 - g0 >= need, String (id).paddedRight (' ', 12) + " -12 .. +12 dB at " + String ((int) hz) + " Hz: " + String (g1 - g0, 1) + " dB swing");
    }
    {
        auto thd = [] (const AudioBuffer<float>& x)
        {
            auto amp = [&] (double hz) { double re = 0, im = 0; const int a = (int) (0.5 * fs), n = (int) fs; // 1 с, ціле число періодів
                for (int i = 0; i < n; ++i) { const double v = x.getSample (0, a + i); re += v * std::cos (twoPi * hz * i / fs); im += v * std::sin (twoPi * hz * i / fs); }
                return re * re + im * im; };
            double h = 0; for (int k = 2; k <= 10; ++k) h += amp (200.0 * k);
            return db (h / amp (200.0));
        };
        const auto s = sine (200.0, 0.3f);
        const float t0 = thd (render (s, dryRack ([] (auto& p) { set (p, DecIDs::gritDrive, -100.0f); set (p, DecIDs::filterOn, 0.0f); })));
        const float t1 = thd (render (s, dryRack ([] (auto& p) { set (p, DecIDs::gritDrive, 0.0f); set (p, DecIDs::filterOn, 0.0f); })));
        const float t2 = thd (render (s, dryRack ([] (auto& p) { set (p, DecIDs::gritDrive, 100.0f); set (p, DecIDs::filterOn, 0.0f); })));
        check (t0 < -60.0f && t2 > t1 + 10.0f && t2 > -25.0f, "gritDrive    harmonics of 200 Hz sine: -100 % " + String (t0, 0) + " dB, era " + String (t1, 0) + " dB, +100 % " + String (t2, 0) + " dB");
    }
    audit (DecIDs::gritChar, 1, 3, [] (B x) { return bandDb (x, 8000, 16000) - bandDb (x, 600, 1500); }, "highs (tape vs sampler)", 1);
    audit (DecIDs::filterHp, -3, 3, [] (B x) { return bandDb (x, 40, 110) - bandDb (x, 600, 1500); }, "lows vs mids", 10);
    audit (DecIDs::filterLp, -3, 3, [] (B x) { return bandDb (x, 6000, 16000) - bandDb (x, 600, 1500); }, "highs vs mids", 10);
    audit (DecIDs::filterReso, 0, 100, [] (B x) { return bandDb (x, 9000, 14000) - bandDb (x, 600, 1500); }, "peak at low-pass edge", 3);
    audit (DecIDs::squashAmt, -100, 100, crestDb, "crest", 3);
    audit (DecIDs::squashSpeed, -100, 100, crestDb, "crest", 1);
    audit (DecIDs::hitAttack, -100, 100, crestDb, "crest", 3);
    audit (DecIDs::hitSustain, -100, 100, [] (B x) { return db (energy (x, (int) (0.08 * fs), (int) (0.24 * fs))) - db (energy (x, 0, (int) (0.02 * fs))); }, "body vs hit", 3);
    audit (DecIDs::roomSize, -100, 100, [&] (B x) { return db (energy (x, tailFrom, tailTo)); }, "tail", 4);
    audit (DecIDs::roomDecay, -100, 100, [&] (B x) { return db (energy (x, tailFrom, tailTo)); }, "tail", 6);
    audit (DecIDs::roomType, 1, 6, [&] (B x) { return db (energy (x, (int) (6.0 * fs), (int) (6.4 * fs))); }, "tail 0.2-0.6 s (studio vs gated)", 3);
    audit (DecIDs::room, 0, 100, [&] (B x) { return db (energy (x, tailFrom, tailTo)); }, "tail", 15);
    audit (DecIDs::year, 1950, 2025, [] (B x) { return bandDb (x, 8000, 16000) - bandDb (x, 600, 1500); }, "highs vs mids", 8);
    audit (DecIDs::auth, 0, 100, [] (B x) { return bandDb (x, 40, 110) - bandDb (x, 600, 1500); }, "lows vs mids", 1);
    audit (DecIDs::mix, 0, 100, [&] (B x) { return db (energy (x, tailFrom, tailTo)); }, "tail", 6);
    audit (DecIDs::inGain, -12, 12, [] (B x) { return db (energy (x)); }, "level", 6);
    audit (DecIDs::outGain, -12, 12, [] (B x) { return db (energy (x)); }, "level", 20);
    for (auto id : { DecIDs::toneOn, DecIDs::gritOn, DecIDs::filterOn, DecIDs::squashOn, DecIDs::hitOn, DecIDs::roomOn })
        audit (id, 0, 1, [] (B x) { return db (energy (x)); }, "level", 0.0f);

    // Фільтр: обертання ручок під час звучання — без клацань і стрибків, стабільний на максимумі резонансу
    for (auto [id, lo, hi] : { std::tuple { DecIDs::filterLp, -3.0f, 3.0f }, { DecIDs::filterHp, -3.0f, 3.0f } })
    {
        SpacenerdDecadeProcessor p;
        set (p, DecIDs::format, 6.0f); set (p, DecIDs::fmtAmount, 0.0f); set (p, DecIDs::room, 0.0f); set (p, DecIDs::roomOn, 0.0f);
        set (p, DecIDs::squashOn, 0.0f); set (p, DecIDs::hitOn, 0.0f); set (p, DecIDs::gritOn, 0.0f); set (p, DecIDs::filterReso, 100.0f);
        p.prepareToPlay (fs, 512);
        const float amp = 0.2f, hz = 700.0f;
        float prev = 0.0f, maxJump = 0.0f, peak = 0.0f; long n = 0;
        MidiBuffer midi;
        for (int blk = 0; blk < 400; ++blk)
        {
            const float t = (float) blk / 399.0f, v = lo + (hi - lo) * (t < 0.5f ? 2 * t : 2 - 2 * t);
            set (p, id, v);
            AudioBuffer<float> b (2, 512);
            for (int i = 0; i < 512; ++i, ++n) { const float x = amp * (float) std::sin (twoPi * hz * n / fs); b.setSample (0, i, x); b.setSample (1, i, x); }
            p.processBlock (b, midi);
            for (int i = 0; i < 512; ++i) { const float y = b.getSample (0, i); if (blk > 20) { maxJump = std::max (maxJump, std::abs (y - prev)); peak = std::max (peak, std::abs (y)); } prev = y; }
        }
        const float sineStep = amp * (float) (twoPi * hz / fs);
        check (maxJump < 4.0f * sineStep && peak < 1.0f && std::isfinite (peak), String (id).paddedRight (' ', 12) + " swept with max resonance: biggest step " + String (maxJump / sineStep, 1) + "x of the sine's own, peak " + String (peak, 2));
    }

    // Носії: кожен звучить інакше, Amount і Age працюють
    std::cout << "\nFORMATS" << std::endl;
    for (int f = 1; f <= 6; ++f)
    {
        const auto clean = render (loop, [f] (auto& p) { set (p, DecIDs::format, (float) f); set (p, DecIDs::fmtAmount, 0.0f); });
        const auto worn = render (loop, [f] (auto& p) { set (p, DecIDs::format, (float) f); set (p, DecIDs::fmtAmount, 100.0f); set (p, DecIDs::fmtAge, 0.0f); });
        const auto old = render (loop, [f] (auto& p) { set (p, DecIDs::format, (float) f); set (p, DecIDs::fmtAmount, 100.0f); set (p, DecIDs::fmtAge, 100.0f); });
        const float dAmt = diffDb (worn, clean), dAge = diffDb (old, worn);
        const bool ages = f != 4 && f != 6;   // CD і стрім не старіють (ручка Age вимкнена)
        check (dAmt > -30.0f && (! ages || dAge > -40.0f), String (dec::formatName (f - 1)).paddedRight (' ', 13) + " Amount changes the sound by " + String (dAmt, 0) + " dB, Age by " + String (dAge, 0) + " dB");
    }

    std::cout << (failures == 0 ? "\nALL CONTROLS OK" : "\n" + String (failures) + " PROBLEMS") << std::endl;
    return 0;
}
