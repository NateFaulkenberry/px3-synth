#pragma once

#include <array>
#include <cstdint>

class VibeEngine
{
public:
    struct Tuning
    {
        float oscillatorDrift { 0.55f };
        float voiceVariation { 0.55f };
        float filterVariation { 0.45f };
        float saturation { 0.40f };
        float noise { 0.25f };
        float psuMovement { 0.38f };
        float vcaNonlinearity { 0.42f };
        float waveformAsymmetry { 0.32f };
        float temperatureDrift { 0.40f };
        float correlatedChaos { 0.50f };
    };

    struct SharedState
    {
        float oscillatorDrift { 0.0f };
        float psu { 0.0f };
        float temperature { 0.0f };
        float chaos { 0.0f };
    };

    struct VoiceVariation
    {
        float pitchCents { 0.0f };
        float cutoffOffset { 0.0f };
        float resonanceOffset { 0.0f };
        float gainOffset { 0.0f };
        float asymmetryBias { 0.0f };
        float saturationBias { 0.0f };
    };

    void prepare(double sampleRate, int voiceCount, uint32_t seed);
    void reset();

    void setSeed(uint32_t seed);
    uint32_t getSeed() const { return randomSeed; }

    void setBypass(bool shouldBypass) { bypass = shouldBypass; }
    bool isBypassed() const { return bypass; }

    void setTuning(const Tuning& newTuning) { tuning = newTuning; }
    const Tuning& getTuning() const { return tuning; }

    void setGlobalAmount(float amount) { globalAmount = amount; }
    float getGlobalAmount() const { return globalAmount; }
    float getEffectiveAmount() const;

    void advance(int samples);
    const SharedState& getSharedState() const { return shared; }
    VoiceVariation getVoiceVariation(int voiceIndex) const;

private:
    float nextSignedNoise();
    static float clamp01(float v);
    static float clampSigned(float v, float limit);
    void rebuildVoiceVariations(int voiceCount);

    double sampleRateHz { 44100.0 };
    bool bypass { false };
    float globalAmount { 0.0f };
    Tuning tuning;
    SharedState shared;

    uint32_t randomSeed { 0x13579BDFu };
    uint32_t noiseState { 0x2468ACE1u };

    float psuPhase { 0.0f };
    float tempPhase { 0.0f };
    float driftPhase { 0.0f };

    float tempTarget { 0.0f };
    float tempState { 0.0f };
    float driftTarget { 0.0f };
    float driftState { 0.0f };

    // Bounded Lorenz-like state, updated at block rate for correlated movement.
    float chaosX { 0.01f };
    float chaosY { 0.0f };
    float chaosZ { 0.0f };

    std::array<VoiceVariation, 32> voiceVariations {};
    int preparedVoiceCount { 16 };
};
