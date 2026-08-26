#include "AmpEnvelope.h"

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
    outputSmoother.reset(sampleRateHz, outputSmoothingSeconds);
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
    adsr.noteOn();
}

void AmpEnvelope::noteOff()
{
    adsr.noteOff();
}

void AmpEnvelope::reset()
{
    adsr.reset();
    outputSmoother.setCurrentAndTargetValue(0.0f);
}

bool AmpEnvelope::isActive() const
{
    return adsr.isActive() || std::abs(outputSmoother.getCurrentValue()) > 1.0e-5f;
}

float AmpEnvelope::getNextSample()
{
    const auto raw = juce::jlimit(0.0f, 1.0f, adsr.getNextSample());
    outputSmoother.setTargetValue(raw);
    return outputSmoother.getNextValue();
}
