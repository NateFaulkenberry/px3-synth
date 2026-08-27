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

    // 0 at note-off, 1 at the end of the release. Downstream release-dependent
    // processing should schedule off this rather than off the envelope value,
    // so its timing does not change when the envelope curve changes.
    float getReleaseProgress() const;

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

    // One-pole rather than a linear ramp. A linear smoother lands on its target
    // and stops dead, leaving a corner in the envelope every time a stage
    // plateaus; that corner is inaudible under a rich waveform but ticks on a
    // sine. A one-pole approaches asymptotically and never corners.
    float smoothedOutput { 0.0f };
    float outputSmoothingCoefficient { 1.0f };

    float lastRawAdsrValue { 0.0f };
    float releaseProgress { 0.0f };
    // Anchors for the release curve. The raw anchor drives progress (it tracks
    // the ADSR's own linear ramp); the level anchor scales the shaped output and
    // must be the level the envelope was ACTUALLY at, so that re-entering
    // release mid-tail is continuous.
    float releaseRawAnchor { 0.0f };
    float releaseLevelAnchor { 0.0f };
    bool inRelease { false };
};
