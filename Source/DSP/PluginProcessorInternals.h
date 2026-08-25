#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

// Shared internal constants/helpers for split PluginProcessor implementation
// files. This is intentionally private processor internals, not a public API.

namespace px3::processor_internal
{
inline constexpr int kCurrentStateVersion = 9;

inline const juce::Identifier kStateTypeId("PX3_STATE");
inline const juce::Identifier kStateVersionId("stateVersion");
inline const juce::Identifier kModuleOrderId("MODULE_ORDER");
inline const juce::Identifier kModuleEntryId("MODULE");
inline const juce::Identifier kModuleIdProperty("id");
inline const juce::Identifier kModuleOrderRevisionId("moduleOrderRevision");
inline const juce::Identifier kLfoStateId("LFO");
inline const juce::Identifier kLfoEnabledId("enabled");
inline const juce::Identifier kLfoFrequencyId("frequency");
inline const juce::Identifier kLfoWaveformId("waveform");
inline const juce::Identifier kLfoAssignmentId("assignment");
inline const juce::Identifier kEnvelopeStateId("ENVELOPE");
inline const juce::Identifier kEnvelopeAssignmentId("assignment");
inline const juce::Identifier kLfoSourcesStateId("LFO_SOURCES");
inline const juce::Identifier kEnvelopeSourcesStateId("ENVELOPE_SOURCES");
inline const juce::Identifier kSourceEntryId("SOURCE");
inline const juce::Identifier kSourceIndexId("index");
inline const juce::Identifier kSubOscStateId("SUBOSC");
inline const juce::Identifier kSubOscEnabledId("enabled");
inline const juce::Identifier kSubOscLevelId("level");
inline const juce::Identifier kSubOscOctaveId("octave");
inline const juce::Identifier kSubOscWaveformId("waveform");
inline const juce::Identifier kTopMenuViewId("topMenuView");
inline const juce::Identifier kVibeStateId("VIBE");
inline const juce::Identifier kVibeBypassId("bypass");
inline const juce::Identifier kVibeSeedId("seed");

inline std::atomic<uint32_t> kInstanceCounter { 0u };
inline std::atomic<int> kActiveInstanceCount { 0 };

inline const juce::StringArray kVibeTypeChoices {
    "Warm",
    "Hot",
    "Cool",
    "Vintage",
    "Clean",
    "LoFi"
};

inline const std::array<juce::String, 3> kFxModuleIds { juce::String("harmonicDrive"),
                                                         juce::String("delay"),
                                                         juce::String("reverb") };

inline juce::String nowTimestamp()
{
    const auto now = juce::Time::getCurrentTime();
    const auto ms = static_cast<int>(juce::Time::getMillisecondCounter() % 1000u);
    return now.formatted("%H:%M:%S") + "." + juce::String(ms).paddedLeft('0', 3);
}

inline juce::String moduleIdForStage(int stage)
{
    const auto clamped = juce::jlimit(0, 2, stage);
    return kFxModuleIds[static_cast<std::size_t>(clamped)];
}

inline int stageForModuleId(const juce::String& moduleId)
{
    for (int stage = 0; stage < 3; ++stage)
    {
        if (moduleId.equalsIgnoreCase(kFxModuleIds[static_cast<std::size_t>(stage)]))
        {
            return stage;
        }
    }

    return -1;
}

inline juce::String formatOrderString(const std::array<int, 3>& order)
{
    juce::StringArray items;
    for (const auto stage : order)
    {
        items.add(moduleIdForStage(stage));
    }
    return items.joinIntoString(",");
}

inline uint32_t packFxOrder(const std::array<int, 3>& order)
{
    return (static_cast<uint32_t>(order[0] & 0x3)
            | (static_cast<uint32_t>(order[1] & 0x3) << 2)
            | (static_cast<uint32_t>(order[2] & 0x3) << 4));
}

inline std::array<int, 3> unpackFxOrder(uint32_t packed)
{
    return {
        { static_cast<int>(packed & 0x3u),
          static_cast<int>((packed >> 2) & 0x3u),
          static_cast<int>((packed >> 4) & 0x3u) }
    };
}

inline float divisionBeatsForIndex(int index)
{
    static constexpr std::array<float, 8> kBeatDivisions { 0.0f, 4.0f, 2.0f, 1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f };
    const auto clamped = juce::jlimit(0, static_cast<int>(kBeatDivisions.size()) - 1, index);
    return kBeatDivisions[static_cast<std::size_t>(clamped)];
}

inline float clamp01(float v)
{
    return juce::jlimit(0.0f, 1.0f, v);
}

inline float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

inline float smoothstep(float x)
{
    const auto t = clamp01(x);
    return t * t * (3.0f - 2.0f * t);
}
}
