#include "MoodControlModel.h"

#include <algorithm>
#include <cmath>

namespace px3
{
namespace
{

float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

float lerp(float t, float at0, float at1)
{
    return at0 + (at1 - at0) * clamp01(t);
}

// Three octaves of semitone steps.
constexpr int kClockMaxSteps = 36;

// Playback speeds for TAPE, exactly the harmonised set the mode is specified
// with: quarter, half, unity and double, in each direction.
constexpr std::array<float, 8> kTapeSpeeds { { -4.0f, -2.0f, -1.0f, -0.5f,
                                                0.5f, 1.0f, 2.0f, 4.0f } };

} // namespace

namespace mood_control
{

// ---- CLOCK ----------------------------------------------------------------

int clockStepCount()
{
    return kClockMaxSteps;
}

float mapClockToSemitones(float clock)
{
    // Quantised, so the transposition lands on musical intervals rather than
    // between them. Full clock is no transposition; the knob runs downwards.
    return std::round((1.0f - clamp01(clock)) * static_cast<float>(kClockMaxSteps));
}

float mapClockToDivider(float clock)
{
    // Clamped to three octaves. Beyond that the zero-order hold on the way
    // back up to the host rate stops being character and starts being noise.
    const auto ratio = std::pow(2.0f, mapClockToSemitones(clock) / 12.0f);
    return std::clamp(ratio, 1.0f, 8.0f);
}

// ---- LOOP LENGTH ----------------------------------------------------------

float mapLoopLengthToEnvSlice(float length)
{
    // Short, because ENV captures a slice rather than a phrase.
    constexpr auto kShortest = 0.03f;
    constexpr auto kLongest = 0.40f;
    return lerp(length, kShortest, kLongest);
}

float mapLoopLengthToTapeLoop(float length)
{
    // The full range a tape loop wants: a flutter at the bottom, a phrase at
    // the top.
    constexpr auto kShortest = 0.05f;
    constexpr auto kLongest = 2.20f;
    return lerp(length, kShortest, kLongest);
}

float mapLoopLengthToStretchGrain(float length)
{
    // Slice size. Longer slices carry recognisable phrases, shorter ones blur
    // into grain, which is the whole span this mode plays across.
    constexpr auto kShortestMs = 22.0f;
    constexpr auto kLongestMs = 210.0f;
    return lerp(length, kShortestMs, kLongestMs) / 1000.0f;
}

// ---- LOOP MODIFY ----------------------------------------------------------

float mapLoopModifyToEnvThreshold(float modify)
{
    // INVERTED. The knob is sensitivity, so the threshold has to fall as it
    // rises - see the header for why the range is where it is.
    constexpr auto kLeastSensitive = 0.140f;
    constexpr auto kMostSensitive = 0.002f;
    return lerp(modify, kLeastSensitive, kMostSensitive);
}

int tapeRateCount()
{
    return static_cast<int>(kTapeSpeeds.size());
}

float tapeRateAt(int index)
{
    const auto i = std::clamp(index, 0, static_cast<int>(kTapeSpeeds.size()) - 1);
    return kTapeSpeeds[static_cast<std::size_t>(i)];
}

int mapLoopModifyToTapeRateIndex(float modify, int rateCount)
{
    // Eight countable positions rather than a continuum. A tape head at an
    // arbitrary ratio is a detuning; at one of these it is an interval.
    const auto count = std::max(1, rateCount);
    return std::clamp(static_cast<int>(clamp01(modify) * static_cast<float>(count)),
                      0, count - 1);
}

float mapLoopModifyToStretchWalk(float modify)
{
    // Bipolar around noon: below it the playhead walks backwards through the
    // loop, above it forwards, and at the top it keeps pace with the recording
    // so nothing is stretched at all.
    return (clamp01(modify) - 0.5f) * 2.0f;
}

float mapLoopModifyToStretchPanHz(float modify)
{
    // Distance from frozen, not the knob itself: motion is slowest at noon and
    // speeds up in both directions.
    constexpr auto kSlowest = 0.05f;
    constexpr auto kRange = 0.60f;
    return kSlowest + std::abs(clamp01(modify) - 0.5f) * kRange;
}

// ---- WET TIME -------------------------------------------------------------

float mapWetTimeToReverbScale(float time)
{
    constexpr auto kSmallest = 0.4f;
    constexpr auto kLargest = 3.0f;
    return lerp(time, kSmallest, kLargest);
}

float mapWetTimeToDelaySeconds(float time)
{
    constexpr auto kShortest = 0.03f;
    constexpr auto kLongest = 1.60f;
    return lerp(time, kShortest, kLongest);
}

float mapWetTimeToSlipWindow(float time)
{
    constexpr auto kShortest = 0.05f;
    constexpr auto kLongest = 0.55f;
    return lerp(time, kShortest, kLongest);
}

// ---- WET MODIFY -----------------------------------------------------------

float mapWetModifyToReverbDiffusion(float modify)
{
    // Smear, which is what the allpass chain is being asked for. Named
    // diffusion rather than feedback because that is what it actually is.
    return clamp01(modify);
}

float mapWetModifyToDelayFeedback(float modify)
{
    // Reaches a true unity at the top: repeats are stable and pile up like a
    // looper, held there by the saturator rather than by a coefficient below
    // one. Anything less than 1.0 here would make the top of the knob a lie.
    return clamp01(modify);
}

float mapWetModifyToSlipSemitones(float modify)
{
    // Quantised, an octave down through neutral to an octave up, in each
    // direction - the same reasoning as TAPE's table.
    constexpr auto kLowest = -24.0f;
    constexpr auto kHighest = 24.0f;
    return std::round(lerp(modify, kLowest, kHighest));
}

// ---- interaction and degradation ------------------------------------------

float mapFeedbackToRecycle(float feedback)
{
    // Stops just short of unity. The recycle path runs through the history
    // buffer, which has no saturator of its own, so this is the one place the
    // ceiling has to come from the coefficient.
    constexpr auto kCeiling = 0.98f;
    return lerp(feedback, 0.0f, kCeiling);
}

float mapDegradeToBits(float degrade)
{
    constexpr auto kCleanest = 16.0f;
    constexpr auto kCoarsest = 3.0f;
    return lerp(degrade, kCleanest, kCoarsest);
}

} // namespace mood_control

MoodDerivedParameters deriveMoodParameters(const MoodUserParameters& user)
{
    using namespace mood_control;

    MoodDerivedParameters d;

    d.clockSemitones = mapClockToSemitones(user.clock);
    d.clockDivider = mapClockToDivider(user.clock);
    d.clockIncrement = 1.0f / d.clockDivider;

    d.envSliceSeconds = mapLoopLengthToEnvSlice(user.loopLength);
    d.envThreshold = mapLoopModifyToEnvThreshold(user.loopModify);

    d.tapeLoopSeconds = mapLoopLengthToTapeLoop(user.loopLength);
    d.tapeRateIndex = mapLoopModifyToTapeRateIndex(user.loopModify, tapeRateCount());
    d.tapePlaybackRate = tapeRateAt(d.tapeRateIndex);

    d.stretchGrainSeconds = mapLoopLengthToStretchGrain(user.loopLength);
    d.stretchWalk = mapLoopModifyToStretchWalk(user.loopModify);
    d.stretchPanHz = mapLoopModifyToStretchPanHz(user.loopModify);

    d.reverbTimeScale = mapWetTimeToReverbScale(user.wetTime);
    d.reverbDiffusion = mapWetModifyToReverbDiffusion(user.wetModify);

    d.delaySeconds = mapWetTimeToDelaySeconds(user.wetTime);
    d.delayFeedback = mapWetModifyToDelayFeedback(user.wetModify);

    d.slipWindowSeconds = mapWetTimeToSlipWindow(user.wetTime);
    d.slipSemitones = mapWetModifyToSlipSemitones(user.wetModify);
    d.slipPlaybackRate = std::pow(2.0f, d.slipSemitones / 12.0f);

    d.loopFeedback = mapFeedbackToRecycle(user.feedback);
    d.spread = clamp01(user.spread);
    d.mix = clamp01(user.mix);

    d.degradeAmount = clamp01(user.degrade);
    d.degradeBits = mapDegradeToBits(user.degrade);

    return d;
}

} // namespace px3
