#pragma once

#include <JuceHeader.h>

#include "EnvelopeTypes.h"
#include "BreakpointEnvelope.h"

// Dedicated per-voice amplitude envelope for the hardwired AMP->VCA path.
// This class intentionally does not participate in modulation-source routing.
class AmpEnvelope
{
public:
    void prepare(double sampleRateHz);
    void setSettings(const EnvelopeSettings& settings);

    // The full shape, when the envelope is more than the four ADSR numbers can
    // describe. setSettings is the same call with an ADSR built for it.
    void setEnvelope(const px3::BreakpointEnvelope& envelope);
    void noteOn();
    void noteOff();
    void reset();
    bool isActive() const;
    float getNextSample();

    // 0 at note-off, 1 at the end of the release. Downstream release-dependent
    // processing should schedule off this rather than off the envelope value,
    // so its timing does not change when the envelope curve changes.
    float getReleaseProgress() const;

private:
    // juce::ADSR ramps the release linearly in amplitude. Perceived loudness is
    // logarithmic, so a linear ramp spends half the release time in its top 6 dB
    // and then falls away steeply: the tail hangs, then drops. This maps the
    // linear ramp's progress onto an exponential (constant dB/second) decay that
    // still reaches exact silence at the set release time.
    //
    // Applied only when the release segment has NO curve of its own. A user who
    // bends the release has said what they want it to do, and applying this on
    // top would mean their curve did something different here than in ENV 1/2/3.
    static float shapeReleaseProgress(float progress);

    double sampleRateHz { 44100.0 };
    EnvelopeSettings settings;

    px3::BreakpointEnvelope envelope;
    px3::BreakpointEnvelope::Snapshot snapshot;
    bool releaseIsCurved { false };

    double heldSeconds { 0.0 };
    double releasedSeconds { 0.0 };
    bool noteHeld { false };
    bool finished { true };

    // One-pole rather than a linear ramp. A linear smoother lands on its target
    // and stops dead, leaving a corner in the envelope every time a stage
    // plateaus; that corner is inaudible under a rich waveform but ticks on a
    // sine. A one-pole approaches asymptotically and never corners.
    float smoothedOutput { 0.0f };
    float outputSmoothingCoefficient { 1.0f };

    float lastRawValue { 0.0f };
    float releaseProgress { 0.0f };

    // The level the envelope was ACTUALLY producing at note-off, which the
    // release is scaled from. Anchoring to anything else makes a tail jump.
    float releaseLevelAnchor { 0.0f };
    bool inRelease { false };
};
