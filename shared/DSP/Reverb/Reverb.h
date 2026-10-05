#pragma once

#include "ReverbMapping.h"
#include "ReverbTypes.h"

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

// PX3's reverb: six algorithmic types behind one set of controls.
//
//   ROOM   image-source early reflections feeding a dense, rotating 16-line field
//   PLATE  Dattorro's tank, corrected (decay in seconds, true damping, dense input)
//   HALL   stereo-diffused 16-line FDN with three-band Jot decay
//   CLOUD  bloom diffusers into a long 16-line FDN; SHIMMER = +12 st regeneration
//   SPRING Valimaki/Parker/Abel stretched-allpass chirp loops, two springs
//   GATED  feed-forward shaped burst (gate / reverse / falling)
//
// Signal path: input DC block -> PRE-DELAY (two-tap crossfade on change) ->
// type -> WIDTH (mid/side; never changes the mono sum) -> per-type level ->
// equal-power MIX. Nothing nonlinear and nothing adaptive on the way.
//
// Storage: one arena sized for the largest type, allocated in prepare(). A
// type switch fades the old tail out (50 ms, smoothstep), zeroes the arena a
// chunk per control tick on the audio thread, binds the new type and fades it
// in. Nothing allocates after prepare(). The engine also sleeps (and clears
// itself) when MIX is at zero or after 0.3 s of silence in and out.
class Reverb
{
public:
    Reverb();
    ~Reverb();

    void prepare(double sampleRate);
    void reset();

    void updateForBlock(const ReverbSettings& settings, int numSamples);
    void processSampleFrame(float inL, float inR, float& outL, float& outR);

    // Diagnostics and tests.
    int boundType() const noexcept { return boundTypeIndex; }
    bool isRunning() const noexcept { return phase == Phase::running; }
    bool isSwitching() const noexcept { return phase == Phase::fadingOut || phase == Phase::clearing; }
    std::size_t arenaBytes() const noexcept { return arena.size() * sizeof(float); }
    static std::size_t arenaFloatsFor(double sampleRate);

    static constexpr int kControlInterval = 32;

private:
    struct Engines;
    std::unique_ptr<Engines> engines;

    enum class Phase { idle, running, fadingOut, clearing };

    void controlTick();
    void bindType(int type);
    void processEngine(float inL, float inR, float& outL, float& outR) noexcept;

    double sampleRateHz { 48000.0 };
    std::vector<float> arena;
    std::size_t dirtyFloats { 0 };    // how much of the arena the bound type may have written
    std::size_t clearCursor { 0 };
    std::vector<float> preDelayBuffer;  // interleaved L/R
    int preDelayWrite { 0 }, preDelaySize { 0 };
    int preDelayCurrent { 0 }, preDelayNext { 0 };
    float preDelayFade { 1.0f }, preDelayFadeStep { 0.0f };

    ReverbSettings target;
    // Control-rate smoothed copies of the normalised controls.
    std::array<float, 11> smoothed {};
    std::array<float, 11> smoothedTarget {};
    bool controlsPrimed { false };
    float controlCoefficient { 0.4f };

    Phase phase { Phase::idle };
    int boundTypeIndex { -1 };
    int tickCounter { 0 };
    float fade { 0.0f };            // 0..1, through smoothstep
    float fadeInStep { 0.0f }, fadeOutStep { 0.0f };
    // After a type switch the new type's INPUT is faded in (10 ms): audio
    // arriving mid-note into an empty network is a step, and a reverb
    // answers a step with a click.
    float inputFade { 1.0f }, inputFadeStep { 0.0f };
    bool switching { false };

    float amountSmoothed { 0.0f }, amountCoefficient { 0.001f };
    float mixFor { -1.0f }, mixWet { 0.0f }, mixDry { 1.0f };
    float widthSmoothed { 1.0f }, widthCoefficient { 0.001f };
    float wetGain { 1.0f }, wetGainTarget { 1.0f }, wetGainCoefficient { 0.002f };

    float dcCoefficient { 0.996f };
    std::array<float, 2> dcX {}, dcY {};

    float tickInputPeak { 0.0f }, tickWetPeak { 0.0f };
    int silentTicks { 0 }, silentTicksToSleep { 450 };
};
