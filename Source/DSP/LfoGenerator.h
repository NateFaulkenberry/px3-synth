#pragma once

#include "LfoMode.h"
#include "LfoTypes.h"

class LfoGenerator
{
public:
    void prepare(double newSampleRateHz);
    void setSettings(const LfoSettings& newSettings);

    void resetPhase(float phaseRadians = 0.0f);
    float getNextSample();
    float getMidpointSignalAndAdvance(int numSamples);

    float getPhaseRadians() const;

private:
    static float waveformSampleAtPhase(float phaseRadians, int waveformIndex);

    double sampleRateHz { 44100.0 };
    LfoSettings settings;
    float phaseRadians { 0.0f };
};
