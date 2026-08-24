#include "VibeComponent.h"

#include <cmath>

void VibeComponent::prepare(double sampleRate, int voiceCount, uint32_t seed)
{
    setSeed(seed);
    engine.prepare(sampleRate, voiceCount, getSeed());
    lastAppliedSeed.store(getSeed(), std::memory_order_relaxed);
    applyTypeProfile(0);
}

void VibeComponent::updateForBlock(const VibeSettings& settings, int numSamples)
{
    applyTypeProfile(settings.typeIndex);

    const auto seed = getSeed();
    if (seed != lastAppliedSeed.load(std::memory_order_relaxed))
    {
        engine.setSeed(seed);
        lastAppliedSeed.store(seed, std::memory_order_relaxed);
    }

    VibeEngine::Tuning t;
    t.oscillatorDrift = juce::jlimit(0.0f, 1.0f, tuneOscDrift.load(std::memory_order_relaxed));
    t.voiceVariation = juce::jlimit(0.0f, 1.0f, tuneVoiceVar.load(std::memory_order_relaxed));
    t.filterVariation = juce::jlimit(0.0f, 1.0f, tuneFilterVar.load(std::memory_order_relaxed));
    t.saturation = juce::jlimit(0.0f, 1.0f, tuneSaturation.load(std::memory_order_relaxed));
    t.noise = juce::jlimit(0.0f, 1.0f, tuneNoise.load(std::memory_order_relaxed));
    t.psuMovement = juce::jlimit(0.0f, 1.0f, tunePsu.load(std::memory_order_relaxed));
    t.vcaNonlinearity = juce::jlimit(0.0f, 1.0f, tuneVca.load(std::memory_order_relaxed));
    t.waveformAsymmetry = juce::jlimit(0.0f, 1.0f, tuneAsym.load(std::memory_order_relaxed));
    t.temperatureDrift = juce::jlimit(0.0f, 1.0f, tuneTemp.load(std::memory_order_relaxed));
    t.correlatedChaos = juce::jlimit(0.0f, 1.0f, tuneChaos.load(std::memory_order_relaxed));

    engine.setTuning(t);
    engine.setBypass(!settings.enabled);

    const auto amount = juce::jlimit(0.0f, 1.0f, settings.globalAmount);
    lastGlobalAmount.store(amount, std::memory_order_relaxed);
    engine.setGlobalAmount(amount);
    engine.advance(numSamples);
}

void VibeComponent::setSeed(uint32_t seed)
{
    seedValue.store(seed == 0u ? 1u : seed, std::memory_order_relaxed);
}

uint32_t VibeComponent::getSeed() const
{
    return seedValue.load(std::memory_order_relaxed);
}

float VibeComponent::getGlobalAmount() const
{
    return lastGlobalAmount.load(std::memory_order_relaxed);
}

float VibeComponent::getEffectiveAmount() const
{
    return juce::jlimit(0.0f, 1.0f, engine.getEffectiveAmount());
}

bool VibeComponent::isBypassed() const
{
    return engine.isBypassed();
}

VibeTuning VibeComponent::getTuning() const
{
    VibeTuning t;
    t.oscillatorDrift = juce::jlimit(0.0f, 1.0f, tuneOscDrift.load(std::memory_order_relaxed));
    t.voiceVariation = juce::jlimit(0.0f, 1.0f, tuneVoiceVar.load(std::memory_order_relaxed));
    t.filterVariation = juce::jlimit(0.0f, 1.0f, tuneFilterVar.load(std::memory_order_relaxed));
    t.saturation = juce::jlimit(0.0f, 1.0f, tuneSaturation.load(std::memory_order_relaxed));
    t.noise = juce::jlimit(0.0f, 1.0f, tuneNoise.load(std::memory_order_relaxed));
    t.psuMovement = juce::jlimit(0.0f, 1.0f, tunePsu.load(std::memory_order_relaxed));
    t.vcaNonlinearity = juce::jlimit(0.0f, 1.0f, tuneVca.load(std::memory_order_relaxed));
    t.waveformAsymmetry = juce::jlimit(0.0f, 1.0f, tuneAsym.load(std::memory_order_relaxed));
    t.temperatureDrift = juce::jlimit(0.0f, 1.0f, tuneTemp.load(std::memory_order_relaxed));
    t.correlatedChaos = juce::jlimit(0.0f, 1.0f, tuneChaos.load(std::memory_order_relaxed));
    return t;
}

void VibeComponent::setTuningValue(const juce::String& key, float value)
{
    const auto v = juce::jlimit(0.0f, 1.0f, value);
    if (key.equalsIgnoreCase("oscillatorDrift")) tuneOscDrift.store(v, std::memory_order_relaxed);
    else if (key.equalsIgnoreCase("voiceVariation")) tuneVoiceVar.store(v, std::memory_order_relaxed);
    else if (key.equalsIgnoreCase("filterVariation")) tuneFilterVar.store(v, std::memory_order_relaxed);
    else if (key.equalsIgnoreCase("saturation")) tuneSaturation.store(v, std::memory_order_relaxed);
    else if (key.equalsIgnoreCase("noise")) tuneNoise.store(v, std::memory_order_relaxed);
    else if (key.equalsIgnoreCase("psuMovement")) tunePsu.store(v, std::memory_order_relaxed);
    else if (key.equalsIgnoreCase("vcaNonlinearity")) tuneVca.store(v, std::memory_order_relaxed);
    else if (key.equalsIgnoreCase("waveformAsymmetry")) tuneAsym.store(v, std::memory_order_relaxed);
    else if (key.equalsIgnoreCase("temperatureDrift")) tuneTemp.store(v, std::memory_order_relaxed);
    else if (key.equalsIgnoreCase("correlatedChaos")) tuneChaos.store(v, std::memory_order_relaxed);
}

float VibeComponent::getTuningValue(const juce::String& key) const
{
    if (key.equalsIgnoreCase("oscillatorDrift")) return tuneOscDrift.load(std::memory_order_relaxed);
    if (key.equalsIgnoreCase("voiceVariation")) return tuneVoiceVar.load(std::memory_order_relaxed);
    if (key.equalsIgnoreCase("filterVariation")) return tuneFilterVar.load(std::memory_order_relaxed);
    if (key.equalsIgnoreCase("saturation")) return tuneSaturation.load(std::memory_order_relaxed);
    if (key.equalsIgnoreCase("noise")) return tuneNoise.load(std::memory_order_relaxed);
    if (key.equalsIgnoreCase("psuMovement")) return tunePsu.load(std::memory_order_relaxed);
    if (key.equalsIgnoreCase("vcaNonlinearity")) return tuneVca.load(std::memory_order_relaxed);
    if (key.equalsIgnoreCase("waveformAsymmetry")) return tuneAsym.load(std::memory_order_relaxed);
    if (key.equalsIgnoreCase("temperatureDrift")) return tuneTemp.load(std::memory_order_relaxed);
    if (key.equalsIgnoreCase("correlatedChaos")) return tuneChaos.load(std::memory_order_relaxed);
    return 0.0f;
}

VibeSharedState VibeComponent::getSharedState() const
{
    const auto shared = engine.getSharedState();
    VibeSharedState out;
    out.oscillatorDrift = shared.oscillatorDrift;
    out.psu = shared.psu;
    out.temperature = shared.temperature;
    out.chaos = shared.chaos;
    return out;
}

VibeVoiceVariation VibeComponent::getVoiceVariation(int voiceIndex) const
{
    const auto variation = engine.getVoiceVariation(voiceIndex);
    VibeVoiceVariation out;
    out.pitchCents = variation.pitchCents;
    out.cutoffOffset = variation.cutoffOffset;
    out.resonanceOffset = variation.resonanceOffset;
    out.gainOffset = variation.gainOffset;
    out.asymmetryBias = variation.asymmetryBias;
    out.saturationBias = variation.saturationBias;
    return out;
}

int VibeComponent::sanitizeTypeIndex(int typeIndex) const
{
    return juce::jlimit(0, static_cast<int>(kTypeChoices.size()) - 1, typeIndex);
}

void VibeComponent::applyTypeProfile(int typeIndex)
{
    const auto clamped = sanitizeTypeIndex(typeIndex);
    if (clamped == lastAppliedType.load(std::memory_order_relaxed))
    {
        return;
    }

    struct Profile
    {
        float oscillatorDrift;
        float voiceVariation;
        float filterVariation;
        float saturation;
        float noise;
        float psuMovement;
        float vcaNonlinearity;
        float waveformAsymmetry;
        float temperatureDrift;
        float correlatedChaos;
    };

    static const std::array<Profile, 6> profiles {
        {
            { 0.55f, 0.55f, 0.45f, 0.40f, 0.25f, 0.38f, 0.42f, 0.32f, 0.40f, 0.50f },
            { 0.62f, 0.66f, 0.56f, 0.74f, 0.36f, 0.60f, 0.78f, 0.68f, 0.48f, 0.72f },
            { 0.35f, 0.34f, 0.30f, 0.18f, 0.08f, 0.22f, 0.18f, 0.14f, 0.24f, 0.20f },
            { 0.74f, 0.72f, 0.52f, 0.48f, 0.52f, 0.72f, 0.44f, 0.40f, 0.76f, 0.64f },
            { 0.16f, 0.14f, 0.12f, 0.08f, 0.03f, 0.10f, 0.08f, 0.06f, 0.10f, 0.08f },
            { 0.68f, 0.82f, 0.62f, 0.56f, 0.84f, 0.70f, 0.52f, 0.74f, 0.58f, 0.78f }
        }
    };

    const auto& p = profiles[static_cast<std::size_t>(clamped)];
    tuneOscDrift.store(p.oscillatorDrift, std::memory_order_relaxed);
    tuneVoiceVar.store(p.voiceVariation, std::memory_order_relaxed);
    tuneFilterVar.store(p.filterVariation, std::memory_order_relaxed);
    tuneSaturation.store(p.saturation, std::memory_order_relaxed);
    tuneNoise.store(p.noise, std::memory_order_relaxed);
    tunePsu.store(p.psuMovement, std::memory_order_relaxed);
    tuneVca.store(p.vcaNonlinearity, std::memory_order_relaxed);
    tuneAsym.store(p.waveformAsymmetry, std::memory_order_relaxed);
    tuneTemp.store(p.temperatureDrift, std::memory_order_relaxed);
    tuneChaos.store(p.correlatedChaos, std::memory_order_relaxed);
    lastAppliedType.store(clamped, std::memory_order_relaxed);
}
