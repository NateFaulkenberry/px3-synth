#pragma once

#include <array>
#include <cmath>

enum class LfoClockMode { free, tempo, transport };

inline constexpr std::array<double, 8> lfoClockBeats { 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 1.0 / 3.0 };

struct LfoSettings
{
    bool enabled { true };
    float frequencyHz { 1.0f };
    int waveformIndex { 0 };

    // How long RAMP UP and RAMP DOWN take to travel. Ignored by the cyclic shapes.
    float rampSeconds { 4.0f };

    // Restart on each new note. Off by default, so an LFO nobody has touched keeps
    // running freely, exactly as it did before the control existed.
    bool keySync { false };
    LfoClockMode clockMode { LfoClockMode::free };
    int clockDivision { 4 };
    double tempoBpm { 120.0 };
    double transportPpq { 0.0 };
    double beatsPerBar { 4.0 };
    bool clockAvailable { false };
    bool transportPlaying { false };
    float clockRateScale { 1.0f };
    float clockRampScale { 1.0f };
};
