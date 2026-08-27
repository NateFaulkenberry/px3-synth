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

    // juce::ADSR ramps the release linearly in amplitude. Perceived loudness is
    // logarithmic, so a linear ramp spends half the release time in its top 6 dB
    // and then falls away steeply: the tail hangs, then drops. This maps the
    // linear ramp's progress onto an exponential (constant dB/second) decay that
    // still reaches exact silence at the set release time.
    static float shapeReleaseProgress(float progress);

    double sampleRateHz { 44100.0 };
    EnvelopeSettings settings;
    juce::ADSR adsr;
    juce::ADSR::Parameters adsrParameters;
    juce::ADSR::Parameters lastAppliedParameters;
    bool parametersInitialized { false };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputSmoother;

    float lastRawAdsrValue { 0.0f };
    float releaseStartLevel { 0.0f };
    bool inRelease { false };
};
