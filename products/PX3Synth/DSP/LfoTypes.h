#pragma once

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
};
