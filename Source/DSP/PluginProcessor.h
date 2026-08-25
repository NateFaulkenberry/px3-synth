#pragma once

#include <JuceHeader.h>

#include "Delay.h"
#include "LfoGenerator.h"
#include "PianoKeyboard.h"
#include "Reverb.h"
#include "SubOscTypes.h"
#include "SynthSound.h"
#include "SynthVoice.h"
#include "Vibe.h"

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

    juce::AudioParameterBool& getOscillatorEnabledParam(int oscIndex) const;
    juce::AudioParameterFloat& getOscillatorLevelParam(int oscIndex) const;
    juce::AudioParameterFloat& getOscillatorCoarseParam(int oscIndex) const;
    juce::AudioParameterFloat& getOscillatorFineParam(int oscIndex) const;
    juce::AudioParameterChoice& getOscillatorModeParam(int oscIndex) const;
    juce::AudioParameterFloat& getOscillatorMacroAParam(int oscIndex) const;
    juce::AudioParameterFloat& getOscillatorMacroBParam(int oscIndex) const;
    juce::AudioParameterFloat& getOscillatorMacroCParam(int oscIndex) const;
    juce::AudioParameterChoice& getOscillatorVowelParam(int oscIndex) const;
    juce::AudioParameterFloat& getOscillatorHarmonicParam(int oscIndex, int harmonicIndex) const;
    juce::AudioParameterBool& getSubOscEnabledParam() const;
    juce::AudioParameterFloat& getSubOscLevelParam() const;
    juce::AudioParameterChoice& getSubOscOctaveParam() const;
    juce::AudioParameterChoice& getSubOscWaveformParam() const;
    juce::AudioParameterBool& getFilterEnabledParam(int filterIndex) const;
    juce::AudioParameterFloat& getFilterCutoffParam(int filterIndex) const;
    juce::AudioParameterFloat& getFilterResonanceParam(int filterIndex) const;
    juce::AudioParameterChoice& getFilterTypeParam(int filterIndex) const;
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
    juce::AudioParameterFloat& getFxSendGainParam() const;
    juce::AudioParameterFloat& getFxReturnGainParam() const;
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
    float debugGetOscillatorBusRms() const;
    float debugGetDryBusRms() const;
    float debugGetFxBusRms() const;
    float debugGetMasterBusRms() const;
    float debugGetInstanceCpuLoadPercent() const;
    int debugGetActiveInstanceCount() const;
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
    juce::ValueTree createPresetStateTree() const;
    bool applyParameterStateTree(const juce::ValueTree& state,
                                 juce::String* error = nullptr,
                                 bool restoreUiSessionState = true);

private:
    void updateActiveNotesFromMidi(const juce::MidiBuffer& midiMessages);
    void clearAllActiveNotes();
    void incrementNoteCount(std::size_t index);
    void decrementNoteCount(std::size_t index);
    SubtractiveSettings currentSubtractiveSettings() const;
    SubOscSettings currentSubOscillatorSettings() const;
    std::array<OscillatorLayerSettings, kOscillatorSourceCount> currentOscillatorLayerSettings() const;
    std::array<FilterSettings, kFilterInstanceCount> currentFilterSettings() const;
    EnvelopeSettings currentEnvelopeSettings() const;
    LfoSettings currentLfoSettings() const;
    VibeSettings currentVibeSettings() const;
    DelaySettings currentDelaySettings() const;
    ReverbSettings currentReverbSettings() const;

    void updateTransportState();
    void buildLfoAssignableTargets();
    float lfoDepthForParameterId(const juce::String& parameterId) const;
    float applyLfoToNormalizedValue(juce::RangedAudioParameter* parameter,
                                    float baseNormalized,
                                    float lfoSignal,
                                    float* outBaseNormalized = nullptr,
                                    float* outEffectiveNormalized = nullptr) const;
    float currentLfoSignalForBlock(int numSamples);

    juce::Synthesiser synth;

    std::array<juce::AudioParameterBool*, kOscillatorSourceCount> oscEnabledParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kOscillatorSourceCount> oscLevelParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kOscillatorSourceCount> oscCoarseParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kOscillatorSourceCount> oscFineParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterChoice*, kOscillatorSourceCount> oscModeParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kOscillatorSourceCount> oscMacroAParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kOscillatorSourceCount> oscMacroBParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kOscillatorSourceCount> oscMacroCParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterChoice*, kOscillatorSourceCount> oscVowelParams { { nullptr, nullptr, nullptr } };
    std::array<std::array<juce::AudioParameterFloat*, 8>, kOscillatorSourceCount> oscHarmonicParams { { { { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr } }, { { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr } }, { { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr } } } };
    juce::AudioParameterBool* subOscEnabledParam { nullptr };
    juce::AudioParameterFloat* subOscLevelParam { nullptr };
    juce::AudioParameterChoice* subOscOctaveParam { nullptr };
    juce::AudioParameterChoice* subOscWaveformParam { nullptr };
    std::array<juce::AudioParameterBool*, kFilterInstanceCount> filterEnabledParams { { nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kFilterInstanceCount> filterCutoffParams { { nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kFilterInstanceCount> filterResonanceParams { { nullptr, nullptr } };
    std::array<juce::AudioParameterChoice*, kFilterInstanceCount> filterTypeParams { { nullptr, nullptr } };
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
    juce::AudioParameterFloat* fxSendGainParam { nullptr };
    juce::AudioParameterFloat* fxReturnGainParam { nullptr };
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
    std::atomic<float> debugOscillatorBusRms { 0.0f };
    std::atomic<float> debugDryBusRms { 0.0f };
    std::atomic<float> debugFxBusRms { 0.0f };
    std::atomic<float> debugMasterBusRms { 0.0f };
    std::atomic<float> debugInstanceCpuLoadPercent { 0.0f };

    /*
     * VIBE is a correlated imperfection system. It is intentionally not a
     * single post-distortion. Shared slow processes (PSU, temperature, chaos,
     * drift) are generated once and distributed across multiple DSP points.
     */
    Vibe vibeComponent;
    Delay delayComponent;
    ::Reverb reverb;

    // Internal routing buses (prepared once, reused per block).
    juce::AudioBuffer<float> oscillatorBusBuffer;
    juce::AudioBuffer<float> dryBusBuffer;
    juce::AudioBuffer<float> fxBusBuffer;
    juce::AudioBuffer<float> masterBusBuffer;

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
