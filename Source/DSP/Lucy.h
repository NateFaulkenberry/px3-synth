#pragma once

#include "LucyTypes.h"
#include "StftEngine.h"

#include <JuceHeader.h>

#include <array>
#include <vector>

namespace px3
{

// LUCY - a Lossy-inspired spectral degradation instrument.
//
// The heart of it is a masking-based coder, not a bitcrusher: the source's own
// documentation says the loss modes work by manipulating the frequency
// spectrum, and offers a switch between equal and psychoacoustic frequency
// weighting. A bitcrusher has no spectrum and nothing to weight.
//
// See docs/LUCY_DSP_DESIGN.md for what is documented, what is inferred, and
// which DSP reproduces each behaviour.
class Lucy
{
public:
    void prepare(double sampleRate);
    void reset();
    void updateForBlock(const LucySettings& settings);
    void processSampleFrame(float inL, float inR, float& outL, float& outR);

    // The packet chain and the jitter walk are stochastic; tests drive them
    // through this so a run is reproducible.
    void setSeed(uint32_t seed);

    // The wet path is one STFT frame late. The dry path is NOT delayed, so
    // nothing combs against the FX bus's dry sum - see the design document.
    int wetLatencySamples() const noexcept;

private:
    struct Frame
    {
        float l { 0.0f };
        float r { 0.0f };
    };

    static constexpr int kFastFftOrder = 9;    // 512
    static constexpr int kSlowFftOrder = 10;   // 1024
    static constexpr int kMaxBins = (1 << kSlowFftOrder) / 2 + 1;
    static constexpr int kCriticalBands = 24;
    static constexpr int kFilterSections = 8;  // 96 dB
    static constexpr int kVerbLines = 4;
    static constexpr int kLimiterLookahead = 64;

    static float sanitize(float v);
    static float onePoleCoeff(float hz, float rate);

    float nextRandom(int channel);
    float nextBipolar(int channel);

    // ---- the spectral engine --------------------------------------------
    void buildCriticalBands(int numBins);
    void spectralFrame(int channel, float* real, float* imag, int numBins);
    void applyLoss(int channel, int numBins);
    void applyPackets(int channel, int numBins);
    void applyFreeze(int channel, int numBins);

    // ---- time-domain stages ---------------------------------------------
    float applyJitterTiming(int channel, float input);
    Frame applyFilter(Frame in);
    Frame applyVerb(Frame in, float mix);
    Frame applyGate(Frame in);
    Frame applyLimiter(Frame in);

    LucySettings settings;
    double sampleRateHz { 44100.0 };

    // Both plans are built in prepare(), so toggling SLOW allocates nothing.
    StftEngine fastStft;
    StftEngine slowStft;
    bool slowActive { false };

    // Per channel spectral working state. Sized for the larger plan.
    std::array<std::array<float, kMaxBins>, 2> magnitude {};
    std::array<std::array<float, kMaxBins>, 2> phase {};
    std::array<std::array<float, kMaxBins>, 2> coded {};
    std::array<std::array<float, kMaxBins>, 2> maskThreshold {};
    std::array<std::array<float, kMaxBins>, 2> frozenMagnitude {};
    std::array<std::array<float, kMaxBins>, 2> frozenPhase {};
    std::array<std::array<float, kMaxBins>, 2> lastGoodMagnitude {};
    std::array<std::array<float, kMaxBins>, 2> lastGoodPhase {};
    std::array<std::array<float, kMaxBins>, 2> jitterWalk {};

    // Bark-scale band edges, rebuilt when the transform size changes.
    std::array<int, kCriticalBands + 1> bandEdges {};
    std::array<float, kCriticalBands> bandEnergy {};
    std::array<float, kCriticalBands> bandSpread {};
    int bandsForBins { 0 };

    // SPEED holds a decision for N frames. One counter drives loss, packets
    // and freeze together, which is what makes them one control.
    std::array<int, 2> decisionCounter { { 0, 0 } };
    std::array<bool, 2> decisionDue { { true, true } };

    // Gilbert-Elliott: GOOD and BAD, with runs rather than independent draws.
    std::array<bool, 2> packetBadState { { false, false } };
    std::array<int, 2> packetStateFrames { { 0, 0 } };

    std::array<bool, 2> freezeLatched { { false, false } };
    std::array<float, 2> autoGainState { { 1.0f, 1.0f } };

    // ---- jitter timing ---------------------------------------------------
    std::array<std::vector<float>, 2> jitterLine;
    int jitterSize { 1 };
    int jitterWrite { 0 };
    std::array<float, 2> jitterOffset { { 0.0f, 0.0f } };
    std::array<float, 2> jitterTarget { { 0.0f, 0.0f } };

    // ---- filter -----------------------------------------------------------
    struct SvfState
    {
        float ic1 { 0.0f };
        float ic2 { 0.0f };
    };
    std::array<std::array<SvfState, kFilterSections>, 2> filterState {};

    // ---- verb -------------------------------------------------------------
    std::array<std::array<std::vector<float>, kVerbLines>, 2> verbLines;
    std::array<int, kVerbLines> verbLength {};
    std::array<std::array<int, kVerbLines>, 2> verbWrite {};
    std::array<std::array<float, kVerbLines>, 2> verbDamp {};
    float verbModPhase { 0.0f };

    // ---- gate -------------------------------------------------------------
    std::array<float, 2> gateEnv { { 0.0f, 0.0f } };
    float gateGain { 1.0f };
    bool gateOpen { false };

    // ---- limiter ----------------------------------------------------------
    std::array<std::array<float, kLimiterLookahead>, 2> limiterDelay {};
    int limiterWrite { 0 };
    float limiterGain { 1.0f };

    std::array<uint32_t, 2> rngState { { 0x9E3779B9u, 0x85EBCA6Bu } };

    bool wasEnabled { false };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> enabledSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> globalSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lossSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> speedSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> filterWidthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> filterFreqSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> verbSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> verbDecaySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> freezerSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gateCutoffSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> thresholdSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> spreadSmoothed;
};

} // namespace px3
