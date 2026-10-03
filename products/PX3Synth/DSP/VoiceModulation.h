#pragma once

#include "ModulationGraph.h"

#include <array>
#include <cmath>

// Per-voice modulation.
//
// Envelopes are per-voice sources: each note has its own. Averaging them across
// voices and applying one value to every voice - what the global graph does -
// smears one note's contour over the whole chord. For destinations that live
// inside a voice, the processor therefore evaluates everything EXCEPT the
// envelope routes globally, and hands each voice a small fixed-size plan: the
// unfolded global value, the destination's range, and the envelope routes.
// The voice adds its own envelopes' contribution with the same arithmetic the
// graph uses (routeContribution), folds, and maps to the parameter's range.
//
// Allocation-free and fixed-size: it is copied to every voice once per block.
namespace px3::synth
{
enum class VoiceModTarget : int
{
    filter1Cutoff, filter2Cutoff, filter1Resonance, filter2Resonance,
    osc1Fine, osc2Fine, osc3Fine,
    osc1PitchMod, osc2PitchMod, osc3PitchMod,
    osc1MacroA, osc2MacroA, osc3MacroA,
    osc1MacroB, osc2MacroB, osc3MacroB,
    osc1MacroC, osc2MacroC, osc3MacroC,
    osc1WtPosition, osc2WtPosition, osc3WtPosition,
    count
};

inline constexpr int kVoiceModTargetCount = static_cast<int>(VoiceModTarget::count);
inline constexpr int kVoiceModRoutesPerTarget = 8;

struct VoiceModRoute
{
    int envelope { 0 };
    float depth { 0.0f };
    bool sourceBipolar { false };
    ModulationPolarity polarity { ModulationPolarity::native };
    ModulationCurve curve { ModulationCurve::linear };
};

struct VoiceModDestination
{
    float base { 0.0f };       // the parameter's own normalised value (for unipolar swing)
    float unfolded { 0.0f };   // base + every global contribution, before folding
    float start { 0.0f }, end { 1.0f }, skew { 1.0f };
    int routeCount { 0 };
    std::array<VoiceModRoute, kVoiceModRoutesPerTarget> routes {};
};

struct VoiceModulationPlan
{
    bool active { false };
    std::array<VoiceModDestination, kVoiceModTargetCount> destinations {};
};

// Reflect back into 0..1, matching the processor's foldIntoUnitRange.
inline float foldUnit(float value) noexcept
{
    if (! std::isfinite(value)) { return 0.0f; }
    auto wrapped = std::fmod(value, 2.0f);
    if (wrapped < 0.0f) { wrapped += 2.0f; }
    return wrapped <= 1.0f ? wrapped : 2.0f - wrapped;
}

// The parameter's convertFrom0to1 for a plain (start, end, skew) range.
inline float rangeFrom0to1(const VoiceModDestination& d, float proportion) noexcept
{
    auto x = std::clamp(proportion, 0.0f, 1.0f);
    if (d.skew != 1.0f && x > 0.0f) { x = std::exp(std::log(x) / d.skew); }
    return d.start + (d.end - d.start) * x;
}
}
