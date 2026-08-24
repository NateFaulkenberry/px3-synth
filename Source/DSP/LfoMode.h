#pragma once

#include <JuceHeader.h>

namespace px3
{
enum class LfoWaveform : int
{
    sine = 0,
    triangle,
    saw,
    square
};

inline constexpr int lfoWaveformMinIndex = static_cast<int>(LfoWaveform::sine);
inline constexpr int lfoWaveformMaxIndex = static_cast<int>(LfoWaveform::square);
inline constexpr int lfoWaveformCount = lfoWaveformMaxIndex - lfoWaveformMinIndex + 1;

inline constexpr int lfoWaveformToIndex(LfoWaveform waveform)
{
    return static_cast<int>(waveform);
}

inline constexpr int clampLfoWaveformIndex(int index)
{
    return index < lfoWaveformMinIndex ? lfoWaveformMinIndex
                                       : (index > lfoWaveformMaxIndex ? lfoWaveformMaxIndex : index);
}

inline juce::StringArray lfoWaveformChoices()
{
    return juce::StringArray {
        "SINE",
        "TRIANGLE",
        "SAW",
        "SQUARE"
    };
}
}
