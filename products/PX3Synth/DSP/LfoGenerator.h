#pragma once

#include "LfoMode.h"
#include "LfoTypes.h"

#include <cstdint>

class LfoGenerator
{
public:
    // The value a random shape (S&H, SMOOTH RND) takes in a given cycle.
    // `seed` keeps each LFO's series its own: without it LFO 1, 2 and 3 on S&H
    // stepped through the same values.
    static float randomForCycle(std::int64_t cycle, std::uint64_t seed = 0) noexcept;
    void setRandomSeed(std::uint64_t newSeed) noexcept { randomSeed = newSeed; }
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
    // Random shapes need to know which cycle they are in; everything else is
    // a function of phase alone.
    float sampleAtPhase(float phaseRadians, int waveformIndex) const;
    std::int64_t cycleIndex { 0 };
    std::uint64_t randomSeed { 0 };
    static float rampSampleAt(double elapsedSeconds, float rampSeconds, int waveformIndex);

    double sampleRateHz { 44100.0 };
    LfoSettings settings;
    float phaseRadians { 0.0f };

    // Audio time since the ramp last started, counted in SECONDS rather than in
    // blocks or samples - which is what keeps a ramp the same length at every
    // sample rate and every block size.
    double rampElapsedSeconds { 0.0 };
};
