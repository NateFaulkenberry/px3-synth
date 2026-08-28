#pragma once

#include <JuceHeader.h>

namespace px3
{
enum class FilterMode : int
{
    lp12 = 0,
    lp24,
    hp12,
    hp24,
    bp,
    notch,
    allPass,
    comb
};

inline constexpr int filterModeMinIndex = static_cast<int>(FilterMode::lp12);
inline constexpr int filterModeMaxIndex = static_cast<int>(FilterMode::comb);
inline constexpr int filterModeCount = filterModeMaxIndex - filterModeMinIndex + 1;

// The comb is a tuned resonator rather than a biquad response, so callers that
// care which kind of filter they are dealing with ask here rather than
// comparing indices.
inline constexpr bool isCombMode(int index)
{
    return index == static_cast<int>(FilterMode::comb);
}

inline constexpr int clampFilterModeIndex(int index)
{
    return index < filterModeMinIndex ? filterModeMinIndex
                                      : (index > filterModeMaxIndex ? filterModeMaxIndex : index);
}

inline juce::StringArray filterModeChoices()
{
    return juce::StringArray { "LP12", "LP24", "HP12", "HP24", "BandPass", "Notch", "AllPass", "Comb" };
}
}
