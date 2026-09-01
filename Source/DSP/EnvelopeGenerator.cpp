#include "EnvelopeGenerator.h"

#include <cmath>

void EnvelopeGenerator::prepare(double sampleRate)
{
    sampleRateHz = juce::jmax(1.0, sampleRate);
    snapshot.rebuild(envelope, sampleRateHz);

    // Keep the ramp short: enough to kill clicks at very fast transients,
    // but not long enough to blur envelope timing.
    constexpr double outputSmoothingSeconds = 0.0008;
    outputSmoother.reset(sampleRateHz, outputSmoothingSeconds);
}

void EnvelopeGenerator::setSettings(const EnvelopeSettings& settings)
{
    envelopeSettings = settings;
    setEnvelope(px3::BreakpointEnvelope::fromAdsr(settings));
}

void EnvelopeGenerator::setEnvelope(const px3::BreakpointEnvelope& newEnvelope)
{
    envelope = newEnvelope;
    snapshot.rebuild(envelope, sampleRateHz);
}

void EnvelopeGenerator::noteOn()
{
    heldSeconds = 0.0;
    releasedSeconds = 0.0;
    noteHeld = true;
    inRelease = false;
    finished = false;
    releaseLevelAnchor = 0.0f;

    // Where the attack starts from - see the member's comment.
    attackLevelAnchor = juce::jlimit(0.0f, 1.0f, outputSmoother.getCurrentValue());
}

void EnvelopeGenerator::noteOff()
{
    // From wherever the envelope actually is. A modulation envelope released
    // during its attack must not jump to the sustain level on the way out any
    // more than an amplitude one may.
    // A one-shot envelope ignores note-off and plays its trajectory out. The
    // key triggered it; it does not gate it.
    if (snapshot.isOneShot())
    {
        noteHeld = false;
        return;
    }

    releaseLevelAnchor = juce::jlimit(0.0f, 1.0f, snapshot.valueAtHeld(heldSeconds));
    releasedSeconds = 0.0;
    noteHeld = false;
    inRelease = true;
}

void EnvelopeGenerator::reset()
{
    heldSeconds = 0.0;
    releasedSeconds = 0.0;
    noteHeld = false;
    inRelease = false;
    finished = true;
    releaseLevelAnchor = 0.0f;
    attackLevelAnchor = 0.0f;
    outputSmoother.setCurrentAndTargetValue(0.0f);
}

bool EnvelopeGenerator::isActive() const
{
    return noteHeld || inRelease || std::abs(outputSmoother.getCurrentValue()) > 1.0e-5f;
}

float EnvelopeGenerator::getNextSample()
{
    const auto step = 1.0 / sampleRateHz;

    float raw = 0.0f;

    // level = f(t), on one clock, advancing whether or not the key is down.
    if (snapshot.isOneShot())
    {
        if (! finished)
        {
            raw = juce::jlimit(0.0f, 1.0f, snapshot.valueAtElapsed(heldSeconds));
            heldSeconds += step;

            if (heldSeconds >= snapshot.totalSeconds())
            {
                finished = true;
                noteHeld = false;
            }
        }
    }
    else if (inRelease)
    {
        raw = juce::jlimit(0.0f, 1.0f,
                           snapshot.valueAtReleased(releasedSeconds, releaseLevelAnchor));
        releasedSeconds += step;

        if (snapshot.releaseProgress(releasedSeconds) >= 1.0f)
        {
            inRelease = false;
            finished = true;
        }
    }
    else if (! finished)
    {
        raw = juce::jlimit(0.0f, 1.0f, snapshot.valueAtHeld(heldSeconds, attackLevelAnchor));
        if (noteHeld)
        {
            heldSeconds += step;
        }
    }

    outputSmoother.setTargetValue(raw);
    return outputSmoother.getNextValue();
}
