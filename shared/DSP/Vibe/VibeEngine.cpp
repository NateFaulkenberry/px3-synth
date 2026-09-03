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

    chaosStepAccumulator = 0.0f;
    psuSag = 0.0f;
    loadFollower = 0.0f;

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

        auto& out = voiceStatic[static_cast<std::size_t>(i)];
        out.pitchCents = randUnit(0x01u) * 4.0f;
        out.cutoffOffset = randUnit(0x02u) * 0.018f;
        out.resonanceOffset = randUnit(0x03u) * 0.030f;
        out.gainOffset = randUnit(0x04u) * 0.025f;
        out.asymmetryBias = randUnit(0x05u) * 0.9f;
        out.saturationBias = randUnit(0x06u) * 0.9f;
        voiceVariations[static_cast<std::size_t>(i)] = out;

        // Each voice gets its OWN drift oscillator, at its own rate and phase,
        // so no two voices track each other. Rates are deliberately slow and
        // mutually unrelated: vintage polysynth tuning wanders over seconds to
        // minutes, and it is the divergence, not the depth, that thickens a chord.
        auto& drift = voiceDrift[static_cast<std::size_t>(i)];
        drift.phase = (randUnit(0x11u) * 0.5f + 0.5f) * kTwoPi;
        drift.rateHz = 0.020f + (randUnit(0x12u) * 0.5f + 0.5f) * 0.075f;
        drift.thermalPhase = (randUnit(0x13u) * 0.5f + 0.5f) * kTwoPi;
        drift.thermalRateHz = 0.005f + (randUnit(0x14u) * 0.5f + 0.5f) * 0.017f;
        drift.walk = 0.0f;
        drift.walkTarget = 0.0f;
    }

    for (std::size_t i = static_cast<std::size_t>(preparedVoiceCount); i < voiceVariations.size(); ++i)
    {
        voiceVariations[i] = {};
        voiceStatic[i] = {};
        voiceDrift[i] = {};
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

void VibeEngine::advance(int samples, float load)
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

    // Bounded chaotic source (Lorenz-inspired). The number of integration steps
    // now follows elapsed TIME rather than being a fixed count per block: at a
    // fixed three steps per call the attractor evolved eight times faster on a
    // 64-sample buffer than on a 512-sample one, so the character of the effect
    // depended on the host's buffer size.
    {
        constexpr float sigma = 10.0f;
        constexpr float rho = 24.0f;
        constexpr float beta = 8.0f / 3.0f;
        // Reference: three steps of h = 0.0055 per 512 samples at 48 kHz.
        constexpr float stepsPerSecond = 3.0f * 48000.0f / 512.0f;
        constexpr float h = 0.0055f;

        // Fractional steps are carried over rather than rounded. Rounding up to
        // a minimum of one step per call makes small buffers over-integrate,
        // which reintroduces the buffer-size dependence this is meant to remove.
        chaosStepAccumulator += stepsPerSecond * dt;
        auto steps = static_cast<int>(chaosStepAccumulator);
        steps = std::clamp(steps, 0, 64);
        chaosStepAccumulator -= static_cast<float>(steps);
        for (int i = 0; i < steps; ++i)
        {
            const auto dx = sigma * (chaosY - chaosX);
            const auto dy = chaosX * (rho - chaosZ) - chaosY;
            const auto dz = chaosX * chaosY - beta * chaosZ;

            chaosX = clampSigned(chaosX + h * dx, 32.0f);
            chaosY = clampSigned(chaosY + h * dy, 32.0f);
            chaosZ = std::clamp(chaosZ + h * dz, 0.0f, 64.0f);
        }
    }

    // Supply rail. A reservoir capacitor droops in proportion to the current
    // drawn and recharges through the rectifier, so sag follows the programme
    // quickly and recovers slowly. The residual ripple is what remains when the
    // instrument is quiet.
    {
        const auto drawn = std::clamp(load, 0.0f, 1.0f);
        const auto attack = std::clamp(dt * 14.0f, 0.0f, 1.0f);
        const auto release = std::clamp(dt * 1.1f, 0.0f, 1.0f);
        loadFollower += (drawn - loadFollower) * (drawn > loadFollower ? attack : release);

        // Sag is negative: the rail falls as the instrument gets louder.
        const auto targetSag = -std::sqrt(std::clamp(loadFollower, 0.0f, 1.0f)) * 0.85f;
        psuSag += (targetSag - psuSag) * (targetSag < psuSag ? attack : release);
    }

    const auto ripple = std::sin(psuPhase) * 0.22f
                        + std::sin(psuPhase * 6.1f) * 0.05f
                        + nextSignedNoise() * 0.008f;
    const auto tempSlow = std::sin(tempPhase + std::sin(tempPhase * 0.21f) * 0.6f) * 0.35f + tempState * 0.65f;
    const auto driftSlow = std::sin(driftPhase * 0.73f) * 0.35f + driftState * 0.65f;
    const auto chaos = std::tanh(chaosX * 0.08f);

    // Ripple rides on the sagged rail, and gets a little deeper as the rail
    // drops - a loaded supply ripples more, which is the audible part.
    shared.psu = clampSigned(psuSag + ripple * (1.0f + 0.6f * -psuSag), 1.0f);
    shared.temperature = clampSigned(tempSlow, 1.0f);
    shared.oscillatorDrift = clampSigned(driftSlow, 1.0f);
    shared.chaos = clampSigned(chaos, 1.0f);

    // Per-voice drift, advanced independently.
    for (int i = 0; i < preparedVoiceCount; ++i)
    {
        const auto index = static_cast<std::size_t>(i);
        auto& drift = voiceDrift[index];

        drift.phase += kTwoPi * dt * drift.rateHz;
        if (drift.phase > kTwoPi) drift.phase -= kTwoPi;
        drift.thermalPhase += kTwoPi * dt * drift.thermalRateHz;
        if (drift.thermalPhase > kTwoPi) drift.thermalPhase -= kTwoPi;

        drift.walkTarget += nextSignedNoise() * dt * 0.22f;
        drift.walkTarget = clampSigned(drift.walkTarget, 1.0f);
        drift.walk += (drift.walkTarget - drift.walk) * std::clamp(dt * 0.9f, 0.0f, 1.0f);

        const auto wander = std::sin(drift.phase) * 0.55f + drift.walk * 0.45f;
        const auto thermal = std::sin(drift.thermalPhase);

        // The static per-voice character stays as the voice's own "build
        // tolerance"; the drift is added on top and is what evolves.
        const auto& base = voiceStatic[index];
        auto& out = voiceVariations[index];
        out.pitchCents = base.pitchCents + wander * 5.5f + thermal * 3.0f;
        out.cutoffOffset = base.cutoffOffset + wander * 0.020f + thermal * 0.010f;
        out.resonanceOffset = base.resonanceOffset + wander * 0.012f;
        out.gainOffset = base.gainOffset + wander * 0.014f;
        out.asymmetryBias = base.asymmetryBias;
        out.saturationBias = base.saturationBias;
    }
}
