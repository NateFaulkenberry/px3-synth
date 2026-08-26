#pragma once

#include <JuceHeader.h>

#include "EnvelopeTypes.h"

// Generic runtime ADSR generator. It does not assume destination usage
// (amplitude, filter cutoff modulation, etc.).
class EnvelopeGenerator
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
    double sampleRateHz { 44100.0 };
    EnvelopeSettings envelopeSettings;
    juce::ADSR adsr;
    juce::ADSR::Parameters adsrParameters;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputSmoother;
};
