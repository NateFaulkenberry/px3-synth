#pragma once

// MOOD's user-facing control set.
//
// These are the things a musician turns. Four of them - LENGTH, MODIFY, TIME
// and the second MODIFY - mean different things depending on the mode beside
// them, and that is the control scheme rather than an inconsistency:
// MoodControlModel turns each into whichever DSP quantity the current mode
// needs, so the engine is handed a slice duration or a playback rate rather
// than a knob position.
//
// See docs/MOOD_DSP_DESIGN.md.
//
// MOOD reproduces a documented two-channel micro-looper control philosophy
// using its own DSP. It does not reproduce, and does not claim to reproduce,
// any proprietary implementation.

namespace px3
{

// What the wet channel is fed.
//
// A three-way choice, and an enum rather than the float it used to be. That
// float carried index/2, was smoothed per sample as though it were audio, and
// was recovered by comparing against 0.33 and 0.66 - an arrangement in which
// LOOP->WET and PARALLEL were once wired the wrong way round, each doing what
// the other's label said. This makes that unrepresentable.
enum class MoodRouting
{
    dryToWet = 0,   // the wet channel hears the input
    loopToWet,      // the wet channel hears the micro-loop alone
    parallel        // the wet channel hears both
};

enum class MoodWetMode
{
    reverb = 0,
    delay,
    slip
};

enum class MoodLoopMode
{
    env = 0,        // envelope-gated slice capture
    tape,           // variable-speed heads
    stretch         // granular cloud
};

struct MoodUserParameters
{
    bool enabled { true };

    // ---- global ------------------------------------------------------------
    float mix { 0.35f };

    // CLOCK is the engine's sample rate, and it is what ties the two channels
    // together: audio recorded at one rate and played back at another changes
    // speed and pitch at once.
    float clock { 1.0f };

    // Per-mode stereo, not a width algorithm bolted on the end. Every mode
    // images differently and SPREAD at zero must leave an incoming image alone.
    float spread { 0.50f };

    MoodRouting routing { MoodRouting::dryToWet };

    // ---- micro-looper ------------------------------------------------------
    MoodLoopMode loopMode { MoodLoopMode::env };
    float loopLength { 0.28f };   // ENV slice / TAPE loop / STRETCH grain
    float loopModify { 0.50f };   // ENV sensitivity / TAPE rate / STRETCH motion

    // ---- wet channel -------------------------------------------------------
    MoodWetMode wetMode { MoodWetMode::reverb };
    float wetTime { 0.40f };      // REVERB decay / DELAY time / SLIP window
    float wetModify { 0.45f };    // REVERB smear / DELAY feedback / SLIP rate

    // ---- interaction -------------------------------------------------------
    //
    // How much of both channels is recycled into the always-listening history,
    // which is what makes the two aware of each other.
    float feedback { 0.35f };
    bool freeze { false };

    // ---- a PX3 extension, not a pedal control ------------------------------
    //
    // Bit reduction, sample-rate reduction and a gated noise floor behind one
    // knob. Deliberately separate from CLOCK: DEGRADE roughens the loop, CLOCK
    // transposes it, and neither does the other's job.
    float degrade { 0.20f };
};

} // namespace px3
