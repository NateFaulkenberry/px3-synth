#pragma once

// What each normalised reverb control means, per type. One place, used by the
// DSP (Reverb.cpp), by the parameters' value text and by the presets, so the
// number a knob shows is the number the algorithm uses.

#include <algorithm>
#include <cmath>

namespace px3::reverb
{
enum Type : int { room = 0, plate, hall, cloud, spring, gated };
inline constexpr int kTypeCount = 6;
inline constexpr const char* kTypeNames[kTypeCount] = { "ROOM", "PLATE", "HALL", "CLOUD", "SPRING", "GATED" };

inline int clampType(int type) noexcept { return std::clamp(type, 0, kTypeCount - 1); }

inline float logMap(float t, float lo, float hi) noexcept
{
    return lo * std::pow(hi / lo, std::clamp(t, 0.0f, 1.0f));
}

// DECAY: reverberation time in seconds (GATED: the burst length), log-mapped
// so the knob is equally fine at the short and the long end. Ranges are per
// type: halfway is "medium for this space", which is what makes switching
// type predictable.
struct DecayRange { float lo, hi; };
inline constexpr DecayRange kDecayRange[kTypeCount] = {
    { 0.15f, 3.0f },    // ROOM
    { 0.4f, 8.0f },     // PLATE
    { 0.8f, 16.0f },    // HALL
    { 2.0f, 60.0f },    // CLOUD
    { 0.6f, 6.0f },     // SPRING
    { 0.08f, 0.7f },    // GATED: length of the shaped burst
};
inline float decaySeconds(int type, float d) noexcept
{
    const auto& r = kDecayRange[clampType(type)];
    return logMap(d, r.lo, r.hi);
}
inline float decayNormalised(int type, float seconds) noexcept
{
    const auto& r = kDecayRange[clampType(type)];
    return std::clamp(std::log(std::max(seconds, 1.0e-4f) / r.lo) / std::log(r.hi / r.lo), 0.0f, 1.0f);
}

// SIZE: a scale on every delay of the type (room dimensions, plate area,
// spring length). Linear; the ranges keep each type inside the region where
// its own delay set stays dense and free of flutter.
struct SizeRange { float lo, hi; };
inline constexpr SizeRange kSizeRange[kTypeCount] = {
    { 0.35f, 1.6f },    // ROOM: ~1.8 m .. 8.5 m equivalent shoebox
    { 0.5f, 1.25f },    // PLATE: Dattorro's tank at 0.5x .. 1.25x
    { 0.55f, 1.6f },    // HALL
    { 0.5f, 1.5f },     // CLOUD
    { 0.7f, 1.5f },     // SPRING: transit time 25 .. 63 ms
    { 0.6f, 1.4f },     // GATED: diffuser scale (grain of the burst)
};
inline float sizeScale(int type, float s) noexcept
{
    const auto& r = kSizeRange[clampType(type)];
    return r.lo + (r.hi - r.lo) * std::clamp(s, 0.0f, 1.0f);
}

// PRE-DELAY: 0 .. 250 ms, squared so the first half of the travel covers the
// 0 .. 62 ms that is used most.
inline constexpr float kMaxPreDelayMs = 250.0f;
inline float preDelayMs(float p) noexcept
{
    const auto t = std::clamp(p, 0.0f, 1.0f);
    return kMaxPreDelayMs * t * t;
}
inline float preDelayNormalised(float ms) noexcept { return std::sqrt(std::clamp(ms / kMaxPreDelayMs, 0.0f, 1.0f)); }

// LOW: the low band's decay against the mid band's, x0.5 .. x2, centre x1.
inline float lowMultiplier(float l) noexcept { return std::pow(2.0f, 2.0f * std::clamp(l, 0.0f, 1.0f) - 1.0f); }
inline float lowNormalised(float multiplier) noexcept
{
    return std::clamp((std::log2(std::max(multiplier, 1.0e-3f)) + 1.0f) * 0.5f, 0.0f, 1.0f);
}

// DAMPING: how much faster the top end dies. 0 = the high band decays with the
// mid band; 1 = it decays five times faster, from a corner that has moved down
// from ~10 kHz to ~1.2 kHz (zita-rev1's "frequency where RT halves", folded
// into one knob).
inline float highDecayRatio(float d) noexcept { return std::max(0.08f, 1.0f - 0.8f * std::pow(std::clamp(d, 0.0f, 1.0f), 0.8f)); }
inline float highCornerHz(float d, float cornerAtZero) noexcept { return cornerAtZero * std::pow(0.12f, std::clamp(d, 0.0f, 1.0f)); }

// SHIMMER: how much of the loop is replaced by its own +12 st copy.
inline float shimmerAlpha(float s) noexcept { return 0.55f * std::pow(std::clamp(s, 0.0f, 1.0f), 1.3f); }
} // namespace px3::reverb
