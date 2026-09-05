#pragma once

#include "MoodTypes.h"

#include <array>

namespace px3
{

// What the ENGINE consumes.
//
// Every field is a DSP quantity - a duration in seconds, a playback rate, a
// detection threshold. None is a knob. The renderers used to convert their own
// knob inline, which meant "what does LENGTH at 0.4 mean in TAPE" could only be
// answered by reading DSP and could not be tested without rendering audio.
struct MoodDerivedParameters
{
    // ---- the shared clock --------------------------------------------------
    //
    // The divider is the engine's rate relative to the host's; the semitones
    // are the transposition that produces, and are what makes the control
    // musical rather than a sample-rate slider.
    float clockDivider { 1.0f };
    float clockIncrement { 1.0f };
    float clockSemitones { 0.0f };

    // ---- micro-looper: ENV -------------------------------------------------
    float envSliceSeconds { 0.15f };
    // INVERTED against the knob: MODIFY is sensitivity, so turning it up has
    // to make the detector fire on quieter input.
    float envThreshold { 0.07f };

    // ---- micro-looper: TAPE ------------------------------------------------
    float tapeLoopSeconds { 0.7f };
    // From the harmonised table, not a continuum: quarter, half, unity and
    // double, in each direction.
    float tapePlaybackRate { 1.0f };
    int tapeRateIndex { 5 };

    // ---- micro-looper: STRETCH ---------------------------------------------
    float stretchGrainSeconds { 0.06f };
    // -1 walks the playhead backwards through the loop, 0 is frozen, +1 keeps
    // pace with the recording so nothing is stretched at all.
    float stretchWalk { 0.0f };
    float stretchPanHz { 0.05f };

    // ---- wet: REVERB -------------------------------------------------------
    float reverbTimeScale { 1.0f };
    float reverbDiffusion { 0.45f };

    // ---- wet: DELAY --------------------------------------------------------
    float delaySeconds { 0.3f };
    // Reaches unity at the top of the knob, held there by the saturator rather
    // than by a coefficient below one.
    float delayFeedback { 0.45f };

    // ---- wet: SLIP ---------------------------------------------------------
    float slipWindowSeconds { 0.3f };
    float slipSemitones { 0.0f };
    float slipPlaybackRate { 1.0f };

    // ---- interaction -------------------------------------------------------
    float loopFeedback { 0.0f };
    float spread { 0.5f };
    float mix { 0.35f };

    // ---- degradation -------------------------------------------------------
    float degradeBits { 16.0f };
    float degradeAmount { 0.0f };
};

namespace mood_control
{

// ---- CLOCK ----------------------------------------------------------------
//
// Three octaves in semitone steps. Quantised so the transposition lands on
// musical intervals, and returned as a divider because that is what the engine
// runs on: audio recorded at one rate and played back at another changes speed
// and pitch together.
int clockStepCount();
float mapClockToSemitones(float clock);
float mapClockToDivider(float clock);

// ---- LOOP LENGTH, per mode -------------------------------------------------
//
// One user concept, three DSP meanings. Separate functions rather than one
// switch because their ranges have nothing to do with each other.
float mapLoopLengthToEnvSlice(float length);      // seconds of captured slice
float mapLoopLengthToTapeLoop(float length);      // seconds of loop
float mapLoopLengthToStretchGrain(float length);  // seconds per grain

// ---- LOOP MODIFY, per mode -------------------------------------------------

// SENSITIVITY, so the threshold FALLS as the knob rises. The range is set
// against the levels this actually sees: a one-pole average of |x| for a sine
// of amplitude A settles at 0.64A, and on the FX bus that puts the envelope
// around 0.16 on ordinary material - so a threshold of 0.35 would mean the
// detector never fires at any setting a player would use.
float mapLoopModifyToEnvThreshold(float modify);

// An index into the harmonised speed table, and the rate it selects.
int mapLoopModifyToTapeRateIndex(float modify, int rateCount);
float tapeRateAt(int index);
int tapeRateCount();

// -1 backwards, 0 frozen, +1 keeping pace with the recording.
float mapLoopModifyToStretchWalk(float modify);
// Panning speed follows MODIFY: the further from frozen, the faster the image
// moves.
float mapLoopModifyToStretchPanHz(float modify);

// ---- WET TIME, per mode ----------------------------------------------------
float mapWetTimeToReverbScale(float time);
float mapWetTimeToDelaySeconds(float time);
float mapWetTimeToSlipWindow(float time);

// ---- WET MODIFY, per mode --------------------------------------------------
float mapWetModifyToReverbDiffusion(float modify);
float mapWetModifyToDelayFeedback(float modify);
float mapWetModifyToSlipSemitones(float modify);

// ---- interaction and degradation -------------------------------------------

// How much of the two channels is recycled into the always-listening history.
float mapFeedbackToRecycle(float feedback);

// 16 bits down to about 3.
float mapDegradeToBits(float degrade);

} // namespace mood_control

// The whole translation, once per block.
//
// Every mode's values are computed, not only the active one: between them they
// are a handful of multiplies, and computing all of them lets a test assert
// what a mode WOULD do without switching into it.
MoodDerivedParameters deriveMoodParameters(const MoodUserParameters& user);

} // namespace px3
