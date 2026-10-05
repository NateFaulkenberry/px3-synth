#pragma once

// The reverb's parameters, declared once for the Synth and PX3 Reverb: ids,
// names, defaults, the per-type value text, and which controls each type
// actually reads ("live"). The UI dims a control its type does not read; the
// tests pin the same table, so an inert knob cannot quietly reappear.

#include <JuceHeader.h>

#include "ReverbMapping.h"
#include "ReverbTypes.h"

#include <functional>

namespace px3::reverb
{
enum class Control : int
{
    preDelay = 0, decay, size, damping, low, diffusion, modulation, width, early, shimmer, drip, shape
};
inline constexpr int kControlCount = 12;

struct ParameterSpec
{
    Control control;
    const char* id;
    const char* name;
    float defaultValue;
};

// MIX (fx.reverb.amount), the bypass and the type are declared by each
// product next to the other effects' amount and enable parameters.
inline constexpr ParameterSpec kParameterSpecs[kControlCount] = {
    { Control::preDelay, "fx.reverb.pre.delay", "Reverb PreDelay", 0.2f },        // 10 ms
    { Control::decay, "fx.reverb.decay", "Reverb Decay", 0.45f },
    { Control::size, "fx.reverb.size", "Reverb Size", 0.5f },
    { Control::damping, "fx.reverb.damping", "Reverb Damping", 0.35f },
    { Control::low, "fx.reverb.low", "Reverb Low Decay", 0.5f },                  // x1
    { Control::diffusion, "fx.reverb.diffusion", "Reverb Diffusion", 0.7f },
    { Control::modulation, "fx.reverb.mod", "Reverb Modulation", 0.35f },
    { Control::width, "fx.reverb.width", "Reverb Width", 1.0f },
    { Control::early, "fx.reverb.early", "Reverb Early", 0.5f },
    { Control::shimmer, "fx.reverb.shimmer", "Reverb Shimmer", 0.0f },
    { Control::drip, "fx.reverb.spring.drip", "Reverb Spring Drip", 0.5f },
    { Control::shape, "fx.reverb.gate.shape", "Reverb Gate Shape", 0.5f },
};

inline const ParameterSpec& spec(Control c) noexcept { return kParameterSpecs[static_cast<int>(c)]; }

inline juce::StringArray typeChoices()
{
    juce::StringArray names;
    for (const auto* n : kTypeNames) { names.add(n); }
    return names;
}

// Whether `type` reads `control`. Everything else is inert by design.
inline bool isLive(Control control, int type) noexcept
{
    switch (control)
    {
        case Control::early:      return type == room || type == hall;
        case Control::shimmer:    return type == cloud;
        case Control::drip:       return type == spring;
        case Control::shape:      return type == gated;
        case Control::modulation: return type != gated;
        case Control::low:        return type != gated && type != spring;
        default:                  return true;
    }
}

inline juce::String secondsText(float seconds)
{
    if (seconds < 1.0f) { return juce::String(juce::roundToInt(seconds * 1000.0f)) + " ms"; }
    return juce::String(seconds, seconds < 10.0f ? 2 : 1) + " s";
}

inline juce::String percentText(float v) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; }

inline juce::String valueText(Control control, int type, float v)
{
    type = clampType(type);
    switch (control)
    {
        case Control::preDelay:
        {
            const auto ms = preDelayMs(v);
            // String(x, 0) is full precision in JUCE, not "no decimals".
            return (ms < 10.0f ? juce::String(ms, 1) : juce::String(juce::roundToInt(ms))) + " ms";
        }
        case Control::decay:    return secondsText(decaySeconds(type, v));
        case Control::size:
        {
            const auto s = sizeScale(type, v);
            if (type == room) { return juce::String(5.3f * s, 1) + " m"; }
            if (type == hall) { return juce::String(juce::roundToInt(5.3f * 2.2f * s)) + " m"; }
            if (type == spring) { return juce::String(juce::roundToInt(39.0f * s)) + " ms"; }
            return "x" + juce::String(s, 2);
        }
        case Control::low:      return "x" + juce::String(lowMultiplier(v), 2);
        case Control::shape:
            if (v < 0.45f) { return "REVERSE " + percentText(1.0f - 2.0f * v); }
            if (v > 0.55f) { return "FALL " + percentText(2.0f * v - 1.0f); }
            return "GATE";
        default:                return percentText(v);
    }
}

// Value text that follows the current type. `currentType` is read on the
// message thread whenever a host or the UI asks for the text.
inline juce::AudioParameterFloatAttributes attributesFor(Control control, std::function<int()> currentType)
{
    return juce::AudioParameterFloatAttributes().withStringFromValueFunction(
        [control, currentType](float value, int length)
        {
            const auto text = valueText(control, currentType != nullptr ? currentType() : 0, value);
            return length > 0 ? text.substring(0, length) : text;
        });
}

inline float& settingsField(ReverbSettings& s, Control c) noexcept
{
    switch (c)
    {
        case Control::preDelay:   return s.preDelay;
        case Control::decay:      return s.decay;
        case Control::size:       return s.size;
        case Control::damping:    return s.damping;
        case Control::low:        return s.low;
        case Control::diffusion:  return s.diffusion;
        case Control::modulation: return s.modulation;
        case Control::width:      return s.width;
        case Control::early:      return s.early;
        case Control::shimmer:    return s.shimmer;
        case Control::drip:       return s.drip;
        default:                  return s.shape;
    }
}
} // namespace px3::reverb
