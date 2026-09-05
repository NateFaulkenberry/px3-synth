#pragma once

// LUCY's user-facing control set.
//
// These are the things a person turns. They describe MUSICAL intent - how
// degraded, how fast, how intense - and deliberately do not name any DSP
// quantity. LucyControlModel turns them into LucyDerivedParameters, which is
// what the engine consumes. See docs/LUCY_DSP_DESIGN.md.
//
// Lossy documents six primary knobs, each with an alternate function, three
// toggles and a freeze footswitch. LUCY reproduces that CONTROL PHILOSOPHY
// using its own DSP; it does not reproduce, and does not claim to reproduce,
// the proprietary Chase Bliss / Goodhertz implementation.

namespace px3
{

// OFF, SOLID and SLUSHY as one state rather than two booleans that can express
// a fourth combination nobody means. "Slushy while not frozen" was reachable
// before and did nothing.
enum class LucyFreezeMode
{
    off = 0,
    solid,
    slushy
};

// Which end of the spectrum the coder protects. Three states rather than a
// continuous tilt: the user is choosing a character, not dialling a curve.
enum class LucyWeighting
{
    dark = 0,
    neutral,
    bright
};

// 6 / 24 / 96 dB, as printed on the control. How many filter sections that
// takes is the filter's business and is not a user parameter.
enum class LucyFilterSlope
{
    gentle6dB = 0,
    medium24dB,
    steep96dB
};

// CLEAN, LOSS, REPEAT. The burst model behind them stays internal.
enum class LucyPacketMode
{
    clean = 0,
    loss,
    repeat
};

// STANDARD, INVERSE, JITTER.
enum class LucyLossMode
{
    standard = 0,
    inverse,
    jitter
};

struct LucyUserParameters
{
    bool enabled { true };

    // ---- the three macros ------------------------------------------------
    //
    // GLOBAL is how strongly the character the other controls describe is
    // expressed - not a wet/dry. Zero by default: adding an effect must not
    // change what existing patches sound like.
    float global { 0.0f };

    // LOSS is degradation DEPTH and spectral COVERAGE together, which is how
    // the source describes it: the affected region widens as it rises.
    float loss { 0.55f };

    // SPEED is one temporal control for loss decisions, packet state and
    // freeze evolution alike. There is deliberately no per-stage rate.
    float speed { 0.5f };

    // ---- direct controls --------------------------------------------------
    float filter { 0.0f };      // band width; zero is no filtering at all
    float filterFreq { 0.5f };  // band centre
    LucyFilterSlope slope { LucyFilterSlope::medium24dB };
    bool filterInvert { false };

    float verb { 0.0f };
    bool verbPost { false };    // false = PRE, feeding the degradation

    LucyPacketMode packets { LucyPacketMode::clean };
    LucyLossMode mode { LucyLossMode::standard };
    LucyWeighting weighting { LucyWeighting::neutral };

    LucyFreezeMode freeze { LucyFreezeMode::off };
    bool gate { false };
    bool slow { false };        // bigger, darker, slower, more latency

    // ---- alternate functions of the six knobs -----------------------------
    //
    // FILTER/GATE, GLOBAL/FREEZER, VERB/DECAY, FREQ/THRESHOLD,
    // SPEED/AUTO GAIN, LOSS/LOSS GAIN. Each is a real parameter in its own
    // right; "alternate" describes where it lives on the panel, not how it is
    // automated.
    float gateThreshold { 0.25f };
    float freezer { 1.0f };        // live <-> frozen balance
    float verbDecay { 0.45f };

    // The LIMITER's threshold. Named in full because the loss coder has a
    // masking threshold of its own and "threshold" would be ambiguous; the
    // masking one is derived, internal, and never a user parameter.
    float limiterThreshold { 0.8f };

    float autoGain { 0.75f };
    float lossGainDb { 0.0f };     // wet gain, -36 .. +36

    // Not on the pedal's face, and not promoted to one of the six.
    float spread { 0.5f };
};

} // namespace px3
