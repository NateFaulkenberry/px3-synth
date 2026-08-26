#pragma once

#include <JuceHeader.h>

#include "EnvelopeTypes.h"

// Dedicated per-voice amplitude envelope for the hardwired AMP->VCA path.
// This class intentionally does not participate in modulation-source routing.
class AmpEnvelope
{
public:
    void prepare(double sampleRateHz);
    void setSettings(const EnvelopeSettings& settings);
    void noteOn();
    void noteOff();
    void reset();
    bool isActive() const;
    float getNextSample();

private:
    static bool paramsDiffer(const juce::ADSR::Parameters& a, const juce::ADSR::Parameters& b);

    double sampleRateHz { 44100.0 };
    EnvelopeSettings settings;
    juce::ADSR adsr;
    juce::ADSR::Parameters adsrParameters;
    juce::ADSR::Parameters lastAppliedParameters;
    bool parametersInitialized { false };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputSmoother;
};
