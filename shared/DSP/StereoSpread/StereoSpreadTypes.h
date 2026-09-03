#pragma once

// Everything STEREO SPREAD needs for one block.
// See docs/STEREO_SPREAD_DSP_DESIGN.md.
struct StereoSpreadSettings
{
    bool enabled { true };

    // The macro. Zero by default: adding an effect must not change what
    // existing patches sound like.
    float amount { 0.0f };

    int modeIndex { 0 };        // 0=CLASSIC, 1=WIDE, 2=DEEP, 3=MONO SAFE

    float width { 0.6f };
    float depth { 0.4f };       // decorrelation depth
    float center { 0.7f };      // how strongly mid is anchored
    float lowWidth { 0.0f };    // width permitted below the low crossover
    float highWidth { 0.8f };
    // Placed so a synth bass fundamental sits comfortably INSIDE the mono
    // band rather than near its edge - 100 Hz is a bass note, not a mid.
    float lowFreq { 0.55f };    // low crossover, ~250 Hz
    float highFreq { 0.5f };    // high crossover
    float tone { 0.0f };        // tilt on the SIDE signal only
    float mix { 1.0f };
};
