#pragma once

#include "MixAdvisor.h"

/** Сторона SN Master: збирає дані всіх SN Listen, оцінює реальну гучність кожної доріжки в міксі
    (фейдери й посили невидимі для вставок, тому вони оцінюються регресією потужності по таймлайну),
    і пише кожній доріжці висновок із порадами. */
namespace mix
{
//==============================================================================
/** Потужність входу Master'а по кадрах 100 мс таймлайну хоста (пише аудіопотік). */
class MasterFrames
{
public:
    void prepare (double fs)
    {
        frameLen = std::max (1, (int) std::round (0.1 * fs));
        for (int b = 0; b < an::kBands; ++b) { bp[(size_t) b].setBandPass (fs, an::bandHz[(size_t) b], 4.32); bp[(size_t) b].reset(); }   // як у SN Listen
        std::fill (std::begin (acc), std::end (acc), 0.0); pos = 0; curIdx = -1;
    }

    void push (const float* l, const float* r, int n, int64_t startSample)
    {
        for (int i = 0; i < n; ++i)
        {
            const int64_t idx = startSample >= 0 ? (startSample + i) / frameLen : -1;
            if (pos == 0) curIdx = idx;
            else if (idx != curIdx) { std::fill (std::begin (acc), std::end (acc), 0.0); pos = 0; curIdx = idx; }
            const float m = 0.5f * (l[i] + (r != nullptr ? r[i] : l[i]));
            if (curIdx >= 0)
                for (int b = 0; b < an::kBands; ++b) { const float y = bp[(size_t) b].process (m); acc[groupOfBand (b)] += (double) y * y; }
            if (++pos >= frameLen)
            {
                if (curIdx >= 0)
                {
                    auto& rec = ring[(size_t) (curIdx % kFrames)];
                    rec.idx.store (-1, std::memory_order_release);
                    for (int k = 0; k < kGroups; ++k) rec.p[k] = (float) (acc[k] / frameLen);
                    rec.ms = nowMs();
                    rec.idx.store (curIdx, std::memory_order_release);
                }
                std::fill (std::begin (acc), std::end (acc), 0.0); pos = 0; curIdx = curIdx >= 0 ? curIdx + 1 : -1;
            }
        }
    }

    struct Rec { std::atomic<int64_t> idx { -1 }; float p[kGroups] {}; int64_t ms = 0; };
    std::array<Rec, kFrames> ring;

private:
    int frameLen = 4800, pos = 0;
    int64_t curIdx = -1;
    double acc[kGroups] {};
    std::array<sn::Biquad, an::kBands> bp;
};

//==============================================================================
class MixWatch
{
public:
    struct TrackView
    {
        int slot = -1;
        juce::String name;
        int inst = Other;
        bool manual = false;
        Verdict v;
        float faderDb = 0.0f;
        bool faderKnown = false;
    };

    MasterFrames frames;

    /** Викликати з message thread (~30 Гц); важка робота — раз на секунду. */
    void tick (int genre)
    {
        if (++ticks < 30 && ! force) return;
        ticks = 0; force = false;
        if (! bus.open()) return;
        auto& h = bus.layout()->h;
        h.masterHeartbeat = nowMs();
        h.genre = genre;
        if (resetPending.exchange (false)) h.resetCounter.fetch_add (1);
        analyse (genre);
    }

    void requestReset() { resetPending = true; }
    void refreshNow() { force = true; }

    std::vector<TrackView> tracks;      // для інтерфейсу (message thread)
    Verdict overall;
    float explained = 0.0f;             // яку частку міксу покривають доріжки з SN Listen
    bool timelineOk = false;

    /** Доступ для тестів. */
    Bus& getBus() { return bus; }

private:
    struct T { int slot; juce::String name; Features f; double g = 1.0; bool gKnown = false; std::vector<double> p; std::vector<Advice> adv; };

    void analyse (int genre)
    {
        const auto now = nowMs();
        std::vector<T> ts;
        for (int i = 0; i < kSlots; ++i)
        {
            if (! bus.alive (i, now)) continue;
            T t; t.slot = i;
            if (! bus.readFeatures (i, t.f, t.name)) continue;
            ts.push_back (std::move (t));
        }

        estimateFaders (ts, now);

        for (auto& t : ts) trackChecks (t.f, t.adv, genre);
        std::vector<Advice> mixAdv;
        if (timelineOk) { balance (ts, genre, mixAdv); masking (ts, mixAdv); }
        tuningMatch (ts, mixAdv);

        // Доріжка, якої не чути в міксі
        for (auto& t : ts)
            if (t.gKnown && t.g < 1.0e-4 && t.f.activity > 0.2f)
                t.adv.push_back ({ 1, "Not in the mix?", "This track plays but barely reaches the master: muted, fader down, or routed elsewhere?" });

        tracks.clear();
        for (auto& t : ts)
        {
            TrackView v;
            v.slot = t.slot; v.name = t.name; v.inst = t.f.inst; v.manual = t.f.manual != 0;
            v.v = makeVerdict (t.adv, t.f.valid != 0);
            v.faderDb = (float) (10.0 * std::log10 (std::max (t.g, 1.0e-9)));
            v.faderKnown = t.gKnown;
            v.v.faderDb = v.faderDb;
            bus.writeVerdict (t.slot, v.v);
            tracks.push_back (v);
        }
        std::stable_sort (tracks.begin(), tracks.end(), [] (const TrackView& a, const TrackView& b) { return a.inst < b.inst; });

        // Загальний висновок: проблеми міксу + найгірші доріжки
        std::vector<Advice> all = mixAdv;
        for (auto& t : tracks)
            if (t.v.status >= 1 && t.v.numItems > 0
                && std::none_of (mixAdv.begin(), mixAdv.end(), [&] (const Advice& a) { return a.title == juce::String::fromUTF8 (t.v.items[0].title); }))
                all.push_back ({ t.v.status, (t.name.isNotEmpty() ? t.name : juce::String (instName (t.inst))) + ": " + juce::String::fromUTF8 (t.v.items[0].title),
                                 juce::String::fromUTF8 (t.v.items[0].text) });
        if (! ts.empty() && ! timelineOk)
            all.push_back ({ 0, "Press play in your DAW", "Balance and frequency clashes need the song playing from the timeline." });
        else if (! ts.empty() && explained < 0.5f)
            all.push_back ({ 0, "Some tracks without SN Listen", "Listened tracks cover only " + juce::String (juce::roundToInt (explained * 100.0f))
                                                                 + "% of the mix: balance advice is partial." });
        bool anyValid = false;
        for (auto& t : ts) anyValid = anyValid || t.f.valid != 0;
        overall = makeVerdict (all, anyValid);
    }

    /** Фейдер кожної доріжки: потужність міксу ≈ Σ g_i · потужність доріжки, окремо в 4 смугах
        (спільний g для всіх смуг; невід'ємні найменші квадрати, смуги зважені однаково). */
    void estimateFaders (std::vector<T>& ts, int64_t now)
    {
        timelineOk = false; explained = 0.0f;
        mRows.clear();
        std::vector<std::array<double, kGroups>> m;
        std::vector<int64_t> rowIdx, rowMs;
        const auto* L = bus.layout();
        for (auto& rec : frames.ring)
        {
            const auto idx = rec.idx.load (std::memory_order_acquire);
            if (idx < 0 || now - rec.ms > 125000) continue;
            std::array<double, kGroups> v {};
            for (int k = 0; k < kGroups; ++k) v[(size_t) k] = rec.p[k];
            m.push_back (v); rowIdx.push_back (idx); rowMs.push_back (rec.ms);
        }
        if (ts.empty() || m.size() < 40) return;

        // Кадри, де є дані всіх доріжок
        std::vector<size_t> rows;
        std::vector<std::vector<std::array<double, kGroups>>> P (ts.size());
        for (size_t r = 0; r < m.size(); ++r)
        {
            bool ok = true;
            std::vector<std::array<double, kGroups>> vals (ts.size());
            for (size_t i = 0; i < ts.size() && ok; ++i)
            {
                const auto& fr = L->slots[ts[i].slot].frames[(size_t) (rowIdx[r] % kFrames)];
                ok = fr.idx.load (std::memory_order_acquire) == rowIdx[r] && std::abs (fr.ms - rowMs[r]) < 3000;
                for (int k = 0; k < kGroups; ++k) vals[i][(size_t) k] = fr.p[k];
            }
            if (! ok) continue;
            rows.push_back (r);
            for (size_t i = 0; i < ts.size(); ++i) P[i].push_back (vals[i]);
        }
        if (rows.size() < 30) return;
        timelineOk = true;

        // Вага смуги: 1 / середня потужність міксу в ній
        std::array<double, kGroups> w {};
        for (int k = 0; k < kGroups; ++k)
        {
            double s = 0.0;
            for (auto r : rows) s += m[r][(size_t) k];
            w[(size_t) k] = s > 1.0e-20 ? (double) rows.size() / s : 0.0;
        }
        const size_t n = ts.size(), R = rows.size();
        auto x = [&] (size_t i, size_t row, int k) { return P[i][row][(size_t) k] * w[(size_t) k]; };
        auto y = [&] (size_t row, int k) { return m[rows[row]][(size_t) k] * w[(size_t) k]; };

        std::vector<double> g (n, 1.0), pp (n, 0.0);
        std::vector<std::array<double, kGroups>> resid (R);
        for (size_t i = 0; i < n; ++i) for (size_t r = 0; r < R; ++r) for (int k = 0; k < kGroups; ++k) pp[i] += x (i, r, k) * x (i, r, k);
        for (size_t r = 0; r < R; ++r)
            for (int k = 0; k < kGroups; ++k)
            {
                double s = y (r, k);
                for (size_t i = 0; i < n; ++i) s -= g[i] * x (i, r, k);
                resid[r][(size_t) k] = s;
            }
        for (int it = 0; it < 200; ++it)
            for (size_t i = 0; i < n; ++i)
            {
                if (pp[i] < 1.0e-30) continue;
                double num = 0.0;
                for (size_t r = 0; r < R; ++r) for (int k = 0; k < kGroups; ++k) num += x (i, r, k) * (resid[r][(size_t) k] + g[i] * x (i, r, k));
                const double gn = std::max (0.0, num / pp[i]);
                const double d = gn - g[i];
                if (d != 0.0) for (size_t r = 0; r < R; ++r) for (int k = 0; k < kGroups; ++k) resid[r][(size_t) k] -= d * x (i, r, k);
                g[i] = gn;
            }

        // Широкосмугові потужності для балансу
        double sumM = 0.0, sumPred = 0.0;
        for (size_t r = 0; r < R; ++r)
        {
            double mm = 0.0;
            for (int k = 0; k < kGroups; ++k) mm += m[rows[r]][(size_t) k];
            mRows.push_back (mm);
            sumM += mm;
            for (size_t i = 0; i < n; ++i) for (int k = 0; k < kGroups; ++k) sumPred += g[i] * P[i][r][(size_t) k];
        }
        explained = sumM > 0.0 ? (float) juce::jlimit (0.0, 1.0, sumPred / sumM) : 0.0f;
        for (size_t i = 0; i < n; ++i)
        {
            ts[i].g = g[i];
            ts[i].p.assign (R, 0.0);
            int act = 0;
            for (size_t r = 0; r < R; ++r)
            {
                for (int k = 0; k < kGroups; ++k) ts[i].p[r] += P[i][r][(size_t) k];
                act += ts[i].p[r] > 1.0e-8;
            }
            ts[i].gKnown = act >= 20;
        }
    }

    /** Гучність кожної категорії в міксі (поки вона звучить) проти жанрового орієнтира. */
    void balance (std::vector<T>& ts, int genre, std::vector<Advice>& mixAdv)
    {
        bool hasKS = false;
        for (auto& t : ts) hasKS = hasKS || t.f.inst == Kick || t.f.inst == Snare;
        const size_t rows = mRows.size();
        for (int c = 0; c < Other; ++c)
        {
            std::vector<T*> members;
            for (auto& t : ts) if (t.f.inst == c && t.gKnown && t.f.valid) members.push_back (&t);
            if (members.empty()) continue;
            double num = 0.0, den = 0.0;
            std::vector<double> thr;          // поріг «звучить»: -25 дБ від гучних кадрів доріжки
            for (auto* t : members) thr.push_back (threshold (*t));
            for (size_t r = 0; r < rows; ++r)
            {
                double pc = 0.0;
                bool on = false;
                for (size_t k = 0; k < members.size(); ++k)
                {
                    pc += members[k]->g * members[k]->p[r];
                    on = on || members[k]->p[r] > thr[k];
                }
                if (on) { num += pc; den += mRows[r]; }
            }
            if (den <= 0.0 || num <= 0.0) continue;
            const float rel = (float) (10.0 * std::log10 (num / den));
            const float target = balanceTarget (c, genre, hasKS);
            const float d = rel - target;
            if (std::abs (d) <= 3.0f) continue;
            const int sev = std::abs (d) > 6.0f ? 2 : 1;
            const juce::String who = c == Guitar && members.size() > 1 ? juce::String ("Guitars (all together)")
                                   : c == Drums ? juce::String (hasKS ? "Overheads / room" : "Drums") : juce::String (instName (c));
            const juce::String text = who + (members.size() > 1 ? " are ~" : " is ~") + dbText (d) + (d > 0 ? " louder" : " quieter") + " than usual for " + genreLabel (genre)
                                    + ". " + (d > 0 ? "Lower" : "Raise") + " by about " + dbText (d * 0.8f) + " (fader or bus).";
            const Advice a { sev, who + (d > 0 ? " too loud" : " too quiet"), text };
            mixAdv.push_back (a);
            for (auto* t : members) t->adv.push_back (a);
        }
    }

    static double threshold (const T& t)
    {
        std::vector<double> v (t.p);
        if (v.empty()) return 1.0e-8;
        const size_t k = (size_t) (0.9 * (double) (v.size() - 1));
        std::nth_element (v.begin(), v.begin() + (long) k, v.end());
        return std::max (1.0e-8, v[k] * std::pow (10.0, -2.5));
    }

    /** Конфлікти частот між інструментами з урахуванням фейдерів. */
    void masking (std::vector<T>& ts, std::vector<Advice>& mixAdv)
    {
        std::array<std::array<double, an::kBands>, kNumInst> cat {};
        std::array<double, an::kBands> total {};
        for (auto& t : ts)
        {
            if (! t.f.valid) continue;
            for (int b = 0; b < an::kBands; ++b)
            {
                const double p = t.g * std::pow (10.0, t.f.bands[b] / 10.0) * t.f.activity;
                cat[(size_t) t.f.inst][(size_t) b] += p;
                total[(size_t) b] += p;
            }
        }
        for (const auto& c : clashes())
        {
            int hits = 0; double bestSum = 0.0; float bestHz = 0.0f;
            for (int b = 0; b < an::kBands; ++b)
            {
                const float hz = an::bandHz[(size_t) b];
                if (hz < c.lo || hz > c.hi) continue;
                const double a = cat[(size_t) c.a][(size_t) b], bb = cat[(size_t) c.b][(size_t) b], tot = total[(size_t) b];
                if (a <= 0.0 || bb <= 0.0 || tot <= 0.0) continue;
                if (std::min (a, bb) / std::max (a, bb) > 0.4 && (a + bb) / tot > 0.6)
                {
                    ++hits;
                    if (a + bb > bestSum) { bestSum = a + bb; bestHz = hz; }
                }
            }
            if (hits < 2) continue;
            const juce::String A = instName (c.a), B = instName (c.b);
            const Advice adv { 1, A + " vs " + B + " at " + hzText (bestHz),
                               A + " and " + B.toLowerCase() + " fight for " + hzText (bestHz) + ". " + juce::String (c.fix).replace ("%f", hzText (bestHz)) };
            mixAdv.push_back (adv);
            for (auto& t : ts) if (t.f.inst == c.a || t.f.inst == c.b) t.adv.push_back (adv);
        }
    }

    /** Різний стрій у баса, гітар і клавіш. */
    static void tuningMatch (std::vector<T>& ts, std::vector<Advice>& mixAdv)
    {
        for (size_t i = 0; i < ts.size(); ++i)
            for (size_t j = i + 1; j < ts.size(); ++j)
            {
                auto& a = ts[i]; auto& b = ts[j];
                auto tuned = [] (const T& t) { return (t.f.inst == Bass || t.f.inst == Guitar || t.f.inst == Keys) && t.f.tuneFrames >= 40.0f; };
                if (! tuned (a) || ! tuned (b)) continue;
                const float d = std::abs (a.f.tuneCents - b.f.tuneCents);
                if (d < 10.0f) continue;
                const juce::String an = a.name.isNotEmpty() ? a.name : juce::String (instName (a.f.inst));
                const juce::String bn = b.name.isNotEmpty() ? b.name : juce::String (instName (b.f.inst));
                const Advice adv { d >= 18.0f ? 2 : 1, "Tuned apart: " + an + " / " + bn,
                                   an + " and " + bn + " are " + juce::String (juce::roundToInt (d)) + " cents apart: chords and octaves will beat. Retune one of them." };
                mixAdv.push_back (adv);
                a.adv.push_back (adv); b.adv.push_back (adv);
            }
    }

    Bus bus;
    std::vector<double> mRows;
    int ticks = 0;
    bool force = false;
    std::atomic<bool> resetPending { false };
};
} // namespace mix
