#pragma once

#include <JuceHeader.h>

namespace px3
{
enum class OscillatorMode : int
{
    sine = 0,
    saw,
    square,
    triangle,
    noise,
    pinkNoise,
    superSaw,
    pwm,
    wavetable,
    additive,
    formant,
    fm,
    hardSync,
    karplus,
    organ,
    digital,
    physical,
    rob,
    isaac,
    px3
};

inline constexpr int oscillatorModeMinIndex = static_cast<int>(OscillatorMode::sine);
inline constexpr int oscillatorModeMaxIndex = static_cast<int>(OscillatorMode::px3);
inline constexpr int oscillatorModeCount = oscillatorModeMaxIndex - oscillatorModeMinIndex + 1;

inline constexpr int oscillatorModeToIndex(OscillatorMode mode)
{
    return static_cast<int>(mode);
}

inline constexpr int clampOscillatorModeIndex(int index)
{
    return index < oscillatorModeMinIndex ? oscillatorModeMinIndex
                                          : (index > oscillatorModeMaxIndex ? oscillatorModeMaxIndex : index);
}

inline juce::StringArray oscillatorModeChoices()
{
    return juce::StringArray {
        "SINE",
        "SAW",
        "SQUARE",
        "TRIANGLE",
        "NOISE",
        "PINK NOISE",
        "SUPER SAW",
        "PWM",
        "WAVETABLE",
        "ADDITIVE",
        "FORMANT",
        "FM",
        "HARD SYNC",
        "KARPLUS",
        "ORGAN",
        "DIGITAL",
        "PHYSICAL",
        "ROB",
        "ISAAC",
        "PX3"
    };
}
}