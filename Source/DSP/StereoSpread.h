#pragma once

#include "StereoSpreadTypes.h"

#include <JuceHeader.h>

#include <array>

namespace px3
{

// STEREO SPREAD - a mono-compatible widener.
//
// Width comes from an ALLPASS DECORRELATION network, not from a delay and not
// from M/S gain. An allpass leaves each channel's magnitude response flat while
// changing the phase relationship BETWEEN the channels, so the two differ
// without either being filtered - and because the phase difference varies
// smoothly with frequency rather than as the linear ramp a delay produces, the
// mono sum ripples gently instead of combing at regular intervals.
//
// M/S gain alone cannot do this: for a mono source the side signal is zero, and
// no gain applied to zero produces anything. Something has to CREATE the side
// content first, and that is what the allpass network is for.
//
// Split by frequency, because hearing is: lows are summed to mono (there is no
// width available at a wavelength longer than the room), mids are decorrelated
// by phase (where the ear localises by phase), and highs by level (where it
// does not, and where a timing difference would only comb).
class StereoSpread
{
public:
    void prepare(double sampleRate);
    void reset();
    void updateForBlock(const StereoSpreadSettings& settings);
    void processSampleFrame(float inL, float inR, float& outL, float& outR);

    // The running interchannel correlation, for tests and for the guard.
    float currentCorrelation() const noexcept { return correlation; }

    static int modeCount();

private:
    struct Frame
    {
        float l { 0.0f };
        float r { 0.0f };
    };

    struct Bands
    {
        Frame low;
        Frame mid;
        Frame high;
    };

    // What a mode actually is, rather than a preset of the same numbers.
    struct ModeSpec
    {
        float sideGain;
        int allpassSections;
        float lowFreqScale;
        float correlationFloor;
        float modulationRate;
    };

    static constexpr int kMaxAllpass = 8;

    static float sanitize(float v);
    static float onePoleCoeff(float hz, float rate);

    static ModeSpec specFor(int modeIndex);

    Bands split(float inL, float inR);
    float allpassChain(int channel, float x, int sections, float spreadAmount);

    StereoSpreadSettings settings;
    double sampleRateHz { 44100.0 };

    // ---- Linkwitz-Riley crossovers ---------------------------------------
    // Two cascaded Butterworth sections per split. LR4's two outputs sum flat
    // in magnitude, which is the whole reason to use it: a widener that splits
    // bands must be able to put them back together without a hole, or the
    // "widening" includes an EQ curve.
    struct Lr4
    {
        std::array<float, 2> s1 {};
        std::array<float, 2> s2 {};
        std::array<float, 2> s3 {};
        std::array<float, 2> s4 {};
    };

    Lr4 lowSplit {};
    Lr4 highSplit {};

    // ---- allpass decorrelation -------------------------------------------
    std::array<std::array<float, kMaxAllpass>, 2> allpassState {};

    // ---- slow correlated modulation --------------------------------------
    float modPhase { 0.0f };

    // ---- correlation guard ------------------------------------------------
    float correlation { 1.0f };
    float sumLR { 0.0f };
    float sumLL { 0.0f };
    float sumRR { 0.0f };
    float guardGain { 1.0f };

    std::array<float, 2> toneState { { 0.0f, 0.0f } };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> enabledSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> amountSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> widthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> depthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> centerSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lowWidthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> highWidthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lowFreqSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> highFreqSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> toneSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
};

} // namespace px3
