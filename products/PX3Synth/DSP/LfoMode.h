#pragma once

#include <JuceHeader.h>

namespace px3
{
enum class LfoWaveform : int
{
    sine = 0,
    triangle,
    saw,
    square,
    // One-shot ramps. APPENDED rather than inserted, so every existing index
    // keeps its meaning - a stored index is what presets remember.
    rampUp,
    rampDown
};

inline constexpr int lfoWaveformMinIndex = static_cast<int>(LfoWaveform::sine);
inline constexpr int lfoWaveformMaxIndex = static_cast<int>(LfoWaveform::rampDown);
inline constexpr int lfoWaveformCount = lfoWaveformMaxIndex - lfoWaveformMinIndex + 1;

// A ramp's travel time. Long enough for an evolving pad; short enough at the
// bottom that it still reads as a sweep rather than a step.
inline constexpr float lfoMinRampSeconds = 0.05f;
inline constexpr float lfoMaxRampSeconds = 60.0f;

inline constexpr int lfoWaveformToIndex(LfoWaveform waveform)
{
    return static_cast<int>(waveform);
}

inline constexpr int clampLfoWaveformIndex(int index)
{
    return index < lfoWaveformMinIndex ? lfoWaveformMinIndex
                                       : (index > lfoWaveformMaxIndex ? lfoWaveformMaxIndex : index);
}

// A ramp is TIMED, not cyclic: it runs once from its start value to its end
// value over a duration and then holds. It is therefore driven by elapsed audio
// time rather than by the phase every other shape shares.
inline constexpr bool isRampLfoWaveformIndex(int index)
{
    return index == lfoWaveformToIndex(LfoWaveform::rampUp)
        || index == lfoWaveformToIndex(LfoWaveform::rampDown);
}

inline juce::StringArray lfoWaveformChoices()
{
    return juce::StringArray {
        "SINE",
        "TRIANGLE",
        "SAW",
        "SQUARE",
        "RAMP UP",
        "RAMP DOWN"
    };
}
}
