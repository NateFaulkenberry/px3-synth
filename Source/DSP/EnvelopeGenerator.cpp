#include "EnvelopeGenerator.h"

#include <cmath>

bool EnvelopeGenerator::paramsDiffer(const juce::ADSR::Parameters& a, const juce::ADSR::Parameters& b)
{
    constexpr float epsilon = 1.0e-6f;
    return std::abs(a.attack - b.attack) > epsilon
           || std::abs(a.decay - b.decay) > epsilon
           || std::abs(a.sustain - b.sustain) > epsilon
           || std::abs(a.release - b.release) > epsilon;
}

void EnvelopeGenerator::prepare(double sampleRate)
{
    sampleRateHz = juce::jmax(1.0, sampleRate);
    adsr.setSampleRate(sampleRateHz);

    // Keep the ramp short: enough to kill clicks at very fast transients,
    // but not long enough to blur envelope timing.
    constexpr double outputSmoothingSeconds = 0.0008;
    outputSmoother.reset(sampleRateHz, outputSmoothingSeconds);
}

void EnvelopeGenerator::setSettings(const EnvelopeSettings& settings)
{
    envelopeSettings = settings;

    adsrParameters.attack = envelopeSettings.attackSeconds;
    adsrParameters.decay = envelopeSettings.decaySeconds;
    adsrParameters.sustain = envelopeSettings.sustainLevel;
    adsrParameters.release = envelopeSettings.releaseSeconds;

    if (!parametersInitialized || paramsDiffer(adsrParameters, lastAppliedParameters))
    {
        adsr.setParameters(adsrParameters);
        lastAppliedParameters = adsrParameters;
        parametersInitialized = true;
    }
}

void EnvelopeGenerator::noteOn()
{
    adsr.noteOn();
}

void EnvelopeGenerator::noteOff()
{
    adsr.noteOff();
}

void EnvelopeGenerator::reset()
{
    adsr.reset();
    outputSmoother.setCurrentAndTargetValue(0.0f);
}

bool EnvelopeGenerator::isActive() const
{
    return adsr.isActive() || std::abs(outputSmoother.getCurrentValue()) > 1.0e-5f;
}

float EnvelopeGenerator::getNextSample()
{
    const auto raw = juce::jlimit(0.0f, 1.0f, adsr.getNextSample());
    outputSmoother.setTargetValue(raw);
    return outputSmoother.getNextValue();
}
