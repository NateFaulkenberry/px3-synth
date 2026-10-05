#pragma once

// The reverb's controls, as the processors hand them over once per block.
// Every continuous control is NORMALISED 0..1; what a position means is up to
// each type (ReverbMapping in Reverb.cpp), because a medium room and a medium
// hall are not the same number of seconds. The UI shows the mapped value.
struct ReverbSettings
{
    float amount { 0.0f };          // MIX: equal-power dry/wet, 1 = fully wet
    bool enabled { true };
    int algorithmIndex { 0 };       // ROOM, PLATE, HALL, CLOUD, SPRING, GATED

    float preDelay { 0.0f };        // 0..250 ms, skewed toward the short end
    float decay { 0.45f };          // per-type seconds (GATED: length)
    float size { 0.5f };            // per-type space size
    float damping { 0.35f };        // 0 = flat, 1 = dark (HF decays far faster)
    float low { 0.5f };             // LF decay multiplier, x0.5 .. x2, centre x1
    float diffusion { 0.7f };       // smear: grainy .. smooth
    float modulation { 0.35f };     // depth + rate macro, per type
    float width { 1.0f };           // 0 = mono wet, 1 = the algorithm's own stereo

    // Type-specific. Each is read only by the type(s) named.
    float early { 0.5f };           // ROOM, HALL: early reflections against the late field
    float shimmer { 0.0f };         // CLOUD: +12 st regeneration inside the loop
    float drip { 0.5f };            // SPRING: chirp dispersion ("drip")
    float shape { 0.5f };           // GATED: 0 reverse, 0.5 flat gate, 1 falling
};
