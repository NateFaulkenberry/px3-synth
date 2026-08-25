#pragma once

#include <JuceHeader.h>

#include "ReverbTypes.h"

#include <array>
#include <vector>

class Reverb
{
public:
    void prepare(double sampleRate);
    void reset();

    void updateForBlock(const ReverbSettings& settings, int numSamples);
    void processSampleFrame(float inL, float inR, float& outL, float& outR);
    void applyPostBlockCompensation(juce::AudioBuffer<float>& buffer);

private:
    struct DelayLine
    {
        std::vector<float> buffer;
        int writePos { 0 };
        float lpState { 0.0f };
        float modPhase { 0.0f };
    };

    static float clamp01(float v);
    static float lerp(float a, float b, float t);
    static float smoothstep(float x);
    static float sanitizeAudioSample(float x);

    static void resizeLine(DelayLine& line, int size);
    static void writeLine(DelayLine& line, float sample);
    static float readLine(const DelayLine& line, float delaySamples);
    static float processAllpass(DelayLine& line, float in, float delaySamples, float gain);
    static float processDelay(DelayLine& line, float in, float delaySamples);

    void processCore(float inL, float inR, float amount, int algorithmIndex, float& outL, float& outR);

    juce::Reverb reverb;
    std::array<DelayLine, 2> preDelayLines;
    std::array<DelayLine, 6> plateLines;
    std::array<DelayLine, 8> hallLines;
    std::array<DelayLine, 8> cloudLines;

    std::array<float, 2> plateTankState { { 0.0f, 0.0f } };
    std::array<float, 8> hallReadCache { { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } };
    std::array<float, 8> cloudReadCache { { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } };

    std::array<float, 2> inputDcX1 { { 0.0f, 0.0f } };
    std::array<float, 2> inputDcY1 { { 0.0f, 0.0f } };
    std::array<float, 2> wetDcX1 { { 0.0f, 0.0f } };
    std::array<float, 2> wetDcY1 { { 0.0f, 0.0f } };
    std::array<float, 2> wetSlewState { { 0.0f, 0.0f } };

    ReverbSettings currentSettings;

    double sampleRateHz { 44100.0 };
    int blockSampleCount { 0 };
    float amountSmoothed { 0.0f };
    float amountSmoothingCoeff { 0.0f };
    float outputCompGain { 1.0f };
    double blockPreEnergy { 0.0 };
    double blockPostEnergy { 0.0 };
};
