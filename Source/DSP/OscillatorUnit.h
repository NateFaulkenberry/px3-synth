#pragma once

#include "OscillatorMode.h"
#include "OscillatorTypes.h"

#include <array>
#include <vector>
#include <cstdint>

class OscillatorUnit
{
public:
    // Lowest note frequency the Karplus delay must represent.
    static constexpr double kKarplusLowestFrequencyHz = 20.0;

    // Allocates sample-rate dependent storage. Must be called off the audio
    // thread before any note is rendered; SynthVoice does so from
    // setCurrentPlaybackSampleRate, which JUCE drives from prepareToPlay.
    void prepare(double sampleRate);

    struct RenderContext
    {
        double currentAngle { 0.0 };
        double currentFrequencyHz { 440.0 };
        int noteAgeSamples { 0 };
        float pitchRatio { 1.0f };
        float modWheelNorm { 0.0f };
        float pwmModWheelNorm { 0.0f };
    };

    void setSettings(const OscillatorSettings& settings);
    void resetForNote(double sampleRate, double currentFrequencyHz);
    float nextDeterministicNoise();
    float renderSample(double sampleRate, const RenderContext& context);

private:
    // Everything a mode derives from its macro controls, precomputed.
    //
    // The render functions used to call std::pow on the macros on every sample
    // - up to sixteen times per sample in the additive modes - for values that
    // only change when the user moves a control. Profiling PX3 mode attributed
    // 30% of all CPU to powf alone. The macros are refreshed once per block, so
    // these are recomputed in setSettings and only when the settings actually
    // differ, which makes a held note cost nothing at all here.
    struct HarmonicSet
    {
        std::array<float, 8> amplitude { {} };
        std::array<float, 8> ratio { {} };
        float norm { 0.0f };
    };

    struct DerivedCurves
    {
        float superSawSpread { 0.0f };
        float superSawWidth { 0.0f };
        float superSawEdgeSoft { 1.0f };
        // Kept as double: the original computed pow(2.0, ...) in double and
        // multiplied the frequency by it in double. Narrowing to float here
        // would change the rendered pitch in the last bits.
        std::array<double, 7> superSawRatios { {} };

        float pwmWidthCurve { 0.0f };

        float additiveRolloff { 0.0f };
        float additiveOddEven { 0.0f };
        HarmonicSet additiveStatic;   // inharmonicity fixed at zero
        HarmonicSet additiveDynamic;  // inharmonicity driven by macro C
        HarmonicSet formant;
        HarmonicSet organ;

        float fmRatio { 1.0f };
        float fmIndex { 0.0f };
        float fmOutputScale { 1.0f };

        float hardSyncRatio { 1.0f };
        float hardSyncDrive { 1.0f };

        float karplusDecay { 0.0f };
        float karplusBrightness { 0.0f };

        float organClick { 0.0f };
        float organClickDecay { 0.0f };

        int digitalBitDepth { 2 };
        int digitalHoldSamples { 1 };
        double digitalAliasFold { 1.0 }; // double: the original folded in double
        float digitalSteps { 4.0f };
        float digitalCrushSteps { 2.0f };

        float physicalDamping { 0.0f };
        float physicalMaterial { 1.0f };

        float robTrans { 0.0f };
        float robBody { 0.0f };
        float robChaos { 0.0f };

        float px3Morph { 0.0f };
        float px3Character { 0.0f };
        float px3Movement { 0.0f };

        float wavetablePos { 0.0f };

        // Per-mode output trim that renderSample applies after the mode switch.
        float modeGainTrim { 1.0f };
    };

    void updateDerivedCurves();
    static HarmonicSet buildHarmonicSet(const std::array<float, 8>& harmonics,
                                        float rolloffBias,
                                        float oddEvenBias,
                                        float inharmonicity);
    static float readHarmonicSum(double currentAngle, const HarmonicSet& set);

    float renderPinkNoise(float white);
    float renderSuperSaw(double sampleRate, const RenderContext& context);
    float renderPwm(const RenderContext& context) const;
    float renderAdditive(const RenderContext& context, bool dynamic);
    float renderFm(double sampleRate, const RenderContext& context);
    float renderHardSync(double sampleRate, const RenderContext& context);
    float renderKarplus(const RenderContext& context);
    float renderOrgan(const RenderContext& context);
    float renderDigital(double sampleRate, const RenderContext& context);
    float renderPhysical(double sampleRate, const RenderContext& context);
    float renderRobOsc(double sampleRate, const RenderContext& context);
    float renderPx3(double sampleRate, const RenderContext& context);

    OscillatorSettings oscillatorSettings;
    DerivedCurves derived;
    bool derivedValid { false };

    std::array<double, 7> superSawAngles { { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 } };
    std::array<float, 7> superSawOffsets { { -0.22f, -0.14f, -0.07f, 0.0f, 0.07f, 0.14f, 0.22f } };
    std::array<float, 7> superSawDrift { { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } };

    std::array<float, 7> pinkState { { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } };
    uint32_t noiseSeed { 0x13579BDFu };
    float noiseColorState { 0.0f };
    float pinkColorState { 0.0f };

    double fmModAngle { 0.0 };
    double syncMasterAngle { 0.0 };
    double syncSlaveAngle { 0.0 };

    // Sized from the sample rate rather than fixed: the Karplus delay length is
    // sampleRate / lowest supported note, and resetForNote floors the note at
    // kKarplusLowestFrequencyHz, so that product is an exact bound. A fixed
    // 32768-float array cost 128 KB per oscillator - 24 MB across the voice pool
    // - to serve a worst case of 2400 samples at 48 kHz.
    std::vector<float> karplusBuffer;
    int karplusWriteIndex { 0 };
    int karplusDelaySamples { 220 };
    float karplusLastSample { 0.0f };

    int digitalHoldCounter { 0 };
    int digitalHoldSamples { 1 };
    float digitalHeldSample { 0.0f };

    std::array<double, 4> physicalPhase { { 0.0, 0.0, 0.0, 0.0 } };
    std::array<float, 4> physicalState { { 0.0f, 0.0f, 0.0f, 0.0f } };
};
