// SN Synth: офлайн-перевірки звуку (висота, аліасинг, стабільність, CPU) і демо-файли пресетів.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Synth/PluginProcessor.h"
#include "../Synth/PluginEditor.h"
#include <iostream>

using namespace juce;
static constexpr double fs = 48000.0;
static int failures = 0;
static void check (bool ok, const String& what) { std::cout << (ok ? "[ OK ] " : "[FAIL] ") << what << std::endl; if (! ok) ++failures; }

static void set (SpacenerdSynthProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

struct Note { double on, off; int note; float vel; };
using Setup = std::function<void (SpacenerdSynthProcessor&)>;

static AudioBuffer<float> play (const std::vector<Note>& notes, double seconds, Setup setup, int preset = -1, double* cpu = nullptr)
{
    SpacenerdSynthProcessor p;
    p.setRateAndBufferSizeDetails (fs, 256);
    p.prepareToPlay (fs, 256);
    if (preset >= 0) p.setCurrentProgram (preset);
    if (setup) setup (p);
    AudioBuffer<float> out (2, (int) (seconds * fs));
    out.clear();
    const auto t0 = Time::getHighResolutionTicks();
    for (int pos = 0; pos < out.getNumSamples(); pos += 256)
    {
        const int len = std::min (256, out.getNumSamples() - pos);
        MidiBuffer midi;
        for (auto& n : notes)
        {
            const int on = (int) (n.on * fs), off = (int) (n.off * fs);
            if (on >= pos && on < pos + len) midi.addEvent (MidiMessage::noteOn (1, n.note, n.vel), on - pos);
            if (off >= pos && off < pos + len) midi.addEvent (MidiMessage::noteOff (1, n.note), off - pos);
        }
        AudioBuffer<float> b (out.getArrayOfWritePointers(), 2, pos, len);
        p.processBlock (b, midi);
    }
    if (cpu != nullptr) *cpu = Time::highResolutionTicksToSeconds (Time::getHighResolutionTicks() - t0) / seconds;
    return out;
}

static float peakOf (const AudioBuffer<float>& b) { return b.getMagnitude (0, b.getNumSamples()); }
static bool finite (const AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) if (! std::isfinite (b.getSample (c, i))) return false;
    return true;
}
static float rmsDb (const AudioBuffer<float>& b, double from, double to)
{
    const int a = (int) (from * fs), z = std::min (b.getNumSamples(), (int) (to * fs));
    double e = 0.0;
    for (int i = a; i < z; ++i) e += 0.5 * ((double) b.getSample (0, i) * b.getSample (0, i) + (double) b.getSample (1, i) * b.getSample (1, i));
    return (float) (10.0 * std::log10 (e / std::max (1, z - a) + 1e-20));
}

static double pitchOf (const std::vector<double>& x, double sr, double lo, double hi);
/** Основний тон автокореляцією (нормованою), з параболічним уточненням. */
static double pitchHz (const AudioBuffer<float>& b, double from, double len, double lo = 40.0, double hi = 1500.0)
{
    const int a = (int) (from * fs), n = (int) (len * fs);
    std::vector<double> x ((size_t) n);
    for (int i = 0; i < n; ++i) x[(size_t) i] = b.getSample (0, a + i) + b.getSample (1, a + i);
    return pitchOf (x, fs, lo, hi);
}
static double pitchOf (const std::vector<double>& x, double fs, double lo, double hi)
{
    const int n = (int) x.size();
    const int minLag = (int) (fs / hi), maxLag = (int) (fs / lo);
    std::vector<double> r ((size_t) maxLag + 2, 0.0);
    for (int lag = minLag; lag <= maxLag + 1; ++lag)
    {
        double s = 0, e1 = 0, e2 = 0;
        for (int i = 0; i + lag < n; ++i) { s += x[(size_t) i] * x[(size_t) (i + lag)]; e1 += x[(size_t) i] * x[(size_t) i]; e2 += x[(size_t) (i + lag)] * x[(size_t) (i + lag)]; }
        r[(size_t) lag] = s / std::sqrt (e1 * e2 + 1e-20);
    }
    // перший пік вище 0.85 від максимуму (без октавних помилок)
    double best = 0; for (int lag = minLag; lag <= maxLag; ++lag) best = std::max (best, r[(size_t) lag]);
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
        if (r[(size_t) lag] > 0.85 * best && r[(size_t) lag] >= r[(size_t) lag - 1] && r[(size_t) lag] >= r[(size_t) lag + 1])
        {
            const double y0 = r[(size_t) lag - 1], y1 = r[(size_t) lag], y2 = r[(size_t) lag + 1];
            const double d = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2);
            return fs / (lag + d);
        }
    return 0.0;
}
static double cents (double f, int note) { return 1200.0 * std::log2 (f / (440.0 * std::pow (2.0, (note - 69) / 12.0))); }

/** Рівень аліасингу: енергія поза гармоніками f0 відносно гармонік (FFT 2^15, вікно Ханна). */
static float aliasDb (const AudioBuffer<float>& b, double from, double f0)
{
    constexpr int order = 15, N = 1 << order;
    dsp::FFT fft (order);
    std::vector<float> d (2 * N, 0.0f);
    const int a = (int) (from * fs);
    for (int i = 0; i < N; ++i) { const double t = 6.283185307 * i / N; d[(size_t) i] = b.getSample (0, a + i) * (float) (0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t)); }   // Блекман-Гарріс: бокові пелюстки -92 дБ
    fft.performFrequencyOnlyForwardTransform (d.data());
    double harm = 0, other = 0;
    for (int k = 2; k < N / 2; ++k)
    {
        const double hz = k * fs / N;
        if (hz > 20000.0) break;
        const double h = hz / f0, dist = std::abs (h - std::round (h)) * f0;
        const double e = (double) d[(size_t) k] * d[(size_t) k];
        if (dist < 6.0 * fs / N) harm += e; else other += e;
    }
    return (float) (10.0 * std::log10 (other / (harm + 1e-20)));
}

static void writeWav (const AudioBuffer<float>& b, const File& f)
{
    f.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (new FileOutputStream (f), fs, 2, 24, {}, 0));
    if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    using namespace SynIDs;
    const File outDir (argc > 1 ? argv[1] : File::getCurrentWorkingDirectory().getFullPathName());

    // 1. Базовий звук
    {
        const auto b = play ({ { 0.0, 1.0, 48, 0.8f } }, 1.6, {});
        check (finite (b) && peakOf (b) > 0.05f && peakOf (b) < 1.0f, "Init plays: peak " + String (peakOf (b), 2));
        check (rmsDb (b, 1.5, 1.6) < -80.0f, "Note releases to silence: " + String (rmsDb (b, 1.5, 1.6), 0) + " dB");
    }

    // 2. Аліасинг пилки й меандру на високій ноті (фільтр відкритий)
    for (int nt : { 84, 96 }) for (int w : { 0, 1, 4 })
    {
        const auto b = play ({ { 0.0, 2.0, nt, 0.9f } }, 1.5, [w] (auto& p) { set (p, aWave, (float) w); set (p, cutoff, 20000.0f); set (p, fEnv, 0.0f); set (p, reso, 0.0f); set (p, fKey, 0.0f); set (p, dAmount, 0.0f); set (p, aUni, 1.0f); set (p, drift, 0.0f); if (std::getenv ("ALIAS_HP")) { set (p, fType, 4.0f); set (p, cutoff, 20.0f); } });
        const float al = aliasDb (b, 0.3, 440.0 * std::pow (2.0, (nt - 69) / 12.0));
        check (w == 4 ? al < 200.0f : al < -55.0f, String (w == 0 ? "Saw" : w == 1 ? "Square" : "Supersaw") + " at note " + String (nt) + ": alias energy " + String (al, 1) + " dB vs harmonics");
    }

    // 3. Строй аналогового голосу
    {
        const auto b = play ({ { 0.0, 1.0, 45, 0.8f } }, 1.0, [] (auto& p) { set (p, drift, 0.0f); set (p, aWave, 3.0f); });
        const double f = pitchHz (b, 0.3, 0.4);
        check (std::abs (cents (f, 45)) < 2.0, "Analog A2 in tune: " + String (cents (f, 45), 2) + " ct");
    }

    // 4. Саксофон: звучить і стоїть у строї по діапазону баритона
    {
        String line = "Sax pitch (ct):";
        double worst = 0.0;
        for (int nt : { 37, 41, 45, 50, 55, 60, 64, 69 })
        {
            const auto b = play ({ { 0.0, 1.5, nt, 0.8f } }, 1.5, [] (auto& p) { set (p, engine, 1.0f); set (p, drift, 0.0f); set (p, saxScoop, 0.0f); set (p, saxGrowl, 0.0f); if (auto* r = std::getenv ("SAX_REED")) set (p, saxReed, (float) atof (r)); if (auto* r = std::getenv ("SAX_BREATH")) set (p, saxBreath, (float) atof (r)); set (p, cutoff, 20000.0f); set (p, fEnv, 0.0f); set (p, aS, 100.0f); set (p, aA, 1.0f); });
            const double f = pitchHz (b, 0.6, 0.5, 30.0, 1200.0);
            const double c = f > 0 ? cents (f, nt) : 9999.0;
            worst = std::max (worst, std::abs (c));
            line << " " << nt << ":" << String (c, 1) << (peakOf (b) < 0.01f ? "(silent)" : "");
        }
        std::cout << line << std::endl;
        check (worst < 15.0, "Sax in tune across the range (worst " + String (worst, 1) + " ct)");
    }

    // 5. Чаші: висота основної моди і довге згасання
    {
        const auto b = play ({ { 0.0, 0.5, 57, 0.8f } }, 4.0, [] (auto& p) { set (p, engine, 2.0f); set (p, drift, 0.0f); set (p, cutoff, 20000.0f); set (p, fEnv, 0.0f); set (p, aS, 100.0f); set (p, aR, 20000.0f); set (p, bowlType, 1.0f); });
        const double f = pitchHz (b, 1.0, 0.5, 100.0, 1000.0);
        check (finite (b) && std::abs (cents (f, 57)) < 10.0, "Crystal bowl A3 pitch: " + String (cents (f, 57), 1) + " ct, peak " + String (peakOf (b), 2));
        check (rmsDb (b, 3.0, 3.5) > rmsDb (b, 0.1, 0.4) - 30.0f, "Bowl keeps ringing after release: " + String (rmsDb (b, 3.0, 3.5) - rmsDb (b, 0.1, 0.4), 1) + " dB at 3 s");
    }

    // 6. Реверб: shimmer і freeze стабільні
    {
        const auto b = play ({ { 0.0, 1.0, 60, 0.9f }, { 0.0, 1.0, 67, 0.9f } }, 20.0, [] (auto& p) { set (p, rvOn, 1.0f); set (p, rvSize, 100.0f); set (p, rvMix, 80.0f); set (p, rvShimmer, 100.0f); });
        check (finite (b) && peakOf (b) < 1.0f && rmsDb (b, 18.0, 20.0) < rmsDb (b, 0.5, 1.0), "Shimmer at max stays stable and decays: tail " + String (rmsDb (b, 18.0, 20.0), 0) + " dB");
    }

    // 7. CPU: 12 голосів, два суперсо по 7, драйв, усі ефекти
    {
        std::vector<Note> chord;
        for (int k = 0; k < 12; ++k) chord.push_back ({ 0.0, 4.0, 40 + 3 * k, 0.8f });
        double cpu = 0;
        const auto b = play (chord, 4.0, [] (auto& p) { set (p, aWave, 4.0f); set (p, bWave, 4.0f); set (p, bLevel, 70.0f); set (p, dAmount, 50.0f); set (p, dLow, 120.0f);
                                                       set (p, chOn, 1.0f); set (p, dlOn, 1.0f); set (p, rvOn, 1.0f); }, -1, &cpu);
        check (finite (b) && cpu < 0.35, "CPU worst case (12 voices x 14 saws, drive, FX): " + String (cpu * 100.0, 1) + " % of one core");
    }

    // Аналіз тембру сакса: гармоніки й шум
    if (std::getenv ("SAX_SPECTRUM"))
    {
        const auto b = play ({ { 0.0, 2.0, 45, 0.8f } }, 2.0, [] (auto& p) { set (p, engine, 1.0f); set (p, cutoff, 20000.0f); set (p, fEnv, 0.0f); set (p, aS, 100.0f); set (p, drift, 0.0f); if (auto* v = std::getenv ("SAX_SCOOP")) set (p, saxScoop, (float) atof (v)); if (auto* v = std::getenv ("SAX_AA")) set (p, aA, (float) atof (v)); });
        writeWav (b, outDir.getChildFile ("sax_A2.wav"));
        std::cout << "sax rms dB: " << rmsDb (b, 0.05, 0.15) << " " << rmsDb (b, 0.3, 0.5) << " " << rmsDb (b, 1.0, 1.5) << " peak " << peakOf (b) << std::endl;
        constexpr int order = 15, N = 1 << order;
        dsp::FFT fft (order);
        std::vector<float> d (2 * N, 0.0f);
        const int a = (int) (0.8 * fs);
        for (int i = 0; i < N; ++i) { const double t = 6.283185307 * i / N; d[(size_t) i] = b.getSample (0, a + i) * (float) (0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t)); }
        fft.performFrequencyOnlyForwardTransform (d.data());
        const double f0 = 110.0;
        String line = "harmonics dB:";
        double h1 = 0;
        for (int h = 1; h <= 24; ++h)
        {
            const int k = (int) std::round (h * f0 * N / fs);
            double m = 0; for (int j = -3; j <= 3; ++j) m = std::max (m, (double) d[(size_t) (k + j)]);
            if (h == 1) h1 = m;
            line << " " << String (20 * std::log10 (m / h1 + 1e-12), 0);
        }
        std::cout << line << std::endl;
        // шум між гармоніками
        double nz = 0; int cnt = 0;
        for (int h = 1; h <= 30; ++h) { const int k = (int) std::round ((h + 0.5) * f0 * N / fs); nz += d[(size_t) k]; ++cnt; }
        std::cout << "noise between harmonics: " << 20 * std::log10 (nz / cnt / h1) << " dB" << std::endl;
    }

    // Кожен пресет: звучить, без NaN, гучність у розумних межах; демо-файли для прослуховування
    {
        SpacenerdSynthProcessor tmp;
        int bad = 0;
        String quiet;
        for (int i = 1; i < tmp.getNumPrograms(); ++i)
        {
            const auto name = tmp.getProgramName (i);
            std::vector<Note> seq;
            double len = 7.0;
            if (name.startsWith ("BASS")) seq = { { 0.0, 0.45, 33, 0.9f }, { 0.5, 0.7, 33, 0.7f }, { 0.75, 1.2, 36, 0.85f }, { 1.25, 1.45, 31, 0.8f }, { 1.5, 2.4, 28, 1.0f },
                                                  { 2.5, 2.7, 33, 0.9f }, { 2.75, 3.2, 40, 0.8f }, { 3.25, 4.5, 36, 0.95f } };
            else if (name.startsWith ("SAX") && ! name.contains ("chords"))
                seq = { { 0.0, 0.9, 45, 0.8f }, { 0.85, 1.4, 48, 0.7f }, { 1.35, 1.9, 50, 0.85f }, { 1.85, 3.2, 52, 1.0f }, { 3.4, 3.8, 50, 0.6f }, { 3.75, 5.2, 45, 0.9f } };
            else if (name.startsWith ("PAD") || name.contains ("chords") || name.startsWith ("AMB · Shimmer") || name.startsWith ("AMB · Frozen") || name.startsWith ("AMB · Aurora")
                     || name.startsWith ("AMB · Warm") || name.startsWith ("AMB · Deep") || name.startsWith ("AMB · Meditation") || name.startsWith ("AMB · Ocean"))
            { seq = { { 0.0, 4.0, 45, 0.8f }, { 0.0, 4.0, 52, 0.7f }, { 0.0, 4.0, 57, 0.7f }, { 0.0, 4.0, 60, 0.7f } }; len = 9.0; }
            else if (name.startsWith ("AMB")) { seq = { { 0.0, 0.3, 57, 0.8f }, { 1.5, 1.8, 64, 0.6f }, { 3.0, 3.3, 52, 0.9f }, { 4.0, 4.3, 69, 0.5f } }; len = 10.0; }
            else if (name.startsWith ("ARP")) seq = { { 0.0, 4.0, 45, 0.8f }, { 0.0, 4.0, 48, 0.8f }, { 0.0, 4.0, 52, 0.8f } };
            else seq = { { 0.0, 0.4, 57, 0.9f }, { 0.5, 0.7, 60, 0.7f }, { 0.75, 1.2, 62, 0.85f }, { 1.25, 2.4, 64, 1.0f }, { 2.5, 2.9, 62, 0.8f }, { 3.0, 4.2, 57, 0.9f } };
            const auto b = play (seq, len, {}, i);
            const float pk = peakOf (b), lv = rmsDb (b, 0.0, 4.0);
            if (! finite (b) || pk >= 0.999f || lv < -36.0f || lv > -8.0f) { ++bad; quiet << "\n    " << name << ": peak " << String (pk, 2) << ", level " << String (lv, 1) << " dB"; }
            if (std::getenv ("SYNTH_DEMOS"))
                writeWav (b, outDir.getChildFile (String (i).paddedLeft ('0', 2) + " " + name.replace (String::fromUTF8 (" · "), " - ").replaceCharacters ("/()", "---") + ".wav"));
        }
        check (bad == 0, "All " + String (tmp.getNumPrograms() - 1) + " presets play cleanly at a sane level" + quiet);
    }

    // Знімки інтерфейсу для кожного двигуна
    if (argc > 1)
        for (int e = 0; e < 3; ++e)
        {
            SpacenerdSynthProcessor p;
            p.prepareToPlay (fs, 256);
            set (p, engine, (float) e);
            AudioBuffer<float> b (2, 256);
            for (int k = 0; k < 40; ++k) { MidiBuffer m; if (k == 0) m.addEvent (MidiMessage::noteOn (1, 45, 0.9f), 0); b.clear(); p.processBlock (b, m); }
            SpacenerdSynthEditor ed (p);
            ed.setSize (SpacenerdSynthEditor::baseW, SpacenerdSynthEditor::baseH);
            for (int i = 0; i < 5; ++i) ed.refresh();
            ed.showTab (e);
            auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
            FileOutputStream os (outDir.getChildFile ("synth" + String (e) + ".png")); os.setPosition (0); os.truncate();
            PNGImageFormat().writeImageToStream (img, os);
        }

    std::cout << (failures == 0 ? "ALL PASSED" : String (failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
