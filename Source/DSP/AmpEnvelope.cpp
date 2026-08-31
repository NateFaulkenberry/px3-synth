#include "AmpEnvelope.h"

#include "PX3Diagnostics.h"

#include <cmath>

void AmpEnvelope::prepare(double sampleRate)
{
    sampleRateHz = juce::jmax(1.0, sampleRate);
    snapshot.rebuild(envelope, sampleRateHz);

    // Keep this very short: enough to remove hard control-rate edges while
    // preserving audible ADSR timing and transient definition.
    constexpr double outputSmoothingSeconds = 0.0008;
    outputSmoothingCoefficient =
        1.0f - std::exp(-1.0f / static_cast<float>(outputSmoothingSeconds * sampleRateHz));
}

void AmpEnvelope::setSettings(const EnvelopeSettings& newSettings)
{
    settings = newSettings;
    settings.attackSeconds = juce::jmax(0.001f, settings.attackSeconds);
    settings.decaySeconds = juce::jmax(0.001f, settings.decaySeconds);
    settings.sustainLevel = juce::jlimit(0.0f, 1.0f, settings.sustainLevel);
    settings.releaseSeconds = juce::jmax(0.001f, settings.releaseSeconds);

    setEnvelope(px3::BreakpointEnvelope::fromAdsr(settings));
}

void AmpEnvelope::setEnvelope(const px3::BreakpointEnvelope& newEnvelope)
{
    envelope = newEnvelope;
    snapshot.rebuild(envelope, sampleRateHz);

    // Whether the user has bent the release themselves, which decides if the
    // perceptual reshaping below still applies.
    const auto sustainIndex = envelope.getSustainPoint();
    releaseIsCurved = false;
    for (int i = sustainIndex; i + 1 < envelope.getPointCount(); ++i)
    {
        if (std::abs(envelope.getPoint(i).curveToNext) > 1.0e-6)
        {
            releaseIsCurved = true;
        }
    }
}

void AmpEnvelope::noteOn()
{
    inRelease = false;
    noteHeld = true;
    finished = false;
    heldSeconds = 0.0;
    releasedSeconds = 0.0;
    releaseProgress = 0.0f;
    releaseLevelAnchor = 0.0f;
}

void AmpEnvelope::noteOff()
{
    // Scaled from the level the envelope was ACTUALLY producing, which is no
    // longer the same number as the underlying curve once the release is
    // reshaped. Anchoring to the raw value makes a tail jump back up.
    //
    // This is reachable in normal playing: juce::Synthesiser::noteOff matches
    // voices by getCurrentlyPlayingNote() regardless of key state, so replaying
    // a pitch whose previous voice is still releasing delivers a second noteOff
    // to that releasing voice.
    releaseLevelAnchor = inRelease ? smoothedOutput : lastRawValue;
    releaseProgress = 0.0f;
    releasedSeconds = 0.0;
    noteHeld = false;
    inRelease = releaseLevelAnchor > 1.0e-6f;

    if (! inRelease)
    {
        finished = true;
    }
}

void AmpEnvelope::reset()
{
    smoothedOutput = 0.0f;
    lastRawValue = 0.0f;
    releaseProgress = 0.0f;
    releaseLevelAnchor = 0.0f;
    inRelease = false;
    noteHeld = false;
    finished = true;
    heldSeconds = 0.0;
    releasedSeconds = 0.0;
}

float AmpEnvelope::shapeReleaseProgress(float progress)
{
    // -60 dB across the release, normalised so the curve starts at exactly 1.0
    // and reaches exactly 0.0 at the end of the set release time.
    constexpr float decayConstant = 6.9077553f; // ln(1000)
    constexpr float floorValue = 0.001f;        // exp(-decayConstant)

    const auto shaped = std::exp(-decayConstant * juce::jlimit(0.0f, 1.0f, progress));
    return juce::jmax(0.0f, (shaped - floorValue) / (1.0f - floorValue));
}

bool AmpEnvelope::isActive() const
{
    return noteHeld || inRelease || std::abs(smoothedOutput) > 1.0e-5f;
}

float AmpEnvelope::getReleaseProgress() const
{
    return releaseProgress;
}

float AmpEnvelope::getNextSample()
{
    const auto step = 1.0 / sampleRateHz;

    float raw;
    if (! inRelease)
    {
        raw = juce::jlimit(0.0f, 1.0f, snapshot.valueAtHeld(heldSeconds));
        if (noteHeld)
        {
            heldSeconds += step;
        }
    }
    else
    {
        releaseProgress = snapshot.releaseProgress(releasedSeconds);
        raw = juce::jlimit(0.0f, 1.0f,
                           snapshot.valueAtReleased(releasedSeconds, releaseLevelAnchor));
        releasedSeconds += step;
    }

    lastRawValue = raw;
    auto shaped = raw;

#if PX3_DIAGNOSTICS
    const auto useLinearRelease = px3::diag::state().legacyLinearRelease;
#else
    constexpr auto useLinearRelease = false;
#endif

    if (inRelease)
    {
        // Reshaped to constant dB/second unless the user has bent this segment
        // themselves - see shapeReleaseProgress.
        if (! useLinearRelease && ! releaseIsCurved)
        {
            shaped = releaseLevelAnchor * shapeReleaseProgress(releaseProgress);
        }

        if (releaseProgress >= 1.0f)
        {
            inRelease = false;
            finished = true;
            shaped = 0.0f;
        }
    }
    else if (finished)
    {
        shaped = 0.0f;
    }

    smoothedOutput += (shaped - smoothedOutput) * outputSmoothingCoefficient;
    return smoothedOutput;
}
