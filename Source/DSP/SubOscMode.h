#pragma once

#include <JuceHeader.h>

namespace px3
{
enum class SubOscWaveform : int
{
    sine = 0,
    square
};

inline constexpr int subOscWaveformMinIndex = static_cast<int>(SubOscWaveform::sine);
inline constexpr int subOscWaveformMaxIndex = static_cast<int>(SubOscWaveform::square);
inline constexpr int subOscWaveformCount = subOscWaveformMaxIndex - subOscWaveformMinIndex + 1;

inline constexpr int subOscOctaveMinIndex = 0;
inline constexpr int subOscOctaveMaxIndex = 2;
inline constexpr int subOscOctaveCount = subOscOctaveMaxIndex - subOscOctaveMinIndex + 1;

inline constexpr int clampSubOscWaveformIndex(int index)
{
    return index < subOscWaveformMinIndex ? subOscWaveformMinIndex
                                          : (index > subOscWaveformMaxIndex ? subOscWaveformMaxIndex : index);
}

inline constexpr int clampSubOscOctaveIndex(int index)
{
    return index < subOscOctaveMinIndex ? subOscOctaveMinIndex
                                        : (index > subOscOctaveMaxIndex ? subOscOctaveMaxIndex : index);
}

inline constexpr int subOscSemitoneOffsetForOctaveIndex(int index)
{
    switch (clampSubOscOctaveIndex(index))
    {
        case 0:  return 0;
        case 1:  return -12;
        case 2:  return -24;
        default: return -12;
    }
}

inline juce::StringArray subOscWaveformChoices()
{
    return juce::StringArray { "SINE", "SQUARE" };
}

inline juce::StringArray subOscOctaveChoices()
{
    return juce::StringArray { "0 OCT", "-1 OCT", "-2 OCT" };
}
}
