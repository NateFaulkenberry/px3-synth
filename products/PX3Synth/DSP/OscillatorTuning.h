#pragma once

#include <JuceHeader.h>

#include <cmath>

// ONE tuning model for every oscillator - the three main oscillators and the
// sub use exactly this, so the same settings mean the same pitch on each.
//
// The order of operations, written down here and nowhere else:
//
//   1. base note pitch      the MIDI note, in the voice
//   2. static tuning        Coarse Tune in whole octaves, Fine Tune in cents
//   3. dynamic modulation   Pitch Mod - what modulation adds, never a stored
//                           value - then pitch bend, mod-wheel vibrato and
//                           VIBE drift, applied by the voice to the note
//
// Each stage is a pitch offset, so they multiply; the order is recorded because
// it is what a reader needs to find each one, and because static tuning is the
// only place a patch's intended pitch lives.
namespace px3::tuning
{

inline constexpr float kCoarseMinOctaves = -2.0f;
inline constexpr float kCoarseMaxOctaves = 2.0f;
inline constexpr float kFineMinCents = -24.0f;
inline constexpr float kFineMaxCents = 24.0f;
inline constexpr float kPitchModRangeSemitones = 24.0f;

// Static tuning in semitones. Coarse is rounded to a whole octave even when
// modulation has moved it between two, so modulating COARSE steps in octaves
// rather than gliding - it is a stepped control however it is driven.
inline double staticSemitones(float coarseOctaves, float fineCents)
{
    const auto octaves = std::round(juce::jlimit(kCoarseMinOctaves, kCoarseMaxOctaves, coarseOctaves));
    const auto cents = juce::jlimit(kFineMinCents, kFineMaxCents, fineCents);
    return 12.0 * static_cast<double>(octaves) + static_cast<double>(cents) / 100.0;
}

// Static tuning plus the oscillator's own dynamic modulation.
inline double totalSemitones(float coarseOctaves, float fineCents, float pitchModSemitones)
{
    return staticSemitones(coarseOctaves, fineCents)
           + static_cast<double>(juce::jlimit(-kPitchModRangeSemitones, kPitchModRangeSemitones, pitchModSemitones));
}

inline double pitchRatio(float coarseOctaves, float fineCents, float pitchModSemitones)
{
    return std::pow(2.0, totalSemitones(coarseOctaves, fineCents, pitchModSemitones) / 12.0);
}

inline juce::String formatCoarse(double octaves)
{
    const auto whole = juce::roundToInt(octaves);
    return (whole > 0 ? "+" : "") + juce::String(whole) + " oct";
}

inline juce::String formatFine(double cents)
{
    const auto whole = juce::roundToInt(cents);
    return (whole > 0 ? "+" : "") + juce::String(whole) + " ct";
}

} // namespace px3::tuning
