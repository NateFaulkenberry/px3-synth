#pragma once

#include <JuceHeader.h>

#include "EnvelopeGenerator.h"
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

    void setEnvelope(const EnvelopeSettings& settings);
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
    void setVoiceIndex(int index);
    void setVibeState(float globalAmount,
                      bool bypass,
                      const VibeSharedState& sharedState,
                      const VibeVoiceVariation& variation,
                      const VibeTuning& tuningState);

private:
    void updateAngleDelta();

    // Cached control settings for this voice. The processor refreshes these
    // every block so render code can run branch-light in the inner loop.
    EnvelopeSettings envelopeSettings;
    std::array<FilterSettings, kFilterInstanceCount> filterSettings;
    SubtractiveSettings subtractiveSettings;
    SubOscSettings subOscillatorSettings;
    std::array<OscillatorLayerSettings, kOscillatorSourceCount> oscillatorLayerSettings;

    EnvelopeGenerator ampEnvelope;
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
    SubOscillator subOscillator;

    int noteAgeSamples { 0 };
    int voiceIndex { 0 };
    double ampEnvelopePreparedSampleRate { 0.0 };

    float vibeGlobalAmount { 0.0f };
    bool vibeBypass { false };
    VibeSharedState vibeShared;
    VibeVoiceVariation vibeVariation;
    VibeTuning vibeTuning;
};
