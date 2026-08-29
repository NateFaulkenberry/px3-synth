#pragma once

#include <JuceHeader.h>

#include "FilterMode.h"

#include <array>

namespace px3
{

// The filter's frequency response, derived from the SAME coefficients the audio
// path runs. The graph under the filter card used to be drawn from invented
// shapes - pow(t, 1.4) for the 12 dB modes, pow(t, 2.3) for the 24 dB ones, and
// a gaussian bump stuck on top for resonance - which meant it could not be
// wrong about the filter, because it was never describing it in the first
// place. Notch and all-pass drew a flat line.
//
// Both the audio path and the display build their coefficients here, so the
// curve on screen is the response you hear, and a test can hold the drawn curve
// against the measured one.

struct FilterBiquadPair
{
    // JUCE coefficient order: b0, b1, b2, a0, a1, a2.
    std::array<float, 6> stageA { { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f } };
    std::array<float, 6> stageB { { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f } };
    bool usesStageB { false };
};

// A 4-pole filter is two biquads in series, and building both at the user's Q
// multiplies their peaks - at Q 10 the 24 dB modes resonated at +40 dB where
// the 12 dB modes reached +20. The pair is split the way a 4th-order design is
// meant to be: one section holds the slope flat, the other carries the
// resonance. At the default Q the two land on the Butterworth pair, which is
// maximally flat.
inline constexpr float kFourPoleFlatQ = 0.5412f;
inline constexpr float kFourPoleResonantQ = 1.3065f;
inline constexpr float kButterworthQ = 0.7071f;

inline float fourPoleResonantQ(float userQ)
{
    return kFourPoleResonantQ * juce::jmax(0.05f, userQ) / kButterworthQ;
}

inline FilterBiquadPair makeFilterCoefficients(int modeIndex,
                                               double sampleRate,
                                               float cutoffHz,
                                               float resonanceQ)
{
    FilterBiquadPair pair;

    if (sampleRate <= 0.0)
    {
        return pair;
    }

    // Clamped below Nyquist as well as to the parameter range: JUCE's
    // coefficient builders require frequency <= sampleRate / 2, and a high
    // cutoff at a low sample rate would otherwise cross it.
    const auto maxCutoff = static_cast<float>(sampleRate * 0.5) * 0.98f;
    const auto cutoff = juce::jlimit(20.0f, juce::jmax(20.0f, maxCutoff), cutoffHz);
    const auto q = juce::jlimit(0.20f, 10.0f, resonanceQ);

    using Array = juce::dsp::IIR::ArrayCoefficients<float>;

    switch (static_cast<FilterMode>(clampFilterModeIndex(modeIndex)))
    {
        case FilterMode::lp12:
            pair.stageA = Array::makeLowPass(sampleRate, cutoff, q);
            break;
        case FilterMode::lp24:
            pair.stageA = Array::makeLowPass(sampleRate, cutoff, kFourPoleFlatQ);
            pair.stageB = Array::makeLowPass(sampleRate, cutoff, fourPoleResonantQ(q));
            pair.usesStageB = true;
            break;
        case FilterMode::hp12:
            pair.stageA = Array::makeHighPass(sampleRate, cutoff, q);
            break;
        case FilterMode::hp24:
            pair.stageA = Array::makeHighPass(sampleRate, cutoff, kFourPoleFlatQ);
            pair.stageB = Array::makeHighPass(sampleRate, cutoff, fourPoleResonantQ(q));
            pair.usesStageB = true;
            break;
        case FilterMode::bp:
            pair.stageA = Array::makeBandPass(sampleRate, cutoff, q);
            break;
        case FilterMode::notch:
            pair.stageA = Array::makeNotch(sampleRate, cutoff, q);
            break;
        case FilterMode::allPass:
            pair.stageA = Array::makeAllPass(sampleRate, cutoff, q);
            break;
        case FilterMode::comb:
        default:
            break;
    }

    return pair;
}

// Magnitude at one frequency, in decibels. JUCE's own coefficient object does
// the arithmetic, so this cannot drift from what the biquads actually do.
inline float filterMagnitudeDb(const FilterBiquadPair& pair, double frequencyHz, double sampleRate)
{
    if (sampleRate <= 0.0)
    {
        return 0.0f;
    }

    const auto hz = juce::jlimit(1.0, sampleRate * 0.5 - 1.0, frequencyHz);

    juce::dsp::IIR::Coefficients<float> a { pair.stageA[0], pair.stageA[1], pair.stageA[2],
                                            pair.stageA[3], pair.stageA[4], pair.stageA[5] };
    auto magnitude = a.getMagnitudeForFrequency(hz, sampleRate);

    if (pair.usesStageB)
    {
        juce::dsp::IIR::Coefficients<float> b { pair.stageB[0], pair.stageB[1], pair.stageB[2],
                                                pair.stageB[3], pair.stageB[4], pair.stageB[5] };
        magnitude *= b.getMagnitudeForFrequency(hz, sampleRate);
    }

    return juce::Decibels::gainToDecibels(static_cast<float>(magnitude), -96.0f);
}

} // namespace px3
