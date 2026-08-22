#pragma once

#include <JuceHeader.h>

#include "PianoKeyboard.h"
#include "SynthSound.h"
#include "SynthVoice.h"

#include <array>
#include <atomic>
#include <vector>

class SynthProjectAudioProcessor final : public juce::AudioProcessor
{
public:
    struct MidiStatus
    {
        int noteNumber { -1 };
        int velocity { 0 };
        bool noteOn { false };
    };

    SynthProjectAudioProcessor();
    ~SynthProjectAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    std::array<bool, PianoKeyboard::totalKeys> copyActiveNoteStates() const;
    std::array<float, PianoKeyboard::totalKeys> copyActiveNoteVelocities() const;
    MidiStatus copyMidiStatus() const;

    juce::AudioParameterFloat& getOscSineParam() const;
    juce::AudioParameterFloat& getOscSawParam() const;
    juce::AudioParameterFloat& getOscSquareParam() const;
    juce::AudioParameterFloat& getFilterCutoffParam() const;
    juce::AudioParameterFloat& getFilterResonanceParam() const;
    juce::AudioParameterFloat& getAttackParam() const;
    juce::AudioParameterFloat& getDecayParam() const;
    juce::AudioParameterFloat& getSustainParam() const;
    juce::AudioParameterFloat& getReleaseParam() const;
    juce::AudioParameterFloat& getMasterGainParam() const;

    juce::AudioParameterFloat& getRobAmountParam() const;
    juce::AudioParameterFloat& getIsaacAmountParam() const;
    juce::AudioParameterChoice& getGranularSyncDivisionParam() const;
    juce::AudioParameterFloat& getReverbAmountParam() const;
    juce::AudioParameterChoice& getReverbAlgorithmParam() const;
    juce::AudioParameterInt& getPitchBendRangeParam() const;

    float copyPitchBendNormalized() const;
    float copyModWheelNormalized() const;
    float copyPitchBendActivity() const;
    float copyModWheelActivity() const;

    void setPitchBendNormalizedFromUI(float normalized);
    void setModWheelNormalizedFromUI(float normalized);

private:
    struct Grain
    {
        bool active { false };
        float readPos { 0.0f };
        float increment { 1.0f };
        int ageSamples { 0 };
        int lengthSamples { 1 };
        float gain { 0.0f };
        float pan { 0.5f };
    };

    static constexpr int maxGrains = 24;

    void updateActiveNotesFromMidi(const juce::MidiBuffer& midiMessages);
    void clearAllActiveNotes();
    void incrementNoteCount(std::size_t index);
    void decrementNoteCount(std::size_t index);
    SubtractiveSettings currentSubtractiveSettings() const;
    EnvelopeSettings currentEnvelopeSettings() const;

    void prepareIsaacEngine(double sampleRate);
    void prepareReverbEngine(double sampleRate);
    void updateTransportState();
    float processRobSample(float x, int channel, float robAmount);
    void processIsaacGranularSample(float inL,
                                    float inR,
                                    float amount,
                                    int syncDivisionIndex,
                                    float& outL,
                                    float& outR);
    void spawnIsaacGrain(float amount, int syncDivisionIndex);
    float readDelaySample(int channel, float readPos) const;
    void processReverbSampleFrame(float inL, float inR, float amount, int algorithmIndex, float& outL, float& outR);
    float readMoonDelaySample(int channel, float readPos) const;

    juce::Synthesiser synth;

    juce::AudioParameterFloat* oscSineParam { nullptr };
    juce::AudioParameterFloat* oscSawParam { nullptr };
    juce::AudioParameterFloat* oscSquareParam { nullptr };
    juce::AudioParameterFloat* filterCutoffParam { nullptr };
    juce::AudioParameterFloat* filterResonanceParam { nullptr };
    juce::AudioParameterFloat* attackParam { nullptr };
    juce::AudioParameterFloat* decayParam { nullptr };
    juce::AudioParameterFloat* sustainParam { nullptr };
    juce::AudioParameterFloat* releaseParam { nullptr };
    juce::AudioParameterFloat* masterGainParam { nullptr };
    juce::AudioParameterFloat* robAmountParam { nullptr };
    juce::AudioParameterFloat* isaacAmountParam { nullptr };
    juce::AudioParameterChoice* granularSyncDivisionParam { nullptr };
    juce::AudioParameterFloat* reverbAmountParam { nullptr };
    juce::AudioParameterChoice* reverbAlgorithmParam { nullptr };
    juce::AudioParameterInt* pitchBendRangeParam { nullptr };

    std::array<std::atomic<int>, PianoKeyboard::totalKeys> activeNoteCounts {};
    std::array<std::atomic<int>, PianoKeyboard::totalKeys> activeNoteVelocities {};
    std::atomic<int> lastMidiNote { -1 };
    std::atomic<int> lastMidiVelocity { 0 };
    std::atomic<int> lastMidiNoteOn { 0 };

    std::atomic<float> pitchBendNormalized { 0.0f };
    std::atomic<float> modWheelNormalized { 0.0f };
    std::atomic<float> pitchBendActivity { 0.0f };
    std::atomic<float> modWheelActivity { 0.0f };

    float vibratoPhaseRadians { 0.0f };

    std::array<float, 2> robDcState { { 0.0f, 0.0f } };
    std::array<float, 2> robToneState { { 0.0f, 0.0f } };

    std::array<std::vector<float>, 2> isaacDelayBuffer;
    std::array<float, 2> isaacFeedbackFilter { { 0.0f, 0.0f } };
    std::array<Grain, maxGrains> isaacGrains {};

    juce::Reverb reverb;
    std::array<std::vector<float>, 2> moonDelayBuffer;

    int isaacBufferSize { 1 };
    int isaacWritePos { 0 };
    int isaacSpawnCounter { 0 };
    float isaacPanPhase { 0.0f };
    int moonBufferSize { 1 };
    int moonWritePos { 0 };
    float moonPhase { 0.0f };

    double currentSampleRateHz { 44100.0 };
    double currentBpm { 120.0 };
    double currentTimelineSeconds { 0.0 };
};
