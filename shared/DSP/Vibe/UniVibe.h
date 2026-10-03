#pragma once

#include <JuceHeader.h>

#include <array>

namespace px3
{
struct UniVibeSettings
{
    float speed { 0.35f };       // 0..1: 0.5 to 10 Hz, exponential
    float intensity { 0.0f };    // 0..1: sweep depth; 0 = off (the default)
    int mode { 0 };              // 0 CHORUS (dry + phased), 1 VIBRATO (phased only)
};

// A Uni-Vibe-inspired phase-shift modulator - the sound of the photocell
// vibe pedal, not a circuit model. Four first-order all-pass stages with
// staggered ranges, swept together by a light-dependent resistor model: an LFO
// drives a lamp that brightens fast and cools slowly, and the LDR's response to
// it is strongly nonlinear. That lopsided, "throbbing" sweep is what separates
// it from an ordinary phaser. In CHORUS the phased signal is mixed with the dry
// one, so the stages make moving notches; in VIBRATO only the phased signal
// passes, so the moving phase is heard as pitch wobble. The right channel's LFO
// runs a quarter cycle ahead for width.
class UniVibe
{
public:
    void prepare(double sampleRate);
    void reset();
    void updateForBlock(const UniVibeSettings& settings);
    void processSampleFrame(float inL, float inR, float& outL, float& outR);

private:
    struct Channel
    {
        float lfoPhase { 0.0f };
        float lamp { 0.0f };
        std::array<float, 4> x1 { { 0.0f, 0.0f, 0.0f, 0.0f } };
        std::array<float, 4> y1 { { 0.0f, 0.0f, 0.0f, 0.0f } };
    };
    float processChannel(Channel& channel, float input, float rateHz, float intensity) noexcept;

    double sampleRateHz { 48000.0 };
    UniVibeSettings current;
    juce::SmoothedValue<float> intensitySmoothed, rateSmoothed, wetMixSmoothed;
    float lampAttack { 0.0f }, lampRelease { 0.0f };
    std::array<Channel, 2> channels;
    bool idle { true };
};
} // namespace px3
