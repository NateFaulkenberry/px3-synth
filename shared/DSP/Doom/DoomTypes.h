#pragma once

// DOOM's user-facing control set.
//
// Six primary controls, each carrying a second function, plus the mode and
// routing selectors - which is how the pedal DOOM takes its control philosophy
// from is operated. See docs/DOOM_DSP_DESIGN.md.
//
//   TIME          / CROSS        wet channel
//   WET MODIFY    / EQ           wet channel
//   LENGTH        / FADE         micro-looper
//   LOOP MODIFY   / BLEND        micro-looper
//   CLOCK         / GLUE         both channels
//   MIX           / BALANCE      output
//
// The four macros - TIME, WET MODIFY, LENGTH, LOOP MODIFY - mean different
// things in different modes. That is deliberate and is the whole point of the
// control scheme: DoomControlModel turns each of them into whichever DSP
// quantity the current mode needs, so the engine is handed a delay time or a
// harmony index rather than a knob position.
//
// DOOM reproduces the documented BAD MOOD control philosophy using its own
// DSP; it does not reproduce, and does not claim to reproduce, the proprietary
// Chase Bliss implementation.

namespace px3
{

// What the wet channel does. Named rather than indexed so a mode cannot be
// confused with a loop mode - both used to be a bare int.
enum class DoomWetMode
{
    soup = 0,   // spectral decay
    relay,      // countable parallel repeats
    flip        // granular harmony
};

enum class DoomLoopMode
{
    burst = 0,  // onset-sliced re-sequencing
    radio,      // five stations on a scan axis
    mask        // envelope-gated disguise
};

// What the wet channel is fed. The micro-looper keeps recording in every one
// of these: always-listening is a property of the looper, not of the routing.
enum class DoomRouting
{
    input = 0,
    inputPlusLoop,
    loop
};

// What CROSS follows.
enum class DoomCrossSource
{
    input = 0,  // your playing modulates both channels
    channel     // the channels modulate one another
};

struct DoomUserParameters
{
    bool enabled { true };

    // ---- the six primaries ------------------------------------------------
    //
    // MIX is how much DOOM you hear, and nothing else: it is not a channel
    // balance and not an output gain. Those are BALANCE and GLUE.
    float mix { 0.35f };

    // CLOCK is the ENGINE's sample rate, which is what ties the two channels
    // together: the loop's length and pitch and the wet channel's time and
    // bandwidth, all from one control.
    float clock { 1.0f };

    float wetTime { 0.45f };      // mode dependent: decay / delay / lag
    float wetModify { 0.40f };    // mode dependent: character / repeats / voices
    float loopLength { 0.45f };   // mode dependent: pace / station length / character
    float loopModify { 0.50f };   // mode dependent: fills / station / threshold

    // ---- their alternate functions ----------------------------------------
    float cross { 0.0f };         // alt of TIME       - channel interference
    float balance { 0.5f };       // alt of MIX        - looper <-> wet
    float fade { 1.0f };          // alt of LENGTH     - loop retention per lap
    float eq { 0.0f };            // alt of WET MODIFY - global tilt, -1 dark .. +1 bright
    float glue { 0.15f };         // alt of CLOCK      - warm, then destroy
    float blend { 0.0f };         // alt of LOOP MODIFY- clean loop past the wet channel

    // ---- modes and routing -------------------------------------------------
    DoomWetMode wetMode { DoomWetMode::soup };
    DoomLoopMode loopMode { DoomLoopMode::radio };
    DoomRouting routing { DoomRouting::input };
    DoomCrossSource crossSource { DoomCrossSource::input };

    // ---- channel states ----------------------------------------------------
    //
    // Not knobs, and not MIX or BALANCE: a channel can be on and still be
    // almost inaudible because of where those two are set.
    bool loopActive { false };    // false = always-listening (still recording)
    bool wetActive { true };

    // ---- secondary options -------------------------------------------------
    bool clockSmooth { false };   // continuous sweep instead of harmonised steps
    bool loopHalf { false };      // half the captured length
    bool freeze { false };        // hold the wet channel
    float overdub { 0.0f };
    float spread { 0.5f };        // per-mode stereo, not a width algorithm
};

} // namespace px3
