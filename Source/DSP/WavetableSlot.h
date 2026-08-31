#pragma once

#include "Wavetable.h"

#include <atomic>
#include <memory>
#include <vector>

namespace px3
{

// Hands a wavetable from the thread that built it to the thread that plays it.
//
// The audio thread cannot allocate, cannot free, and cannot take a lock, which
// rules out the obvious implementation. It also rules out the nearly-obvious
// one: swapping a std::shared_ptr looks atomic and is not - the reference count
// is a shared cache line that both threads write, and dropping the last
// reference frees on whichever thread happened to be last. That thread would
// sometimes be the audio thread.
//
// So the audio thread only ever reads a raw pointer, and ownership stays on the
// message thread. A replaced table is not freed when it is replaced; it is
// retired, and freed later, once the audio thread has demonstrably started a
// block after the swap and therefore cannot still be holding it.
//
// The contract that makes that safe is small but real: **the audio thread takes
// the pointer once per block, via beginBlock, and does not hold it across
// blocks.**
class WavetableSlot
{
public:
    // Message thread. The previous table is retired rather than freed.
    void publish(std::shared_ptr<const Wavetable> table)
    {
        if (owner != nullptr)
        {
            retired.push_back({ std::move(owner), epoch.load(std::memory_order_relaxed) });
        }

        owner = std::move(table);
        live.store(owner.get(), std::memory_order_release);
    }

    // Audio thread, once per block. The returned pointer is valid until the next
    // call, and must not be kept past it.
    const Wavetable* beginBlock() noexcept
    {
        // Published before the load and freed only well after it: the release
        // above pairs with this acquire so the table's contents are visible,
        // not just its address.
        epoch.fetch_add(1, std::memory_order_relaxed);
        return live.load(std::memory_order_acquire);
    }

    // Audio thread, within a block. Whatever beginBlock last returned.
    const Wavetable* current() const noexcept
    {
        return live.load(std::memory_order_acquire);
    }

    // Message thread. Frees what the audio thread can no longer be inside.
    //
    // One epoch would be enough if beginBlock and publish could not overlap;
    // they can, so a table retired during a block that had already taken it
    // needs that block to end AND the next one to start. Two is that guarantee.
    void collectRetired()
    {
        const auto now = epoch.load(std::memory_order_relaxed);
        retired.erase(std::remove_if(retired.begin(), retired.end(),
                                     [now](const Retired& r)
                                     {
                                         return now > r.atEpoch + 1;
                                     }),
                      retired.end());
    }

    // Message thread. Diagnostics - a number that never falls is a leak.
    int getRetiredCount() const { return static_cast<int>(retired.size()); }

    bool hasTable() const noexcept { return live.load(std::memory_order_acquire) != nullptr; }

private:
    struct Retired
    {
        std::shared_ptr<const Wavetable> table;
        std::uint32_t atEpoch { 0 };
    };

    std::shared_ptr<const Wavetable> owner;          // message thread only
    std::vector<Retired> retired;                    // message thread only
    std::atomic<const Wavetable*> live { nullptr };
    std::atomic<std::uint32_t> epoch { 0 };
};

} // namespace px3
