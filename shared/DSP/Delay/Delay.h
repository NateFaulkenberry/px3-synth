#pragma once

#include "DelayTypes.h"

#include <JuceHeader.h>

#include <array>
#include <vector>

class Delay
{
public:
    void prepare(double sampleRate);
    void reset();
    void updateForBlock(const DelaySettings& settings);
    void processSampleFrame(float inL, float inR, float& outL, float& outR);

    // The granular spawner and the BBD noise floor are stochastic; tests drive
    // them through this so a run is reproducible. DELAY used
    // juce::Random::getSystemRandom() from the audio thread, which is a shared
    // global that cannot be pinned - so nothing about its output could be
    // asserted exactly, only its bounds.
    void setSeed(uint32_t seed);

private:
    // xorshift, per instance, matching Doom::nextRandom, Lucy::nextRandom and
    // Mood::nextRandom. No allocation, no lock, no global.
    float nextRandom();
    int nextRandomInt(int upperExclusive);

    // Never zero: xorshift is stuck at zero forever.
    uint32_t rngState { 0xC2B2AE35u };

    struct Grain
    {
        bool active { false };
        bool reverse { false };
        float readPos { 0.0f };
        float increment { 1.0f };
        int ageSamples { 0 };
        int lengthSamples { 1 };
        float gain { 0.0f };
        float pan { 0.5f };
        // Grains read the stereo buffer with a per-grain balance rather than
        // collapsing it to mono, so material that arrives wide stays wide.
        float sourceBalance { 0.5f };
    };

    enum class GranularMode
    {
        classic = 0,
        cloud,
        shimmer,
        rhythmic
    };

    // A delay tap that changes its length by crossfading to the new position
    // rather than sliding the read pointer. Sliding is what a tape machine
    // does and it pitch-shifts; the digital algorithms are not supposed to.
    struct CrossfadeTap
    {
        float activeSamples { 1000.0f };
        float fadingSamples { 1000.0f };
        int fadeRemaining { 0 };
        bool primed { false };
    };

    // Two-pole state-variable filter. Used for the BBD's anti-alias and
    // reconstruction stages, where a one-pole is nowhere near steep enough to
    // stand in for the multi-pole active filters those chips need, and for the
    // tape head bump.
    struct Svf
    {
        float ic1 { 0.0f };
        float ic2 { 0.0f };
        void reset() { ic1 = 0.0f; ic2 = 0.0f; }
    };

    static constexpr int maxGrains = 48;
    // Four allpass stages per channel in the feedback path of the diffusion
    // algorithm. Lengths are mutually incommensurate for the same reason they
    // are in the reverb: common factors make the echo pattern repeat audibly.
    static constexpr int diffusionStages = 4;

    static float clamp01(float v);
    static float lerp(float a, float b, float t);
    static float smoothstep(float x);
    static float sanitizeAudioSample(float x);
    static float divisionBeatsForIndex(int index);
    // A one-pole coefficient for a real cutoff frequency. The old code used
    // bare constants, which meant every filter in here moved an octave when the
    // host changed sample rate.
    static float onePoleCoeff(float hz, float sampleRate);
    static float softSaturate(float x);

    float readDelaySample(int channel, float readPos) const;
    float slewReadLength(int channel, float target);
    float readCrossfadedTap(int channel, CrossfadeTap& tap, float targetSamples);
    float processSvfLowpass(Svf& state, float x, float cutoffHz, float q) const;
    float processSvfBandpass(Svf& state, float x, float cutoffHz, float q) const;

    void clearGranularDiffusionState();
    float processAllpassSample(float x, std::vector<float>& line, int& index, float feedback) const;
    void processGranularDiffusion(float& wetL, float& wetR, float diffusionAmount, float stereoAmount);
    void renderActiveGranularGrains(float& wetL, float& wetR);
    void spawnIsaacGrain(float amount,
                         float timeControl,
                         float feedbackControl,
                         int syncDivisionIndex,
                         GranularMode mode,
                         int rhythmicStep);
    void processIsaacGranularSample(float inL,
                                    float inR,
                                    float amount,
                                    float timeControl,
                                    float feedbackControl,
                                    int syncDivisionIndex,
                                    float& outL,
                                    float& outR);
    void processDelayAlgorithmSample(float inL,
                                     float inR,
                                     float amount,
                                     int algorithmIndex,
                                     float timeControl,
                                     float feedbackControl,
                                     int syncDivisionIndex,
                                     float& outL,
                                     float& outR);

    // Base delay length in samples for the current controls, honouring tempo
    // sync. Shared so every algorithm agrees on what the TIME knob means.
    float baseDelaySamplesFor(float timeControl, int syncDivisionIndex) const;
    float processDiffusionChain(int channel, float x, float amount);

    DelaySettings currentSettings;

    std::array<std::vector<float>, 2> delayBuffer;
    std::array<std::vector<float>, 2> isaacDiffusionLineA;
    std::array<std::vector<float>, 2> isaacDiffusionLineB;
    std::array<int, 2> isaacDiffusionIndexA { { 0, 0 } };
    std::array<int, 2> isaacDiffusionIndexB { { 0, 0 } };
    std::array<Grain, maxGrains> isaacGrains {};

    // Feedback-path damping, shared by the granular modes.
    std::array<float, 2> isaacFeedbackFilter { { 0.0f, 0.0f } };
    std::array<float, 2> isaacShimmerSmooth { { 0.0f, 0.0f } };

    // ---- tape ----------------------------------------------------------
    // Wow, flutter and scrape are separate mechanisms in a real transport
    // (capstan eccentricity, roller and motor cogging, tape rubbing the heads)
    // and they run at decades-apart rates. One sine cannot stand in for them.
    float tapeWowPhase { 0.0f };
    float tapeFlutterPhase { 0.0f };
    float tapeScrapePhase { 0.0f };
    float tapeDriftPhase { 0.0f };
    std::array<float, 2> tapeGapLoss { { 0.0f, 0.0f } };
    std::array<Svf, 2> tapeHeadBump {};
    std::array<float, 2> tapeDcX1 { { 0.0f, 0.0f } };
    std::array<float, 2> tapeDcY1 { { 0.0f, 0.0f } };
    std::array<float, 2> tapeHysteresis { { 0.0f, 0.0f } };

    // ---- BBD -----------------------------------------------------------
    // A bucket-brigade chip is a fixed number of stages clocked at whatever
    // rate produces the wanted delay, so its bandwidth is set by the delay
    // time. Long settings are dark; short settings are bright. That coupling
    // is the single most recognisable thing about the format.
    std::array<Svf, 2> bbdAntiAlias {};
    std::array<Svf, 2> bbdReconstruct {};
    std::array<float, 2> bbdCompressorEnv { { 0.0f, 0.0f } };
    std::array<float, 2> bbdExpanderEnv { { 0.0f, 0.0f } };
    std::array<float, 2> bbdNoiseState { { 0.0f, 0.0f } };

    // ---- diffusion -----------------------------------------------------
    std::array<std::array<std::vector<float>, diffusionStages>, 2> diffusionLines;
    std::array<std::array<int, diffusionStages>, 2> diffusionIndices { {} };

    // ---- shared modulation and taps ------------------------------------
    std::array<CrossfadeTap, 2> algorithmTaps {};
    float delayModPhaseA { 0.0f };
    float delayModPhaseB { 0.0f };
    float delayModPhaseC { 0.0f };
    int delayBufferSize { 1 };
    int writePos { 0 };
    int isaacSpawnCounter { 0 };
    int isaacRhythmicStepIndex { 0 };
    int isaacRhythmicSamplesUntilNext { 0 };
    bool isaacRhythmicSwingToggle { false };
    float isaacPanPhase { 0.0f };
    float delayAmountSmoothed { 0.0f };
    float delayTimeControlSmoothed { 0.5f };
    float delayFeedbackControlSmoothed { 0.35f };
    float delayControlSmoothingCoeff { 0.0f };
    // The delay length TAPE and MODULATED actually read from, slew-limited so
    // the read pointer can never overtake the write pointer. Those two slide
    // the pointer instead of crossfading taps, which is what gives them their
    // pitch glide - and what makes an abrupt time change able to outrun the
    // write head and read samples that have not been written yet.
    float slidingDelaySamples { 0.0f };
    bool slidingDelayPrimed { false };
    // The final per-channel read length, after modulation. Limiting the base
    // alone is not enough: MODULATED adds its depth on top, so the length the
    // pointer actually uses can still jump.
    std::array<float, 2> slidingReadSamples { { 0.0f, 0.0f } };
    bool slidingReadPrimed { false };
    int crossfadeLengthSamples { 512 };
    int lastDelayAlgorithmIndex { -1 };
    int lastGranularModeIndex { -1 };

    double currentSampleRateHz { 44100.0 };
    double currentBpm { 120.0 };
    bool smoothingPrimed { false };
    // Latches so the clear on bypass happens once, not every block.
    bool bypassCleared { false };
};
