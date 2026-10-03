#pragma once

#include <array>

#include "CombResonator.h"

inline constexpr int kFilterInstanceCount = 2;

struct FilterSettings
{
    bool enabled { true };
    float cutoffHz { 10000.0f };
    float resonanceQ { 0.8f };
    int modeIndex { 0 };
    // Keyboard tracking, applied per voice: cutoff * 2^((note - reference) / 12
    // * keyTrack). 1 follows the keyboard exactly, 0 ignores it, negative
    // closes the filter as you play higher.
    float keyTrack { 0.0f };
    float keyTrackReference { 60.0f };
    // Only read in comb mode. Carried in the same struct as the biquad's
    // settings because the whole per-block path - modulation, smoothing,
    // delivery to the voice - already exists for that struct, and a parallel
    // one would have to duplicate all of it.
    px3::CombSettings comb {};
};
