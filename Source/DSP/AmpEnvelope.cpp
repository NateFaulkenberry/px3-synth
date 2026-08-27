#include "AmpEnvelope.h"

#include "PX3Diagnostics.h"

#include <cmath>

bool AmpEnvelope::paramsDiffer(const juce::ADSR::Parameters& a, const juce::ADSR::Parameters& b)
{
    constexpr float epsilon = 1.0e-6f;
    return std::abs(a.attack - b.attack) > epsilon
           || std::abs(a.decay - b.decay) > epsilon
           || std::abs(a.sustain - b.sustain) > epsilon
           || std::abs(a.release - b.release) > epsilon;
}

void AmpEnvelope::prepare(double sampleRate)
{
    sampleRateHz = juce::jmax(1.0, sampleRate);
    adsr.setSampleRate(sampleRateHz);

    if (parametersInitialized)
    {
        adsr.setParameters(lastAppliedParameters);
    }

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

    adsrParameters.attack = settings.attackSeconds;
    adsrParameters.decay = settings.decaySeconds;
    adsrParameters.sustain = settings.sustainLevel;
    adsrParameters.release = settings.releaseSeconds;

    if (!parametersInitialized || paramsDiffer(adsrParameters, lastAppliedParameters))
    {
        adsr.setParameters(adsrParameters);
        lastAppliedParameters = adsrParameters;
        parametersInitialized = true;
    }
}

void AmpEnvelope::noteOn()
{
    inRelease = false;
    releaseRawAnchor = 0.0f;
    releaseLevelAnchor = 0.0f;
    releaseProgress = 0.0f;
    adsr.noteOn();
}

void AmpEnvelope::noteOff()
{
    // juce::ADSR restarts its linear release from wherever it currently is, so
    // release progress is measured against that value.
    releaseRawAnchor = lastRawAdsrValue;

    // Scale the shaped curve from the level the envelope was ACTUALLY producing,
    // which is no longer the same number as the raw ADSR value once the release
    // is curve-shaped. Anchoring to the raw value here makes a tail jump back up
    // to the linear ramp's height.
    //
    // This is reachable in normal playing: juce::Synthesiser::noteOff matches
    // voices by getCurrentlyPlayingNote() regardless of key state, so replaying a
    // pitch whose previous voice is still releasing delivers a second noteOff to
    // that releasing voice.
    releaseLevelAnchor = inRelease ? smoothedOutput : lastRawAdsrValue;
    releaseProgress = 0.0f;
    inRelease = releaseRawAnchor > 1.0e-6f && releaseLevelAnchor > 1.0e-6f;

    adsr.noteOff();
}

void AmpEnvelope::reset()
{
    adsr.reset();
    smoothedOutput = 0.0f;
    lastRawAdsrValue = 0.0f;
    releaseRawAnchor = 0.0f;
    releaseLevelAnchor = 0.0f;
    releaseProgress = 0.0f;
    inRelease = false;
}

float AmpEnvelope::getReleaseProgress() const
{
    return inRelease ? releaseProgress : 0.0f;
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
    return adsr.isActive() || std::abs(smoothedOutput) > 1.0e-5f;
}

float AmpEnvelope::getNextSample()
{
    const auto raw = juce::jlimit(0.0f, 1.0f, adsr.getNextSample());
    lastRawAdsrValue = raw;

    auto shaped = raw;

#if PX3_DIAGNOSTICS
    const auto useLinearRelease = px3::diag::state().legacyLinearRelease;
#else
    constexpr auto useLinearRelease = false;
#endif

    if (inRelease)
    {
        // The ADSR's own linear ramp doubles as the release progress clock.
        releaseProgress = juce::jlimit(0.0f, 1.0f, 1.0f - raw / releaseRawAnchor);

        if (!useLinearRelease)
        {
            shaped = releaseLevelAnchor * shapeReleaseProgress(releaseProgress);
        }

        if (!adsr.isActive())
        {
            inRelease = false;
        }
    }

    smoothedOutput += (shaped - smoothedOutput) * outputSmoothingCoefficient;
    return smoothedOutput;
}
