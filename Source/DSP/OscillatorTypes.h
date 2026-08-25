#pragma once

#include <array>

/**
 * Oscillator mode and macro controls shared by all active voices.
 *
 * Mode-specific rendering functions interpret macroA/B/C differently, so these
 * are intentionally normalized control lanes rather than mode-specific structs.
 */
struct OscillatorSettings
{
    int modeIndex { 0 };
    float macroA { 0.5f };
    float macroB { 0.5f };
    float macroC { 0.5f };
    int vowelIndex { 0 };
    std::array<float, 8> harmonics { { 1.0f, 0.7f, 0.45f, 0.3f, 0.2f, 0.14f, 0.1f, 0.07f } };
};

struct OscillatorLayerSettings
{
    bool enabled { true };
    float level { 1.0f };
    float coarseSemitones { 0.0f };
    float fineCents { 0.0f };
    OscillatorSettings oscillator;
};
