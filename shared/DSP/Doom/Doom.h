#pragma once

#include "DoomControlModel.h"
#include "DoomTypes.h"
#include "StftEngine.h"

#include <JuceHeader.h>

#include <array>
#include <vector>

namespace px3
{

// DOOM - a BAD MOOD-inspired two-channel ambient processor.
//
// Two channels that are aware of each other: a micro-looper that is always
// listening, and a set of spatial effects that can process the input, the loop,
// or both. One clock underneath them: dropping it lengthens the loop, drops its
// pitch, slows the wet channel and narrows the band, all at once.
//
// This is NOT the Mood engine with different names. Mood and DOOM share a
// control scheme and nothing below it - see docs/DOOM_DSP_DESIGN.md for what
// each subsystem is and which documented behaviour it reproduces.
class Doom
{
public:
    void prepare(double sampleRate);
    void reset();
    // The user's controls go in; deriveDoomParameters turns the four macros
    // into whichever DSP quantity the current mode needs.
    void updateForBlock(const DoomUserParameters& settings);
    void processSampleFrame(float inL, float inR, float& outL, float& outR);

    // Tests drive the stochastic parts (Burst's fills, Radio's static, Mask's
    // excitation) through this, so a run is reproducible.
    void setSeed(uint32_t seed);

    // The harmonised CLOCK steps, exposed so the ratio table is testable
    // without reaching into the engine.
    static float clockRatioFor(float clockNormalised, bool smooth);
    static int clockStepCount();

private:
    struct Frame
    {
        float l { 0.0f };
        float r { 0.0f };
    };

    // A windowed grain reading the history buffer. Used by RADIO's stations and
    // by FLIP - the same machinery, different scheduling.
    struct Grain
    {
        bool active { false };
        float readPos { 0.0f };
        float rate { 1.0f };
        int age { 0 };
        int length { 1 };
        float gain { 0.0f };
        float pan { 0.0f };
    };

    static constexpr int kMaxGrains = 24;
    static constexpr int kMaxSlices = 8;
    static constexpr int kMaxRelayTaps = 8;
    static constexpr int kRadioStations = 5;
    static constexpr int kSoupFftOrder = 9;    // 512-point

    // ---- helpers ---------------------------------------------------------
    static float sanitize(float v);
    static float softSaturate(float x);
    static float semitoneRatio(float semitones);
    static float onePoleCoeff(float hz, float rate);
    static Frame panStereo(float mono, float position);

    float nextRandom();                 // uniform 0..1, seeded
    float nextBipolar();                // uniform -1..1, seeded

    float readHistory(int channel, float pos) const;
    float readHistorySpliced(int channel, float startAbs, float pos,
                             float loopLength, float fade) const;

    // ---- clock -----------------------------------------------------------
    void updateClock();

    // ---- micro-looper ----------------------------------------------------
    void writeHistory(float l, float r);
    void latchLoop();
    void detectSlices();
    Frame renderLoop(float inL, float inR);
    Frame renderBurst();
    Frame renderRadio();
    Frame renderMask();
    Frame renderStation(int station, float length);

    // ---- wet channel -----------------------------------------------------
    Frame renderWet(float inL, float inR);
    Frame renderSoup(float inL, float inR);
    Frame renderRelay(float inL, float inR);
    Frame renderFlip(float inL, float inR);
    void soupFrame(int channel, float* real, float* imag, int numBins);

    // ---- global stages ---------------------------------------------------
    void updateCross(float inL, float inR, const Frame& loop, const Frame& wet);
    // CROSS's pitch term, as a multiplier on any read head that advances
    // through the loop. Bounded, so no amount of cross can stop or reverse it.
    float crossReadRate() const noexcept { return juce::jlimit(0.5f, 1.6f, 1.0f + crossFm); }
    Frame applyEq(Frame in);
    Frame applyGlue(Frame in);

    Frame processInternalStep(float inL, float inR);

    DoomUserParameters settings;
    DoomDerivedParameters derived;
    double hostSampleRate { 44100.0 };
    double internalRate { 44100.0 };

    // ---- clock state -----------------------------------------------------
    float clockRatio { 1.0f };
    float clockPhase { 0.0f };
    Frame heldOutput {};
    Frame decimateAccum {};
    int decimateCount { 0 };
    std::array<float, 2> reconstructState { { 0.0f, 0.0f } };
    float reconstructCoeff { 0.5f };

    // ---- history / loop --------------------------------------------------
    std::array<std::vector<float>, 2> history;
    int historySize { 1 };
    int historyWrite { 0 };
    double historyAbsolute { 0.0 };     // total internal samples written

    float loopStartAbs { 0.0f };
    float loopLengthSamples { 1.0f };
    bool loopLatched { false };
    bool wasLoopActive { false };
    float loopPhase { 0.0f };

    // BURST
    std::array<float, kMaxSlices> sliceOffsets {};
    int sliceCount { 1 };
    int burstStep { 0 };
    std::array<int, kMaxSlices> burstOrder {};
    float burstStepPhase { 0.0f };
    float burstEnv { 0.0f };
    float burstGrainPhase { 0.0f };

    // RADIO
    std::array<Grain, kMaxGrains> grains {};
    int grainSpawnCounter { 0 };
    float radioStatic { 0.0f };
    std::array<float, 2> radioNoiseState { { 0.0f, 0.0f } };
    float danceRotationPhase { 0.0f };
    int danceStage { 0 };
    float tapeReadPos { 0.0f };
    float shoegazeOrigin { 0.0f };

    // MASK
    float maskEnv { 0.0f };
    float maskGate { 0.0f };
    float maskRingPhase { 0.0f };
    // The disguises read the loop at their own rates, so they need their own
    // phases. Deriving them from the playback position means they inherit ITS
    // wrap, which lands them somewhere the splice crossfade is not.
    float maskPitchPhase { 0.0f };
    float maskReversePhase { 0.0f };
    std::array<float, 2> maskResonator { { 0.0f, 0.0f } };
    std::array<float, 2> maskResonatorPrev { { 0.0f, 0.0f } };

    // ---- wet channel state ----------------------------------------------
    StftEngine soupStft;
    std::array<std::vector<float>, 2> soupMagnitude;
    std::array<std::vector<float>, 2> soupPhase;
    std::array<std::vector<float>, 2> soupBlur;
    bool soupPrepared { false };

    std::array<std::vector<float>, 2> relayBuffer;
    int relaySize { 1 };
    int relayWrite { 0 };
    std::array<float, 2> relayHold { { 0.0f, 0.0f } };

    std::array<Grain, kMaxGrains> flipGrains {};
    std::array<std::vector<float>, 2> flipBuffer;
    int flipSize { 1 };
    int flipWrite { 0 };
    int flipSpawnCounter { 0 };

    std::array<std::vector<float>, 2> freezeBuffer;
    int freezeSize { 1 };
    int freezeWrite { 0 };
    float freezeRead { 0.0f };
    bool freezeLatched { false };

    // ---- global stage state ---------------------------------------------
    float crossEnv { 0.0f };
    float crossMean { 0.0f };
    float crossSlewed { 0.0f };
    float crossAm { 1.0f };
    float crossFm { 0.0f };

    std::array<float, 2> eqLowState { { 0.0f, 0.0f } };
    std::array<float, 2> eqHighState { { 0.0f, 0.0f } };
    std::array<float, 2> glueDcState { { 0.0f, 0.0f } };
    std::array<float, 2> glueDcPrev { { 0.0f, 0.0f } };

    uint32_t rngState { 0x9E3779B9u };

    bool wasEnabled { false };
    // True while the effect is contributing nothing. An effect at zero mix that
    // still runs its whole engine costs the same as one you can hear, which is
    // what pushed a full-voice patch over the real-time budget.
    bool idle { false };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> enabledSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> clockSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> loopLengthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> loopModifySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetTimeSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetModifySmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> crossSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> glueSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> eqSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> balanceSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> blendSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> spreadSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> overdubSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetActiveSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> loopActiveSmoothed;
};

} // namespace px3
