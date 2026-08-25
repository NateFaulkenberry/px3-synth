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
    static float waveformSampleAtPhase(float phaseNorm, float phaseDelta, int waveformIndex);
    static float polyBlep(float t, float dt);
    static float wrapPhase01(float phaseNorm);

    double sampleRateHz { 44100.0 };
    SubOscSettings settings;
    float phaseNorm { 0.0f };
};
