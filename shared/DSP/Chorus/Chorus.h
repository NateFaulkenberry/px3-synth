#pragma once

#include "ChorusTypes.h"

#include <JuceHeader.h>

#include <array>
#include <vector>

namespace px3
{

// CHORUS - four families of bucket-brigade chorus, each built to the topology
// of the hardware it is named after. See docs/CHORUS_DSP_DESIGN.md for the
// sources behind every constant.
//
//   DIM 1-4, 1+4, 2+4, 3+4   Roland SDD-320 Dimension D: two BBD lines driven
//                            in anti-phase by one rounded-triangle LFO, each
//                            through emphasis + 2:1 compander, cross-fed to the
//                            opposite output with inverted polarity. The
//                            combination buttons are switch states on the same
//                            pair, not a second pair.
//   ENSEMBLE                 Solina-style string ensemble: three lines, each
//                            swept by a slow AND a fast generator, the three
//                            120 degrees apart.
//   CE-1                     BOSS CE-1: one line; in stereo, output A carries
//                            the chorus only and output B the direct sound.
//   JUNO-60 I / II / I+II    two 256-stage lines on one triangle LFO, the right
//                            one inverted (I, II) or in phase (I+II); L = dry +
//                            line A, R = dry + line B, both positive. Input and
//                            output filters are the Holters-Parker fit to a
//                            Juno-60.
class Chorus
{
public:
    enum class Family
    {
        dimension,
        ensemble,
        ce1,
        juno
    };

    // What a mode IS. All times in milliseconds, all rates in hertz, and every
    // value is the hardware's figure at the control defaults (RATE 0.35,
    // DEPTH 0.5, SPREAD 0.5). Public so tests can pin them.
    struct ModeSpec
    {
        Family family;
        int lines;
        float rateHz;            // primary LFO
        float baseDelayMs;       // centre of the sweep
        float excursionMs;       // +/- swing at DEPTH 0.5
        float lineSkew;          // centre mismatch: line A x(1 - skew), line B x(1 + skew)
        float lineOffsetCycles;  // pair: LFO phase of line B; ensemble: spacing between lines
        float cornerFraction;    // rounded-triangle corner width (0 = sine LFO)
        bool sineLfo;
        float fastRateHz;        // ensemble only: the "vibrato" generator
        float fastExcursionMs;
        bool compander;          // emphasis + 2:1 compander around the line
        float dryGain;           // dry level at full AMOUNT
        float wetGain;           // per-line wet level
    };

    void prepare(double sampleRate);
    void reset();
    void updateForBlock(const ChorusSettings& settings);
    void processSampleFrame(float inL, float inR, float& outL, float& outR);

    static int modeCount();
    static ModeSpec specFor(int modeIndex);

    // A triangle in [-1, 1] whose corners are replaced by parabolas, so the
    // delay is C1: the pitch (its derivative) moves between +/- a constant
    // detune without stepping. cornerFraction is the share of the cycle each
    // corner occupies on one side. Peak at phase 0, trough at phase 0.5.
    static float roundedTriangle(float phase01, float cornerFraction);

private:
    struct Section
    {
        float b0 { 1.0f };
        float b1 { 0.0f };
        float b2 { 0.0f };
        float a1 { 0.0f };
        float a2 { 0.0f };
    };

    static constexpr int kMaxSections = 3;
    static constexpr int kMaxLines = 3;
    static constexpr int kModeCount = 12;

    struct FilterDesign
    {
        int count { 0 };
        bool parallel { false };
        std::array<Section, kMaxSections> sections {};
    };

    using FilterState = std::array<std::array<float, 2>, kMaxSections>;

    // Everything precomputed in prepare() for one mode. Nothing here is
    // recomputed in the audio path.
    struct ModeFilters
    {
        FilterDesign pre;
        FilterDesign post;
        Section highPass;     // Dimension: bass roll-off before the compander
        Section emphasis;     // Dimension: pre-emphasis shelf
        Section deEmphasis;   // its exact inverse
    };

    struct Line
    {
        std::vector<float> buffer;
        int write { 0 };
        FilterState pre {};
        FilterState post {};
        std::array<float, 2> highPass {};
        std::array<float, 2> emphasis {};
        std::array<float, 2> deEmphasis {};
        float compressorEnv { 0.0f };
        float expanderEnv { 0.0f };
        float lastPost { 0.0f };
    };

    struct Slot
    {
        int mode { 1 };
        double phase { 0.0 };
        double fastPhase { 0.0 };
        std::array<Line, kMaxLines> lines;
    };

    struct Output
    {
        float dryL { 0.0f };
        float dryR { 0.0f };
        float wetL { 0.0f };
        float wetR { 0.0f };
    };

    struct Controls
    {
        float amount;
        float depth;
        float width;
        float spread;
        float drive;
        float feedback;
        float rateTrim;
    };

    static float sanitize(float v);
    static float runSection(const Section& s, std::array<float, 2>& state, float x);
    static float runFilter(const FilterDesign& design, FilterState& state, float x);

    void activateSlot(int slotIndex, int mode);
    float readLine(const Line& line, float delaySamples) const;
    float processLine(Line& line, const ModeFilters& filters, bool compander, float x,
                      float delaySamples, float drive, float feedback);
    Output renderSlot(Slot& slot, float inL, float inR, const Controls& c);

    ChorusSettings settings;
    double sampleRateHz { 44100.0 };
    float msToSamples { 44.1f };
    int lineSize { 1 };

    std::array<ModeFilters, kModeCount> modeFilters {};
    std::array<Slot, 2> slots;
    int activeSlot { 0 };
    int pendingMode { 1 };
    float fadePosition { 1.0f };
    float fadeIncrement { 0.0f };

    // Compander detector coefficients.
    float envAttack { 0.0f };
    float envRelease { 0.0f };

    // Wet-path user filters. The dry path is never filtered.
    float toneCoeff { 0.0f };
    std::array<float, 2> lowCutState { { 0.0f, 0.0f } };
    std::array<float, 2> toneState { { 0.0f, 0.0f } };

    bool idle { true };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> enabledSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> amountSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> rateTrimSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> depthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> widthSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> spreadSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> toneSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lowCutCoeffSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> feedbackSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> characterSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
};

} // namespace px3
