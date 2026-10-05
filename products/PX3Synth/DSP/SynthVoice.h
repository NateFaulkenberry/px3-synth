#pragma once

#include <JuceHeader.h>

#include "AmpEnvelope.h"
#include "EnvelopeGenerator.h"
#include "VoiceModulation.h"
#include "PX3Diagnostics.h"
#include "SmoothedGain.h"
#include "EnvelopeTypes.h"
#include "BreakpointEnvelope.h"
#include "FilterTypes.h"
#include "OscillatorDsp.h"
#include "OscillatorTypes.h"
#include "OscillatorUnit.h"
#include "SubOscillator.h"
#include "SubOscTypes.h"
#include "AnalogDriftVoiceStage.h"
#include "VoiceFilter.h"

#include <array>
#include <limits>

inline constexpr int kOscillatorSourceCount = 3;
inline constexpr int kVoiceMixerSourceCount = 4;

struct SubtractiveSettings
{
    float masterGain { 0.6f };
};

class SynthVoice final : public juce::SynthesiserVoice
{
public:
    // ENV 1..4, the per-voice modulation envelopes. The processor's
    // kEnvelopeSourceCount must match (checked where both are visible).
    static constexpr int kModEnvelopeCount = 4;

    bool canPlaySound(juce::SynthesiserSound* sound) override;

    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int pitchWheel) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void pitchWheelMoved(int newPitchWheelValue) override;
    void controllerMoved(int controllerNumber, int newControllerValue) override;

    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;

    // JUCE calls this from Synthesiser::setCurrentPlaybackSampleRate, i.e. from
    // prepareToPlay and never from the audio callback, so it is the correct
    // place to allocate sample-rate dependent DSP storage.
    void setCurrentPlaybackSampleRate(double newRate) override;

    void setAmpEnvelope(const EnvelopeSettings& settings);
    // The amp envelope's last output, for the onset capture.
    float currentAmpEnvelopeLevel() const noexcept { return currentAmpEnvelopeValue; }
    // The attack the voice is holding, and how long its envelope thinks the
    // note has been running. Together these separate "the voice was given the
    // wrong settings" from "the voice has the right settings but its envelope
    // is in the wrong place".
    float currentAmpAttackSeconds() const noexcept { return envelopeSettings.attackSeconds; }
    double currentAmpHeldSeconds() const noexcept { return ampEnvelope.heldSecondsForDebug(); }

    // For the envelope visualisations: where this voice's envelopes are, and
    // when the voice was started, so the newest one can be picked to draw.
    EnvelopePosition currentAmpEnvelopePosition() const noexcept
    {
        return ampEnvelope.currentPosition();
    }

    EnvelopePosition currentModEnvelopePosition(int envIndex) const noexcept
    {
        return juce::isPositiveAndBelow(envIndex, kModEnvelopeCount)
                   ? modEnvelopeGenerators[static_cast<std::size_t>(envIndex)].currentPosition()
                   : EnvelopePosition {};
    }
    std::uint32_t noteStartSequence() const noexcept { return startSequence; }
    void setAmpEnvelopeEnabled(bool shouldEnable);
    // The shaped envelopes, when they are more than four numbers can describe.
    // Kept alongside the ADSR setters rather than replacing them: an envelope
    // that is still plain ADSR is driven by its parameters, so most of the time
    // these are never called.
    void setAmpEnvelopeShape(const px3::BreakpointEnvelope& envelope);
    void setModEnvelopeShapes(const std::array<px3::BreakpointEnvelope, kModEnvelopeCount>& envelopes);

    void setModEnvelopeSettings(const std::array<EnvelopeSettings, kModEnvelopeCount>& settings,
                                const std::array<bool, kModEnvelopeCount>& enabled);
    float getModEnvelopeValue(int envIndex) const;
    void setFilterSettings(const std::array<FilterSettings, kFilterInstanceCount>& settings);
    // SERIES runs filter 1 into filter 2; PARALLEL feeds both the same input
    // and crossfades their outputs by balance (0 = filter 1, 1 = filter 2).
    void setFilterRouting(bool parallel, float balance);
    void setSubtractiveSettings(const SubtractiveSettings& settings);
    void setSubOscillatorSettings(const SubOscSettings& settings);
    // How many samples the processor's next control block spans. Settings
    // pushed for that block ramp across exactly this many.
    void setControlBlockLength(int samples) noexcept { controlBlockLength = juce::jmax(1, samples); }
    void setOscillatorLayerSettings(const std::array<OscillatorLayerSettings, kOscillatorSourceCount>& settings);
    // Adds this voice's own envelopes to the destinations that live in it.
    // Called after the settings above, every block.
    void setVoiceModulationPlan(const px3::synth::VoiceModulationPlan& plan);
    void setPerformanceModulation(float pitchBendNormalized,
                                  float modWheelNormalized,
                                  float pitchBendRangeSemitones,
                                  float vibratoPhaseRadians,
                                  float vibratoRateHz,
                                  float vibratoMaxDepthSemitones);
    // Retires a voice under the release-tail budget without truncating it.
    // The voice keeps rendering through a short cosine fade to silence and then
    // tears down exactly like a naturally finished release. A hard
    // stopNote(false) here would step the output straight to zero from whatever
    // level the tail happened to be at, which is an audible click.
    void beginFastRelease();
    bool isFastReleasing() const;

    void setVoiceIndex(int index);
    // ANALOG's per-block control state (see AnalogDriftVoiceStage.h).
    void setAnalogDriftState(float globalAmount,
                             bool bypass,
                             const AnalogDriftSharedState& sharedState,
                             const AnalogDriftVoiceVariation& variation,
                             const AnalogDriftTuning& tuningState);
    float getCurrentAmpEnvelopeValue() const;
    float getLastBlockPeak() const;
    float getLastBlockSourcePeak(int sourceIndex) const;
    int getNoteAgeSamples() const;

private:
    void updateAngleDelta();
    void retireVoice();
    // Every per-sample smoothing constant in the voice, rederived from the
    // value it had at 48 kHz for the rate actually running.
    void updateRateDependentCoefficients(double sampleRate);

#if PX3_DIAGNOSTICS
    // Temporary signal-path isolation support; compiled out of plugin builds.
    void diagNoteEnvelopeInactiveClear(int sampleIndex);

    float diagPrevEnv { 0.0f };
    float diagPrevVoiceGain { 0.0f };
    float diagPrevVoiceGain2 { 0.0f };
    int diagVoiceGainHistory { 0 };
    float diagLastVoiceOut { 0.0f };
    bool diagHasPrevEnv { false };
    bool diagHasPrevVoiceGain { false };
    bool diagMarkStart { false };
    bool diagMarkNoteOff { false };
    float diagPrevMasterGain { 0.0f };
    bool diagHasPrevMasterGain { false };
#endif

    // Cached control settings for this voice. The processor refreshes these
    // every block so render code can run branch-light in the inner loop.
    EnvelopeSettings envelopeSettings;
    std::array<FilterSettings, kFilterInstanceCount> filterSettings;

    // The routing is BLENDED rather than switched, per sample. Moving filter 2's
    // input from filter 1's output to the dry source in one step is a
    // discontinuity in its state; ramping it is not. At a blend of exactly
    // zero the maths reduces to the old serial chain bit for bit.
    static constexpr double kFilterRoutingSmoothingSeconds = 0.015;
    float filterParallelTarget { 0.0f };
    float filterParallelCurrent { 0.0f };
    float filterBalanceTarget { 0.5f };
    float filterBalanceCurrent { 0.5f };
    float filterRoutingCoeff { 1.0f };
    SubtractiveSettings subtractiveSettings;
    SubOscSettings subOscillatorSettings;
    std::array<OscillatorLayerSettings, kOscillatorSourceCount> oscillatorLayerSettings;
    // Which oscillators the envelope plan modulated last block: their settings
    // are pushed by setVoiceModulationPlan alone (see setOscillatorLayerSettings).
    std::array<bool, kOscillatorSourceCount> oscillatorModulatedByPlan { { false, false, false } };

    AmpEnvelope ampEnvelope;
    std::array<EnvelopeGenerator, kModEnvelopeCount> modEnvelopeGenerators;
    std::array<EnvelopeSettings, kModEnvelopeCount> modEnvelopeSettings;
    std::array<bool, kModEnvelopeCount> modEnvelopeEnabled { { true, true, true, true } };
    // The envelopes' live values, sample by sample: what in-voice modulation reads.
    std::array<float, kModEnvelopeCount> modEnvelopeValues {};
    // Each envelope's peak over the last block: what the processor's global
    // (cross-voice) read takes, so a short envelope is still seen at block
    // rate. Kept apart from the live values: written over them, every block
    // began its in-voice modulation from the last block's PEAK - a decaying
    // pluck restarted near the top of each block, by an amount set by the
    // host's buffer size.
    std::array<float, kModEnvelopeCount> modEnvelopeBlockPeaks {};
    std::array<std::array<VoiceFilter, kFilterInstanceCount>, kVoiceMixerSourceCount> sourceFilters;

    double currentAngle { 0.0 };
    double angleDelta { 0.0 };
    double baseFrequencyHz { 0.0 };
    double currentFrequencyHz { 0.0 };
    float level { 0.0f };

    float targetPitchBendNorm { 0.0f };
    float currentPitchBendNorm { 0.0f };
    float targetModWheelNorm { 0.0f };
    float currentModWheelNorm { 0.0f };
    float pitchBendRangeSemitones { 2.0f };
    float sharedVibratoPhaseRadians { 0.0f };
    float vibratoRateHz { 5.0f };
    float vibratoMaxDepthSemitones { 1.0f };
    int currentMidiNote { 60 };

    std::array<OscillatorUnit, kOscillatorSourceCount> oscillatorUnits;
    // Tuning and Pitch Mod per oscillator, ramped across each control block.
    std::array<double, kOscillatorSourceCount> sourceRatioStart { { 1.0, 1.0, 1.0 } };
    std::array<double, kOscillatorSourceCount> sourceRatioTarget { { 1.0, 1.0, 1.0 } };
    std::array<double, kOscillatorSourceCount> sourceRatioCurrent { { 1.0, 1.0, 1.0 } };
    int sourceRatioRampPosition { 1 };
    int sourceRatioRampLength { 1 };
    bool sourceRatiosPrimed { false };
    int controlBlockLength { 512 };
    // Per-oscillator analog slop: a smoothed random walk in -1..1, advanced once
    // per control block, with its own generator so voices never share a wander.
    std::array<float, 3> slopValue { { 0.0f, 0.0f, 0.0f } };
    // This block's slop per oscillator, so a pitch retarget inside the block
    // keeps it.
    std::array<float, 3> blockSlopCents { { 0.0f, 0.0f, 0.0f } };
    // In-voice envelope modulation, refreshed inside the block (see render).
    static constexpr int kEnvelopeControlSamples = 32;
    const px3::synth::VoiceModulationPlan* activePlan { nullptr };
    void pushFilterTargets(bool analogActive, float analogDepth);
    float envelopePlanValue(const px3::synth::VoiceModDestination& d) const noexcept;
    void refreshEnvelopeTargets(bool analogActive, float analogDepth, int rampSamples);
    std::array<float, 3> slopTarget { { 0.0f, 0.0f, 0.0f } };
    std::array<int, 3> slopHoldBlocks { { 0, 0, 0 } };
    std::uint32_t slopRandom { 0x9e3779b9u };
    // The oscillator's soft clip and the voice's, as the one curve they are in
    // series, anti-aliased - one stage per source.
    std::array<px3::dsp::Adaa, kVoiceMixerSourceCount> sourceClips;
    // ANALOG's hiss, from this voice's own stream rather than oscillator 1's.
    px3::dsp::NoiseStream analogNoise;
    float bendSmoothing { 0.06f };
    float wheelSmoothing { 0.045f };
    float tailSmoothingLow { 0.02f };
    float tailSmoothingHigh { 0.20f };
    double coefficientsSampleRate { 0.0 };
    // Memo for the per-sample pitch-bend/vibrato ratio. The exponent is
    // constant for the whole block whenever bend, mod wheel and ANALOG drift are
    // settled, which is most of the time, so this turns an exp2 per sample per
    // voice into a compare. Keyed on the exponent itself, so a changing
    // exponent still recomputes and the result is always the same value the
    // unconditional call would have produced.
    double lastPitchExponent { std::numeric_limits<double>::quiet_NaN() };
    double lastPitchRatio { 1.0 };
    std::array<bool, kOscillatorSourceCount> oscillatorAudibleForCurrentNote { { true, true, true } };
    // The sub follows the same rule as the oscillator layers: bypassing it
    // mid-note retires it for the rest of that note rather than leaving a tail
    // that resumes when it is switched back on.
    bool subAudibleForCurrentNote { true };
    std::array<float, kVoiceMixerSourceCount> releaseSmoothingState { { 0.0f, 0.0f, 0.0f, 0.0f } };
    // ANALOG's per-voice stage state: pink hiss filter, one coupling capacitor
    // per source, and the smoother for its block-rate gain variation.
    px3::analogdrift::VoiceState<kVoiceMixerSourceCount> analogStage;
    SubOscillator subOscillator;

    float currentAmpEnvelopeValue { 0.0f };
    float lastBlockPeak { 0.0f };
    std::array<float, kVoiceMixerSourceCount> lastBlockSourcePeaks { { 0.0f, 0.0f, 0.0f, 0.0f } };

    int noteAgeSamples { 0 };
    int voiceIndex { 0 };
    int releaseAgeSamples { 0 };
    int fastReleaseTotalSamples { 0 };
    int fastReleaseSamplesRemaining { 0 };
    // Master gain is a user-facing fader applied per sample inside the voice,
    // so it needs the same per-sample smoothing the mixer gains get.
    SmoothedGain masterGainSmoother;
    // Enabling or disabling a source changes the per-source normalisation, which
    // multiplies every source. Unsmoothed that is a -3 dB step mid-note.
    SmoothedGain sourceNormalisationSmoother;
    bool sourceNormalisationPrimed { false };
#if PX3_DIAGNOSTICS
    float diagPrevSourceNorm { 0.0f };
    bool diagHasPrevSourceNorm { false };
#endif
    double masterGainPreparedSampleRate { 0.0 };
    double ampEnvelopePreparedSampleRate { 0.0 };
    double modEnvelopePreparedSampleRate { 0.0 };
    double filtersPreparedSampleRate { 0.0 };
    std::uint32_t startSequence { 0u };
    // Notes this voice has started. Seeds its random streams: unlike the global
    // start sequence it restarts with the processor, so rendering the same MIDI
    // through a fresh instance produces the same audio.
    std::uint32_t notesStarted { 0u };

    // The shape the processor last handed this voice, and whether it has one.
    //
    // startNote must not rebuild the envelope from the four ADSR parameters
    // when a full shape is in use: the parameters stop being authoritative the
    // moment a curve is edited past what they can describe, and rebuilding
    // from them starts the note on a different envelope entirely.
    px3::BreakpointEnvelope shapedAmpEnvelope;
    bool hasShapedAmpEnvelope { false };
    std::array<px3::BreakpointEnvelope, kModEnvelopeCount> shapedModEnvelopes;
    bool hasShapedModEnvelopes { false };
    bool ampEnvelopeEnabled { true };

    px3::analogdrift::VoiceControl analogControl;
};
