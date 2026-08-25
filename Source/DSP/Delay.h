#pragma once

#include "DelayTypes.h"

#include <JuceHeader.h>

#include <array>
#include <vector>

class Delay
{
public:
    void prepare(double sampleRate);
    void reset();
    void updateForBlock(const DelaySettings& settings);
    void processSampleFrame(float inL, float inR, float& outL, float& outR);

private:
    struct Grain
    {
        bool active { false };
        bool reverse { false };
        float readPos { 0.0f };
        float increment { 1.0f };
        int ageSamples { 0 };
        int lengthSamples { 1 };
        float gain { 0.0f };
        float pan { 0.5f };
    };

    enum class GranularMode
    {
        classic = 0,
        cloud,
        shimmer,
        rhythmic
    };

    static constexpr int maxGrains = 48;

    static float clamp01(float v);
    static float lerp(float a, float b, float t);
    static float smoothstep(float x);
    static float sanitizeAudioSample(float x);
    static float divisionBeatsForIndex(int index);

    float readDelaySample(int channel, float readPos) const;
    void clearGranularDiffusionState();
    float processAllpassSample(float x, std::vector<float>& line, int& index, float feedback) const;
    void processGranularDiffusion(float& wetL, float& wetR, float diffusionAmount, float stereoAmount);
    void renderActiveGranularGrains(float& wetL, float& wetR);
    void spawnIsaacGrain(float amount,
                         float timeControl,
                         float feedbackControl,
                         int syncDivisionIndex,
                         GranularMode mode,
                         int rhythmicStep);
    void processIsaacGranularSample(float inL,
                                    float inR,
                                    float amount,
                                    float timeControl,
                                    float feedbackControl,
                                    int syncDivisionIndex,
                                    float& outL,
                                    float& outR);
    void processDelayAlgorithmSample(float inL,
                                     float inR,
                                     float amount,
                                     int algorithmIndex,
                                     float timeControl,
                                     float feedbackControl,
                                     int syncDivisionIndex,
                                     float& outL,
                                     float& outR);

    DelaySettings currentSettings;

    std::array<std::vector<float>, 2> isaacDelayBuffer;
    std::array<float, 2> isaacFeedbackFilter { { 0.0f, 0.0f } };
    std::array<float, 2> isaacShimmerSmooth { { 0.0f, 0.0f } };
    std::array<std::vector<float>, 2> isaacDiffusionLineA;
    std::array<std::vector<float>, 2> isaacDiffusionLineB;
    std::array<int, 2> isaacDiffusionIndexA { { 0, 0 } };
    std::array<int, 2> isaacDiffusionIndexB { { 0, 0 } };
    std::array<Grain, maxGrains> isaacGrains {};

    int isaacBufferSize { 1 };
    int isaacWritePos { 0 };
    int isaacSpawnCounter { 0 };
    int isaacRhythmicStepIndex { 0 };
    int isaacRhythmicSamplesUntilNext { 0 };
    bool isaacRhythmicSwingToggle { false };
    float isaacPanPhase { 0.0f };
    float delayAmountSmoothed { 0.0f };
    float delayTimeControlSmoothed { 0.5f };
    float delayFeedbackControlSmoothed { 0.35f };
    float delayControlSmoothingCoeff { 0.0f };
    float delayModPhase { 0.0f };
    int lastDelayAlgorithmIndex { -1 };
    int lastGranularModeIndex { -1 };

    double currentSampleRateHz { 44100.0 };
    double currentBpm { 120.0 };
    bool smoothingPrimed { false };
};
