#pragma once

#include <JuceHeader.h>

#include "PianoKeyboard.h"
#include "SynthSound.h"
#include "SynthVoice.h"

#include <array>
#include <atomic>

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

private:
    void updateActiveNotesFromMidi(const juce::MidiBuffer& midiMessages);
    void clearAllActiveNotes();
    void incrementNoteCount(std::size_t index);
    void decrementNoteCount(std::size_t index);
    SubtractiveSettings currentSubtractiveSettings() const;
    EnvelopeSettings currentEnvelopeSettings() const;

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

    std::array<std::atomic<int>, PianoKeyboard::totalKeys> activeNoteCounts {};
    std::array<std::atomic<int>, PianoKeyboard::totalKeys> activeNoteVelocities {};
    std::atomic<int> lastMidiNote { -1 };
    std::atomic<int> lastMidiVelocity { 0 };
    std::atomic<int> lastMidiNoteOn { 0 };
};
