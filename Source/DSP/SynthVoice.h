#pragma once

#include <JuceHeader.h>

#include "AmpEnvelope.h"
#include "EnvelopeGenerator.h"
#include "PX3Diagnostics.h"
#include "EnvelopeTypes.h"
#include "FilterTypes.h"
#include "OscillatorTypes.h"
#include "OscillatorUnit.h"
#include "SubOscillator.h"
#include "SubOscTypes.h"
#include "VibeTypes.h"
#include "VoiceFilter.h"

#include <array>

inline constexpr int kOscillatorSourceCount = 3;
inline constexpr int kVoiceMixerSourceCount = 4;

struct SubtractiveSettings
{
    float masterGain { 0.6f };
};

class SynthVoice final : public juce::SynthesiserVoice
{
public:
    bool canPlaySound(juce::SynthesiserSound* sound) override;

    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int pitchWheel) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void pitchWheelMoved(int newPitchWheelValue) override;
    void controllerMoved(int controllerNumber, int newControllerValue) override;

    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;

    void setAmpEnvelope(const EnvelopeSettings& settings);
    void setAmpEnvelopeEnabled(bool shouldEnable);
    void setModEnvelopeSettings(const std::array<EnvelopeSettings, 3>& settings,
                                const std::array<bool, 3>& enabled);
    float getModEnvelopeValue(int envIndex) const;
    void setFilterSettings(const std::array<FilterSettings, kFilterInstanceCount>& settings);
    void setSubtractiveSettings(const SubtractiveSettings& settings);
    void setSubOscillatorSettings(const SubOscSettings& settings);
    void setOscillatorLayerSettings(const std::array<OscillatorLayerSettings, kOscillatorSourceCount>& settings);
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
    void setVibeState(float globalAmount,
                      bool bypass,
                      const VibeSharedState& sharedState,
                      const VibeVoiceVariation& variation,
                      const VibeTuning& tuningState);
    float getCurrentAmpEnvelopeValue() const;
    float getLastBlockPeak() const;
    float getLastBlockSourcePeak(int sourceIndex) const;
    int getNoteAgeSamples() const;

private:
    void updateAngleDelta();
    void retireVoice();

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
#endif

    // Cached control settings for this voice. The processor refreshes these
    // every block so render code can run branch-light in the inner loop.
    EnvelopeSettings envelopeSettings;
    std::array<FilterSettings, kFilterInstanceCount> filterSettings;
    SubtractiveSettings subtractiveSettings;
    SubOscSettings subOscillatorSettings;
    std::array<OscillatorLayerSettings, kOscillatorSourceCount> oscillatorLayerSettings;

    AmpEnvelope ampEnvelope;
    std::array<EnvelopeGenerator, 3> modEnvelopeGenerators;
    std::array<EnvelopeSettings, 3> modEnvelopeSettings;
    std::array<bool, 3> modEnvelopeEnabled { { true, true, true } };
    std::array<float, 3> modEnvelopeValues { { 0.0f, 0.0f, 0.0f } };
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
    std::array<double, kOscillatorSourceCount> oscillatorAngles { { 0.0, 0.0, 0.0 } };
    std::array<bool, kOscillatorSourceCount> oscillatorAudibleForCurrentNote { { true, true, true } };
    std::array<float, kVoiceMixerSourceCount> releaseSmoothingState { { 0.0f, 0.0f, 0.0f, 0.0f } };
    SubOscillator subOscillator;

    float currentAmpEnvelopeValue { 0.0f };
    float lastBlockPeak { 0.0f };
    std::array<float, kVoiceMixerSourceCount> lastBlockSourcePeaks { { 0.0f, 0.0f, 0.0f, 0.0f } };

    int noteAgeSamples { 0 };
    int voiceIndex { 0 };
    int releaseAgeSamples { 0 };
    int fastReleaseTotalSamples { 0 };
    int fastReleaseSamplesRemaining { 0 };
    double ampEnvelopePreparedSampleRate { 0.0 };
    double modEnvelopePreparedSampleRate { 0.0 };
    bool ampEnvelopeEnabled { true };

    float vibeGlobalAmount { 0.0f };
    bool vibeBypass { false };
    VibeSharedState vibeShared;
    VibeVoiceVariation vibeVariation;
    VibeTuning vibeTuning;
};
