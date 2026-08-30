#pragma once

#include <array>

// Everything the two bus inserts need for one block.
// See docs/V3_1_EQ_COMP_RESEARCH.md.
namespace px3
{

inline constexpr int kEqBandCount = 4;

// Bands 1 and 4 can be a shelf or a pass filter. Bus low end is almost always
// a shelf, but the single most useful move on an FX return that is too bright
// is a low-pass - so the outer bands switch and the inner two stay bells.
enum class EqBandType : int
{
    lowShelf = 0,
    highPass,
    bell,
    highShelf,
    lowPass
};

struct EqBandSettings
{
    EqBandType type { EqBandType::bell };
    float frequencyHz { 1000.0f };
    float gainDb { 0.0f };
    // On a bell this is Q. On a shelf it is the shelf slope S, restricted to a
    // range that cannot overshoot: S = 1 is the steepest slope that stays
    // monotonic, and anything past it dips before the shelf - which on a bus
    // reads as a phase problem rather than as tone.
    float q { 0.707f };
};

struct EqSettings
{
    bool enabled { false };
    std::array<EqBandSettings, kEqBandCount> bands { {
        { EqBandType::lowShelf,  100.0f, 0.0f, 0.707f },
        { EqBandType::bell,      300.0f, 0.0f, 0.900f },
        { EqBandType::bell,     3000.0f, 0.0f, 0.900f },
        { EqBandType::highShelf, 8000.0f, 0.0f, 0.707f },
    } };
};

// The four ratio buttons, plus the state that engaging all of them produces.
// They are not switch positions in a gain computer: in the hardware each
// button applies a bias level to the detector diodes, so ratio and threshold
// are the same control. All four at once puts four bias networks in parallel -
// a state no single button produces.
enum class CompRatio : int
{
    fourToOne = 0,
    eightToOne,
    twelveToOne,
    twentyToOne,
    allButtons
};

// What the moving-coil meter is wired to. The hardware switches its movement
// between gain reduction and two level calibrations; this is the same idea with
// the two levels named for what they are.
enum class CompMeterMode
{
    gainReduction = 0,
    input,
    output
};

struct CompressorSettings
{
    bool enabled { false };

    // The 1176 has no threshold control. Input drives the signal into a fixed
    // threshold, which is why "turn Input up until it sounds right" is the
    // actual workflow, and why this is the primary control.
    float inputDb { 0.0f };
    float outputDb { 0.0f };

    // Both reversed on the hardware - fully clockwise is FASTEST - and the UI
    // reproduces that. Stored here as a plain 0..1 where 1 is fastest, so the
    // DSP never has to know about the panel.
    float attack { 0.6f };
    float release { 0.5f };

    CompRatio ratio { CompRatio::fourToOne };

    // Parallel blend. 0 = dry, 1 = fully compressed. The compressor gets this
    // and the EQ does not: blending a compressed signal against its own dry is
    // standard practice for bus glue, and blending an EQ against its own dry
    // is just a smaller EQ move.
    float mix { 1.0f };

    // Stereo link. Unlinked, a loud transient on one side pulls only that side
    // down and the image shifts.
    bool stereoLink { true };
    CompMeterMode meterMode { CompMeterMode::gainReduction };
};

} // namespace px3
