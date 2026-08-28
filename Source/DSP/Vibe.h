#pragma once

#include <JuceHeader.h>

#include "VibeEngine.h"
#include "VibeTypes.h"

#include <array>
#include <atomic>
#include <cstdint>

class Vibe
{
public:
    void prepare(double sampleRate, int voiceCount, uint32_t seed);
    void updateForBlock(const VibeSettings& settings, int numSamples, float load);

    void setSeed(uint32_t seed);
    uint32_t getSeed() const;

    float getGlobalAmount() const;
    float getEffectiveAmount() const;
    bool isBypassed() const;

    VibeTuning getTuning() const;
    void setTuningValue(const juce::String& key, float value);
    float getTuningValue(const juce::String& key) const;

    VibeSharedState getSharedState() const;
    VibeVoiceVariation getVoiceVariation(int voiceIndex) const;

private:
    int sanitizeTypeIndex(int typeIndex) const;
    void applyTypeProfile(int typeIndex);

    VibeEngine engine;

    std::atomic<uint32_t> seedValue { 1337u };
    std::atomic<uint32_t> lastAppliedSeed { 1337u };
    std::atomic<float> lastGlobalAmount { 0.0f };
    std::atomic<int> lastAppliedType { -1 };

    std::atomic<float> tuneOscDrift { 0.55f };
    std::atomic<float> tuneVoiceVar { 0.55f };
    std::atomic<float> tuneFilterVar { 0.45f };
    std::atomic<float> tuneSaturation { 0.40f };
    std::atomic<float> tuneNoise { 0.25f };
    std::atomic<float> tunePsu { 0.38f };
    std::atomic<float> tuneVca { 0.42f };
    std::atomic<float> tuneAsym { 0.32f };
    std::atomic<float> tuneTemp { 0.40f };
    std::atomic<float> tuneChaos { 0.50f };

    static constexpr std::array<const char*, 6> kTypeChoices {
        "Warm", "Hot", "Cool", "Vintage", "Clean", "LoFi"
    };
};
