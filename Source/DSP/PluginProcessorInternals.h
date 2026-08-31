#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

// Shared internal constants/helpers for split PluginProcessor implementation
// files. This is intentionally private processor internals, not a public API.

#include "FxChain.h"

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

// User wavetable selections. Stored as a NAME rather than an index because the
// factory list has fixed positions and the user library does not - and stored
// per oscillator, not once, because the three are independent.
inline const juce::Identifier kUserWavetablesId("userWavetables");
inline const juce::Identifier kUserWavetableNameId("name");
inline const juce::Identifier kUserWavetableOscId("osc");
// Which preset the editor last loaded. UI session state like topMenuView: the
// processor never reads it, and it is stripped from preset FILES - a preset
// naming itself would be circular. It exists because the editor is rebuilt
// every time the window is reopened, and without it the tab fell back to INIT
// over a patch that had not changed.
// What the preset tab shows before anything has been loaded. The same string
// PresetManager::initPresetName() returns - INIT has no file and no category,
// so this is the only form of it there is.
inline const juce::String kNoPresetLabel { "- INIT -" };

inline const juce::Identifier kLoadedPresetNameId("loadedPresetName");
inline const juce::Identifier kLoadedPresetCategoryId("loadedPresetCategory");
inline const juce::Identifier kLoadedPresetAuthorId("loadedPresetAuthor");
inline const juce::Identifier kLoadedPresetPathId("loadedPresetPath");
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

using px3::kFxStageCount;
using px3::FxOrder;
using px3::kDefaultFxOrder;

inline const std::array<juce::String, kFxStageCount> kFxModuleIds { juce::String("harmonicDrive"),
                                                                     juce::String("delay"),
                                                                     juce::String("reverb"),
                                                                     juce::String("mood"),
                                                                     juce::String("doom"),
                                                                     juce::String("lucy"),
                                                                     juce::String("chorus"),
                                                                     juce::String("stereoSpread") };


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

inline juce::String formatOrderString(const FxOrder& order)
{
    juce::StringArray items;
    for (const auto stage : order)
    {
        items.add(moduleIdForStage(stage));
    }
    return items.joinIntoString(",");
}

// Recovers a valid permutation from anything: a malformed saved order, a
// duplicate, a stage id from a build that had fewer effects. Every stage
// appears exactly once, and stages the input never mentioned are appended in
// their default order rather than dropped - a dropped stage is an effect that
// silently stops processing.
inline FxOrder sanitizeFxOrder(const FxOrder& order)
{
    FxOrder sanitized {};
    std::array<bool, kFxStageCount> seen {};

    int write = 0;
    for (const auto stageIn : order)
    {
        const auto stage = juce::jlimit(0, kFxStageCount - 1, stageIn);
        if (! seen[static_cast<std::size_t>(stage)])
        {
            sanitized[static_cast<std::size_t>(write++)] = stage;
            seen[static_cast<std::size_t>(stage)] = true;
        }
    }

    for (const auto stage : kDefaultFxOrder)
    {
        if (! seen[static_cast<std::size_t>(stage)] && write < kFxStageCount)
        {
            sanitized[static_cast<std::size_t>(write++)] = stage;
            seen[static_cast<std::size_t>(stage)] = true;
        }
    }

    return sanitized;
}

inline uint32_t packFxOrder(const FxOrder& order)
{
    // Three bits per stage. Widening past eight stages needs a wider word, so
    // the assert is here rather than in a comment.
    static_assert(kFxStageCount <= 10, "packFxOrder holds ten 3-bit stages at most");

    uint32_t packed = 0u;
    for (int i = 0; i < kFxStageCount; ++i)
    {
        packed |= (static_cast<uint32_t>(order[static_cast<std::size_t>(i)]) & 0x7u) << (i * 3);
    }
    return packed;
}

inline FxOrder unpackFxOrder(uint32_t packed)
{
    FxOrder order {};
    for (int i = 0; i < kFxStageCount; ++i)
    {
        order[static_cast<std::size_t>(i)] = static_cast<int>((packed >> (i * 3)) & 0x7u);
    }
    return order;
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
