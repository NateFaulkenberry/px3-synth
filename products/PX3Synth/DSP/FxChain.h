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
inline constexpr int kFxStageCount = 10;

enum FxStage
{
    fxStageVibe = 0,
    fxStageDelay = 1,
    fxStageReverb = 2,
    fxStageMood = 3,
    fxStageDoom = 4,
    fxStageLucy = 5,
    fxStageChorus = 6,
    fxStageStereoSpread = 7,
    fxStageDistortion = 8,
    // ANALOG runs inside the voices, before the sources are summed, so its
    // "stage" in the send chain is a no-op and it is not reorderable - the
    // same arrangement as STEREO SPREAD, which runs on the master. It has a
    // stage id so the FX page can address its card like any other.
    fxStageAnalog = 9
};

// One name for the chain order, so widening it is a change to kFxStageCount
// rather than a hunt through every std::array<int, N> in the project.
using FxOrder = std::array<int, kFxStageCount>;

// Where a stage runs. Only the SEND stages are in the reorderable chain; the
// others keep a stage id (MODULE_ORDER and the FX page address cards by it, and
// the order stays a permutation of every id) but their slot in the order does
// nothing.
//
//   ANALOG  inside the voices, before the sources are summed;
//   LUCY    a master insert on the dry + FX sum. In the send chain its output
//           was a (stage - send) difference: the residual dry stayed at full
//           level under it, engaging it cost ~5 dB, and its own output sat
//           5 dB below a dry signal no LUCY control could touch;
//   SPREAD  the master bus after that, sizing the finished picture.
inline constexpr bool isUpstreamFxStage(int stage) noexcept { return stage == fxStageAnalog; }
inline constexpr bool isMasterFxStage(int stage) noexcept
{
    return stage == fxStageLucy || stage == fxStageStereoSpread;
}
inline constexpr bool isSendChainFxStage(int stage) noexcept
{
    return ! isUpstreamFxStage(stage) && ! isMasterFxStage(stage);
}
// The master stages in the order they process, whatever their slot in a saved
// order says: LUCY degrades the mix, SPREAD then sizes what LUCY produced.
inline constexpr std::array<int, 2> kMasterFxStageOrder { { fxStageLucy, fxStageStereoSpread } };

// ANALOG is upstream of everything (it is inside the voices). VIBE colours the
// source, CHORUS widens it, DOOM mangles it, the time-based effects follow,
// REVERB builds the room; then the master stages: LUCY on the whole mix, and
// STEREO SPREAD sizing the whole finished picture - which is why it is last
// rather than early: a widener placed before a reverb only widens what the
// reverb then re-images.
inline constexpr FxOrder kDefaultFxOrder { { fxStageAnalog,
                                             fxStageVibe,
                                             fxStageDistortion,
                                             fxStageChorus,
                                             fxStageDoom,
                                             fxStageDelay,
                                             fxStageMood,
                                             fxStageReverb,
                                             fxStageLucy,
                                             fxStageStereoSpread } };

} // namespace px3
