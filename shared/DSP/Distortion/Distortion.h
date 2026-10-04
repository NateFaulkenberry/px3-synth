#pragma once

#include <JuceHeader.h>

#include "DistortionTypes.h"
#include "PolyphaseOversampler.h"

#include <array>

namespace px3
{
// DRIVE: an overdrive/distortion stage in the spirit of classic pedals - a
// mid-emphasis pre-filter (the Tube Screamer's tight low end), a gain stage,
// a choice of clipper, a tone control and level matching. Not a circuit model.
//
// Clippers, each anti-aliased with first-order antiderivative anti-aliasing
// (ADAA), so heavy drive does not fold harmonics back as inharmonic hash:
//   SOFT - tanh, the op-amp-in-the-feedback-loop curve: smooth, compressed.
//   HARD - a hard clip, diodes to ground: buzzier, more upper harmonics.
//   ASYM - a biased tanh, unmatched diodes: even harmonics, a warmer bite.
// Level matching follows the measured loudness in and out, so DRIVE changes
// character rather than volume; LEVEL trims around that.
class Distortion
{
public:
    void prepare(double sampleRate);
    void reset();
    void updateForBlock(const DistortionSettings& settings);
    void processSampleFrame(float inL, float inR, float& outL, float& outR);

    static float shape(int type, float x) noexcept;
    static float antiderivative(int type, float x) noexcept;
    static double antiderivativeDouble(int type, double x) noexcept;

    // Oversampling factor in use (8x at 44.1/48 kHz, 4x at 96, 2x above).
    int oversamplingFactor() const noexcept { return wetOversampler.factor(); }

private:
    // The clipper, antialiased twice over: it runs at the oversampled rate,
    // and with first-order ADAA there. Either alone was not enough - a saw
    // into HARD at full drive aliased at -37 dB with ADAA alone and -42 dB
    // with 8x alone; together the worst case measured is -66 dB.
    double clip(int channel, double x) noexcept;

    double sampleRateHz { 48000.0 };
    DistortionSettings current;
    juce::SmoothedValue<float> mixSmoothed, driveSmoothed, levelSmoothed;
    float tightCoeff { 0.0f }, toneCoeff { 0.0f }, followCoeff { 0.0f };
    std::array<float, 2> tightState { { 0.0f, 0.0f } };
    std::array<float, 2> toneState { { 0.0f, 0.0f } };
    std::array<double, 2> previousInput { { 0.0, 0.0 } };   // at the oversampled rate
    // F(previousInput), kept from the last step: every step needs F at both
    // ends and one end is always the last step's. Valid for `cachedType` only.
    std::array<double, 2> previousAntiderivative { { 0.0, 0.0 } };
    int cachedType { -1 };
    // The clipper's path, and the dry signal through the same filters with no
    // clipper: the MIX blends two signals with the same phase, or it combs.
    px3::dsp::PolyphaseOversampler wetOversampler, dryOversampler;
    // Engaging and bypassing crossfade between the raw input and the stage's
    // (slightly delayed) output, so neither is a jump in time.
    juce::SmoothedValue<float> engageSmoothed;
    std::array<float, 2> dcX1 { { 0.0f, 0.0f } }, dcY1 { { 0.0f, 0.0f } };
    float dcPole { 0.9995f };   // ~3.8 Hz at any rate (0.9995 was per sample at 48 kHz)
    float inPower { 0.0f }, outPower { 0.0f };
    bool idle { true };
};
} // namespace px3
