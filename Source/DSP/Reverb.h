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
    float processInputDiffusion(float in, float amount);

    // One feedback-delay-network implementation, shared by the room, hall and
    // cloud algorithms. They differ only in how long the delays are, how hard
    // the input is diffused and how the decay time is derived - not in
    // topology, so there is a single place where the network can be wrong.
    struct FdnConfig
    {
        float sizeScale { 1.0f };
        float rt60Seconds { 2.0f };
        float dampingCoeff { 0.1f };
        float modHz { 0.3f };
        float modSamples { 2.0f };
        float allpassGain { 0.55f };
        float inputGain { 0.35f };
    };

    void allocateFdn(std::array<DelayLine, 8>& delays,
                     std::array<DelayLine, 8>& allpasses,
                     float maxScale);

    void processFdn8(std::array<DelayLine, 8>& delays,
                     std::array<DelayLine, 8>& allpasses,
                     std::array<float, 8>& readCache,
                     const FdnConfig& config,
                     float input,
                     float& wetL,
                     float& wetR);

    void processCore(float inL, float inR, float amount, int algorithmIndex, float& outL, float& outR);

    std::array<DelayLine, 2> preDelayLines;

    // Room: a tapped early-reflection delay per channel feeding a compact
    // four-line network. A small space is defined by its early pattern far more
    // than by its tail, so the taps are the character here.
    std::array<DelayLine, 2> roomEarlyLines;
    std::array<DelayLine, 8> roomLines;
    std::array<DelayLine, 8> roomAllpassLines;
    // Dattorro plate: 4 input diffusion allpasses + 8 tank elements
    // (2 modulated allpasses, 2 decay-diffusion allpasses, 4 delays).
    std::array<DelayLine, 12> plateLines;
    float plateModPhase { 0.0f };
    // Shared input diffuser. An FDN with no diffusion in front of it answers an
    // impulse with a burst of discrete taps, which is the classic metallic
    // attack; four short allpasses smear that into noise before it enters the
    // network.
    std::array<DelayLine, 4> inputDiffusionLines;

    // Hall / cloud: an FDN delay line plus an allpass inside each loop, which
    // keeps building density on every circulation rather than only at input.
    std::array<DelayLine, 8> hallLines;
    std::array<DelayLine, 8> hallAllpassLines;
    std::array<DelayLine, 8> cloudLines;
    std::array<DelayLine, 8> cloudAllpassLines;

    std::array<float, 2> plateTankState { { 0.0f, 0.0f } };
    std::array<float, 8> roomReadCache { { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } };
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
