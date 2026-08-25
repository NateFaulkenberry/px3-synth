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

    float processSample(float inputSample);

private:
    void applyFilter(float cutoffHz, float resonanceQ, int modeIndex);

    juce::dsp::IIR::Filter<float> stageA;
    juce::dsp::IIR::Filter<float> stageB;

    FilterSettings targetSettings;
    FilterSettings currentSettings;
    double sampleRate { 0.0 };
    int filterUpdateCounter { 0 };
};
