#include "LucyControlModel.h"

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

// A geometric sweep from `at0` to `at1`. Used for everything SPEED touches,
// because frame counts and drift rates are rates: halving one is the same
// perceptual step wherever on the knob it happens, which a linear map does not
// give you.
float geometric(float t, float at0, float at1)
{
    return at0 * std::pow(at1 / at0, clamp01(t));
}

} // namespace

namespace lucy_control
{

// ---- LOSS -----------------------------------------------------------------

float mapLossToCoverage(float loss)
{
    // A narrow strip that widens to the whole spectrum. The exponent above 1
    // keeps the first quarter of the knob working on a band rather than
    // immediately reaching everywhere; the constant is the strip it starts as.
    constexpr auto kNarrowest = 0.05f;
    constexpr auto kWidest = 1.00f;
    constexpr auto kShape = 1.15f;
    return kNarrowest + (kWidest - kNarrowest) * std::pow(clamp01(loss), kShape);
}

float mapLossToMaskingDepth(float loss)
{
    // Was loss * loss * 1.6, which put the audible change in the top third.
    // 1.6 as an exponent instead reaches a clearly-degraded threshold by half
    // travel while still leaving room above it.
    constexpr auto kFloor = 0.02f;
    constexpr auto kSpan = 1.75f;
    constexpr auto kShape = 1.6f;
    return kFloor + kSpan * std::pow(clamp01(loss), kShape);
}

float mapLossToDiscard(float loss)
{
    // At the bottom only bins far under the threshold are thrown away, so LOSS
    // thins the spectrum before it starts gouging holes in it. At the top the
    // full masking decision applies, which is what a coder does.
    constexpr auto kGentlest = 0.35f;
    constexpr auto kFull = 1.0f;
    constexpr auto kShape = 0.8f;
    return kGentlest + (kFull - kGentlest) * std::pow(clamp01(loss), kShape);
}

float mapLossToQuantisation(float loss)
{
    // Late-weighted on purpose: the magnitude grid is the harshest artifact
    // LUCY has, and fine resolution through the first half is what makes the
    // middle of the knob usable.
    constexpr auto kFinest = 0.05f;
    constexpr auto kCoarsest = 2.40f;
    constexpr auto kShape = 2.0f;
    return kFinest + kCoarsest * std::pow(clamp01(loss), kShape);
}

float mapLossToPacketProbability(float loss)
{
    // Later than everything else. Below about a third of the knob a burst is
    // rare enough to be an event rather than a texture, which is what stops
    // PACKETS from turning into a tremolo the moment LOSS leaves zero.
    constexpr auto kMaximum = 0.50f;
    constexpr auto kShape = 2.4f;
    return kMaximum * std::pow(clamp01(loss), kShape);
}

// ---- SPEED ----------------------------------------------------------------

int mapSpeedToDecisionFrames(float speed)
{
    // 16 frames down to 1. Geometric, so the middle of the knob is 4 rather
    // than the 8 a linear map gives - and 8 to 16 is one perceptual step, not
    // half the travel.
    constexpr auto kSlowest = 16.0f;
    constexpr auto kFastest = 1.0f;
    return std::max(1, static_cast<int>(std::lround(geometric(speed, kSlowest, kFastest))));
}

int mapSpeedToPacketStateFrames(float speed)
{
    // The same shape, over a longer span: a burst decision is a coarser event
    // than a coding decision and wants to be able to persist for longer.
    constexpr auto kSlowest = 24.0f;
    constexpr auto kFastest = 1.0f;
    return std::max(1, static_cast<int>(std::lround(geometric(speed, kSlowest, kFastest))));
}

float mapSpeedToFreezeSlushRate(float speed)
{
    // How fast the frozen spectrum drifts toward the live one. Geometric for
    // the same reason: this is a time constant.
    constexpr auto kSlowest = 0.0015f;
    constexpr auto kFastest = 0.0800f;
    return geometric(speed, kSlowest, kFastest);
}

float mapSpeedToJitterStep(float speed)
{
    constexpr auto kSlowest = 0.02f;
    constexpr auto kFastest = 0.45f;
    return geometric(speed, kSlowest, kFastest);
}

float mapSpeedToJitterTimingRate(float speed)
{
    constexpr auto kSlowest = 0.0006f;
    constexpr auto kFastest = 0.0200f;
    return geometric(speed, kSlowest, kFastest);
}

// ---- GLOBAL ---------------------------------------------------------------

float applyGlobalIntensity(float amount, float global, float exponent)
{
    // The macro's own response comes first. An exponent below 1 on GLOBAL
    // itself is what makes a quarter turn subtle-but-present rather than
    // nearly nothing, which is the documented behaviour of the control.
    constexpr auto kGlobalShape = 0.8f;
    const auto intensity = std::pow(clamp01(global), kGlobalShape);

    // Then the per-parameter shaping, so stages arrive in a deliberate order
    // rather than all at once.
    return amount * std::pow(intensity, exponent);
}

float globalOutputBlend(float global)
{
    // GLOBAL is an intensity macro, NOT a wet/dry - the character the other
    // controls describe has to stay recognisable as it rises, and a crossfade
    // cannot do that because it only ever changes how much of a fixed wet
    // signal you hear.
    //
    // A crossfade survives here across the bottom few percent only, so the
    // effect reaches genuinely clean at zero and the engine's idle path has a
    // continuous way in and out. Above it this is 1 and plays no part.
    constexpr auto kGuardRegion = 0.08f;
    return clamp01(global / kGuardRegion);
}

// ---- direct ---------------------------------------------------------------

float weightingToTilt(LucyWeighting weighting)
{
    switch (weighting)
    {
        case LucyWeighting::dark:   return -1.0f;
        case LucyWeighting::bright: return  1.0f;
        case LucyWeighting::neutral:
        default:                    return  0.0f;
    }
}

int slopeToSections(LucyFilterSlope slope)
{
    switch (slope)
    {
        case LucyFilterSlope::gentle6dB:  return 1;
        case LucyFilterSlope::steep96dB:  return 8;
        case LucyFilterSlope::medium24dB:
        default:                          return 2;
    }
}

float slopeToResonance(LucyFilterSlope slope)
{
    // The per-section Q multiplier, moved here unchanged from the filter.
    //
    // It RISES with the slope, which looks backwards until you read the filter:
    // each section's band-pass output is normalised by 1/Q so its peak gain is
    // 1, which is what lets sections be stacked at all. Normalised that way a
    // cascade of gentle sections is a broad hump, so the steeper settings need
    // a higher per-section Q to read as a steeper version of the same band
    // rather than a wider one.
    switch (slope)
    {
        case LucyFilterSlope::gentle6dB:  return 0.55f;
        case LucyFilterSlope::steep96dB:  return 1.90f;
        case LucyFilterSlope::medium24dB:
        default:                          return 0.90f;
    }
}

} // namespace lucy_control

LucyDerivedParameters deriveLucyParameters(const LucyUserParameters& user)
{
    using namespace lucy_control;

    LucyDerivedParameters d;

    const auto loss = clamp01(user.loss);
    const auto speed = clamp01(user.speed);
    const auto global = clamp01(user.global);

    // ---- LOSS, then GLOBAL over it ---------------------------------------
    //
    // The order matters and is the whole point of the split: LOSS decides the
    // CHARACTER, GLOBAL decides how much of that character is expressed. Doing
    // it the other way - scaling the knob before the curve - would make GLOBAL
    // silently redefine what LOSS means.
    d.spectralCoverage    = applyGlobalIntensity(mapLossToCoverage(loss), global);
    d.maskingDepth        = applyGlobalIntensity(mapLossToMaskingDepth(loss), global);
    d.quantisationAmount  = applyGlobalIntensity(mapLossToQuantisation(loss), global);

    // Discard is a RATIO, not an amount, so scaling it toward zero would mean
    // "discard nothing under the threshold", which is not what a quieter
    // effect sounds like. It follows LOSS alone; GLOBAL reaches this stage
    // through the depth and coverage above.
    d.discardAmount       = mapLossToDiscard(loss);

    // Packets arrive late in GLOBAL as well as late in LOSS. A dropout at a
    // quarter of the intensity macro is not "subtle".
    d.packetProbability   = applyGlobalIntensity(mapLossToPacketProbability(loss), global, 1.6f);
    d.packetStateFrames   = mapSpeedToPacketStateFrames(speed);

    // ---- SPEED, one control -----------------------------------------------
    d.decisionFrames      = mapSpeedToDecisionFrames(speed);
    d.freezeSlushRate     = mapSpeedToFreezeSlushRate(speed);
    d.jitterWalkStep      = mapSpeedToJitterStep(speed);

    // JITTER's phase depth belongs to LOSS - it is degradation depth - while
    // its step belongs to SPEED. Splitting them this way is what keeps the two
    // macros from overlapping.
    d.jitterDepth         = applyGlobalIntensity(loss * 2.827433f, global);   // pi * 0.9
    d.jitterTimingRate    = mapSpeedToJitterTimingRate(speed);
    d.jitterTimingDepth   = applyGlobalIntensity(loss, global);

    // ---- shaping ----------------------------------------------------------
    //
    // The filter comes in EARLY in GLOBAL: it shapes where the artifacts live
    // rather than making them, so it should be doing its job before the
    // destructive stages are at full strength.
    d.filterAmount        = applyGlobalIntensity(clamp01(user.filter), global, 0.7f);
    d.filterFreq          = clamp01(user.filterFreq);
    d.filterSections      = slopeToSections(user.slope);
    d.filterResonance     = slopeToResonance(user.slope);

    d.weightingTilt       = weightingToTilt(user.weighting);

    // ---- untouched by GLOBAL ----------------------------------------------
    //
    // These are levels and times, not intensities. Scaling a limiter threshold
    // or a reverb decay by the intensity macro would make GLOBAL a gain
    // control, which is exactly what it must not be.
    d.reverbAmount        = applyGlobalIntensity(clamp01(user.verb), global);
    d.reverbDecay         = clamp01(user.verbDecay);
    d.gateThreshold       = clamp01(user.gateThreshold);
    d.limiterThreshold    = clamp01(user.limiterThreshold);
    d.autoGain            = clamp01(user.autoGain);
    d.lossGainDb          = user.lossGainDb;

    d.freezeBlend         = user.freeze == LucyFreezeMode::off
                                ? 0.0f
                                : applyGlobalIntensity(clamp01(user.freezer), global);

    d.outputBlend         = globalOutputBlend(global);

    return d;
}

} // namespace px3
