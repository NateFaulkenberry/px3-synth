#pragma once

#include <JuceHeader.h>

#include <array>

namespace px3
{

// The FX chain's shape, in one place, because the processor, the editor, the
// FX panel and the state serialiser all have to agree on it.
//
// Stage ids are permanent - MODULE_ORDER stores them by name, and the FX panel
// addresses its cards by id. Append new effects to the end; never renumber an
// existing one, or every saved session loads with its chain shuffled.
inline constexpr int kFxStageCount = 8;

enum FxStage
{
    fxStageVibe = 0,
    fxStageDelay = 1,
    fxStageReverb = 2,
    fxStageMood = 3,
    fxStageDoom = 4,
    fxStageLucy = 5,
    fxStageChorus = 6,
    fxStageStereoSpread = 7
};

// One name for the chain order, so widening it is a change to kFxStageCount
// rather than a hunt through every std::array<int, N> in the project.
using FxOrder = std::array<int, kFxStageCount>;

// VIBE colours the source, CHORUS widens it, DOOM and LUCY mangle it, the
// time-based effects follow, REVERB builds the room, and STEREO SPREAD sizes
// the whole finished picture - which is why it is last rather than early: a
// widener placed before a reverb only widens what the reverb then re-images.
inline constexpr FxOrder kDefaultFxOrder { { fxStageVibe,
                                             fxStageChorus,
                                             fxStageDoom,
                                             fxStageLucy,
                                             fxStageDelay,
                                             fxStageMood,
                                             fxStageReverb,
                                             fxStageStereoSpread } };

} // namespace px3
