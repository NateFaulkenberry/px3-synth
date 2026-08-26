#pragma once

#include "MoodTypes.h"

#include <JuceHeader.h>

#include <array>
#include <vector>

class Mood
{
public:
    void prepare(double sampleRate);
    void reset();
    void updateForBlock(const MoodSettings& settings);
    void processSampleFrame(float inL, float inR, float& outL, float& outR);

private:
    struct Grain
    {
        bool active { false };
        float readPos { 0.0f };
        float increment { 1.0f };
        int ageSamples { 0 };
        int lengthSamples { 1 };
        float pan { 0.5f };
        float gain { 0.0f };
    };

    static constexpr int kMaxGrains = 8;

    static float clamp01(float v);
    static float sanitizeAudioSample(float v);
    static float semitoneRatio(float semitones);

    float readInterp(const std::vector<float>& line, float pos) const;
    void writeHistory(float l, float r);
    void writeWetDelay(float l, float r);
    void processClockReduction(float inL, float inR, float& outL, float& outR);

    void renderLoopTape(float& loopL, float& loopR);
    void renderLoopEnv(float inL, float inR, float& loopL, float& loopR);
    void renderLoopStretch(float& loopL, float& loopR);

    void renderWetReverb(float inL, float inR, float& wetL, float& wetR);
    void renderWetDelay(float inL, float inR, float& wetL, float& wetR);
    void renderWetSlip(float inL, float inR, float& wetL, float& wetR);

    void maybeSpawnStretchGrain();

    MoodSettings currentSettings;

    std::array<std::vector<float>, 2> historyBuffer;
    std::array<std::vector<float>, 2> wetDelayBuffer;

    std::array<Grain, kMaxGrains> grains {};

    int historySize { 1 };
    int wetDelaySize { 1 };
    int historyWritePos { 0 };
    int wetDelayWritePos { 0 };

    float loopReadPos { 0.0f };
    float loopHeldReadPos { 0.0f };
    float envFollower { 0.0f };
    int envSliceHoldSamples { 0 };
    int stretchSpawnCounter { 0 };

    float slipReadPos { 0.0f };
    float slipCapturePos { 0.0f };

    int clockHoldSamples { 1 };
    int clockSampleCounter { 0 };
    float clockHeldL { 0.0f };
    float clockHeldR { 0.0f };
    bool wasEnabled { false };
    bool pendingResetOnBypass { false };
    float wetSlewL { 0.0f };
    float wetSlewR { 0.0f };
    float wetSlewCoeff { 0.0f };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> enabledSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> clockSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> routingSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetTimeSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetModifySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> loopLengthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> loopModifySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> feedbackSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> spreadSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> degradeSmoothed;

    double sampleRateHz { 44100.0 };
};
