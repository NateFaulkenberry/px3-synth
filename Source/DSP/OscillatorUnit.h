#pragma once

#include "OscillatorMode.h"
#include "OscillatorTypes.h"

#include <array>
#include <cstdint>

class OscillatorUnit
{
public:
    struct RenderContext
    {
        double currentAngle { 0.0 };
        double currentFrequencyHz { 440.0 };
        int noteAgeSamples { 0 };
        float pitchRatio { 1.0f };
        float modWheelNorm { 0.0f };
        float pwmModWheelNorm { 0.0f };
    };

    void setSettings(const OscillatorSettings& settings);
    void resetForNote(double sampleRate, double currentFrequencyHz);
    float nextDeterministicNoise();
    float renderSample(double sampleRate, const RenderContext& context);

private:
    float renderPinkNoise(float white);
    float renderSuperSaw(double sampleRate, const RenderContext& context);
    float renderPwm(const RenderContext& context) const;
    float renderAdditive(const RenderContext& context, bool dynamic);
    float renderFm(double sampleRate, const RenderContext& context);
    float renderHardSync(double sampleRate, const RenderContext& context);
    float renderKarplus(const RenderContext& context);
    float renderOrgan(const RenderContext& context);
    float renderDigital(double sampleRate, const RenderContext& context);
    float renderPhysical(double sampleRate, const RenderContext& context);
    float renderRobOsc(double sampleRate, const RenderContext& context);
    float renderPx3(double sampleRate, const RenderContext& context);
    float readHarmonicSumFromSettings(double currentAngle,
                                      float rolloffBias,
                                      float oddEvenBias,
                                      float inharmonicity) const;

    OscillatorSettings oscillatorSettings;

    std::array<double, 7> superSawAngles { { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 } };
    std::array<float, 7> superSawDetunes { { -0.22f, -0.14f, -0.07f, 0.0f, 0.07f, 0.14f, 0.22f } };
    std::array<float, 7> superSawDrift { { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } };

    std::array<float, 7> pinkState { { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } };
    uint32_t noiseSeed { 0x13579BDFu };
    float noiseColorState { 0.0f };
    float pinkColorState { 0.0f };

    double fmModAngle { 0.0 };
    double syncMasterAngle { 0.0 };
    double syncSlaveAngle { 0.0 };

    static constexpr int karplusBufferSize = 32768;
    std::array<float, karplusBufferSize> karplusBuffer {};
    int karplusWriteIndex { 0 };
    int karplusDelaySamples { 220 };
    float karplusLastSample { 0.0f };

    int digitalHoldCounter { 0 };
    int digitalHoldSamples { 1 };
    float digitalHeldSample { 0.0f };

    std::array<double, 4> physicalPhase { { 0.0, 0.0, 0.0, 0.0 } };
    std::array<float, 4> physicalState { { 0.0f, 0.0f, 0.0f, 0.0f } };
};
