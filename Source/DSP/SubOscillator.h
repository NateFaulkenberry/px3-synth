#pragma once

#include "SubOscMode.h"
#include "SubOscTypes.h"

class SubOscillator
{
public:
    void prepare(double newSampleRateHz);
    void setSettings(const SubOscSettings& newSettings);
    void resetForNote(float phaseRadians = 0.0f);

    float renderSample(double baseFrequencyHz);

private:
    static float waveformSampleAtPhase(float phaseRadians, int waveformIndex);

    double sampleRateHz { 44100.0 };
    SubOscSettings settings;
    float phaseRadians { 0.0f };
};
