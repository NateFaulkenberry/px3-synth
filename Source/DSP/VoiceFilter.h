#pragma once

#include <JuceHeader.h>

#include "FilterMode.h"
#include "FilterTypes.h"

// Generic runtime filter unit for synth voices. It encapsulates filtering mode,
// smoothing, and coefficient updates without any synth-specific UI/state logic.
class VoiceFilter
{
public:
    void prepare(double newSampleRate);
    void reset();

    void setTargetSettings(const FilterSettings& settings);
    void setCurrentSettingsImmediate(const FilterSettings& settings);

    // Owns its own bypass. Callers always process every sample through every
    // filter instance; whether the filter is in circuit is this class's
    // business, so no caller needs a special case per filter.
    float processSample(float inputSample);

private:
    void applyFilter(float cutoffHz, float resonanceQ, int modeIndex);

    juce::dsp::IIR::Filter<float> stageA;
    juce::dsp::IIR::Filter<float> stageB;

    FilterSettings targetSettings;
    FilterSettings currentSettings;
    double sampleRate { 0.0 };
    int filterUpdateCounter { 0 };
    float smoothingCoefficient { 1.0f };

    // Bypass and type changes are discontinuous by nature: there is no correct
    // state to carry across them. Both are therefore taken through a short
    // crossfade to dry rather than switched, which is the transition itself
    // rather than smoothing applied to hide one.
    float bypassBlend { 1.0f };
    float bypassBlendStep { 1.0f };
    int pendingModeIndex { 0 };
    bool modeChangePending { false };
};
