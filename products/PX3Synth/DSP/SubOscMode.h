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


inline constexpr int clampSubOscWaveformIndex(int index)
{
    return index < subOscWaveformMinIndex ? subOscWaveformMinIndex
                                          : (index > subOscWaveformMaxIndex ? subOscWaveformMaxIndex : index);
}

inline juce::StringArray subOscWaveformChoices()
{
    return juce::StringArray { "SINE", "SQUARE" };
}

}
