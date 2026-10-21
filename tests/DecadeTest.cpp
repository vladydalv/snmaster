#include <juce_audio_processors/juce_audio_processors.h>
#include "../Decade/PluginProcessor.h"
#include "../Decade/PluginEditor.h"
#include <iostream>

using namespace juce;

static int failures = 0;
static void check (bool ok, const String& what) { std::cout << (ok ? "[ OK ] " : "[FAIL] ") << what << std::endl; if (! ok) ++failures; }
static constexpr double fs = 48000.0, twoPi = MathConstants<double>::twoPi;

static void set (SpacenerdDecadeProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

/** Барабанний луп (бочка/малий/хет, трохи різні L і R), потім тиша для хвоста. */
static AudioBuffer<float> kit (double sec, double silence = 1.5)
{
    AudioBuffer<float> b (2, (int) ((sec + silence) * fs)); b.clear();
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

static AudioBuffer<float> run (SpacenerdDecadeProcessor& p, const AudioBuffer<float>& in)
{
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

static float rmsDb (const AudioBuffer<float>& b, double t0, double t1)
{
    const int a = (int) (t0 * fs), n = std::min (b.getNumSamples(), (int) (t1 * fs)) - a;
    double e = 0.0;
    for (int ch = 0; ch < 2; ++ch) { const float r = b.getRMSLevel (ch, a, n); e += r * r; }
    return 10.0f * std::log10 ((float) (e / 2.0) + 1.0e-20f);
}

static float midDb (const AudioBuffer<float>& b)
{
    sn::Biquad h, l; h.setHighPass (fs, 300.0, 0.707); l.setLowPass (fs, 3000.0, 0.707);
    double e = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i) { const float y = l.process (h.process (b.getSample (0, i))); e += y * y; }
    return 10.0f * std::log10 ((float) (e / b.getNumSamples()) + 1.0e-20f);
}

/** Енергія вище hz крутим фільтром (8-й порядок) — щоб не мішала нижня смуга. */
static float steepHighDb (const AudioBuffer<float>& b, double hz)
{
    std::array<sn::Biquad, 4> f; for (auto& x : f) x.setHighPass (fs, hz, 0.707);
    double e = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i) { float y = b.getSample (0, i); for (auto& x : f) y = x.process (y); e += (double) y * y; }
    return 10.0f * std::log10 ((float) (e / b.getNumSamples()) + 1.0e-20f);
}

static float bandDb (const AudioBuffer<float>& b, double hz, bool high)
{
    sn::Biquad f; if (high) f.setHighPass (fs, hz, 0.707); else f.setLowPass (fs, hz, 0.707);
    double e = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i) { const float y = f.process (b.getSample (0, i)); e += y * y; }
    return 10.0f * std::log10 ((float) (e / b.getNumSamples()) + 1.0e-20f);
}

int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI init;
    const auto loop = kit (6.0);
    const float inDb = rmsDb (loop, 0.0, 6.0);

    // 1. Нейтрально (Authenticity 0, носій Stream з Amount 0) = вхід із затримкою на латентність
    {
        SpacenerdDecadeProcessor p;
        set (p, DecIDs::auth, 0.0f); set (p, DecIDs::format, 6.0f); set (p, DecIDs::fmtAmount, 0.0f);
        auto out = run (p, loop);
        const int L = p.getLatencySamples();
        double e = 0.0, ref = 0.0;
        for (int i = L; i < loop.getNumSamples(); ++i) { const float d = out.getSample (0, i) - loop.getSample (0, i - L); e += d * d; ref += loop.getSample (0, i - L) * loop.getSample (0, i - L); }
        const float nullDb = 10.0f * std::log10 ((float) (e / ref) + 1.0e-20f);
        check (nullDb < -40.0f, "Authenticity 0 is transparent: null " + String (nullDb, 1) + " dB (latency " + String (L) + ")");
    }

    // 2. Кожен пресет: рівень у межах розумного, без вибухів
    {
        const auto& ps = getDecadePresets();
        String worst;
        float maxDev = 0.0f;
        bool finite = true;
        for (int i = 0; i < (int) ps.size(); ++i)
        {
            SpacenerdDecadeProcessor p;
            p.setCurrentProgram (i);
            auto out = run (p, loop);
            for (int ch = 0; ch < 2; ++ch) finite = finite && std::isfinite (out.getMagnitude (ch, 0, out.getNumSamples()));
            const float dev = rmsDb (out, 0.5, 6.0) - inDb;
            if (std::abs (dev) > maxDev) { maxDev = std::abs (dev); worst = String (ps[(size_t) i].name) + " " + String (dev, 1) + " dB"; }
        }
        check (finite && maxDev < 9.0f, String ((int) ps.size()) + " presets run; largest level change: " + worst);
    }

    // 2b. Рушій кімнати: RT60 по смугах, щільність без «металу», стерео, стабільність, gated, ранні відбиття
    {
        auto impulseResponse = [] (const dec::RoomEngine::Params& prm, double seconds)
        {
            dec::RoomEngine e; e.prepare (fs);
            const int n = (int) (seconds * fs);
            AudioBuffer<float> ir (2, n); ir.clear();
            ir.setSample (0, 0, 1.0f); ir.setSample (1, 0, 1.0f);
            for (int pos = 0; pos < n; pos += 512)
            {
                const int len = std::min (512, n - pos);
                e.process (ir.getWritePointer (0, pos), ir.getWritePointer (1, pos), len, prm, 1.0f);
            }
            ir.setSample (0, 0, 0.0f); ir.setSample (1, 0, 0.0f);   // прибрати сухий імпульс
            return ir;
        };
        auto rt60 = [] (const AudioBuffer<float>& ir, double lo, double hi)
        {
            sn::Biquad a, b; a.setHighPass (fs, lo, 0.707); b.setLowPass (fs, hi, 0.707);
            std::vector<double> e2 ((size_t) ir.getNumSamples());
            for (int i = 0; i < ir.getNumSamples(); ++i) { const float y = b.process (a.process (ir.getSample (0, i))); e2[(size_t) i] = (double) y * y; }
            for (int i = (int) e2.size() - 2; i >= 0; --i) e2[(size_t) i] += e2[(size_t) i + 1];     // інтеграл Шредера
            const double e0 = e2[0];
            int t5 = -1, t25 = -1;
            for (int i = 0; i < (int) e2.size(); ++i)
            {
                const double db = 10.0 * std::log10 (e2[(size_t) i] / e0 + 1e-30);
                if (t5 < 0 && db <= -5.0) t5 = i;
                if (t25 < 0 && db <= -25.0) { t25 = i; break; }
            }
            return t5 >= 0 && t25 > t5 ? 3.0 * (t25 - t5) / fs : 0.0;
        };

        dec::RoomEngine::Params live; live.type = dec::Live; live.size = 1.2f; live.rtMid = 1.4f; live.dampHz = 7000.0f; live.er = 0.7f; live.predelayMs = 12.0f;
        const auto ir = impulseResponse (live, 4.0);
        const double rtLo = rt60 (ir, 80, 250), rtMid = rt60 (ir, 500, 2000), rtHi = rt60 (ir, 5000, 12000);
        check (std::abs (rtMid - 1.4) < 0.3 && rtHi < rtMid * 0.85 && rtLo > rtMid * 0.95,
               "Room RT60 by band (target mid 1.4 s): low " + String (rtLo, 2) + ", mid " + String (rtMid, 2) + ", high " + String (rtHi, 2) + " s");

        // «Метал»: у пізньому хвості не має бути сильних резонансів
        {
            constexpr int order = 14, size = 1 << order;
            juce::dsp::FFT fft (order);
            std::vector<float> buf ((size_t) size * 2, 0.0f);
            const int start = (int) (0.25 * fs);
            for (int i = 0; i < size; ++i) buf[(size_t) i] = ir.getSample (0, start + i) * (0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * i / (size - 1)));
            fft.performFrequencyOnlyForwardTransform (buf.data());
            std::vector<float> band;
            for (int k = (int) (500.0 * size / fs); k < (int) (5000.0 * size / fs); ++k) band.push_back (buf[(size_t) k]);
            // Згладжена огинаюча (1/3 октави) проти піків: скільки піків стирчить над локальним рівнем
            float maxOver = 0.0f;
            for (size_t k = 8; k + 8 < band.size(); ++k)
            {
                double local = 0.0; for (size_t j = k - 8; j <= k + 8; ++j) local += band[j] * band[j];
                local = std::sqrt (local / 17.0);
                maxOver = std::max (maxOver, Decibels::gainToDecibels ((float) (band[k] / (local + 1e-12))));
            }
            check (maxOver < 12.0f, "Tail has no metallic ringing: strongest peak " + String (maxOver, 1) + " dB over its neighbourhood");
        }
        // Стерео хвоста
        {
            double lr = 0, ll = 0, rr = 0;
            for (int i = (int) (0.1 * fs); i < (int) (1.0 * fs); ++i) { const double l = ir.getSample (0, i), r = ir.getSample (1, i); lr += l * r; ll += l * l; rr += r * r; }
            const double corr = lr / std::sqrt (ll * rr + 1e-30);
            check (std::abs (corr) < 0.3, "Tail is wide (L/R correlation " + String (corr, 2) + ")");
        }
        // Стабільність на довгому залі
        {
            dec::RoomEngine::Params hall; hall.type = dec::Hall; hall.size = 2.0f; hall.rtMid = 6.0f; hall.dampHz = 12000.0f; hall.er = 0.4f;
            const auto h = impulseResponse (hall, 20.0);
            const float early = h.getRMSLevel (0, (int) (0.1 * fs), (int) fs), late = h.getRMSLevel (0, (int) (18.0 * fs), (int) fs);
            check (std::isfinite (late) && Decibels::gainToDecibels (late / early) < -60.0f,
                   "Stable at 6 s RT: level after 18 s " + String (Decibels::gainToDecibels (late / early), 1) + " dB");
        }
        // Ранні відбиття: у залі перше відбиття пізніше, ніж у студії
        {
            auto firstReflection = [&] (int type, float size)
            {
                dec::RoomEngine::Params prm; prm.type = type; prm.size = size; prm.rtMid = 0.3f; prm.er = 1.0f; prm.predelayMs = 0.0f;
                const auto r = impulseResponse (prm, 0.3);
                // Центр ваги ранньої енергії (перші 150 мс): у великому залі відбиття приходять пізніше
                double num = 0.0, den = 0.0;
                for (int i = 1; i < (int) (0.15 * fs); ++i) { const double e = (double) r.getSample (0, i) * r.getSample (0, i); num += e * i; den += e; }
                return den > 0 ? num / den / fs * 1000.0 : 0.0;
            };
            const double studio = firstReflection (dec::Studio, 1.0f), hall = firstReflection (dec::Hall, 1.0f);
            check (hall > studio * 1.5, "Early reflections centre: studio " + String (studio, 1) + " ms, hall " + String (hall, 1) + " ms");
        }
    }

    // 3. Вісь Room: хвіст після останнього удару
    {
        auto tailAt = [&] (float roomAxis, int type = 0)
        {
            SpacenerdDecadeProcessor p;
            set (p, DecIDs::year, 1972.0f); set (p, DecIDs::room, roomAxis); set (p, DecIDs::roomType, (float) type);
            set (p, DecIDs::format, 6.0f); set (p, DecIDs::fmtAmount, 0.0f);      // без шуму носія — слухаємо лише кімнату
            auto out = run (p, loop);
            return std::make_pair (rmsDb (out, 6.25, 6.6), rmsDb (out, 6.0, 6.3));
        };
        const auto dry = tailAt (0.0f), huge = tailAt (100.0f);
        check (huge.first - dry.first > 15.0f, "Room axis: tail after the last hit " + String (dry.first, 1) + " dB (dry) -> " + String (huge.first, 1) + " dB (huge)");
        const auto live = tailAt (90.0f, 2), gated = tailAt (90.0f, 6);
        check (live.second - gated.second > 10.0f, "Gated room is cut after the hit: late tail live " + String (live.second, 1) + " vs gated " + String (gated.second, 1) + " dB");
    }

    // 4. Епоха змінює тембр і стерео
    {
        auto at = [&] (float yr, float& hi, float& corr)
        {
            SpacenerdDecadeProcessor p;
            set (p, DecIDs::year, yr); set (p, DecIDs::room, 0.0f); set (p, DecIDs::format, 6.0f); set (p, DecIDs::fmtAmount, 0.0f);
            auto out = run (p, loop);
            hi = bandDb (out, 8000.0, true) - midDb (out);
            double lr = 0, ll = 0, rr = 0;
            for (int i = 0; i < out.getNumSamples(); ++i) { const double l = out.getSample (0, i), r = out.getSample (1, i); lr += l * r; ll += l * l; rr += r * r; }
            corr = (float) (lr / std::sqrt (ll * rr + 1e-30));
        };
        float h50, c50, h12, c12;
        at (1950.0f, h50, c50); at (2012.0f, h12, c12);
        check (h12 - h50 > 10.0f, "1950 is darker than 2012: highs vs mids " + String (h50, 1) + " vs " + String (h12, 1) + " dB");
        double lr = 0, ll = 0, rr = 0;
        for (int i = 0; i < loop.getNumSamples(); ++i) { const double l = loop.getSample (0, i), r = loop.getSample (1, i); lr += l * r; ll += l * l; rr += r * r; }
        const float cin = (float) (lr / std::sqrt (ll * rr + 1e-30));
        check (c50 > 0.9995f && c12 < c50 - 0.0005f, "1950 is mono (corr " + String (c50, 4) + "), 2012 keeps the stereo (" + String (c12, 4) + ", input " + String (cin, 4) + ")");
    }

    // 5. Носії працюють (Acetate вузька смуга і моно)
    {
        SpacenerdDecadeProcessor p;
        set (p, DecIDs::auth, 0.0f); set (p, DecIDs::format, 1.0f); set (p, DecIDs::fmtAmount, 100.0f);
        auto out = run (p, loop);
        const float hi = (bandDb (out, 8000.0, true) - midDb (out)) - (bandDb (loop, 8000.0, true) - midDb (loop));
        check (hi < -15.0f, "Acetate cuts the top: " + String (hi, 1) + " dB above 8 kHz");
        for (int f = 1; f <= 6; ++f)
        {
            SpacenerdDecadeProcessor q;
            set (q, DecIDs::format, (float) f); set (q, DecIDs::fmtAmount, 100.0f); set (q, DecIDs::fmtAge, 100.0f);
            auto o = run (q, loop);
            check (std::isfinite (o.getMagnitude (0, 0, o.getNumSamples())) && o.getMagnitude (0, 0, o.getNumSamples()) < 4.0f,
                   String ("Format ") + dec::formatName (f - 1) + " worn: peak " + String (sn::gainToDb (o.getMagnitude (0, 0, o.getNumSamples())), 1) + " dBFS");
        }
    }

    // 5b. MP3: справжній кодек — без втрат при Amount 0, рівень зберігає, верх обрізає, пре-ехо перед ударом
    {
        auto through = [&] (float amount, const AudioBuffer<float>& in)
        {
            SpacenerdDecadeProcessor p;
            set (p, DecIDs::auth, 0.0f); set (p, DecIDs::format, 5.0f); set (p, DecIDs::fmtAmount, amount); set (p, DecIDs::fmtAge, 0.0f);
            auto out = run (p, in);
            return std::make_pair (out, p.getLatencySamples());
        };
        const auto [clean, L] = through (0.0f, loop);
        double e = 0.0, ref = 0.0;
        for (int i = L; i < loop.getNumSamples(); ++i) { const float d = clean.getSample (0, i) - loop.getSample (0, i - L); e += d * d; ref += (double) loop.getSample (0, i - L) * loop.getSample (0, i - L); }
        const float nullDb = 10.0f * std::log10 ((float) (e / ref) + 1e-20f);
        const auto lossy = through (75.0f, loop).first;
        const float lvl = rmsDb (lossy, 0.5, 6.0) - inDb;
        const float hi = steepHighDb (lossy, 17500.0) - steepHighDb (loop, 17500.0);
        check (nullDb < -90.0f && std::abs (lvl) < 1.5f && hi < -30.0f,
               "MP3 codec: lossless at Amount 0 (null " + String (nullDb, 0) + " dB); at 75 % level " + String (lvl, 1) + " dB, above 17 kHz " + String (hi, 0) + " dB");
        // Пре-ехо: тиша, потім удар — шум квантування з'являється ДО удару (у межах кадру)
        AudioBuffer<float> hitBuf (2, (int) (2.0 * fs)); hitBuf.clear();
        Random rnd (5);
        const int at = (int) (1.0 * fs);
        for (int i = 0; i < (int) (0.2 * fs); ++i) { const float v = 0.5f * (float) std::exp (-i / (0.03 * fs)) * (rnd.nextFloat() * 2 - 1); hitBuf.setSample (0, at + i, v); hitBuf.setSample (1, at + i, v); }
        const auto [pre, Lp] = through (100.0f, hitBuf);
        const float before = rmsDb (pre, (at + Lp - (int) (0.015 * fs)) / fs, (at + Lp - (int) (0.001 * fs)) / fs);
        check (before > -70.0f, "MP3 pre-echo before the hit: " + String (before, 1) + " dB");
    }

    // 5c. Вініл і касета: фізика носія
    {
        auto fmtRun = [&] (int fmtChoice, float amount, float age, const AudioBuffer<float>& in)
        {
            SpacenerdDecadeProcessor p;
            set (p, DecIDs::auth, 0.0f); set (p, DecIDs::format, (float) fmtChoice); set (p, DecIDs::fmtAmount, amount); set (p, DecIDs::fmtAge, age);
            return run (p, in);
        };
        // Тріск: на тиші порахувати клацання, у старої платівки їх значно більше
        AudioBuffer<float> silence (2, (int) (6.0 * fs)); silence.clear();
        auto clicks = [&] (float age)
        {
            const auto o = fmtRun (2, 100.0f, age, silence);
            int n = 0; bool inClick = false;
            for (int i = (int) fs; i < o.getNumSamples(); ++i)
            {
                const bool c = std::abs (o.getSample (0, i)) > 0.01f;
                if (c && ! inClick) ++n;
                inClick = c;
            }
            return n;
        };
        const int fresh = clicks (10.0f), worn = clicks (90.0f);
        check (worn > 3 * std::max (1, fresh), "Vinyl crackle grows with age: " + String (fresh) + " clicks (new) -> " + String (worn) + " (worn) in 5 s");

        // Вініл: низ моно — протифазний 60 Гц слабне відносно синфазного 1 кГц у тому ж сигналі
        AudioBuffer<float> mixSig (2, (int) (3.0 * fs));
        for (int i = 0; i < mixSig.getNumSamples(); ++i)
        {
            const float lo = 0.2f * (float) std::sin (twoPi * 60.0 * i / fs), mid = 0.1f * (float) std::sin (twoPi * 1000.0 * i / fs);
            mixSig.setSample (0, i, mid + lo); mixSig.setSample (1, i, mid - lo);
        }
        auto tone = [&] (const AudioBuffer<float>& b, double hz)
        {
            sn::Biquad f; f.setBandPass (fs, hz, 5.0);
            double e = 0.0;
            for (int i = (int) fs; i < b.getNumSamples(); ++i) { const float y = f.process (b.getSample (0, i)); e += (double) y * y; }
            return 10.0f * std::log10 ((float) e + 1e-20f);
        };
        const auto vo = fmtRun (2, 100.0f, 0.0f, mixSig);
        const float rel = (tone (vo, 60.0) - tone (mixSig, 60.0)) - (tone (vo, 1000.0) - tone (mixSig, 1000.0));
        check (rel < -10.0f, "Vinyl folds the low end to mono: anti-phase 60 Hz " + String (rel, 1) + " dB relative to the mids");

        // Касета: «горб» головки на ~60 Гц відносно 1 кГц
        auto toneLvl = [&] (double hz)
        {
            AudioBuffer<float> t (2, (int) (3.0 * fs));
            for (int i = 0; i < t.getNumSamples(); ++i) { const float v = 0.05f * (float) std::sin (twoPi * hz * i / fs); t.setSample (0, i, v); t.setSample (1, i, v); }
            const auto o = fmtRun (3, 100.0f, 0.0f, t);
            return rmsDb (o, 1.5, 3.0) - rmsDb (t, 1.5, 3.0);
        };
        const float bump = toneLvl (62.0) - toneLvl (1000.0);
        check (bump > 1.0f && bump < 4.0f, "Cassette head bump: +" + String (bump, 1) + " dB at 62 Hz vs 1 kHz");
    }

    // 6. Порядок модулів і стан
    {
        SpacenerdDecadeProcessor p;
        p.setOrder ({ dec::ModRoom, dec::ModTone, dec::ModGrit, dec::ModFilter, dec::ModSquash, dec::ModHit });
        set (p, DecIDs::year, 1984.0f);
        MemoryBlock mb; p.getStateInformation (mb);
        SpacenerdDecadeProcessor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
        check (q.getOrder()[0] == dec::ModRoom && std::abs (q.apvts.getRawParameterValue (DecIDs::year)->load() - 1984.0f) < 0.1f, "Rack order and year saved with the project");
        auto out = run (q, loop);
        check (std::isfinite (out.getMagnitude (0, 0, out.getNumSamples())), "Reordered rack runs");
    }

    // 7. CPU
    {
        SpacenerdDecadeProcessor p;
        set (p, DecIDs::room, 80.0f); set (p, DecIDs::format, 3.0f);
        const auto t0 = Time::getMillisecondCounterHiRes();
        run (p, loop);
        const double cpu = (Time::getMillisecondCounterHiRes() - t0) / (loop.getNumSamples() / fs * 1000.0) * 100.0;
        check (cpu < 15.0, "CPU: " + String (cpu, 1) + " % of one core");
    }

    if (argc > 1)
    {
        SpacenerdDecadeProcessor p;
        p.setCurrentProgram (9);
        run (p, loop);
        SpacenerdDecadeEditor ed (p);
        ed.setSize (SpacenerdDecadeEditor::baseW, SpacenerdDecadeEditor::baseH);
        for (int i = 0; i < 10; ++i) ed.refresh();
        auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.5f);
        FileOutputStream os (File (argv[1]).getChildFile ("decade.png")); os.setPosition (0); os.truncate();
        PNGImageFormat().writeImageToStream (img, os);
    }

    std::cout << (failures == 0 ? "ALL PASSED" : String (failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
