#pragma once

#include <JuceHeader.h>

#include "OscillatorTypes.h"
#include "OscillatorUnit.h"

#include <array>

/**
 * ADSR envelope parameters applied per voice.
 *
 * These values are base settings from processor parameters. The processor may
 * apply modulation before sending them to voices, but voices treat received
 * values as final per-block control inputs.
 */
struct EnvelopeSettings
{
    float attackSeconds { 0.005f };
    float decaySeconds { 0.050f };
    float sustainLevel { 0.8f };
    float releaseSeconds { 0.100f };
};

struct SubtractiveSettings
{
    float sineMix { 1.0f };
    float sawMix { 0.0f };
    float squareMix { 0.0f };
    float filterCutoffHz { 10000.0f };
    float filterResonanceQ { 0.8f };
    int filterTypeIndex { 0 };
    float masterGain { 0.6f };
};

struct VibeSharedState
{
    float oscillatorDrift { 0.0f };
    float psu { 0.0f };
    float temperature { 0.0f };
    float chaos { 0.0f };
};

struct VibeVoiceVariation
{
    float pitchCents { 0.0f };
    float cutoffOffset { 0.0f };
    float resonanceOffset { 0.0f };
    float gainOffset { 0.0f };
    float asymmetryBias { 0.0f };
    float saturationBias { 0.0f };
};

struct VibeTuning
{
    float oscillatorDrift { 0.55f };
    float voiceVariation { 0.55f };
    float filterVariation { 0.45f };
    float saturation { 0.40f };
    float noise { 0.25f };
    float psuMovement { 0.38f };
    float vcaNonlinearity { 0.42f };
    float waveformAsymmetry { 0.32f };
    float temperatureDrift { 0.40f };
    float correlatedChaos { 0.50f };
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
    void setSubtractiveSettings(const SubtractiveSettings& settings);
    void setOscillatorSettings(const OscillatorSettings& settings);
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
    void updateFilter();
    void setFilterResponse(float cutoffHz, float resonanceQ, int filterTypeIndex);

    // Cached control settings for this voice. The processor refreshes these
    // every block so render code can run branch-light in the inner loop.
    EnvelopeSettings envelopeSettings;
    SubtractiveSettings subtractiveSettings;
    OscillatorSettings oscillatorSettings;

    // JUCE ADSR keeps per-voice envelope phase/level state.
    juce::ADSR adsr;
    juce::ADSR::Parameters adsrParameters;

    // Reused for multiple filter modes; stage2 enables pseudo 24 dB responses.
    juce::dsp::IIR::Filter<float> lowPassFilter;
    juce::dsp::IIR::Filter<float> lowPassFilterStage2;
    float targetFilterCutoffHz { 10000.0f };
    float targetFilterResonanceQ { 0.8f };
    float currentFilterCutoffHz { 10000.0f };
    float currentFilterResonanceQ { 0.8f };
    int activeFilterTypeIndex { 0 };
    int filterUpdateCounter { 0 };

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

    OscillatorUnit oscillatorUnit;

    int noteAgeSamples { 0 };
    int voiceIndex { 0 };

    float vibeGlobalAmount { 0.0f };
    bool vibeBypass { false };
    VibeSharedState vibeShared;
    VibeVoiceVariation vibeVariation;
    VibeTuning vibeTuning;
};
