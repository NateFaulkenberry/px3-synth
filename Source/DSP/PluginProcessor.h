#pragma once

#include <JuceHeader.h>

#include "LfoGenerator.h"
#include "PianoKeyboard.h"
#include "SynthSound.h"
#include "SynthVoice.h"
#include "VibeEngine.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

/**
 * P(X3) main audio processor.
 *
 * The processor is the DAW-facing core of the plugin. It owns:
 * - AudioParameters (automation/state source of truth)
 * - Voice rendering and DSP effect processing
 * - State serialization/deserialization for DAW project restore
 * - Cross-thread handoff data used by the UI/debug console
 *
 * Data flow (simplified):
 *
 *   MIDI + virtual keyboard
 *        -> synth voice render (osc/filter/amp env)
 *        -> FX chain in user-defined module order
 *        -> output buffer
 *
 * Important architecture rule:
 * parameter/base values are persisted/automated; transient DSP-effective
 * values may include modulation (LFO etc.) and are computed at read points.
 *
 * Implementation note:
 * this class remains a single processor/orchestrator type, but its member
 * function implementations are intentionally split across multiple
 * PluginProcessor*.cpp files by responsibility (audio, MIDI, state, debug,
 * source engines, and DSP effects).
 */
class PX3SynthAudioProcessor final : public juce::AudioProcessor
{
public:
    struct MidiStatus
    {
        int noteNumber { -1 };
        int velocity { 0 };
        bool noteOn { false };
    };

    PX3SynthAudioProcessor();
    ~PX3SynthAudioProcessor() override;

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

    juce::AudioParameterFloat& getVibeAmountParam() const;
    juce::AudioParameterBool& getVibeEnabledParam() const;
    juce::AudioParameterChoice& getVibeTypeParam() const;
    juce::AudioParameterFloat& getDelayAmountParam() const;
    juce::AudioParameterChoice& getGranularSyncDivisionParam() const;
    juce::AudioParameterChoice& getGranularModeParam() const;
    juce::AudioParameterChoice& getDelayAlgorithmParam() const;
    juce::AudioParameterBool& getDelayEnabledParam() const;
    juce::AudioParameterFloat& getDelayTimeParam() const;
    juce::AudioParameterFloat& getDelayFeedbackParam() const;
    juce::AudioParameterFloat& getReverbAmountParam() const;
    juce::AudioParameterBool& getReverbEnabledParam() const;
    juce::AudioParameterChoice& getReverbAlgorithmParam() const;
    juce::AudioParameterInt& getPitchBendRangeParam() const;
    juce::AudioParameterFloat& getLfoFrequencyParam() const;
    juce::AudioParameterChoice& getLfoWaveformParam() const;
    const juce::StringArray& getLfoAssignmentDisplayNames() const;
    int getLfoAssignmentIndex() const;
    juce::String getLfoAssignmentParameterId() const;
    bool setLfoAssignmentIndex(int index, bool notifyHost = true);
    bool setLfoAssignmentByParameterId(const juce::String& parameterId, bool notifyHost = true);
    std::array<int, 3> getFxProcessingOrder() const;
    void setFxProcessingOrder(const std::array<int, 3>& order);
    void setFxProcessingOrderWithReason(const std::array<int, 3>& order,
                                        const juce::String& source,
                                        const juce::String& reason,
                                        int fromIndex = -1,
                                        int toIndex = -1);

    juce::String debugGetInstanceId() const;
    juce::String debugGetProcessorCreatedTime() const;
    juce::String debugNowTimestamp() const;
    void debugNotifyEditorCreated(void* editorPtr);
    void debugNotifyEditorDestroyed(void* editorPtr);
    void debugLogEvent(const juce::String& source,
                       const juce::String& event,
                       const juce::String& details = {});
    void debugClearEventLog();
    juce::String debugGetEventLogText() const;
    int debugGetLastSerializedStateSize() const;
    juce::String debugGetLastSerializedStateXml() const;
    juce::MemoryBlock debugGetLastSerializedStateCopy() const;
    bool debugRestoreLastSerializedState(juce::String& report);
    bool debugRoundTripCurrentState(juce::String& report);
    uint32_t debugGetModuleOrderGeneration() const;
    uint32_t debugGetModuleOrderHash() const;
    juce::String debugDescribeOrder(const std::array<int, 3>& order) const;
    float debugGetLfoPhase() const;
    float debugGetLfoCurrentValue() const;
    float debugGetLfoBaseNormalized() const;
    float debugGetLfoEffectiveNormalized() const;
    juce::String debugGetLfoAssignmentName() const;
    float debugGetVibeGlobalAmount() const;
    float debugGetVibeEffectiveAmount() const;
    bool debugGetVibeBypass() const;
    uint32_t debugGetVibeSeed() const;
    VibeTuning debugGetVibeTuning() const;
    void debugSetVibeBypass(bool shouldBypass);
    void debugSetVibeSeed(uint32_t seed);
    void debugSetVibeTuningValue(const juce::String& key, float value);
    float debugGetVibeTuningValue(const juce::String& key) const;

    float copyPitchBendNormalized() const;
    float copyModWheelNormalized() const;
    float copyPitchBendActivity() const;
    float copyModWheelActivity() const;

    void queueVirtualKeyboardNoteOn(int midiNote, float velocityNorm);
    void queueVirtualKeyboardNoteOff(int midiNote);

    void setPitchBendNormalizedFromUI(float normalized);
    void setModWheelNormalizedFromUI(float normalized);
    int getTopMenuViewIndex() const;
    void setTopMenuViewIndex(int index, bool notifyHost = true);

    juce::ValueTree createParameterStateTree() const;
    bool applyParameterStateTree(const juce::ValueTree& state, juce::String* error = nullptr);

private:
    struct Grain
    {
        bool active { false };
        bool reverse { false };
        float readPos { 0.0f };
        float increment { 1.0f };
        int ageSamples { 0 };
        int lengthSamples { 1 };
        float gain { 0.0f };
        float pan { 0.5f };
    };

    struct ReverbDelayLine
    {
        std::vector<float> buffer;
        int writePos { 0 };
        float lpState { 0.0f };
        float modPhase { 0.0f };
    };

    enum class GranularMode
    {
        classic = 0,
        cloud,
        shimmer,
        rhythmic
    };

    static constexpr int maxGrains = 48;

    void updateActiveNotesFromMidi(const juce::MidiBuffer& midiMessages);
    void clearAllActiveNotes();
    void incrementNoteCount(std::size_t index);
    void decrementNoteCount(std::size_t index);
    SubtractiveSettings currentSubtractiveSettings() const;
    FilterSettings currentFilterSettings() const;
    OscillatorSettings currentOscillatorSettings() const;
    EnvelopeSettings currentEnvelopeSettings() const;
    LfoSettings currentLfoSettings() const;

    void prepareReverbEngine(double sampleRate);
    void applyVibeTypeProfile(int typeIndex);
    int sanitizeVibeTypeIndex(int typeIndex) const;
    void updateTransportState();
    void processIsaacGranularSample(float inL,
                                    float inR,
                                    float amount,
                                    float timeControl,
                                    float feedbackControl,
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
    void spawnIsaacGrain(float amount,
                        float timeControl,
                        float feedbackControl,
                        int syncDivisionIndex,
                        GranularMode mode,
                        int rhythmicStep);
    void clearGranularDiffusionState();
    void renderActiveGranularGrains(float& wetL, float& wetR);
    void processGranularDiffusion(float& wetL, float& wetR, float diffusionAmount, float stereoAmount);
    float processAllpassSample(float x, std::vector<float>& line, int& index, float feedback) const;
    float sanitizeAudioSample(float x) const;
    float readDelaySample(int channel, float readPos) const;
    void processReverbSampleFrame(float inL, float inR, float amount, int algorithmIndex, float& outL, float& outR);
    void resizeReverbLine(ReverbDelayLine& line, int size);
    float readReverbLine(const ReverbDelayLine& line, float delaySamples) const;
    void writeReverbLine(ReverbDelayLine& line, float sample);
    float processReverbAllpass(ReverbDelayLine& line, float in, float delaySamples, float gain);
    float processReverbDelay(ReverbDelayLine& line, float in, float delaySamples);
    void buildLfoAssignableTargets();
    float lfoDepthForParameterId(const juce::String& parameterId) const;
    float applyLfoToNormalizedValue(juce::RangedAudioParameter* parameter,
                                    float baseNormalized,
                                    float lfoSignal,
                                    float* outBaseNormalized = nullptr,
                                    float* outEffectiveNormalized = nullptr) const;
    float currentLfoSignalForBlock(int numSamples);
    void updateVibeStateForBlock(int numSamples, float lfoSignal);

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
    juce::AudioParameterFloat* vibeAmountParam { nullptr };
    juce::AudioParameterBool* vibeEnabledParam { nullptr };
    juce::AudioParameterChoice* vibeTypeParam { nullptr };
    juce::AudioParameterFloat* delayAmountParam { nullptr };
    juce::AudioParameterChoice* granularSyncDivisionParam { nullptr };
    juce::AudioParameterChoice* granularModeParam { nullptr };
    juce::AudioParameterChoice* delayAlgorithmParam { nullptr };
    juce::AudioParameterBool* delayEnabledParam { nullptr };
    juce::AudioParameterFloat* delayTimeParam { nullptr };
    juce::AudioParameterFloat* delayFeedbackParam { nullptr };
    juce::AudioParameterFloat* reverbAmountParam { nullptr };
    juce::AudioParameterBool* reverbEnabledParam { nullptr };
    juce::AudioParameterChoice* reverbAlgorithmParam { nullptr };
    juce::AudioParameterFloat* reverbSizeParam { nullptr };
    juce::AudioParameterFloat* reverbDecayParam { nullptr };
    juce::AudioParameterFloat* reverbDampingParam { nullptr };
    juce::AudioParameterFloat* reverbPreDelayParam { nullptr };
    juce::AudioParameterFloat* reverbModDepthParam { nullptr };
    juce::AudioParameterFloat* reverbModRateParam { nullptr };
    juce::AudioParameterFloat* reverbWidthParam { nullptr };
    juce::AudioParameterFloat* reverbCloudFeedbackParam { nullptr };
    juce::AudioParameterFloat* reverbCloudDiffusionParam { nullptr };
    juce::AudioParameterInt* pitchBendRangeParam { nullptr };
    juce::AudioParameterFloat* lfoFrequencyParam { nullptr };
    juce::AudioParameterChoice* lfoWaveformParam { nullptr };

    struct LfoAssignableTarget
    {
        juce::String parameterId;
        juce::String displayName;
        juce::RangedAudioParameter* parameter { nullptr };
        float normalizedDepth { 0.10f };
    };

    std::vector<LfoAssignableTarget> lfoAssignableTargets;
    juce::StringArray lfoAssignmentDisplayNames;
    std::atomic<int> lfoAssignmentIndex { 0 };

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
    std::atomic<int> topMenuViewIndex { 0 };

    float vibratoPhaseRadians { 0.0f };
    LfoGenerator lfoGenerator;
    std::atomic<float> lfoPhaseForDebug { 0.0f };
    std::atomic<float> lfoCurrentValue { 0.0f };
    std::atomic<float> lfoDebugBaseNormalized { 0.0f };
    std::atomic<float> lfoDebugEffectiveNormalized { 0.0f };

    /*
     * VIBE is a correlated imperfection system. It is intentionally not a
     * single post-distortion. Shared slow processes (PSU, temperature, chaos,
     * drift) are generated once and distributed across multiple DSP points.
     */
    VibeEngine vibeEngine;
    static constexpr float kVibeDefaultOscillatorDrift = 0.55f;
    static constexpr float kVibeDefaultVoiceVariation = 0.55f;
    static constexpr float kVibeDefaultFilterVariation = 0.45f;
    static constexpr float kVibeDefaultSaturation = 0.40f;
    static constexpr float kVibeDefaultNoise = 0.25f;
    static constexpr float kVibeDefaultPsuMovement = 0.38f;
    static constexpr float kVibeDefaultVcaNonlinearity = 0.42f;
    static constexpr float kVibeDefaultWaveformAsymmetry = 0.32f;
    static constexpr float kVibeDefaultTemperatureDrift = 0.40f;
    static constexpr float kVibeDefaultCorrelatedChaos = 0.50f;

    std::atomic<uint32_t> vibeSeed { 1337u };
    std::atomic<uint32_t> vibeLastAppliedSeed { 1337u };
    std::atomic<float> vibeTuneOscDrift { kVibeDefaultOscillatorDrift };
    std::atomic<float> vibeTuneVoiceVar { kVibeDefaultVoiceVariation };
    std::atomic<float> vibeTuneFilterVar { kVibeDefaultFilterVariation };
    std::atomic<float> vibeTuneSaturation { kVibeDefaultSaturation };
    std::atomic<float> vibeTuneNoise { kVibeDefaultNoise };
    std::atomic<float> vibeTunePsu { kVibeDefaultPsuMovement };
    std::atomic<float> vibeTuneVca { kVibeDefaultVcaNonlinearity };
    std::atomic<float> vibeTuneAsym { kVibeDefaultWaveformAsymmetry };
    std::atomic<float> vibeTuneTemp { kVibeDefaultTemperatureDrift };
    std::atomic<float> vibeTuneChaos { kVibeDefaultCorrelatedChaos };
    std::atomic<int> vibeTypeLastApplied { -1 };

    std::array<std::vector<float>, 2> isaacDelayBuffer;
    std::array<float, 2> isaacFeedbackFilter { { 0.0f, 0.0f } };
    std::array<float, 2> isaacShimmerSmooth { { 0.0f, 0.0f } };
    std::array<std::vector<float>, 2> isaacDiffusionLineA;
    std::array<std::vector<float>, 2> isaacDiffusionLineB;
    std::array<int, 2> isaacDiffusionIndexA { { 0, 0 } };
    std::array<int, 2> isaacDiffusionIndexB { { 0, 0 } };
    std::array<Grain, maxGrains> isaacGrains {};

    juce::Reverb reverb;
    std::array<ReverbDelayLine, 2> reverbPreDelayLines;
    std::array<ReverbDelayLine, 6> plateLines;
    std::array<ReverbDelayLine, 8> hallLines;
    std::array<ReverbDelayLine, 8> cloudLines;
    std::array<float, 2> plateTankState { { 0.0f, 0.0f } };
    std::array<float, 8> hallReadCache { { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } };
    std::array<float, 8> cloudReadCache { { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } };
    std::array<float, 2> reverbInputDcX1 { { 0.0f, 0.0f } };
    std::array<float, 2> reverbInputDcY1 { { 0.0f, 0.0f } };
    std::array<float, 2> reverbWetDcX1 { { 0.0f, 0.0f } };
    std::array<float, 2> reverbWetDcY1 { { 0.0f, 0.0f } };
    std::array<float, 2> reverbWetSlewState { { 0.0f, 0.0f } };

    int isaacBufferSize { 1 };
    int isaacWritePos { 0 };
    int isaacSpawnCounter { 0 };
    int isaacRhythmicStepIndex { 0 };
    int isaacRhythmicSamplesUntilNext { 0 };
    bool isaacRhythmicSwingToggle { false };
    float isaacPanPhase { 0.0f };
    float delayAmountSmoothed { 0.0f };
    float delayTimeControlSmoothed { 0.5f };
    float delayFeedbackControlSmoothed { 0.35f };
    float delayControlSmoothingCoeff { 0.0f };
    float reverbAmountSmoothed { 0.0f };
    float reverbAmountSmoothingCoeff { 0.0f };
    float delayModPhase { 0.0f };
    int lastDelayAlgorithmIndex { -1 };
    int lastGranularModeIndex { -1 };
    float reverbOutputCompGain { 1.0f };
    std::atomic<uint32_t> fxProcessingOrderPacked { 0u };
    std::atomic<uint32_t> fxOrderRevision { 0u };
    juce::String debugInstanceId;
    juce::String debugProcessorCreatedTime;
    mutable std::mutex debugStateMutex;
    juce::StringArray debugEventLogLines;
    juce::MemoryBlock debugLastSerializedState;
    juce::String debugLastSerializedStateXml;

    double currentSampleRateHz { 44100.0 };
    double currentBpm { 120.0 };
    double currentTimelineSeconds { 0.0 };
};
