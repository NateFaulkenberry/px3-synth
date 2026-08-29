#pragma once

#include "ChorusTypes.h"

#include <JuceHeader.h>

#include <array>
#include <vector>

namespace px3
{

// CHORUS - a Dimension D-inspired stereo chorus.
//
// The structure is the point. Two delay lines are modulated in ANTI-PHASE and
// summed to the two outputs with opposite polarity:
//
//     L = dry + wetA - wetB
//     R = dry - wetA + wetB
//
// When A goes sharp B goes flat by the same amount, so the pair has no average
// pitch deviation and there is no audible vibrato - while the two copies are
// decorrelated from each other, which is what width is. And L + R = 2*dry
// exactly, so the effect cancels in mono rather than combing.
//
// That is Roland's documented claim - "a new dimension without the apparent
// movement of sound" - as an architecture rather than as a setting. A single
// modulated delay cannot produce it at any depth.
//
// See docs/CHORUS_DSP_DESIGN.md.
class Chorus
{
public:
    void prepare(double sampleRate);
    void reset();
    void updateForBlock(const ChorusSettings& settings);
    void processSampleFrame(float inL, float inR, float& outL, float& outR);

    // The very slow rate/depth wander is seeded, so tests are reproducible.
    void setSeed(uint32_t seed);

    static int modeCount();

    // What a mode actually IS: a base delay, a rate, a depth and a path count.
    // Public so a test can assert that the modes differ structurally rather
    // than being one set of numbers scaled.
    struct ModeSpec
    {
        float baseDelayMs;
        float rateScale;
        float depthScale;
        int paths;          // 2 = the anti-phase pair, 3 = ensemble, 1 = CE
        int stackedMode;    // -1, or the mode this one runs alongside
        float bandwidthHz;
        float companding;
    };

    static ModeSpec specFor(int modeIndex);

private:
    struct Frame
    {
        float l { 0.0f };
        float r { 0.0f };
    };

    static constexpr int kMaxPaths = 3;
    static constexpr int kMaxStacks = 2;

    static float sanitize(float v);
    static float onePoleCoeff(float hz, float rate);
    static float softSaturate(float x);

    // A trapezoid rather than a sine: pitch shift is the LFO's VELOCITY, and a
    // trapezoid's velocity is constant over most of the cycle. A sine's is
    // sinusoidal, which is the "wooo".
    static float trapezoid(float phase01);

    float readDelay(int channel, int path, float delaySamples) const;
    Frame renderStack(int modeIndex, float inL, float inR, float depth, float spread);

    ChorusSettings settings;
    double sampleRateHz { 44100.0 };

    // [stack][path][channel]
    std::array<std::array<std::array<std::vector<float>, 2>, kMaxPaths>, kMaxStacks> lines;
    std::array<std::array<int, kMaxPaths>, kMaxStacks> writePos {};
    std::array<std::array<float, kMaxPaths>, kMaxStacks> lfoPhase {};
    int lineSize { 1 };

    // The wander. Bounded, slow and smoothed - the only randomness here, and it
    // exists so the cycle never repeats exactly.
    float rateWander { 0.0f };
    float depthWander { 0.0f };
    float wanderTargetRate { 0.0f };
    float wanderTargetDepth { 0.0f };
    int wanderCounter { 0 };

    // BBD group state, per stack, per path, per channel.
    std::array<std::array<std::array<float, 2>, kMaxPaths>, kMaxStacks> preEmphasisState {};
    std::array<std::array<std::array<float, 2>, kMaxPaths>, kMaxStacks> bandwidthState {};
    std::array<std::array<std::array<float, 2>, kMaxPaths>, kMaxStacks> deEmphasisState {};
    std::array<std::array<std::array<float, 2>, kMaxPaths>, kMaxStacks> compandState {};
    std::array<std::array<std::array<float, 2>, kMaxPaths>, kMaxStacks> feedbackStore {};

    // Wet-path filters. The dry path is never filtered.
    std::array<float, 2> lowCutState { { 0.0f, 0.0f } };
    std::array<float, 2> toneState { { 0.0f, 0.0f } };

    uint32_t rngState { 0x2545F491u };
    bool wasEnabled { false };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> enabledSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> amountSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> rateSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> depthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> widthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> spreadSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> toneSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lowCutSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> feedbackSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> characterSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
};

} // namespace px3
