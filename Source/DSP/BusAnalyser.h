#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>

namespace px3
{

// A lock-free tap for drawing a bus's spectrum.
//
// The audio thread writes single samples into a fixed ring; the UI thread reads
// the most recent window out of it whenever it feels like repainting. There is
// no allocation, no lock and no queue on the audio side - one store and one
// relaxed increment - which is the only shape of tap that belongs in
// processBlock.
//
// The read is deliberately allowed to race with the writer. A display is not
// worth synchronising for: the worst case is one frame containing a sample
// boundary that never existed, which is invisible after windowing. The ring is
// sized at twice the window so the writer cannot lap the reader within a frame
// even at 192 kHz.
//
// Nothing is written at all unless a UI has asked for it. An overlay that is
// closed costs the audio thread one relaxed load per sample and nothing else,
// which keeps the "an insert you are not using is free" property intact.
class BusAnalyser
{
public:
    static constexpr int kWindowSize = 2048;
    static constexpr int kRingSize = 4096;   // power of two, twice the window

    void reset()
    {
        buffer.fill(0.0f);
        writeIndex.store(0, std::memory_order_relaxed);
    }

    void setActive(bool shouldBeActive) noexcept
    {
        active.store(shouldBeActive, std::memory_order_relaxed);
    }

    bool isActive() const noexcept { return active.load(std::memory_order_relaxed); }

    // Audio thread. Mono sum: the display shows what the bus is carrying, and
    // two traces that differ only in stereo detail are harder to read, not
    // more informative.
    void push(float left, float right) noexcept
    {
        if (! active.load(std::memory_order_relaxed))
        {
            return;
        }

        const auto index = writeIndex.load(std::memory_order_relaxed);
        buffer[index & (kRingSize - 1)] = 0.5f * (left + right);
        // Release, paired with the reader's acquire: the sample must be visible
        // before the index that advertises it.
        writeIndex.store(index + 1u, std::memory_order_release);
    }

    // UI thread. Copies the most recent kWindowSize samples, oldest first.
    void readWindow(float* destination) const noexcept
    {
        const auto end = writeIndex.load(std::memory_order_acquire);
        const auto start = end - static_cast<std::uint32_t>(kWindowSize);
        for (int i = 0; i < kWindowSize; ++i)
        {
            destination[i] = buffer[(start + static_cast<std::uint32_t>(i)) & (kRingSize - 1)];
        }
    }

private:
    // Unsigned so wrap-around is defined. A signed index wrapping negative
    // would index the ring backwards.
    std::array<float, kRingSize> buffer {};
    std::atomic<std::uint32_t> writeIndex { 0 };
    std::atomic<bool> active { false };
};

} // namespace px3
