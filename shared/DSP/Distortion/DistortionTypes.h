#pragma once

namespace px3
{
// One block's worth of DRIVE controls.
struct DistortionSettings
{
    bool enabled { true };
    float drive { 0.35f };   // 0..1: +0 to +40 dB into the clipper
    int type { 0 };          // 0 SOFT (op-amp), 1 HARD (diodes to ground), 2 ASYM (unmatched diodes)
    float tight { 0.5f };    // 0..1: how much low end is kept out of the clipper (the mid hump)
    float tone { 0.55f };    // 0..1: post-clip low-pass, dark to open
    float level { 0.5f };    // 0..1: -12..+12 dB around the auto level match; 0.5 is unity
    float mix { 0.0f };      // 0..1: 0 = off, the default, so adding it changes no patch
};
} // namespace px3
