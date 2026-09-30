// Точність третинооктавного аналізатора: сигнал, синтезований точно за цільовою кривою, має дати відхилення ≈ 0.
#include "../Common/Analysis.h"
#include <iostream>
#include <random>
using namespace juce;
constexpr double fs = 48000.0;

int main()
{
    constexpr int N = 1 << 19;
    std::mt19937 rng (5);
    std::uniform_real_distribution<float> ph (0.0f, MathConstants<float>::twoPi);
    dsp::FFT fft (19);
    const auto curve = an::targetCurve (1995.0f, 1);
    std::vector<float> spec ((size_t) N * 2, 0.0f);
    for (int k = 1; k < N / 2; ++k)
    {
        const float f = (float) (k * fs / N);
        if (f < 20.0f || f > 20000.0f) continue;
        int b = 0; while (b < an::kBands - 2 && an::bandHz[(size_t) b + 1] < f) ++b;
        const float t = jlimit (0.0f, 1.0f, std::log (f / an::bandHz[(size_t) b]) / std::log (an::bandHz[(size_t) b + 1] / an::bandHz[(size_t) b]));
        const float db = curve[(size_t) b] + t * (curve[(size_t) b + 1] - curve[(size_t) b]);
        const float mag = std::pow (10.0f, db / 20.0f) / std::sqrt (f), a = ph (rng);
        spec[(size_t) (2 * k)] = mag * std::cos (a); spec[(size_t) (2 * k + 1)] = mag * std::sin (a);
    }
    fft.performRealOnlyInverseTransform (spec.data());
    float pk = 0; for (int i = 0; i < N; ++i) pk = std::max (pk, std::abs (spec[(size_t) i]));
    for (int i = 0; i < N; ++i) spec[(size_t) i] *= 0.3f / pk;

    {
        AudioBuffer<float> rec (2, N);
        rec.copyFrom (0, 0, spec.data(), N); rec.copyFrom (1, 0, spec.data(), N);
        sn::LoudnessMeter l1; l1.prepare (fs, 2); l1.process (rec);
        rec.applyGain (sn::dbToGain (-16.0f - l1.integrated.load()));
        sn::LoudnessMeter l2; l2.prepare (fs, 2); l2.process (rec);
        const auto r = an::matchReference (rec, fs, 1);
        std::cout << "whole-buffer before gain " << l1.integrated.load() << ", after " << l2.integrated.load() << ", matchReference " << r.lufs << std::endl;
    }
    an::Analyzer a; a.prepare (fs);
    a.push (spec.data(), spec.data(), N);
    const auto m = an::normalise (a.average()), t = an::normalise (curve);
    for (int b = 0; b < an::kBands; ++b)
        std::cout << an::bandHz[(size_t) b] << " Hz: " << String (m[(size_t) b] - t[(size_t) b], 2) << " dB" << std::endl;
    return 0;
}
