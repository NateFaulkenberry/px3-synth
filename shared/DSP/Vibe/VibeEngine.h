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

    // `load` is the previous block's source level, 0..1. A real supply rail
    // droops in proportion to the current the circuit draws, so the sag has to
    // see the signal; without it the "PSU movement" is just a free-running LFO.
    void advance(int samples, float load);
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

    // Per-voice drift. Real polysynth voices drift INDEPENDENTLY - that
    // divergence is what makes a stacked chord thicken over time. A single
    // shared drift signal moves every voice in lockstep and cannot do that,
    // however deep it is set.
    struct VoiceDrift
    {
        float phase { 0.0f };
        float rateHz { 0.03f };
        float walk { 0.0f };
        float walkTarget { 0.0f };
        float thermalPhase { 0.0f };
        float thermalRateHz { 0.011f };
    };

    // One entry per voice in the pool. Sized 32 previously, while the synth
    // runs 64 voices, so the upper half of the pool received no variation.
    static constexpr int kMaxVoices = 64;
    std::array<VoiceVariation, kMaxVoices> voiceVariations {};
    std::array<VoiceVariation, kMaxVoices> voiceStatic {};
    std::array<VoiceDrift, kMaxVoices> voiceDrift {};
    int preparedVoiceCount { 16 };

    // Reservoir behaviour: droops quickly under load, recovers slowly.
    float chaosStepAccumulator { 0.0f };
    float psuSag { 0.0f };
    float loadFollower { 0.0f };
};
