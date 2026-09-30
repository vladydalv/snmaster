// Аудит октавера на реалістичних гітарних/басових нотах (Karplus-Strong), а не на синусах.
#include "../Stomp/PluginProcessor.h"
#include <iostream>
#include <random>

using namespace juce;
using namespace StompIDs;

constexpr double fs = 48000.0;

static void set (SpacenerdStompProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

/** Щипок струни: шумовий імпульс у лінії затримки з усереднювальним фільтром + гребінь звукознімача. */
static void pluck (AudioBuffer<float>& b, double f0, double start, float amp, unsigned seed, double decay = 0.996)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    const double N = fs / f0;
    const int len = (int) N + 2;
    std::vector<float> line ((size_t) len);
    for (auto& v : line) v = u (rng);
    // Злегка згладити збудження (медіатор середньої жорсткості)
    for (int k = 0; k < 3; ++k) for (int i = 1; i < len; ++i) line[(size_t) i] = 0.5f * (line[(size_t) i] + line[(size_t) i - 1]);
    const int s0 = (int) (start * fs);
    double pos = 0.0;
    std::vector<float> out;
    const int total = b.getNumSamples() - s0;
    float prev = 0.0f;
    int w = 0;
    std::vector<float> hist ((size_t) (int) (0.12 * N) + 2, 0.0f);   // звукознімач на 1/8 струни
    for (int i = 0; i < total; ++i)
    {
        const int r = w;
        const float y = line[(size_t) r];
        const int r1 = (r + 1) % len;
        float nv = (float) decay * 0.5f * (y + line[(size_t) r1]);
        line[(size_t) w] = nv;
        w = r1;
        // дробова частина затримки ігнорується (для аудиту висоти достатньо len ≈ N)
        const size_t hp = (size_t) i % hist.size();
        const float pick = y - hist[hp];
        hist[hp] = y;
        const float v = amp * pick;
        for (int c = 0; c < 2; ++c) b.addSample (c, s0 + i, v);
        (void) prev; (void) pos;
    }
}

static float band (const AudioBuffer<float>& b, double f, double from, double to)
{
    const int s = (int) (from * fs), e = std::min (b.getNumSamples(), (int) (to * fs));
    double re = 0, im = 0;
    for (int i = s; i < e; ++i) { const double ph = MathConstants<double>::twoPi * f * i / fs; re += b.getSample (0, i) * std::cos (ph); im += b.getSample (0, i) * std::sin (ph); }
    return (float) (2.0 * std::sqrt (re * re + im * im) / (e - s));
}

static AudioBuffer<float> run (const AudioBuffer<float>& in, std::function<void (SpacenerdStompProcessor&)> setup)
{
    SpacenerdStompProcessor p;
    set (p, driveOn, 0);
    setup (p);
    p.setRateAndBufferSizeDetails (fs, 256);
    p.prepareToPlay (fs, 256);
    AudioBuffer<float> out (in);
    MidiBuffer m;
    for (int pos = 0; pos < out.getNumSamples(); pos += 256)
    {
        AudioBuffer<float> chunk (out.getArrayOfWritePointers(), 2, pos, std::min (256, out.getNumSamples() - pos));
        p.processBlock (chunk, m);
    }
    return out;
}

static float rms (const AudioBuffer<float>& b, double from, double to)
{
    const int s = (int) (from * fs), e = std::min (b.getNumSamples(), (int) (to * fs));
    return sn::gainToDb (b.getRMSLevel (0, s, e - s));
}

int main()
{
    ScopedJuceInitialiser_GUI init;
    struct Note { const char* name; std::vector<double> f; };
    const std::vector<Note> notes {
        { "E2 (low E)", { 82.41 } }, { "A2", { 110.0 } }, { "G3", { 196.0 } }, { "E1 bass", { 41.2 } }, { "A1 bass", { 55.0 } },
        { "E5 power chord", { 82.41, 123.47, 164.81 } }, { "open E major", { 82.41, 123.47, 164.81, 207.65, 246.94, 329.63 } } };

    for (const auto& nt : notes)
    {
        AudioBuffer<float> in (2, (int) (fs * 2.0)); in.clear();
        unsigned seed = 1;
        for (double f : nt.f) pluck (in, f, 0.05, 0.25f / (float) std::sqrt ((double) nt.f.size()), seed++);
        const float inR = rms (in, 0.1, 1.0);
        float oddE = 0.0f;
        for (double f : nt.f) for (int k = 1; k <= 5; k += 2) { const float a = band (in, f * k, 0.1, 1.0); oddE += a * a; }
        std::cout << "\n" << nt.name << "  input " << String (inR, 1) << " dB rms; ideal -1 octave comps " << String (sn::gainToDb (std::sqrt (oddE)) - inR, 1) << " dB" << std::endl;

        for (int engine : { 0, 100 })
            for (int voice : { 0, 1, 2 })
            {
                auto out = run (in, [&] (SpacenerdStompProcessor& p)
                {
                    set (p, octOn, 1); set (p, octDry, 0); set (p, octChar, (float) engine); set (p, octTone, 8000);
                    set (p, sub1, voice == 0 ? 100.0f : 0.0f); set (p, sub2, voice == 1 ? 100.0f : 0.0f); set (p, octUp, voice == 2 ? 100.0f : 0.0f);
                });
                const double ratio = voice == 0 ? 0.5 : voice == 1 ? 0.25 : 2.0;
                // Нові (октавні) складові: для −1 — непарні кратні f0/2; −2 — непарні кратні f0/4; +1 — 2·f0 (у сухому вона теж є)
                float newE = 0.0f;
                String comps;
                for (double f : nt.f)
                    for (int k = 1; k <= 5; k += 2)
                    {
                        const double fq = f * ratio * k;
                        if (voice == 2 && k > 1) break;
                        const float a = band (out, fq, 0.1, 1.0);
                        newE += a * a;
                        if (nt.f.size() == 1) comps << String (fq, 0) << "Hz " << String (sn::gainToDb (a), 0) << "  ";
                    }
                const float outR = rms (out, 0.1, 1.0);
                std::cout << "  " << (engine == 0 ? "Poly  " : "Analog") << " " << (voice == 0 ? "-1" : voice == 1 ? "-2" : "+1")
                          << ": out " << String (outR - inR, 1) << " dB re input, octave comps " << String (sn::gainToDb (std::sqrt (newE)) - inR, 1)
                          << " dB  | " << comps << std::endl;
            }
    }
    // Навантаження CPU: 10 с стерео, октавер + усе інше
    {
        AudioBuffer<float> in (2, (int) (fs * 10.0)); in.clear();
        for (int k = 0; k < 10; ++k) pluck (in, 82.41 * (1 + k % 3), 0.05 + k, 0.2f, (unsigned) k + 20);
        auto t0 = Time::getMillisecondCounterHiRes();
        run (in, [] (SpacenerdStompProcessor& p) { set (p, octOn, 1); set (p, driveOn, 1); set (p, modOn, 1); set (p, echoOn, 1); });
        const double all = Time::getMillisecondCounterHiRes() - t0;
        t0 = Time::getMillisecondCounterHiRes();
        run (in, [] (SpacenerdStompProcessor& p) { set (p, octOn, 0); set (p, driveOn, 1); set (p, modOn, 1); set (p, echoOn, 1); });
        const double noOct = Time::getMillisecondCounterHiRes() - t0;
        std::cout << "CPU (10 s audio): all " << String (all, 0) << " ms, without octave " << String (noOct, 0) << " ms (incl. prepare/calibration)" << std::endl;
    }

    // Пресети з драйвом: чи чути суб після фузу
    {
        AudioBuffer<float> in (2, (int) (fs * 2.0)); in.clear();
        pluck (in, 82.41, 0.05, 0.25f, 7);
        for (int preset : { 15, 16 })
            for (int oct : { 1, 0 })
            {
                auto out = run (in, [&] (SpacenerdStompProcessor& p) { p.setCurrentProgram (preset); set (p, octOn, (float) oct); });
                SpacenerdStompProcessor tmp;
                std::cout << "preset " << tmp.getProgramName (preset) << (oct ? " ON " : " OFF") << ": 41 Hz " << String (sn::gainToDb (band (out, 41.2, 0.1, 1.0)), 1)
                          << ", 124 Hz " << String (sn::gainToDb (band (out, 123.6, 0.1, 1.0)), 1) << ", total " << String (rms (out, 0.1, 1.0), 1) << std::endl;
            }
    }
    return 0;
}
