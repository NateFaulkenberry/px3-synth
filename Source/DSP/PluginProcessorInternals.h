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
// Sources generate this far below full scale so modulation has somewhere to go.
// Declared once and shared, so the source trim and the mixer fader range cannot
// drift apart: the fader tops out at exactly the reciprocal of this, which is
// what keeps a channel able to reach full scale.
inline constexpr float kSourceHeadroomDb = -4.0f;

inline float sourceHeadroomGain()
{
    return juce::Decibels::decibelsToGain(kSourceHeadroomDb);
}

inline float channelFaderMaxGain()
{
    return juce::Decibels::decibelsToGain(-kSourceHeadroomDb);
}

inline constexpr int kCurrentStateVersion = 11;

inline const juce::Identifier kStateTypeId("PX3_STATE");
inline const juce::Identifier kStateVersionId("stateVersion");
inline const juce::Identifier kModuleOrderId("MODULE_ORDER");
inline const juce::Identifier kModuleEntryId("MODULE");
inline const juce::Identifier kModuleIdProperty("id");
inline const juce::Identifier kModuleOrderRevisionId("moduleOrderRevision");
inline const juce::Identifier kLfoEnabledId("enabled");
inline const juce::Identifier kLfoFrequencyId("frequency");
inline const juce::Identifier kLfoWaveformId("waveform");
inline const juce::Identifier kLfoAssignmentId("assignment");
inline const juce::Identifier kEnvelopeAssignmentId("assignment");
inline const juce::Identifier kLfoSourcesStateId("LFO_SOURCES");
inline const juce::Identifier kEnvelopeSourcesStateId("ENVELOPE_SOURCES");
inline const juce::Identifier kSourceEntryId("SOURCE");
inline const juce::Identifier kSourceIndexId("index");
inline const juce::Identifier kSubOscStateId("SUBOSC");
inline const juce::Identifier kSubOscEnabledId("enabled");
inline const juce::Identifier kSubOscLevelId("level");
inline const juce::Identifier kSubOscPitchId("pitch");
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

inline constexpr int kFxStageCount = 4;

inline const std::array<juce::String, kFxStageCount> kFxModuleIds { juce::String("harmonicDrive"),
                                                                     juce::String("delay"),
                                                                     juce::String("reverb"),
                                                                     juce::String("mood") };

inline juce::String nowTimestamp()
{
    const auto now = juce::Time::getCurrentTime();
    const auto ms = static_cast<int>(juce::Time::getMillisecondCounter() % 1000u);
    return now.formatted("%H:%M:%S") + "." + juce::String(ms).paddedLeft('0', 3);
}

inline juce::String moduleIdForStage(int stage)
{
    const auto clamped = juce::jlimit(0, kFxStageCount - 1, stage);
    return kFxModuleIds[static_cast<std::size_t>(clamped)];
}

inline int stageForModuleId(const juce::String& moduleId)
{
    for (int stage = 0; stage < kFxStageCount; ++stage)
    {
        if (moduleId.equalsIgnoreCase(kFxModuleIds[static_cast<std::size_t>(stage)]))
        {
            return stage;
        }
    }

    return -1;
}

inline juce::String formatOrderString(const std::array<int, kFxStageCount>& order)
{
    juce::StringArray items;
    for (const auto stage : order)
    {
        items.add(moduleIdForStage(stage));
    }
    return items.joinIntoString(",");
}

inline uint32_t packFxOrder(const std::array<int, kFxStageCount>& order)
{
    return (static_cast<uint32_t>(order[0] & 0x3)
            | (static_cast<uint32_t>(order[1] & 0x3) << 2)
            | (static_cast<uint32_t>(order[2] & 0x3) << 4)
            | (static_cast<uint32_t>(order[3] & 0x3) << 6));
}

inline std::array<int, kFxStageCount> unpackFxOrder(uint32_t packed)
{
    return {
        { static_cast<int>(packed & 0x3u),
          static_cast<int>((packed >> 2) & 0x3u),
          static_cast<int>((packed >> 4) & 0x3u),
          static_cast<int>((packed >> 6) & 0x3u) }
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
