#pragma once

#include <JuceHeader.h>

#include "AnalogDriftTypes.h"
#include "SmoothedGain.h"

#include <array>
#include <cmath>

// ANALOG's per-voice signal stage.
//
// ANALOG is an analog-imperfection layer, not an insert effect: it works INSIDE
// each voice, before the sources are summed (per-source nonlinearity sums
// differently from one nonlinearity on the mix), and it moves things only a
// voice has - oscillator pitch, filter cutoff and resonance, voice gain. So it
// has no processSampleFrame; the voice calls the pieces below at the points in
// its own signal path where they belong. AnalogDriftEngine produces the shared
// slow control signals once per block; this applies them per sample.
//
// Every expression here was moved verbatim out of SynthVoice when ANALOG was
// split from VIBE, and the move is held bit-exact by tests/golden (PX3Tests
// analoggolden). Reordering any arithmetic below changes those hashes.
namespace px3::analogdrift
{

// Sine saturation, after the approach used in Airwindows' Console family
// (Chris Johnson, MIT licence - see THIRD_PARTY_NOTICES.md). Below the quarter
// cycle sin(x) is very nearly x, so it is transparent at low level and folds
// smoothly as it approaches the peak; past that point it is held rather than
// allowed to fold back, which would sound like ring modulation.
inline float sineSaturate(float x)
{
    constexpr float quarterCycle = 1.57079633f;
    if (x > quarterCycle) return 1.0f;
    if (x < -quarterCycle) return -1.0f;
    return std::sin(x);
}

// Output gain that makes a sine saturator unity at a nominal operating level,
// so driving it harder trades peaks for density instead of simply turning the
// signal down. Normalising at zero level instead makes the stage quieter the
// harder it is driven; normalising by an unrelated constant makes it a
// level-dependent gain, loud on quiet signals and quiet on loud ones.
//
// gain = nominal / sin(nominal * drive), evaluated through the series expansion
// of 1/sinc so the hot path does not need a second sine per sample.
inline float saturationMakeupGain(float drive)
{
    constexpr float nominal = 0.30f;
    const auto y = nominal * drive;
    const auto y2 = y * y;
    const auto inverseSinc = 1.0f + y2 * (1.0f / 6.0f) + y2 * y2 * (7.0f / 360.0f);
    return inverseSinc / drive;
}

// Global AMOUNT -> the depth every stage below scales by. A macro control:
// low values stay subtle, the top of the range drives hard.
inline float depthForAmount(float globalAmount, bool bypass)
{
    const auto base = bypass ? 0.0f : std::pow(juce::jlimit(0.0f, 1.0f, globalAmount), 1.35f);
    return juce::jlimit(0.0f, 3.50f, base * (0.30f + 3.10f * base));
}

// The control state a voice receives from AnalogDrift once per block.
struct VoiceControl
{
    float globalAmount { 0.0f };
    bool bypass { false };
    AnalogDriftSharedState shared;
    AnalogDriftVoiceVariation variation;
    AnalogDriftTuning tuning;
};

// Pitch drift in cents for this voice (clamped to +/-60).
inline float pitchDriftCents(const VoiceControl& c, float depth)
{
    const auto driftCents = (c.shared.oscillatorDrift * c.tuning.oscillatorDrift * 32.0f
                             + c.shared.psu * c.tuning.psuMovement * 13.0f
                             + c.shared.temperature * c.tuning.temperatureDrift * 18.0f
                             + c.shared.chaos * c.tuning.correlatedChaos * 12.0f
                             + c.variation.pitchCents * c.tuning.voiceVariation) * depth;
    return juce::jlimit(-60.0f, 60.0f, driftCents);
}

// Filter drift: the cutoff multiplier and the resonance delta.
inline float cutoffMultiplier(const VoiceControl& c, float depth)
{
    const auto temperatureCutoff = c.shared.temperature * c.tuning.temperatureDrift * 0.34f;
    const auto psuCutoff = c.shared.psu * c.tuning.psuMovement * 0.22f;
    const auto voiceCutoff = c.variation.cutoffOffset * c.tuning.voiceVariation;
    const auto chaosCutoff = c.shared.chaos * c.tuning.correlatedChaos * 0.28f;
    return 1.0f + (temperatureCutoff + psuCutoff + voiceCutoff + chaosCutoff) * depth;
}

inline float resonanceDelta(const VoiceControl& c, float depth)
{
    return (c.variation.resonanceOffset * c.tuning.voiceVariation
            + c.shared.chaos * c.tuning.filterVariation * 0.16f) * depth;
}

// The voice-gain target (supply sag, temperature, per-voice tolerance).
inline float gainTarget(const VoiceControl& c, float depth)
{
    const auto gainVariation = (c.variation.gainOffset * c.tuning.voiceVariation
                                + c.shared.psu * c.tuning.psuMovement * 0.12f
                                + c.shared.temperature * c.tuning.temperatureDrift * 0.10f) * depth;
    return 1.0f + gainVariation;
}

// The VCA nonlinearity applied to a voiced sample, crossfaded by detailMix.
inline float applyVca(const VoiceControl& c, float depth, float releaseTailShape, float detailMix, float voicedSample)
{
    const auto vcaAmount = (c.tuning.vcaNonlinearity * depth
                            + c.shared.chaos * c.tuning.correlatedChaos * 0.16f * depth)
                           * (0.28f + 0.72f * releaseTailShape);
    // Normalised by its own drive so a quiet voice passes at unity.
    const auto vcaDrive = 1.0f + vcaAmount * 3.2f;
    const auto shapedSample = sineSaturate(voicedSample * vcaDrive)
                              * saturationMakeupGain(vcaDrive);
    return voicedSample + (shapedSample - voicedSample) * detailMix;
}

// The per-voice state the source stage carries: pink-noise filter, one
// coupling capacitor per source, and the gain smoother.
template <int SourceCount>
struct VoiceState
{
    std::array<float, 3> pinkPole { { 0.99765f, 0.96300f, 0.57000f } };
    std::array<float, 3> pinkGain { { 0.0990460f, 0.2965164f, 1.0526913f } };
    std::array<float, 3> pinkState { { 0.0f, 0.0f, 0.0f } };
    std::array<float, SourceCount> couplingX1 {};
    std::array<float, SourceCount> couplingY1 {};
    float couplingCoeff { 0.999f };
    SmoothedGain gainSmoother;
    bool gainPrimed { false };

    // ~12 Hz coupling capacitor: blocks DC without touching the bass.
    void prepareCoupling(double sampleRate)
    {
        couplingCoeff = std::exp(-2.0f * juce::MathConstants<float>::pi * 12.0f
                                 / static_cast<float>(juce::jmax(1.0, sampleRate)));
    }

    // Pinking poles, kept at the frequencies they have at 48 kHz, each branch
    // keeping its DC gain.
    void preparePink(double sampleRate, double referenceSampleRate)
    {
        static constexpr std::array<double, 3> kPoles { { 0.99765, 0.96300, 0.57000 } };
        static constexpr std::array<double, 3> kGains { { 0.0990460, 0.2965164, 1.0526913 } };
        for (std::size_t i = 0; i < kPoles.size(); ++i)
        {
            const auto pole = std::pow(kPoles[i], referenceSampleRate / sampleRate);
            pinkPole[i] = static_cast<float>(pole);
            pinkGain[i] = static_cast<float>(kGains[i] * (1.0 - pole) / (1.0 - kPoles[i]));
        }
    }

    // One source through the stage: asymmetry, sine saturation, correlated
    // chaos, pink hiss and the coupling capacitor, crossfaded by detailMix.
    // `noise` supplies one white sample per call (the voice's own stream).
    template <typename Noise>
    float processSource(const VoiceControl& c, float depth, float releaseTailShape, float detailMix,
                        float inSample, float noiseScale, int sourceSlot, Noise& noise)
    {
        const auto asym = (c.tuning.waveformAsymmetry * (0.35f + 0.65f * c.variation.asymmetryBias)) * depth;
        const auto sat = (c.tuning.saturation * (0.45f + 0.55f * c.variation.saturationBias))
                         * depth
                         * (0.30f + 0.70f * releaseTailShape);
        const auto chaos = c.shared.chaos * c.tuning.correlatedChaos * 0.18f * depth * releaseTailShape;

        const auto asymShaped = inSample
                                + asym * inSample * inSample * (inSample >= 0.0f ? 0.9f : -0.7f)
                                + chaos;

        // Sine saturation normalised by its own drive so the small-signal gain
        // is unity.
        const auto drive = 1.0f + sat * 3.4f;
        auto stageSample = sineSaturate(asymShaped * drive) * saturationMakeupGain(drive);

        // Pink-weighted hiss, strictly PROPORTIONAL to the amount: a fixed
        // floor made the hiss jump 86 dB the instant the stage engaged.
        const auto white = noise.white();
        pinkState[0] = pinkPole[0] * pinkState[0] + white * pinkGain[0];
        pinkState[1] = pinkPole[1] * pinkState[1] + white * pinkGain[1];
        pinkState[2] = pinkPole[2] * pinkState[2] + white * pinkGain[2];
        const auto pink = (pinkState[0] + pinkState[1] + pinkState[2] + white * 0.1848f) * 0.22f;

        const auto noiseAmount = c.tuning.noise * depth * (0.55f + 0.45f * std::abs(c.shared.psu));
        const auto noiseTailScale = 0.18f + 0.82f * releaseTailShape;
        stageSample += pink
                       * (0.0218f * noiseAmount)
                       * noiseTailScale
                       * juce::jlimit(0.0f, 1.0f, noiseScale);

        // Coupling capacitor. The asymmetry term is squared and therefore
        // carries DC; a real circuit blocks it here.
        const auto slot = static_cast<std::size_t>(juce::jlimit(0, SourceCount - 1, sourceSlot));
        const auto coupled = stageSample - couplingX1[slot] + couplingCoeff * couplingY1[slot];
        couplingX1[slot] = stageSample;
        couplingY1[slot] = coupled;

        return inSample + (coupled - inSample) * juce::jlimit(0.0f, 1.0f, detailMix);
    }
};

} // namespace px3::analogdrift
