#pragma once

#include "MoodTypes.h"

#include <JuceHeader.h>

#include <array>
#include <vector>

// Two channels that are aware of each other: a micro-looper that is always
// listening, and a suite of spatial effects that can process the input, the
// loop, or both. Everything runs on an internal sample clock whose rate is set
// by the CLOCK control, which is what ties the two channels together - dropping
// the clock lengthens the loop, drops its pitch, and slows the wet channel, all
// at once.
class Mood
{
public:
    void prepare(double sampleRate);
    void reset();
    void updateForBlock(const MoodSettings& settings);
    void processSampleFrame(float inL, float inR, float& outL, float& outR);

private:
    struct Grain
    {
        bool active { false };
        float readPos { 0.0f };
        float increment { 1.0f };
        int ageSamples { 0 };
        int lengthSamples { 1 };
        float pan { 0.5f };
        float gain { 0.0f };
        // Where in the stereo field the grain draws from. Reading a mono sum
        // throws away the image before the panner ever gets to place it.
        float sourceBalance { 0.5f };
    };

    // A stereo pair, so the per-mode renderers can return one value and the
    // call sites stop juggling two out-parameters.
    struct Frame
    {
        float l { 0.0f };
        float r { 0.0f };
    };

    // A wet-channel tap that changes length by crossfading rather than by
    // sliding, so moving TIME does not pitch-bend the echoes already in flight.
    struct CrossfadeTap
    {
        float activeSamples { 1000.0f };
        float fadingSamples { 1000.0f };
        int fadeRemaining { 0 };
        bool primed { false };
    };

    static constexpr int kMaxGrains = 16;
    static constexpr int kDiffusionStages = 4;

    static float clamp01(float v);
    static float sanitizeAudioSample(float v);
    static float semitoneRatio(float semitones);
    static float softSaturate(float x);
    static float onePoleCoeff(float hz, float sampleRate);
    // Equal-power pan, taking -1..+1 rather than 0..1 so that "no pan" is zero
    // and the per-mode stereo treatments can be scaled by SPREAD directly.
    static Frame panStereo(float mono, float position);

    float readInterp(const std::vector<float>& line, float pos) const;
    float readAllpass(int channel, int stage, float x, float g);

    void writeHistory(float l, float r);
    void writeWet(float l, float r);

    // One step of the internal, clock-divided engine. Everything below runs at
    // the internal rate, not the host rate.
    Frame processInternalStep(float inL, float inR);

    Frame renderLoopTape(float spread);
    Frame renderLoopEnv(float inL, float inR, float spread);
    Frame renderLoopStretch(float spread);

    Frame renderWetReverb(float inL, float inR, float spread);
    Frame renderWetDelay(float inL, float inR, float spread);
    Frame renderWetSlip(float inL, float inR, float spread);

    void maybeSpawnStretchGrain(float spread);
    float readCrossfadedWetTap(int channel, CrossfadeTap& tap, float targetSamples);
    // The CLASSIC-mode artifacts, gathered behind one control: quantisation
    // noise and a rising noise floor, applied where they compound over repeats.
    float applyDegradation(float x, int channel);

    MoodSettings currentSettings;

    std::array<std::vector<float>, 2> historyBuffer;
    std::array<std::vector<float>, 2> wetBuffer;
    std::array<std::array<std::vector<float>, kDiffusionStages>, 2> diffusionLines;
    std::array<std::array<int, kDiffusionStages>, 2> diffusionIndices { {} };

    std::array<Grain, kMaxGrains> grains {};
    std::array<CrossfadeTap, 2> wetTaps {};

    int historySize { 1 };
    int wetSize { 1 };
    int historyWritePos { 0 };
    int wetWritePos { 0 };

    // ---- internal clock -------------------------------------------------
    // CLOCK sets the rate this engine runs at. Audio recorded at one rate and
    // played back at another changes speed and pitch together, which is the
    // whole point of the control: dropping the clock an octave half-speeds the
    // micro-loop and the wet channel alike.
    float clockPhase { 0.0f };
    float clockIncrement { 1.0f };
    float clockDivider { 1.0f };
    double internalSampleRate { 44100.0 };
    Frame heldOutput {};

    float loopReadPos { 0.0f };
    float loopHeldReadPos { 0.0f };
    float envFollower { 0.0f };
    float envPanPhase { 0.0f };
    float envPanDirection { 1.0f };
    bool envGateOpen { false };
    int envSliceHoldSamples { 0 };
    int stretchSpawnCounter { 0 };
    float stretchPanPhase { 0.0f };

    float slipReadPos { 0.0f };
    float slipPanPhase { 0.0f };

    std::array<float, 2> wetDampState { { 0.0f, 0.0f } };
    std::array<float, 2> degradeNoiseState { { 0.0f, 0.0f } };
    std::array<float, 2> degradeEnvelope { { 0.0f, 0.0f } };
    std::array<float, 2> degradeHeld { { 0.0f, 0.0f } };
    std::array<int, 2> degradeHoldCounter { { 0, 0 } };
    std::array<float, 2> reverbFeedbackStore { { 0.0f, 0.0f } };

    bool wasEnabled { false };
    bool pendingResetOnBypass { false };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> enabledSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> clockSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> routingSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetTimeSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetModifySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> loopLengthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> loopModifySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> feedbackSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> spreadSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> degradeSmoothed;

    double sampleRateHz { 44100.0 };
};
