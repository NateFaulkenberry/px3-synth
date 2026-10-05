#pragma once

#include <atomic>
#include <memory>

namespace px3
{

// An effect whose storage exists only once the patch can hear it.
//
// DOOM's history and grain buffers are ~3.3 MB per instance at 48 kHz and
// LUCY's FFT plans and lines ~0.7 MB, and an instance whose effect is off or at
// zero mix contributes nothing from either - the engines go idle and clear
// themselves. So the engine object is built only when it is first wanted.
//
// The audio thread can neither allocate nor free, so the lifecycle is split:
//   - audio thread: get() once per block. Null means "not built yet": the
//     caller passes dry and calls request() + triggerAsyncUpdate() (both
//     lock-free) when the patch wants the effect;
//   - message thread: build() constructs, prepares and initialises the
//     engine, then publishes the pointer. The engine fades itself in from
//     silence on its first block, so the handover cannot click;
//   - release() frees it, and is only called where the audio thread is known
//     not to be running (prepareToPlay / releaseResources). The audio thread
//     therefore never sees a pointer freed under it, and no retire protocol is
//     needed.
//
// Callers serialise build/release/prepare on the non-audio threads with their
// own lock; this class only makes the audio thread's read safe.
template <typename Effect>
class LazyEffect
{
public:
    // Audio thread, once per block; the pointer must not be kept past it.
    Effect* get() const noexcept { return live.load(std::memory_order_acquire); }

    // Audio thread. Lock-free.
    void request() noexcept { requested.store(true, std::memory_order_relaxed); }

    // Non-audio threads.
    bool isRequested() const noexcept { return requested.load(std::memory_order_relaxed); }
    bool isBuilt() const noexcept { return owner != nullptr; }
    Effect* owned() noexcept { return owner.get(); }

    // Non-audio thread. `initialise` prepares the new engine before anything
    // can see it. Does nothing if it already exists.
    template <typename Initialise>
    void build(Initialise&& initialise)
    {
        requested.store(false, std::memory_order_relaxed);
        if (owner != nullptr)
        {
            return;
        }

        auto effect = std::make_unique<Effect>();
        initialise(*effect);
        owner = std::move(effect);
        live.store(owner.get(), std::memory_order_release);
    }

    // Only while the audio thread is not running.
    void release()
    {
        live.store(nullptr, std::memory_order_release);
        owner.reset();
        requested.store(false, std::memory_order_relaxed);
    }

private:
    std::unique_ptr<Effect> owner;                // non-audio threads only
    std::atomic<Effect*> live { nullptr };
    std::atomic<bool> requested { false };
};

} // namespace px3
