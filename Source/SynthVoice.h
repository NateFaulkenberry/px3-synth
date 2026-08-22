#pragma once

#include <JuceHeader.h>

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
    void setSubtractiveSettings(const SubtractiveSettings& settings);
    void setPerformanceModulation(float pitchBendNormalized,
                                  float modWheelNormalized,
                                  float pitchBendRangeSemitones,
                                  float vibratoPhaseRadians,
                                  float vibratoRateHz,
                                  float vibratoMaxDepthSemitones);

private:
    void updateAngleDelta();
    void updateFilter();

    EnvelopeSettings envelopeSettings;
    SubtractiveSettings subtractiveSettings;

    juce::ADSR adsr;
    juce::ADSR::Parameters adsrParameters;

    juce::dsp::IIR::Filter<float> lowPassFilter;

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
};
