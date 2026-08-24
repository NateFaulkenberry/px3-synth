#pragma once

#include <JuceHeader.h>

#include "EnvelopeTypes.h"

// Generic runtime ADSR generator. It does not assume destination usage
// (amplitude, filter cutoff modulation, etc.).
class EnvelopeGenerator
{
public:
    void setSettings(const EnvelopeSettings& settings);
    void noteOn();
    void noteOff();
    void reset();
    bool isActive() const;
    float getNextSample();

private:
    EnvelopeSettings envelopeSettings;
    juce::ADSR adsr;
    juce::ADSR::Parameters adsrParameters;
};
