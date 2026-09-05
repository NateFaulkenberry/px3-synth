#pragma once

#include "LucyTypes.h"

namespace px3
{

// What the ENGINE consumes.
//
// Every field here is a DSP quantity: a coverage width, a step size, a frame
// count. None of them is a knob. The engine reads these and nothing else, so
// "what does LOSS at 0.4 actually do" is answered by one function rather than
// by reading four stages of DSP.
struct LucyDerivedParameters
{
    // ---- the loss coder ---------------------------------------------------
    //
    // Half-width, in normalised bin position, of the region LOSS reaches.
    // Narrow at the bottom of the knob, the whole spectrum at the top.
    float spectralCoverage { 0.0f };

    // How far the masking threshold is raised. This is the "strength" half of
    // LOSS: more of the spectrum falls under the threshold and is discarded.
    float maskingDepth { 0.0f };

    // How much of the masking threshold a bin has to be under before it is
    // thrown away, 0..1. Below 1 the coder only discards what is well under
    // the threshold, which is what keeps the bottom of LOSS gentle.
    float discardAmount { 0.0f };

    // Width of the logarithmic magnitude grid the survivors are snapped to.
    // Coarse steps on strong partials are where the chiming comes from.
    float quantisationAmount { 0.0f };

    // The tilt applied to the masking threshold across the bands: negative
    // protects the lows, positive protects the highs.
    float weightingTilt { 0.0f };

    // ---- packets ----------------------------------------------------------
    //
    // Probability per decision of entering a burst. The probability of leaving
    // one is fixed, so bursts keep a characteristic LENGTH rather than having
    // their length change with the knob that sets their frequency.
    float packetProbability { 0.0f };
    int packetStateFrames { 1 };

    // ---- one temporal control ---------------------------------------------
    //
    // SPEED becomes exactly these four, and nothing else in the engine picks
    // its own rate.
    int decisionFrames { 1 };
    float freezeSlushRate { 0.0f };
    float jitterWalkStep { 0.0f };
    float jitterDepth { 0.0f };

    // JITTER's timing half: how often the read head picks a new target, and
    // how far from nominal it may wander. Separate from the phase walk above
    // because one is a spectral effect and the other a delay-line one, but
    // both come from the same two knobs.
    float jitterTimingRate { 0.0f };
    float jitterTimingDepth { 0.0f };

    // ---- everything downstream of the coder --------------------------------
    float filterAmount { 0.0f };
    float filterFreq { 0.5f };
    int filterSections { 2 };
    float filterResonance { 1.0f };

    float reverbAmount { 0.0f };
    float reverbDecay { 0.45f };

    float gateThreshold { 0.0f };
    float limiterThreshold { 1.0f };
    float autoGain { 0.0f };
    float lossGainDb { 0.0f };

    // How much of the frozen spectrum replaces the live one.
    float freezeBlend { 0.0f };

    // The last few percent of GLOBAL, and only that: a crossfade so the effect
    // can reach silent cleanly and the idle path has somewhere to land. It is
    // 1 for almost the whole range of the knob - GLOBAL is an intensity macro,
    // not a wet/dry, and this is a discontinuity guard rather than the
    // mechanism. See applyGlobalIntensity.
    float outputBlend { 0.0f };
};

// The user -> DSP translation, in one place.
//
// Free functions rather than a class with state: every one of these is a pure
// mapping from a knob position to a DSP quantity, and being pure is what makes
// them testable without instantiating an engine or a plug-in.
namespace lucy_control
{

// ---- LOSS ---------------------------------------------------------------
//
// Four curves, tuned separately, because their perceptual roles differ. Using
// `loss` for all four - which the engine did when these lived inline - makes
// the bottom half of the knob do nothing and the top tenth do everything.

// Slightly slower than linear at the bottom, so early LOSS works on a narrow
// strip rather than immediately colouring the whole spectrum.
float mapLossToCoverage(float loss);

// Rises sooner than the square it replaced: at half travel the coder is
// already making an obvious digital-compression character, which is the
// documented behaviour of the middle of the knob.
float mapLossToMaskingDepth(float loss);

// The fraction of the masking threshold at which a bin is discarded. Starts
// well below 1 so the bottom of LOSS thins rather than gouges.
float mapLossToDiscard(float loss);

// Deliberately late-weighted. Coarse quantisation is the most destructive
// artifact of the set and should not dominate the first half of the knob.
float mapLossToQuantisation(float loss);

// Later still. A burst dropout is the most disruptive thing LUCY does, and a
// knob that stutters at a quarter travel is a knob with no usable bottom half.
float mapLossToPacketProbability(float loss);

// ---- SPEED --------------------------------------------------------------
//
// All four are geometric rather than linear. Frame counts are a RATE, and a
// linear map from a knob to a rate spends most of its travel in a range that
// sounds the same.

// How many frames one decision is held for. Slow means the same bins stay
// discarded for a long time, which IS spectral smearing.
int mapSpeedToDecisionFrames(float speed);

// How long the packet chain stays in a state before it may change. Derived
// from the same knob, so bursts space out as decisions do.
int mapSpeedToPacketStateFrames(float speed);

// How fast a slushy freeze drifts toward what is being played.
float mapSpeedToFreezeSlushRate(float speed);

// The per-frame step of JITTER's phase random walk.
float mapSpeedToJitterStep(float speed);

// How often JITTER's read head chooses a new target to wander toward.
float mapSpeedToJitterTimingRate(float speed);

// ---- GLOBAL -------------------------------------------------------------
//
// Scales an already-derived amount by the intensity macro. Not a plain
// multiply: the exponent shapes how soon that particular character arrives, so
// the shaping controls come in before the destructive ones.
//
//   exponent < 1  arrives early  (filter: shaping, wants to be audible soon)
//   exponent = 1  proportional   (loss depth, coverage, freeze)
//   exponent > 1  arrives late   (packets: the most disruptive stage)
float applyGlobalIntensity(float amount, float global, float exponent = 1.0f);

// The crossfade, which exists ONLY in the bottom of GLOBAL's travel. Returns 1
// for the great majority of the knob.
float globalOutputBlend(float global);

// ---- direct ---------------------------------------------------------------

// -1 protects the lows, +1 protects the highs, 0 leaves the psychoacoustic
// curve alone.
float weightingToTilt(LucyWeighting weighting);

// 6 dB is one section, 24 dB two, 96 dB eight. The count is internal; the user
// sees decibels per octave.
int slopeToSections(LucyFilterSlope slope);
float slopeToResonance(LucyFilterSlope slope);

} // namespace lucy_control

// The whole translation, once per block.
LucyDerivedParameters deriveLucyParameters(const LucyUserParameters& user);

} // namespace px3
