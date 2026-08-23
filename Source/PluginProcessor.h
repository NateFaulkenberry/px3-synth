#pragma once

#include <JuceHeader.h>

#include "PianoKeyboard.h"
#include "SynthSound.h"
#include "SynthVoice.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
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
    juce::AudioParameterChoice& getOscillatorModeParam() const;
    juce::AudioParameterFloat& getOscMacroAParam() const;
    juce::AudioParameterFloat& getOscMacroBParam() const;
    juce::AudioParameterFloat& getOscMacroCParam() const;
    juce::AudioParameterChoice& getOscVowelParam() const;
    juce::AudioParameterFloat& getOscHarmonicParam(int harmonicIndex) const;
    juce::AudioParameterFloat& getFilterCutoffParam() const;
    juce::AudioParameterFloat& getFilterResonanceParam() const;
    juce::AudioParameterChoice& getFilterTypeParam() const;
    juce::AudioParameterFloat& getAttackParam() const;
    juce::AudioParameterFloat& getDecayParam() const;
    juce::AudioParameterFloat& getSustainParam() const;
    juce::AudioParameterFloat& getReleaseParam() const;
    juce::AudioParameterFloat& getMasterGainParam() const;

    juce::AudioParameterFloat& getRobAmountParam() const;
    juce::AudioParameterChoice& getRobModeParam() const;
    juce::AudioParameterFloat& getIsaacAmountParam() const;
    juce::AudioParameterChoice& getGranularSyncDivisionParam() const;
    juce::AudioParameterChoice& getDelayAlgorithmParam() const;
    juce::AudioParameterFloat& getDelayTimeParam() const;
    juce::AudioParameterFloat& getDelayFeedbackParam() const;
    juce::AudioParameterFloat& getReverbAmountParam() const;
    juce::AudioParameterChoice& getReverbAlgorithmParam() const;
    juce::AudioParameterChoice& getSourceEngineParam() const;
    juce::AudioParameterFloat& getImagePositionParam() const;
    juce::AudioParameterFloat& getImageAnimateParam() const;
    juce::AudioParameterFloat& getImageRateParam() const;
    juce::AudioParameterChoice& getImageAnimModeParam() const;
    juce::AudioParameterChoice& getImageAnimSyncParam() const;
    juce::AudioParameterChoice& getImageTargetParam() const;
    juce::AudioParameterFloat& getAudioPositionParam() const;
    juce::AudioParameterFloat& getAudioGrainParam() const;
    juce::AudioParameterFloat& getAudioTextureParam() const;
    juce::AudioParameterFloat& getAudioAnimateParam() const;
    juce::AudioParameterFloat& getAudioRateParam() const;
    juce::AudioParameterChoice& getAudioAnimModeParam() const;
    juce::AudioParameterChoice& getAudioAnimSyncParam() const;
    juce::AudioParameterInt& getPitchBendRangeParam() const;

    float copyPitchBendNormalized() const;
    float copyModWheelNormalized() const;
    float copyPitchBendActivity() const;
    float copyModWheelActivity() const;

    void queueVirtualKeyboardNoteOn(int midiNote, float velocityNorm);
    void queueVirtualKeyboardNoteOff(int midiNote);

    void setPitchBendNormalizedFromUI(float normalized);
    void setModWheelNormalizedFromUI(float normalized);

    void requestImageLoadAsync(const juce::File& imageFile);
    std::shared_ptr<ImageWavetable> buildImageWavetableFromImage(const juce::Image& sourceImage) const;
    int getImageLoadRequestSerial() const;
    void notifyImageLoadError();
    void completeImageLoad(int serial, std::shared_ptr<ImageWavetable> wavetable, const juce::Image& preview, const juce::String& sourcePath);
    bool copyImagePreview(juce::Image& imageOut) const;
    bool hasLoadedImage() const;
    bool consumeImageLoadErrorFlag();
    float copyCurrentImagePosition() const;
    std::vector<float> copyCurrentImageWaveformPreview(int sampleCount) const;
    void requestAudioLoadAsync(const juce::File& audioFile);
    void notifyAudioLoadError();
    void completeAudioLoad(int serial, std::shared_ptr<AudioSourceData> source, const juce::String& sourcePath);
    bool consumeAudioLoadErrorFlag();
    bool copyCurrentAudioWaveformPreview(std::vector<float>& waveformOut) const;
    float copyCurrentAudioPosition() const;
    bool hasLoadedAudio() const;

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
    OscillatorSettings currentOscillatorSettings() const;
    EnvelopeSettings currentEnvelopeSettings() const;

    void prepareIsaacEngine(double sampleRate);
    void prepareReverbEngine(double sampleRate);
    std::shared_ptr<ImageWavetable> createDefaultImageWavetable() const;
    std::shared_ptr<ImageWavetable> createImageWavetableFromImage(const juce::Image& sourceImage) const;
    void installImageWavetable(std::shared_ptr<ImageWavetable> newTable, const juce::Image& sourcePreview);
    float updateImageAnimationPosition(int samplesThisBlock);
    float computeImageTargetControlSignal(float imagePositionNorm, int samplesThisBlock);
    float updateAudioAnimationPosition(int samplesThisBlock);
    float audioSyncBeatsForIndex(int index) const;
    float imageSyncBeatsForIndex(int index) const;
    void updateTransportState();
    float processRobSample(float x, int channel, float robAmount, int modeIndex);
    void processIsaacGranularSample(float inL,
                                    float inR,
                                    float amount,
                                    int syncDivisionIndex,
                                    float& outL,
                                    float& outR);
    void processDelayAlgorithmSample(float inL,
                                     float inR,
                                     float amount,
                                     int algorithmIndex,
                                     float timeControl,
                                     float feedbackControl,
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
    juce::AudioParameterChoice* oscModeParam { nullptr };
    juce::AudioParameterFloat* oscMacroAParam { nullptr };
    juce::AudioParameterFloat* oscMacroBParam { nullptr };
    juce::AudioParameterFloat* oscMacroCParam { nullptr };
    juce::AudioParameterChoice* oscVowelParam { nullptr };
    std::array<juce::AudioParameterFloat*, 8> oscHarmonicParams { { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr } };
    juce::AudioParameterFloat* filterCutoffParam { nullptr };
    juce::AudioParameterFloat* filterResonanceParam { nullptr };
    juce::AudioParameterChoice* filterTypeParam { nullptr };
    juce::AudioParameterFloat* attackParam { nullptr };
    juce::AudioParameterFloat* decayParam { nullptr };
    juce::AudioParameterFloat* sustainParam { nullptr };
    juce::AudioParameterFloat* releaseParam { nullptr };
    juce::AudioParameterFloat* masterGainParam { nullptr };
    juce::AudioParameterFloat* robAmountParam { nullptr };
    juce::AudioParameterChoice* robModeParam { nullptr };
    juce::AudioParameterFloat* isaacAmountParam { nullptr };
    juce::AudioParameterChoice* granularSyncDivisionParam { nullptr };
    juce::AudioParameterChoice* delayAlgorithmParam { nullptr };
    juce::AudioParameterFloat* delayTimeParam { nullptr };
    juce::AudioParameterFloat* delayFeedbackParam { nullptr };
    juce::AudioParameterFloat* reverbAmountParam { nullptr };
    juce::AudioParameterChoice* reverbAlgorithmParam { nullptr };
    juce::AudioParameterChoice* sourceEngineParam { nullptr };
    juce::AudioParameterFloat* imagePositionParam { nullptr };
    juce::AudioParameterFloat* imageAnimateParam { nullptr };
    juce::AudioParameterFloat* imageRateParam { nullptr };
    juce::AudioParameterChoice* imageAnimModeParam { nullptr };
    juce::AudioParameterChoice* imageAnimSyncParam { nullptr };
    juce::AudioParameterChoice* imageTargetParam { nullptr };
    juce::AudioParameterFloat* audioPositionParam { nullptr };
    juce::AudioParameterFloat* audioGrainParam { nullptr };
    juce::AudioParameterFloat* audioTextureParam { nullptr };
    juce::AudioParameterFloat* audioAnimateParam { nullptr };
    juce::AudioParameterFloat* audioRateParam { nullptr };
    juce::AudioParameterChoice* audioAnimModeParam { nullptr };
    juce::AudioParameterChoice* audioAnimSyncParam { nullptr };
    juce::AudioParameterInt* pitchBendRangeParam { nullptr };

    std::array<std::atomic<int>, PianoKeyboard::totalKeys> activeNoteCounts {};
    std::array<std::atomic<int>, PianoKeyboard::totalKeys> activeNoteVelocities {};
    std::atomic<int> lastMidiNote { -1 };
    std::atomic<int> lastMidiVelocity { 0 };
    std::atomic<int> lastMidiNoteOn { 0 };
    juce::CriticalSection virtualMidiLock;
    juce::MidiBuffer virtualMidiMessages;

    std::atomic<float> pitchBendNormalized { 0.0f };
    std::atomic<float> modWheelNormalized { 0.0f };
    std::atomic<float> pitchBendActivity { 0.0f };
    std::atomic<float> modWheelActivity { 0.0f };
    std::atomic<float> currentImagePositionNorm { 0.0f };
    std::atomic<float> currentAudioPositionNorm { 0.0f };

    float vibratoPhaseRadians { 0.0f };
    float imageAnimPhase { 0.0f };
    float audioAnimPhase { 0.0f };
    float imageTargetScanPhase { 0.0f };
    float imageTargetControlSmoothed { 0.5f };
    float imageDriveScaleSmoothed { 1.0f };
    float imageGranularScaleSmoothed { 1.0f };
    float imageReverbScaleSmoothed { 1.0f };
    int imageAnimDirection { 1 };
    int audioAnimDirection { 1 };

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
    float delayModPhase { 0.0f };
    int lastDelayAlgorithmIndex { -1 };
    int moonBufferSize { 1 };
    int moonWritePos { 0 };
    float moonPhase { 0.0f };
    float reverbOutputCompGain { 1.0f };

    std::shared_ptr<const ImageWavetable> activeImageWavetable;
    std::shared_ptr<const AudioSourceData> activeAudioSource;
    std::atomic<int> imageLoadRequestSerial { 0 };
    std::atomic<bool> imageLoadErrorFlag { false };
    std::atomic<bool> imageLoadedFromDisk { false };
    std::atomic<int> audioLoadRequestSerial { 0 };
    std::atomic<bool> audioLoadErrorFlag { false };
    std::atomic<bool> audioLoadedFromDisk { false };
    juce::ThreadPool audioLoadThreadPool { 1 };
    juce::ThreadPool imageLoadThreadPool { 1 };
    mutable std::mutex imagePreviewMutex;
    mutable std::mutex imageStateMutex;
    juce::Image imagePreview;
    juce::String lastLoadedImagePath;
    std::vector<float> audioWaveformPreview;
    juce::String lastLoadedAudioPath;

    double currentSampleRateHz { 44100.0 };
    double currentBpm { 120.0 };
    double currentTimelineSeconds { 0.0 };
};
