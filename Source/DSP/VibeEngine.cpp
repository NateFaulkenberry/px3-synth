#include "VibeEngine.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float kTwoPi = 6.28318530717958647692f;

inline uint32_t hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
}

float VibeEngine::clamp01(float v)
{
    return std::clamp(v, 0.0f, 1.0f);
}

float VibeEngine::clampSigned(float v, float limit)
{
    return std::clamp(v, -limit, limit);
}

void VibeEngine::prepare(double sampleRate, int voiceCount, uint32_t seed)
{
    sampleRateHz = std::max(8000.0, sampleRate);
    preparedVoiceCount = std::clamp(voiceCount, 1, static_cast<int>(voiceVariations.size()));
    setSeed(seed);
    reset();
    rebuildVoiceVariations(preparedVoiceCount);
}

void VibeEngine::reset()
{
    psuPhase = 0.0f;
    tempPhase = 0.0f;
    driftPhase = 0.0f;

    tempTarget = 0.0f;
    tempState = 0.0f;
    driftTarget = 0.0f;
    driftState = 0.0f;

    chaosX = 0.01f;
    chaosY = 0.0f;
    chaosZ = 0.0f;

    shared = {};
}

void VibeEngine::setSeed(uint32_t seed)
{
    randomSeed = seed == 0u ? 1u : seed;
    noiseState = hash32(randomSeed ^ 0xA53C9E11u);
    rebuildVoiceVariations(preparedVoiceCount);
}

float VibeEngine::getEffectiveAmount() const
{
    if (bypass)
    {
        return 0.0f;
    }

    const auto v = clamp01(globalAmount);
    // Perceptual map: keeps low settings subtle while preserving full-scale reach.
    return std::pow(v, 1.35f);
}

float VibeEngine::nextSignedNoise()
{
    noiseState ^= noiseState << 13;
    noiseState ^= noiseState >> 17;
    noiseState ^= noiseState << 5;
    const auto u = static_cast<float>(noiseState & 0x00ffffffu) / static_cast<float>(0x01000000u);
    return u * 2.0f - 1.0f;
}

void VibeEngine::rebuildVoiceVariations(int voiceCount)
{
    preparedVoiceCount = std::clamp(voiceCount, 1, static_cast<int>(voiceVariations.size()));

    for (int i = 0; i < preparedVoiceCount; ++i)
    {
        const auto index = static_cast<uint32_t>(i);
        const auto base = hash32(randomSeed ^ (index * 2654435761u + 0x9E3779B9u));
        auto randUnit = [base](uint32_t salt)
        {
            const auto v = hash32(base ^ salt);
            const auto u = static_cast<float>(v & 0x00ffffffu) / static_cast<float>(0x01000000u);
            return u * 2.0f - 1.0f;
        };

        auto& out = voiceVariations[static_cast<std::size_t>(i)];
        out.pitchCents = randUnit(0x01u) * 4.0f;
        out.cutoffOffset = randUnit(0x02u) * 0.018f;
        out.resonanceOffset = randUnit(0x03u) * 0.030f;
        out.gainOffset = randUnit(0x04u) * 0.025f;
        out.asymmetryBias = randUnit(0x05u) * 0.9f;
        out.saturationBias = randUnit(0x06u) * 0.9f;
    }

    for (std::size_t i = static_cast<std::size_t>(preparedVoiceCount); i < voiceVariations.size(); ++i)
    {
        voiceVariations[i] = {};
    }
}

VibeEngine::VoiceVariation VibeEngine::getVoiceVariation(int voiceIndex) const
{
    if (voiceIndex < 0 || voiceIndex >= preparedVoiceCount)
    {
        return {};
    }

    return voiceVariations[static_cast<std::size_t>(voiceIndex)];
}

void VibeEngine::advance(int samples)
{
    const auto dt = static_cast<float>(std::max(1, samples)) / static_cast<float>(sampleRateHz);

    psuPhase += kTwoPi * dt * 0.11f;
    tempPhase += kTwoPi * dt * 0.018f;
    driftPhase += kTwoPi * dt * 0.035f;

    while (psuPhase > kTwoPi) psuPhase -= kTwoPi;
    while (tempPhase > kTwoPi) tempPhase -= kTwoPi;
    while (driftPhase > kTwoPi) driftPhase -= kTwoPi;

    tempTarget += nextSignedNoise() * dt * 0.05f;
    tempTarget = clampSigned(tempTarget, 1.0f);
    tempState += (tempTarget - tempState) * std::clamp(dt * 0.35f, 0.0f, 1.0f);

    driftTarget += nextSignedNoise() * dt * 0.30f;
    driftTarget = clampSigned(driftTarget, 1.0f);
    driftState += (driftTarget - driftState) * std::clamp(dt * 2.6f, 0.0f, 1.0f);

    // Bounded chaotic source (Lorenz-inspired), stepped a few times per block.
    constexpr float sigma = 10.0f;
    constexpr float rho = 24.0f;
    constexpr float beta = 8.0f / 3.0f;
    constexpr float h = 0.0055f;

    for (int i = 0; i < 3; ++i)
    {
        const auto dx = sigma * (chaosY - chaosX);
        const auto dy = chaosX * (rho - chaosZ) - chaosY;
        const auto dz = chaosX * chaosY - beta * chaosZ;

        chaosX = clampSigned(chaosX + h * dx, 32.0f);
        chaosY = clampSigned(chaosY + h * dy, 32.0f);
        chaosZ = std::clamp(chaosZ + h * dz, 0.0f, 64.0f);
    }

    const auto psuSlow = std::sin(psuPhase) * 0.55f + std::sin(psuPhase * 6.1f) * 0.045f + nextSignedNoise() * 0.008f;
    const auto tempSlow = std::sin(tempPhase + std::sin(tempPhase * 0.21f) * 0.6f) * 0.35f + tempState * 0.65f;
    const auto driftSlow = std::sin(driftPhase * 0.73f) * 0.35f + driftState * 0.65f;
    const auto chaos = std::tanh(chaosX * 0.08f);

    shared.psu = clampSigned(psuSlow, 1.0f);
    shared.temperature = clampSigned(tempSlow, 1.0f);
    shared.oscillatorDrift = clampSigned(driftSlow, 1.0f);
    shared.chaos = clampSigned(chaos, 1.0f);
}
