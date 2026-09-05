#include "DoomControlModel.h"

#include <algorithm>
#include <array>
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

// The harmonised CLOCK steps. Simple integer ratios, so each step is a musical
// interval on everything the engine holds: octaves (1/2, 1/4, 1/8, 1/16),
// fifths (2/3, 1/3), fourths (3/4, 3/8, 3/16) and a twelfth (1/12).
constexpr std::array<float, 11> kClockRatios {
    { 1.0f / 16.0f, 1.0f / 12.0f, 1.0f / 8.0f, 3.0f / 16.0f, 1.0f / 4.0f, 1.0f / 3.0f,
      3.0f / 8.0f, 1.0f / 2.0f, 2.0f / 3.0f, 3.0f / 4.0f, 1.0f }
};

constexpr float kMinClockRatio = 1.0f / 16.0f;

} // namespace

namespace doom_control
{

// ---- CLOCK ----------------------------------------------------------------

int clockStepCount()
{
    return static_cast<int>(kClockRatios.size());
}

float mapClockToRatio(float clock, bool smooth)
{
    const auto c = clamp01(clock);

    if (smooth)
    {
        // Exponential, so the knob's feel matches the stepped version: equal
        // knob travel is equal PITCH travel, which is what the ratios are.
        return kMinClockRatio * std::pow(1.0f / kMinClockRatio, c);
    }

    const auto count = static_cast<int>(kClockRatios.size());
    const auto index = std::clamp(static_cast<int>(c * static_cast<float>(count - 1) + 0.5f),
                                  0, count - 1);
    return kClockRatios[static_cast<std::size_t>(index)];
}

// ---- BURST ----------------------------------------------------------------

float mapLengthToBurstStep(float length)
{
    // INVERTED: more LENGTH is a faster sequence and therefore a SHORTER step,
    // because what the knob sets is the pace and the step size follows from it.
    constexpr auto kSlowestStep = 0.35f;
    constexpr auto kFastestStep = 0.03f;
    return lerp(length, kSlowestStep, kFastestStep);
}

float mapLoopModifyToBurstSensitivity(float modify)
{
    // The envelope a live note has to exceed to scramble the pattern. Inverted
    // and offset: at the top of the knob almost anything you play reorders it,
    // at the bottom the sequence is left alone.
    return (1.0f - clamp01(modify)) * 0.5f + 0.02f;
}

// ---- RADIO ----------------------------------------------------------------

void mapLoopModifyToStation(float modify, int stationCount, int& lower, int& upper, float& blend)
{
    const auto count = std::max(1, stationCount);

    // A SCAN, not a selector. Between two centres both stations are audible
    // and static rises; at a centre one is alone and the static falls away -
    // which is what tuning a radio until it stops hissing is.
    const auto position = clamp01(modify) * static_cast<float>(count - 1);
    lower = std::clamp(static_cast<int>(position), 0, count - 1);
    upper = std::clamp(lower + 1, 0, count - 1);
    blend = position - static_cast<float>(lower);
}

// ---- MASK -----------------------------------------------------------------

float mapLengthToMaskCharacter(float length)
{
    return clamp01(length);
}

float mapLoopModifyToMaskThreshold(float modify)
{
    // Fully down is the untouched loop - the documented "good listen"
    // position, and a useful place to build a loop up before mangling it. The
    // engine tests for <= 0.001, so this must reach a true zero.
    return clamp01(modify);
}

// ---- SOUP -----------------------------------------------------------------

float mapWetTimeToSoupT60(float time)
{
    // Squared: reverberation time is perceived logarithmically, and a linear
    // knob spends its whole top half in decays that are hard to tell apart.
    constexpr auto kShortest = 0.25f;
    constexpr auto kLongest = 14.0f;
    const auto t = clamp01(time);
    return lerp(t * t, kShortest, kLongest);
}

float mapWetModifyToSoupCharacter(float modify)
{
    return clamp01(modify);
}

// ---- RELAY ----------------------------------------------------------------

float mapWetTimeToRelayDelay(float time)
{
    // Squared, as SOUP's is, and for the same reason: the useful short delays
    // would otherwise be crowded into the first few degrees of travel.
    constexpr auto kShortest = 0.03f;
    constexpr auto kLongest = 0.90f;
    const auto t = clamp01(time);
    return lerp(t * t, kShortest, kLongest);
}

int mapWetModifyToRelayTaps(float modify, int maxTaps)
{
    // A COUNT. The knob has eight countable positions rather than a continuum
    // of fractional repeats that have no musical meaning.
    const auto top = std::max(1, maxTaps);
    return std::clamp(1 + static_cast<int>(clamp01(modify) * (static_cast<float>(top) - 0.01f)),
                      1, top);
}

bool mapWetModifyToRelayInfinite(float modify)
{
    // The last position, where the repeats stop decaying and pile up the way a
    // looper does.
    constexpr auto kInfiniteAbove = 0.97f;
    return clamp01(modify) > kInfiniteAbove;
}

// ---- FLIP -----------------------------------------------------------------

float mapWetTimeToFlipLag(float time)
{
    return clamp01(time);
}

int mapWetModifyToFlipHarmony(float modify, int harmonyCount)
{
    // The table widens as the knob rises, so this is an index into an ordered
    // set rather than an arbitrary choice.
    const auto count = std::max(1, harmonyCount);
    return std::clamp(static_cast<int>(clamp01(modify) * (static_cast<float>(count) - 0.01f)),
                      0, count - 1);
}

} // namespace doom_control

DoomDerivedParameters deriveDoomParameters(const DoomUserParameters& user,
                                           int relayMaxTaps,
                                           int harmonyCount,
                                           int radioStationCount)
{
    using namespace doom_control;

    DoomDerivedParameters d;

    // CLOCK first: it is the engine's rate, and everything measured in engine
    // samples below inherits it.
    d.clockRatio = mapClockToRatio(user.clock, user.clockSmooth);

    // Every mode's values, not just the active one. Between them they are a
    // handful of multiplies, and computing all of them lets a test assert what
    // a mode WOULD do without having to switch into it.
    d.burstStepSeconds = mapLengthToBurstStep(user.loopLength);
    d.burstSensitivity = mapLoopModifyToBurstSensitivity(user.loopModify);

    d.radioLength = clamp01(user.loopLength);
    mapLoopModifyToStation(user.loopModify, radioStationCount,
                           d.radioLowerStation, d.radioUpperStation, d.radioStationBlend);

    d.maskCharacter = mapLengthToMaskCharacter(user.loopLength);
    d.maskThreshold = mapLoopModifyToMaskThreshold(user.loopModify);

    d.soupT60Seconds = mapWetTimeToSoupT60(user.wetTime);
    d.soupCharacter = mapWetModifyToSoupCharacter(user.wetModify);

    d.relayDelaySeconds = mapWetTimeToRelayDelay(user.wetTime);
    d.relayTaps = mapWetModifyToRelayTaps(user.wetModify, relayMaxTaps);
    d.relayInfinite = mapWetModifyToRelayInfinite(user.wetModify);

    d.flipLagSeconds = mapWetTimeToFlipLag(user.wetTime);
    d.flipHarmonyIndex = mapWetModifyToFlipHarmony(user.wetModify, harmonyCount);

    // The alternates pass through as themselves. Each is already the quantity
    // its stage wants, and inventing a curve for one would be a change to the
    // sound dressed up as a refactor.
    d.crossDepth = clamp01(user.cross);
    d.crossSource = user.crossSource;
    d.glueDrive = clamp01(user.glue);
    d.eqTilt = std::clamp(user.eq, -1.0f, 1.0f);
    d.channelBalance = clamp01(user.balance);
    d.loopBlend = clamp01(user.blend);
    d.loopFadeRetain = clamp01(user.fade);
    d.stereoSpread = clamp01(user.spread);
    d.mix = clamp01(user.mix);

    return d;
}

} // namespace px3
