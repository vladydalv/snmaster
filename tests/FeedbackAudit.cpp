// Аудит Feedback на реалістичній DI-гітарі (Karplus–Strong: медіатор, згасання, вібрато, бенди, риф).
#include "../Feedback/PluginProcessor.h"
#include <iostream>
#include <random>

using namespace juce;
using namespace FbIDs;

constexpr double fs = 48000.0;

/** Струна Karplus–Strong з дробовою довжиною, медіатором і модуляцією висоти. */
struct KSString
{
    std::vector<float> buf = std::vector<float> (4096, 0.0f);
    int pos = 0;
    double freq = 110.0;
    float damp = 0.9965f;
    std::mt19937 rng { 3 };
    float lp = 0.0f;

    void pluck (double f, float amp)
    {
        freq = f;
        std::uniform_real_distribution<float> ud (-1.0f, 1.0f);
        const int L = (int) (fs / f);
        float z = 0.0f;
        for (int i = 0; i < L; ++i)
        {
            z = 0.6f * z + 0.4f * ud (rng);                     // медіатор: трохи м'якший за білий шум
            buf[(size_t) ((pos - L + i + 4096) % 4096)] += amp * z;
        }
    }
    void mute() { std::fill (buf.begin(), buf.end(), 0.0f); }

    float tick (double pitchMul = 1.0)
    {
        const double L = fs / (freq * pitchMul) - 0.5;
        const double rp = pos - L;
        const int i0 = (int) std::floor (rp);
        const float t = (float) (rp - i0);
        auto at = [&] (int k) { return buf[(size_t) ((k % 4096 + 4096) % 4096)]; };
        const float a = at (i0) + t * (at (i0 + 1) - at (i0));
        const float b = at (i0 - 1) + t * (at (i0) - at (i0 - 1));
        const float y = damp * 0.5f * (a + b);
        buf[(size_t) pos] = y;
        pos = (pos + 1) % 4096;
        lp = y;
        return y;
    }
};

struct Event { double t; double f; bool mute = false; };

struct RunResult { int starts; std::vector<std::pair<double, float>> bloom; std::vector<std::pair<double, float>> det; float outPeak; };

static RunResult run (SpacenerdFeedbackProcessor& p, double seconds, std::vector<Event> events,
                   std::function<double (double)> pitchMod = nullptr, std::function<void (double)> automation = nullptr,
                   float amp = 0.5f)
{
    p.setRateAndBufferSizeDetails (fs, 256);
    p.prepareToPlay (fs, 256);
    KSString s;
    size_t ev = 0;
    RunResult r {};
    MidiBuffer midi;
    const int total = (int) (seconds * fs);
    for (int pos = 0; pos < total; pos += 256)
    {
        if (automation) automation (pos / fs);
        AudioBuffer<float> b (1, 256);
        for (int i = 0; i < 256; ++i)
        {
            const double t = (pos + i) / fs;
            while (ev < events.size() && events[ev].t <= t)
            {
                if (events[ev].mute) s.mute(); else s.pluck (events[ev].f, amp);
                ++ev;
            }
            b.setSample (0, i, 0.25f * s.tick (pitchMod ? pitchMod (t) : 1.0));   // DI: піки ≈ -12…-18 dBFS
        }
        p.processBlock (b, midi);
        r.outPeak = std::max (r.outPeak, b.getMagnitude (0, 0, 256));
        if (pos % 2400 == 0)
        {
            r.bloom.push_back ({ pos / fs, p.engine.bloomLevel.load() });
            r.det.push_back ({ pos / fs, p.engine.detectedHz.load() });
        }
    }
    r.starts = p.engine.starts.load();
    return r;
}

static void setP (SpacenerdFeedbackProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

static String timeline (const RunResult& r, double every = 0.25)
{
    String s;
    double next = 0.0;
    for (auto& [t, b] : r.bloom)
        if (t >= next - 1e-9) { s << (b < 0.02f ? "." : b < 0.3f ? "-" : b < 0.8f ? "+" : "#"); next += every; }
    return s;
}

static int octaveErrors (const RunResult& r, double t0, double t1, double f)
{
    int e = 0;
    for (auto& [t, hz] : r.det)
        if (t >= t0 && t < t1 && hz > 0.0f && std::abs (1200.0 * std::log2 (hz / f)) > 100.0) ++e;
    return e;
}

int main()
{
    ScopedJuceInitialiser_GUI init;
    std::cout << "Timeline: '.' немає  '-' наростає  '+' середній  '#' повний (крок 0.25 с)\n\n";

    // A. Одна витягнута нота (низьке E), налаштування за замовчуванням
    {
        SpacenerdFeedbackProcessor p;
        auto r = run (p, 6.0, { { 0.2, 82.41 } });
        std::cout << "A  held E2, default      " << timeline (r) << "  starts=" << r.starts
                  << "  octave errors=" << octaveErrors (r, 0.3, 6.0, 82.41) << "\n";
    }
    // B. Риф восьмими (не має зриватися), потім витягнута нота (має)
    {
        SpacenerdFeedbackProcessor p;
        std::vector<Event> e;
        const double riff[] { 82.41, 98.0, 110.0, 98.0, 82.41, 98.0, 110.0, 123.47 };
        for (int i = 0; i < 8; ++i) e.push_back ({ 0.2 + i * 0.25, riff[i] });
        e.push_back ({ 2.2, 110.0 });
        auto r = run (p, 7.0, e);
        std::cout << "B  riff then held A2     " << timeline (r) << "  starts=" << r.starts << "\n";
    }
    // C. Тремоло-удари по тій самій ноті (має зриватися, не перезапускатись)
    {
        SpacenerdFeedbackProcessor p;
        std::vector<Event> e;
        for (int i = 0; i < 12; ++i) e.push_back ({ 0.2 + i * 0.4, 110.0 });
        auto r = run (p, 6.0, e);
        std::cout << "C  re-picked A2          " << timeline (r) << "  starts=" << r.starts << "\n";
    }
    // D. Бенд на тон вгору і вібрато
    {
        SpacenerdFeedbackProcessor p;
        auto r = run (p, 6.0, { { 0.2, 146.83 } }, [] (double t)
        {
            const double bend = t > 2.5 && t < 3.5 ? std::pow (2.0, (t - 2.5) * 2.0 / 12.0) : (t >= 3.5 ? std::pow (2.0, 2.0 / 12.0) : 1.0);
            return bend * (1.0 + 0.004 * std::sin (2.0 * MathConstants<double>::pi * 5.5 * t));
        });
        std::cout << "D  D3 bend + vibrato     " << timeline (r) << "  starts=" << r.starts << "\n";
    }
    // E. Глушіння
    {
        SpacenerdFeedbackProcessor p;
        auto r = run (p, 5.0, { { 0.2, 110.0 }, { 3.0, 0.0, true } });
        std::cout << "E  A2, mute at 3 s       " << timeline (r) << "  starts=" << r.starts << "\n";
    }
    // F. Hold: кнопка з 1.5 до 4 с
    {
        SpacenerdFeedbackProcessor p; setP (p, trigger, 1);
        auto r = run (p, 6.0, { { 0.2, 110.0 } }, nullptr, [&] (double t) { setP (p, hold, t > 1.5 && t < 4.0 ? 1.0f : 0.0f); });
        std::cout << "F  Hold 1.5-4 s          " << timeline (r) << "  starts=" << r.starts << "\n";
    }
    // G. Тихий DI (-30 dBFS) і гучний (-3 dBFS)
    for (float amp : { 0.03f, 1.6f })
    {
        SpacenerdFeedbackProcessor p;
        auto r = run (p, 6.0, { { 0.2, 110.0 } }, nullptr, nullptr, amp);
        std::cout << "G  level x" << String (amp, 2).paddedRight (' ', 5) << "           " << timeline (r)
                  << "  starts=" << r.starts << "  out peak " << String (sn::gainToDb (r.outPeak), 1) << " dBFS\n";
    }
    // H. Високі ноти
    for (double f : { 329.63, 659.26 })
    {
        SpacenerdFeedbackProcessor p;
        auto r = run (p, 5.0, { { 0.2, f } });
        std::cout << "H  " << String (f, 0) << " Hz               " << timeline (r) << "  starts=" << r.starts
                  << "  octave errors=" << octaveErrors (r, 0.3, 5.0, f) << "\n";
    }
    return 0;
}
