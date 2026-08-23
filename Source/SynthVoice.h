#pragma once

#include <JuceHeader.h>

#include "AudioSourceData.h"
#include "ImageWavetable.h"

#include <memory>

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
    float imageMix { 0.35f };
};

enum class ExternalSourceMode
{
    image = 0,
    audio = 1
};

struct AudioGranularSettings
{
    bool enabled { false };
    float position { 0.5f };
    float grainSize { 0.5f };
    float texture { 0.4f };
    int rootMidiNote { 60 };
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
    void setImageWavetable(std::shared_ptr<const ImageWavetable> table, float wavetablePosition);
    void setAudioGranularSource(std::shared_ptr<const AudioSourceData> data,
                                const AudioGranularSettings& settings);
    void setExternalSourceMode(ExternalSourceMode mode);

private:
    struct AudioGrain
    {
        bool active { false };
        float sourcePos { 0.0f };
        float increment { 1.0f };
        int ageSamples { 0 };
        int durationSamples { 1 };
        float gain { 0.0f };
        float pan { 0.5f };
    };

    static constexpr int maxAudioGrains = 24;

    void updateAngleDelta();
    void updateFilter();
    void spawnAudioGrain(float pitchRatio, float textureNorm, float grainNorm);
    float renderAudioGranularSample(float pitchRatio, float textureNorm, float grainNorm);
    float readAudioSample(int channel, float position) const;

    EnvelopeSettings envelopeSettings;
    SubtractiveSettings subtractiveSettings;

    juce::ADSR adsr;
    juce::ADSR::Parameters adsrParameters;

    juce::dsp::IIR::Filter<float> lowPassFilter;
    juce::dsp::IIR::Filter<float> lowPassFilterStage2;

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

    std::shared_ptr<const ImageWavetable> imageWavetable;
    float targetImagePosition { 0.0f };
    float currentImagePosition { 0.0f };

    std::shared_ptr<const AudioSourceData> audioSourceData;
    AudioGranularSettings audioGranularSettings;
    ExternalSourceMode externalSourceMode { ExternalSourceMode::image };
    std::array<AudioGrain, maxAudioGrains> audioGrains {};
    int audioSpawnCounter { 0 };
    float audioPanPhase { 0.0f };
    int currentMidiNote { 60 };
};
