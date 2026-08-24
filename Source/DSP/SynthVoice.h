#pragma once

#include <JuceHeader.h>

#include "EnvelopeGenerator.h"
#include "EnvelopeTypes.h"
#include "FilterTypes.h"
#include "OscillatorTypes.h"
#include "OscillatorUnit.h"
#include "SubOscillator.h"
#include "SubOscTypes.h"
#include "VoiceFilter.h"

#include <array>

struct SubtractiveSettings
{
    float sineMix { 1.0f };
    float sawMix { 0.0f };
    float squareMix { 0.0f };
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
    void setFilterSettings(const FilterSettings& settings);
    void setSubtractiveSettings(const SubtractiveSettings& settings);
    void setSubOscillatorSettings(const SubOscSettings& settings);
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

    // Cached control settings for this voice. The processor refreshes these
    // every block so render code can run branch-light in the inner loop.
    EnvelopeSettings envelopeSettings;
    FilterSettings filterSettings;
    SubtractiveSettings subtractiveSettings;
    SubOscSettings subOscillatorSettings;
    OscillatorSettings oscillatorSettings;

    EnvelopeGenerator ampEnvelope;
    VoiceFilter voiceFilter;

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
    SubOscillator subOscillator;

    int noteAgeSamples { 0 };
    int voiceIndex { 0 };

    float vibeGlobalAmount { 0.0f };
    bool vibeBypass { false };
    VibeSharedState vibeShared;
    VibeVoiceVariation vibeVariation;
    VibeTuning vibeTuning;
};
