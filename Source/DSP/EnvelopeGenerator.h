#pragma once

#include <JuceHeader.h>

#include "EnvelopeTypes.h"
#include "BreakpointEnvelope.h"

// Generic runtime ADSR generator. It does not assume destination usage
// (amplitude, filter cutoff modulation, etc.).
class EnvelopeGenerator
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

private:
    double sampleRateHz { 44100.0 };
    EnvelopeSettings envelopeSettings;

    px3::BreakpointEnvelope envelope;
    px3::BreakpointEnvelope::Snapshot snapshot;

    double heldSeconds { 0.0 };
    double releasedSeconds { 0.0 };
    bool noteHeld { false };
    bool inRelease { false };
    bool finished { true };
    float releaseLevelAnchor { 0.0f };

    // Where the attack begins. Zero on a fresh voice; on a retrigger it is the
    // level the envelope had reached, so the new attack rises from there
    // rather than diving to silence first. Same reason as the amp envelope's.
    float attackLevelAnchor { 0.0f };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputSmoother;
};
