#pragma once

#include "LfoMode.h"
#include "LfoTypes.h"

class LfoGenerator
{
public:
    void prepare(double newSampleRateHz);
    void setSettings(const LfoSettings& newSettings);

    void resetPhase(float phaseRadians = 0.0f);

    // Back to the start: phase zero for the cyclic shapes, and a ramp's start
    // value with its clock at zero. What KEY SYNC does on a new note.
    void retrigger();

    float getNextSample();
    float getMidpointSignalAndAdvance(int numSamples);

    float getPhaseRadians() const;
    double getRampElapsedSeconds() const;

private:
    static float waveformSampleAtPhase(float phaseRadians, int waveformIndex);
    static float rampSampleAt(double elapsedSeconds, float rampSeconds, int waveformIndex);

    double sampleRateHz { 44100.0 };
    LfoSettings settings;
    float phaseRadians { 0.0f };

    // Audio time since the ramp last started, counted in SECONDS rather than in
    // blocks or samples - which is what keeps a ramp the same length at every
    // sample rate and every block size.
    double rampElapsedSeconds { 0.0 };
};
