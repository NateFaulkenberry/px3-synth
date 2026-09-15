#pragma once

#include "OscillatorDsp.h"
#include "OscillatorMode.h"
#include "OscillatorTypes.h"
#include "WavetableReader.h"

#include <array>
#include <cstdint>

// One oscillator of one voice: every mode PX3 offers, built on the primitives in
// OscillatorDsp.h. docs/OSCILLATOR_DSP_DESIGN.md is the design and the evidence.
//
// What leaves renderSample is this oscillator's signal at its mode's level,
// px3::dsp::kOscillatorLatencySamples after the phase that made it, and NOT yet
// soft-clipped: the voice's source stage owns that curve, and anti-aliases it.
class OscillatorUnit
{
public:
    // How long the scan takes to reach a new position. Long enough that the
    // once-per-block step in the modulation sum is gone, short enough that a
    // fast envelope sweeping the scan still arrives when it should.
    static constexpr double kWtPositionSmoothingSeconds = 0.003;

    // Changing mode crossfades the old mode out and the new one in over this
    // long. Both render meanwhile, each from its own state.
    static constexpr double kModeCrossfadeSeconds = 0.005;

    struct RenderContext
    {
        // This oscillator's frequency for this sample: the note with bend,
        // vibrato and drift applied, times its tuning and Pitch Mod.
        double frequencyHz { 440.0 };
        int noteAgeSamples { 0 };
        // The smoothed mod wheel. PWM's width is its only destination here.
        float modWheelNorm { 0.0f };
    };

    // Off the audio thread: sample-rate dependent coefficients.
    void prepare(double sampleRate);

    // Once per control block, before it renders. Continuous controls ramp from
    // their previous values across `rampSamples`; a new mode crossfades in.
    void setSettings(const OscillatorSettings& settings, int rampSamples = 0);

    // `startPhase` in cycles; `seed` makes this oscillator's noise and random
    // start state its own and reproducible.
    void resetForNote(double startPhase, std::uint32_t seed);

    double renderSample(const RenderContext& context);

    double currentPhase() const noexcept { return phase; }
    int currentMode() const noexcept { return activeMode; }

private:
    // Nine partials, not eight, because a Hammond has nine drawbars and two of
    // them - the 16' sub at half the fundamental and the 5 1/3' quint at one and
    // a half times it - are not whole harmonics at all.
    static constexpr int kHarmonicCount = 9;
    static constexpr int kSuperSawVoices = 7;
    static constexpr int kPhysicalModes = 4;
    static constexpr int kRobPhases = 10;

    struct HarmonicSet
    {
        std::array<float, kHarmonicCount> amplitude { {} };
        std::array<float, kHarmonicCount> ratio { {} };
        float norm { 0.0f };
    };

    // A formant is a resonance of the vocal tract, so it sits at a FIXED
    // frequency in hertz and does not move with the note - which is what keeps
    // a vowel the same vowel up the keyboard. Held as resonator coefficients.
    struct FormantBank
    {
        std::array<float, 3> a { {} };   // input
        std::array<float, 3> b { {} };   // y[n-1]
        std::array<float, 3> c { {} };   // y[n-2]
        std::array<float, 3> gain { {} };
    };

    // Everything a mode derives from its controls.
    //
    // Rebuilt once per control block, and only when the controls moved. Read
    // per sample as a ramp from the previous block's values, so a macro swept
    // by an LFO is a line rather than a staircase stepping at the block rate.
    struct DerivedCurves
    {
        float superSawWidth { 0.0f };
        float superSawEdgeSoft { 1.0f };
        std::array<double, kSuperSawVoices> superSawRatios { {} };

        float pwmWidthCurve { 0.0f };

        HarmonicSet additive;
        HarmonicSet isaac;
        float isaacShimmer { 0.0f };

        FormantBank formant;
        float formantSourceCoeff { 0.02f };
        float formantTrim { 1.0f };

        HarmonicSet organ;
        float organClick { 0.0f };
        float organClickDecayPerSecond { 0.0f };

        float fmRatio { 1.0f };
        float fmIndex { 0.0f };
        float fmOutputScale { 1.0f };

        float hardSyncRatio { 1.0f };
        float hardSyncDrive { 1.0f };

        float digitalHoldAt48k { 1.0f };   // samples at 48 kHz: a duration
        float digitalFold { 1.0f };
        float digitalSteps { 4.0f };       // structural: stepped by design
        float digitalCrushSteps { 2.0f };

        std::array<float, kPhysicalModes> physicalDecayCoeff { {} };
        float physicalSpread { 1.0f };

        float robTrans { 0.0f };
        float robBody { 0.0f };
        float robChaos { 0.0f };
        float robTransientCoeff { 1.0f };

        float px3Morph { 0.0f };
        float px3Character { 0.0f };
        float px3Movement { 0.0f };
        HarmonicSet px3Isaac;

        float noiseColor { 0.5f };
        float noiseCoeff { 0.1f };
        float pinkCoeff { 0.1f };

        // The level each mode leaves at: its trim, the macro-travel
        // compensation and the fixed level at rest.
        std::array<float, px3::oscillatorModeCount> modeGain { {} };
    };

    struct MainPhase
    {
        double phase { 0.0 };
        double increment { 0.0 };
        bool wrapped { false };
        double tau { 0.0 };       // samples since the wrap, when wrapped
    };

    void updateDerivedCurves();
    static HarmonicSet buildHarmonicSet(const std::array<float, 8>& harmonics,
                                        float rolloffBias,
                                        float oddEvenBias,
                                        float inharmonicity,
                                        float roll);

    float rampValue() const noexcept { return currentRamp; }
    float ramped(float DerivedCurves::* field) const noexcept
    {
        return previous.*field + (target.*field - previous.*field) * currentRamp;
    }

    void requestMode(int mode);
    void beginModeChange(int mode);
    void activateMode(int mode, bool strike);

    double renderMode(int mode, const RenderContext& context, const MainPhase& main);

    double renderPulse(px3::dsp::BlepLine& line, const MainPhase& main, double width);
    double renderTriangle(const MainPhase& main);
    double renderSuperSaw(const RenderContext& context);
    double renderHarmonicSet(std::array<double, kHarmonicCount>& phases,
                             const HarmonicSet& from,
                             const HarmonicSet& to,
                             double increment);
    double renderFormant(const RenderContext& context, const MainPhase& main);
    double renderFmCore(double& modulatorPhase,
                        px3::dsp::HalfbandDecimator& decimator,
                        const MainPhase& main,
                        double ratio,
                        double index);
    double renderHardSync(const MainPhase& main);
    double renderOrgan(const RenderContext& context, const MainPhase& main);
    double renderDigital(const MainPhase& main);
    double renderPhysical(const MainPhase& main);
    double renderRob(const RenderContext& context, const MainPhase& main);
    double renderPx3(const MainPhase& main);

    OscillatorSettings oscillatorSettings;
    DerivedCurves target;
    DerivedCurves previous;
    bool derivedValid { false };
    int rampLength { 1 };
    int rampPosition { 1 };
    float currentRamp { 1.0f };

    double sampleRate { 48000.0 };
    double whiteScale { 1.0 };
    double phase { 0.0 };
    px3::dsp::NoiseStream noise;

    int activeMode { 0 };
    int fadingMode { -1 };
    int pendingMode { -1 };
    int fadeLength { 240 };
    int fadeRemaining { 0 };

    std::array<px3::dsp::LatencyDelay, px3::oscillatorModeCount> plainDelays;
    std::array<px3::dsp::DcBlocker, px3::oscillatorModeCount> dcBlockers;

    // SAW / SQUARE / TRIANGLE / PWM
    px3::dsp::BlepLine sawLine, squareLine, triangleLine, pwmLine;

    // NOISE / PINK NOISE
    float noiseColorState { 0.0f };
    float pinkColorState { 0.0f };
    px3::dsp::PinkFilter pinkFilter;

    // SUPER SAW
    std::array<double, kSuperSawVoices> superSawPhases { {} };
    std::array<double, kSuperSawVoices> superSawDrift { {} };
    std::array<px3::dsp::BlepLine, kSuperSawVoices> superSawLines;
    std::array<px3::dsp::Adaa, kSuperSawVoices> superSawClips;
    double driftCoeff { 0.0 };
    double driftNorm { 1.0 };

    // WAVETABLE
    px3::WavetableReader wavetableReader;
    float smoothedWtPosition { 0.0f };
    float wtPositionCoeff { 1.0f };

    // ADDITIVE / ISAAC
    std::array<double, kHarmonicCount> additivePhases { {} };
    std::array<double, kHarmonicCount> isaacPhases { {} };
    double isaacShimmerPhase { 0.0 };
    px3::dsp::Adaa isaacClip;

    // FORMANT
    float formantSourceState { 0.0f };
    std::array<float, 3> formantY1 { {} };
    std::array<float, 3> formantY2 { {} };
    px3::dsp::Adaa formantClip;

    // FM
    double fmModulatorPhase { 0.0 };
    px3::dsp::HalfbandDecimator fmDecimator;

    // HARD SYNC
    double syncSlavePhase { 0.0 };
    px3::dsp::BlepLine syncLine;
    px3::dsp::Adaa syncClip;

    // ORGAN
    std::array<double, kHarmonicCount> organPhases { {} };
    double organClickPhase { 0.0 };
    px3::dsp::Adaa organClip;

    // DIGITAL
    int digitalHoldCounter { 0 };
    double digitalHeld { 0.0 };
    px3::dsp::Adaa digitalClip;

    // PHYSICAL
    std::array<double, kPhysicalModes> physicalPhases { {} };
    std::array<double, kPhysicalModes> physicalEnvelopes { {} };
    px3::dsp::Adaa physicalClip;

    // ROB
    std::array<double, kRobPhases> robPhases { {} };
    double robTransient { 0.0 };
    px3::dsp::Adaa robBodyClip, robEdgeClip, robOutClip;

    // PX3
    double px3ModulatorPhase { 0.0 };
    px3::dsp::HalfbandDecimator px3Decimator;
    px3::dsp::BlepLine px3SawLine;
    px3::dsp::Adaa px3SawClip, px3IsaacClip, px3OutClip;
    std::array<double, kHarmonicCount> px3IsaacPhases { {} };
    double px3ShimmerPhase { 0.0 };
    double px3MovePhase { 0.0 };
    px3::dsp::LatencyDelay px3IsaacDelay;
};
