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

// How many of the three macro knobs a mode shows - and therefore how many it
// may read. The card shows exactly this many and the DSP's level compensation
// averages exactly these, so a knob that is hidden can never change the sound.
inline constexpr int oscillatorModeMacroCounts[oscillatorModeCount] {
    0, 0, 0, 0,   // SINE SAW SQUARE TRIANGLE
    1, 1, 1, 1,   // NOISE PINK NOISE SUPER SAW PWM
    0, 3, 2, 2,   // WAVETABLE ADDITIVE FORMANT FM
    2, 2, 2, 2,   // HARD SYNC ORGAN DIGITAL PHYSICAL
    3, 3, 3       // ROB ISAAC PX3
};

inline constexpr int oscillatorModeMacroCount(int index)
{
    return oscillatorModeMacroCounts[clampOscillatorModeIndex(index)];
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
        "ORGAN",
        "DIGITAL",
        "PHYSICAL",
        "ROB",
        "ISAAC",
        "PX3"
    };
}
}