#pragma once

#include "SNCommon.h"
#include "MixBus.h"

/** Виправлення SN Listen (кнопка FIX): EQ-точки, зрізи, вирізи гулу, моно низу, рівень.
    Кожне вмикається/вимикається плавно (~60 мс), тож натискання не клацає. */
namespace mix
{
class FixChain
{
public:
    static constexpr int kMax = Features::kMaxFixes;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& s : slots) s = {};
        rampStep = (float) (32.0 / (0.06 * fs));
        sideHp1.reset(); sideHp2.reset(); sideHz = 0.0f; lastGain = 1.0f;
    }

    /** Message thread: новий список (id — стабільний ідентифікатор виправлення). */
    void set (const std::vector<std::pair<uint32_t, Fix>>& list, bool enabled)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        pending = list;
        pendingOn = enabled;
        version.fetch_add (1);
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        sync();
        const int n = buffer.getNumSamples(), numCh = std::min (2, buffer.getNumChannels());
        for (int start = 0; start < n; start += 32)
        {
            const int len = std::min (32, n - start);
            float gainDb = 0.0f, mono = 0.0f, monoHz = 120.0f;
            for (auto& s : slots)
            {
                if (s.id == 0) continue;
                const float target = s.alive && on ? 1.0f : 0.0f;
                if (s.amount != target || s.dirty)
                {
                    s.amount = s.amount < target ? std::min (target, s.amount + rampStep) : std::max (target, s.amount - rampStep);
                    design (s);
                    s.dirty = false;
                }
                if (s.amount <= 0.0f && ! s.alive) { s = {}; continue; }
                if (s.f.type == FixGain) gainDb += s.f.db * s.amount;
                if (s.f.type == FixMonoLow) { mono = std::max (mono, s.amount); monoHz = s.f.hz; }
            }

            for (int ch = 0; ch < numCh; ++ch)
            {
                float* d = buffer.getWritePointer (ch, start);
                for (auto& s : slots)
                {
                    if (s.id == 0 || s.amount <= 0.0f || s.nf == 0) continue;
                    for (int k = 0; k < s.nf; ++k)
                    {
                        auto& f = s.filt[(size_t) k][(size_t) ch];
                        for (int i = 0; i < len; ++i) d[i] = f.process (d[i]);
                    }
                }
            }

            // Моно низу: прибрати низ зі сторони (side)
            if (mono > 0.0f && numCh == 2)
            {
                if (std::abs (monoHz - sideHz) > 0.5f) { sideHp1.setHighPass (fs, monoHz, 0.707); sideHp2.copyCoeffs (sideHp1); sideHz = monoHz; }
                float* l = buffer.getWritePointer (0, start);
                float* r = buffer.getWritePointer (1, start);
                for (int i = 0; i < len; ++i)
                {
                    const float m = 0.5f * (l[i] + r[i]), sd = 0.5f * (l[i] - r[i]);
                    const float s2 = (1.0f - mono) * sd + mono * sideHp2.process (sideHp1.process (sd));   // сторона лише вище частоти (LR4)
                    l[i] = m + s2; r[i] = m - s2;
                }
            }

            const float g = sn::dbToGain (gainDb);
            if (std::abs (g - lastGain) > 1.0e-6f || g != 1.0f)
                for (int ch = 0; ch < numCh; ++ch)
                    buffer.applyGainRamp (ch, start, len, lastGain, g);
            lastGain = g;
        }
    }

private:
    struct Slot
    {
        uint32_t id = 0;
        Fix f;
        bool alive = false, dirty = false;
        float amount = 0.0f;
        int nf = 0;
        std::array<std::array<sn::Biquad, 2>, 3> filt {};
    };

    void sync()
    {
        const auto v = version.load();
        if (v == seenVersion) return;
        const juce::SpinLock::ScopedTryLockType sl (lock);
        if (! sl.isLocked()) return;
        seenVersion = v;
        on = pendingOn;
        for (auto& s : slots) if (s.id != 0) s.alive = false;
        for (const auto& [id, fix] : pending)
        {
            Slot* slot = nullptr;
            for (auto& s : slots) if (s.id == id) slot = &s;
            if (slot == nullptr) for (auto& s : slots) if (s.id == 0) { slot = &s; slot->id = id; slot->amount = 0.0f; for (auto& a : slot->filt) for (auto& b : a) b.reset(); break; }
            if (slot == nullptr) continue;
            slot->alive = true;
            if (slot->f.type != fix.type || slot->f.hz != fix.hz || slot->f.db != fix.db || slot->f.q != fix.q) { slot->f = fix; slot->dirty = true; }
        }
    }

    /** Коефіцієнти з урахуванням «скільки ввімкнено» (amount 0…1). */
    void design (Slot& s)
    {
        const float a = s.amount;
        sn::Biquad b[3];
        int n = 0;
        switch (s.f.type)
        {
            case FixPeak:      b[n++].setPeak (fs, s.f.hz, s.f.q, s.f.db * a); break;
            case FixHighShelf: b[n++].setHighShelf (fs, s.f.hz, 0.707, s.f.db * a); break;
            case FixLowShelf:  b[n++].setLowShelf (fs, s.f.hz, 0.707, s.f.db * a); break;
            case FixHighPass:  b[n++].setHighPass (fs, 10.0 * std::pow (s.f.hz / 10.0, (double) a), 0.707); break;
            case FixLowPass:   b[n++].setLowPass (fs, std::min (0.45 * fs, 22000.0 * std::pow (s.f.hz / 22000.0, (double) a)), 0.707); break;
            case FixHum:       for (int h = 1; h <= 3; ++h) b[n++].setPeak (fs, s.f.hz * h, 14.0, -24.0 * a); break;
            default: break;
        }
        s.nf = n;
        for (int k = 0; k < n; ++k) for (auto& f : s.filt[(size_t) k]) f.copyCoeffs (b[k]);
    }

    double fs = 48000.0;
    float rampStep = 0.01f, lastGain = 1.0f, sideHz = 0.0f;
    bool on = true;
    std::array<Slot, kMax> slots {};
    sn::Biquad sideHp1, sideHp2;
    juce::SpinLock lock;
    std::vector<std::pair<uint32_t, Fix>> pending;
    bool pendingOn = true;
    std::atomic<uint32_t> version { 0 };
    uint32_t seenVersion = 0;
};
} // namespace mix
