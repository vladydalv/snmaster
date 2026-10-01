#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <cstring>
#include "Analysis.h"

/** Спільна пам'ять між SN Listen (на доріжках) і SN Master (на виході).
    Файл, відображений у пам'ять усіма екземплярами — працює і коли хост запускає плагіни в різних процесах. */
namespace mix
{
//==============================================================================
enum Inst : int { Kick = 0, Snare, Drums, Bass, Guitar, Vocal, Keys, Other, kNumInst };

inline const char* instName (int i)
{
    static const char* n[] { "Kick", "Snare", "Drums", "Bass", "Guitar", "Vocal", "Keys", "Other" };
    return i >= 0 && i < kNumInst ? n[i] : "Other";
}

/** Підсумок доріжки, який рахує SN Listen. */
struct Features
{
    int32_t inst = Other, autoInst = Other, manual = 0, valid = 0;
    float autoConf = 0.0f;
    float lufs = -100.0f;          // інтегрована гучність доріжки (поки грає)
    float activity = 0.0f;         // частка часу, коли доріжка звучить
    float seconds = 0.0f;          // скільки прослухано
    float peakDb = -100.0f;        // максимальний пік
    float rangeDb = 0.0f;          // розкид гучності між нотами/фразами (P90 − P10)
    float crestDb = 0.0f;          // медіанний пік/RMS кадру (атака барабанів)
    float decayFrac = 0.0f;        // як часто звук швидко згасає (> 6 дБ за 100 мс): ударні
    float corr = 1.0f;             // кореляція L/R
    float sideDb = -60.0f;         // сторона/середина, дБ
    float lowSideDb = -60.0f;      // сторона/середина нижче 150 Гц
    float noiseDb = 0.0f;          // шум між нотами відносно сигналу (дБ, від'ємне); 0 — невідомо
    float hum = 0.0f;              // частка тихих місць із гулом мережі
    float humHz = 0.0f;            // 50 або 60
    float tuneCents = 0.0f;        // середнє відхилення строю від A = 440
    float tuneFrames = 0.0f;       // скільки надійних нот
    float pitchSpread = 0.0f;      // «плавання» висоти всередині ноти (вокал > гітара)
    float pitchedFrac = 0.0f;
    float medianHz = 0.0f;
    int32_t clips = 0;
    // Уся установка на одній доріжці: рівні ударів бочки й малого та тарілок (зважено, дБ)
    float kitKickDb = -100.0f, kitSnareDb = -100.0f, kitCymDb = -100.0f;
    int32_t kitKickHits = 0, kitSnareHits = 0;
    float bands[an::kBands] {};    // середній спектр, коли доріжка звучить (дБ)
};

/** Висновок SN Master для доріжки. */
struct Verdict
{
    static constexpr int kItems = 6;
    struct Item { int32_t sev = 0; char title[48] {}; char text[232] {}; };
    int32_t status = -1;           // -1 збір даних, 0 ок, 1 увага, 2 проблема
    int32_t numItems = 0;
    float faderDb = 0.0f;          // оцінка фейдера/посилів (наскільки доріжка тихша в міксі, ніж на вставці)
    float relDb = 0.0f;            // гучність у міксі відносно всього міксу
    Item items[kItems];
};

/** Потужність кадру 100 мс у 4 групах смуг (моно): ≤125 Гц, 160-500, 630-2.5k, ≥3.15k. */
static constexpr int kGroups = 4;
inline int groupOfBand (int b) { return b <= 6 ? 0 : b <= 12 ? 1 : b <= 19 ? 2 : 3; }
struct FrameRec { std::atomic<int64_t> idx; float p[kGroups]; int64_t ms; };

static constexpr int kSlots = 32, kFrames = 1200;   // 1200 кадрів × 100 мс = 2 хв історії для оцінки фейдерів
static constexpr uint32_t kMagic = 0x534e4d42, kVersion = 3;

struct Slot
{
    std::atomic<uint64_t> owner;
    std::atomic<int64_t> heartbeat;
    std::atomic<uint32_t> fseq;
    char name[64];
    Features f;
    FrameRec frames[kFrames];
    std::atomic<uint32_t> vseq;
    std::atomic<int64_t> verdictMs;
    Verdict v;
};

struct Header
{
    uint32_t magic, version;
    std::atomic<int64_t> masterHeartbeat;
    std::atomic<int32_t> genre;
    std::atomic<uint32_t> resetCounter;
};

struct Layout { Header h; Slot slots[kSlots]; };

inline int64_t nowMs() { return (int64_t) juce::Time::currentTimeMillis(); }

//==============================================================================
class Bus
{
public:
    /** Для тестів: інший файл. */
    static juce::File& fileOverride() { static juce::File f; return f; }

    static juce::File defaultFile()
    {
        if (fileOverride() != juce::File()) return fileOverride();
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
                   .getChildFile ("Application Support")
#endif
                   .getChildFile ("Spacenerd").getChildFile ("MixBus-v3.bin");
    }

    bool open()
    {
        if (mem != nullptr) return true;
        const auto file = defaultFile();
        juce::InterProcessLock lock ("SpacenerdMixBus");
        const juce::InterProcessLock::ScopedLockType sl (lock);
        file.getParentDirectory().createDirectory();
        if (file.getSize() != (juce::int64) sizeof (Layout))
        {
            juce::MemoryBlock zeros (sizeof (Layout), true);
            if (! file.replaceWithData (zeros.getData(), zeros.getSize())) return false;
        }
        mapped = std::make_unique<juce::MemoryMappedFile> (file, juce::MemoryMappedFile::readWrite, false);
        if (mapped->getData() == nullptr || mapped->getSize() < sizeof (Layout)) { mapped.reset(); return false; }
        mem = static_cast<Layout*> (mapped->getData());
        if (mem->h.magic != kMagic || mem->h.version != kVersion)
        {
            std::memset (static_cast<void*> (mem), 0, sizeof (Layout));
            mem->h.magic = kMagic; mem->h.version = kVersion;
        }
        return true;
    }

    bool isOpen() const noexcept { return mem != nullptr; }
    Layout* layout() noexcept { return mem; }

    /** Зайняти вільний (або покинутий) слот. */
    int claim (uint64_t id)
    {
        if (! open()) return -1;
        const auto now = nowMs();
        for (int i = 0; i < kSlots; ++i)
        {
            auto& s = mem->slots[i];
            uint64_t cur = s.owner.load();
            if (cur == id) return i;
            const bool stale = now - s.heartbeat.load() > 15000;
            if ((cur == 0 || stale) && s.owner.compare_exchange_strong (cur, id))
            {
                s.heartbeat = now;
                s.verdictMs = 0;
                for (auto& fr : s.frames) fr.idx.store (-1);
                writeFeatures (i, {}, {});
                return i;
            }
        }
        return -1;
    }

    void release (int slot, uint64_t id)
    {
        if (mem == nullptr || slot < 0) return;
        uint64_t cur = id;
        mem->slots[slot].owner.compare_exchange_strong (cur, 0);
    }

    bool ownsSlot (int slot, uint64_t id) const noexcept { return mem != nullptr && slot >= 0 && mem->slots[slot].owner.load() == id; }

    void heartbeat (int slot) { if (mem != nullptr && slot >= 0) mem->slots[slot].heartbeat = nowMs(); }

    bool alive (int slot, int64_t now) const noexcept
    {
        return mem != nullptr && mem->slots[slot].owner.load() != 0 && now - mem->slots[slot].heartbeat.load() < 4000;
    }

    void writeFeatures (int slot, const juce::String& name, const Features& f)
    {
        auto& s = mem->slots[slot];
        s.fseq.fetch_add (1, std::memory_order_acq_rel);
        std::atomic_thread_fence (std::memory_order_release);
        std::memset (s.name, 0, sizeof (s.name));
        name.copyToUTF8 (s.name, sizeof (s.name) - 1);
        std::memcpy (&s.f, &f, sizeof (Features));
        std::atomic_thread_fence (std::memory_order_release);
        s.fseq.fetch_add (1, std::memory_order_acq_rel);
    }

    bool readFeatures (int slot, Features& f, juce::String& name) const
    {
        auto& s = mem->slots[slot];
        char nm[64];
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const auto a = s.fseq.load (std::memory_order_acquire);
            if (a & 1u) { juce::Thread::yield(); continue; }
            std::memcpy (&f, &s.f, sizeof (Features));
            std::memcpy (nm, s.name, sizeof (nm));
            std::atomic_thread_fence (std::memory_order_acquire);
            if (s.fseq.load (std::memory_order_acquire) == a)
            {
                nm[63] = 0;
                name = juce::String::fromUTF8 (nm);
                return true;
            }
        }
        return false;
    }

    void writeFrame (int slot, int64_t idx, const float* groups, int64_t ms)
    {
        auto& fr = mem->slots[slot].frames[(size_t) (idx % kFrames)];
        fr.idx.store (-1, std::memory_order_release);
        for (int k = 0; k < kGroups; ++k) fr.p[k] = groups[k];
        fr.ms = ms;
        fr.idx.store (idx, std::memory_order_release);
    }

    void writeVerdict (int slot, const Verdict& v)
    {
        auto& s = mem->slots[slot];
        s.vseq.fetch_add (1, std::memory_order_acq_rel);
        std::atomic_thread_fence (std::memory_order_release);
        std::memcpy (&s.v, &v, sizeof (Verdict));
        std::atomic_thread_fence (std::memory_order_release);
        s.vseq.fetch_add (1, std::memory_order_acq_rel);
        s.verdictMs = nowMs();
    }

    /** Висновок Master'а, якщо свіжий (Master живий). */
    bool readVerdict (int slot, Verdict& v) const
    {
        if (mem == nullptr || slot < 0) return false;
        auto& s = mem->slots[slot];
        if (nowMs() - s.verdictMs.load() > 5000) return false;
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const auto a = s.vseq.load (std::memory_order_acquire);
            if (a & 1u) { juce::Thread::yield(); continue; }
            std::memcpy (&v, &s.v, sizeof (Verdict));
            std::atomic_thread_fence (std::memory_order_acquire);
            if (s.vseq.load (std::memory_order_acquire) == a) return true;
        }
        return false;
    }

private:
    std::unique_ptr<juce::MemoryMappedFile> mapped;
    Layout* mem = nullptr;
};

inline void setItem (Verdict::Item& it, int sev, const juce::String& title, const juce::String& text)
{
    it.sev = sev;
    std::memset (it.title, 0, sizeof (it.title));
    std::memset (it.text, 0, sizeof (it.text));
    title.copyToUTF8 (it.title, sizeof (it.title) - 1);
    text.copyToUTF8 (it.text, sizeof (it.text) - 1);
}
} // namespace mix
