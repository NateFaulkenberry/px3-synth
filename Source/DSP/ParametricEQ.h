#pragma once

#include "BusInsertTypes.h"

#include <JuceHeader.h>

#include <array>

namespace px3
{

// A four-band parametric EQ for bus duty.
//
// RBJ cookbook biquads, one per band, transposed direct form II. The cookbook
// forms are used AS PUBLISHED rather than adjusted toward constant-Q, and that
// is the important decision: RBJ defines a peaking filter's bandwidth at the
// HALF-GAIN point rather than at -3 dB, which gives proportional-Q behaviour -
// a gentle boost is broad, and it tightens as the gain rises.
//
// That is what makes classic console EQ musical, and it is the right behaviour
// for a bus. A constant-Q filter keeps its width in hertz whatever the gain, so
// a 2 dB move is as narrow as a 12 dB one; that is what corrective EQ wants and
// what bus EQ does not.
//
// Parameters are smoothed and the coefficients are rebuilt FROM the smoothed
// values. Interpolating between two sets of biquad coefficients can pass
// through unstable intermediate states; rebuilding from smoothed parameters
// makes every intermediate state a valid filter by construction. This is the
// same reasoning that made VoiceFilter rebuild from a smoothed cutoff.
//
// See docs/V3_1_EQ_COMP_RESEARCH.md.
class ParametricEQ
{
public:
    void prepare(double sampleRate);
    void reset();

    void setSettings(const EqSettings& settings);
    void processSample(float& left, float& right);

    // The response the EQ is actually applying, for the display. Built from the
    // same coefficients the audio path runs, so the curve on screen cannot
    // drift from what is heard - the filter card's graph learned that lesson.
    float magnitudeDb(double frequencyHz) const;

    static constexpr float kMinFrequencyHz = 20.0f;
    static constexpr float kMaxFrequencyHz = 20000.0f;
    static constexpr float kMinGainDb = -18.0f;
    static constexpr float kMaxGainDb = 18.0f;
    static constexpr float kMinQ = 0.30f;
    static constexpr float kMaxQ = 8.0f;

private:
    struct Band
    {
        // b0 b1 b2 a1 a2, already normalised by a0.
        std::array<float, 5> coeffs { { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f } };
        // Transposed direct form II state, per channel.
        std::array<float, 2> z1 { { 0.0f, 0.0f } };
        std::array<float, 2> z2 { { 0.0f, 0.0f } };

        // What the coefficients were built from, so a rebuild only happens when
        // something actually moved.
        EqBandType builtType { EqBandType::bell };
        float builtFrequency { -1.0f };
        float builtGainDb { 0.0f };
        float builtQ { -1.0f };
        bool identity { true };
    };

    void updateBand(int index);
    static float processBandSample(Band& band, int channel, float x);

    double sampleRateHz { 48000.0 };
    EqSettings target;
    // The smoothed values the coefficients are built from.
    std::array<EqBandSettings, kEqBandCount> smoothed;
    std::array<Band, kEqBandCount> bands;
    float smoothingCoeff { 0.002f };
    bool primed { false };
    bool anyBandActive { false };
};

} // namespace px3
