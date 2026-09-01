#pragma once

#include "MidiMapping.h"
#include <JuceHeader.h>

#include "Delay.h"
#include "LfoGenerator.h"
#include "FxChain.h"
#include "Doom.h"
#include "AnalogEngine.h"
#include "BusInsertChain.h"
#include "Chorus.h"
#include "Lucy.h"
#include "StereoSpread.h"
#include "Mood.h"
#include "PianoKeyboard.h"
#include "Reverb.h"
#include "SubOscTypes.h"
#include "SynthSound.h"
#include "SmoothedGain.h"
#include "SynthVoice.h"
#include "WavetableSlot.h"
#include "BreakpointEnvelope.h"
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
// AsyncUpdater is how a wavetable selection made on the audio thread's side of
// the fence gets BUILT on the message thread. Building allocates megabytes, so
// it cannot happen in processBlock; triggerAsyncUpdate is lock-free and safe to
// call from there.
class PX3SynthAudioProcessor final : public juce::AudioProcessor,
                                     private juce::AsyncUpdater
{
public:
    static constexpr int kLfoSourceCount = 3;
    static constexpr int kEnvelopeSourceCount = 3;
    static constexpr int kMixerSourceCount = 4;
    static constexpr int kPolyphonyVoiceCount = 64;

    // How many voices may SOUND at once. The pool above stays at 64 - the
    // objects are cheap to keep and their memory is already accounted for -
    // but the number allowed to run at any moment is budgeted, because the
    // cost of a voice scales directly with the sample rate while the time
    // available to compute it does not.
    //
    // Measured, all effects plus the analog console and vibe enabled, as a
    // percentage of the real-time budget at 128-sample blocks:
    //
    //             48 kHz    96 kHz
    //   64 voices  108.7%    211.2%
    //   48 voices   82.8%    161.4%
    //   32 voices   58.0%    112.5%
    //   24 voices     ~44%     ~85%
    //
    // 64 voices overran the budget at every sample rate tested, and at 96 kHz
    // even 32 did. A single fixed number cannot serve both: 48 is comfortable
    // at 48 kHz and hopeless at 96. So the budget is referenced to 48 kHz and
    // scaled by the rate the host is actually running.
    static constexpr int kSoundingVoiceBudgetAtReference = 48;
    static constexpr double kVoiceBudgetReferenceRate = 48000.0;
    static constexpr int kMinimumSoundingVoiceBudget = 16;

    static int soundingVoiceBudgetForRate(double sampleRate);
    static constexpr int kMixerChannelCount = 5;

    enum MixerSourceId
    {
        mixerSub = 0,
        mixerOsc1,
        mixerOsc2,
        mixerOsc3
    };

    enum MixerChannelId
    {
        mixerChannelSub = 0,
        mixerChannelOsc1,
        mixerChannelOsc2,
        mixerChannelOsc3,
        mixerChannelFx
    };

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
    juce::AudioParameterFloat& getOscillatorCoarseParam(int oscIndex) const;
    juce::AudioParameterFloat& getOscillatorFineParam(int oscIndex) const;
    juce::AudioParameterFloat& getOscillatorPitchParam(int oscIndex) const;
    juce::AudioParameterChoice& getOscillatorModeParam(int oscIndex) const;
    juce::AudioParameterFloat& getOscillatorMacroAParam(int oscIndex) const;
    juce::AudioParameterFloat& getOscillatorMacroBParam(int oscIndex) const;
    juce::AudioParameterFloat& getOscillatorMacroCParam(int oscIndex) const;
    juce::AudioParameterChoice& getOscillatorVowelParam(int oscIndex) const;
    juce::AudioParameterFloat& getOscillatorWtPositionParam(int oscIndex) const;
    juce::AudioParameterChoice& getOscillatorWtTableParam(int oscIndex) const;

    // Loads a factory wavetable into one oscillator's slot. Message thread: it
    // builds the table, which allocates.
    void loadFactoryWavetable(int oscIndex, int tableIndex);

    // Message thread housekeeping - frees tables the audio thread has moved
    // past. Cheap enough to call from the editor's timer.
    void collectRetiredWavetables();

    // Builds whatever each oscillator's Wavetable parameter currently names,
    // for any that have not been built yet. Message thread.
    void refreshWavetableSelections();

    // Which table an oscillator is actually PLAYING, as opposed to which one its
    // parameter names. The two differing is the whole failure mode this exists
    // to make visible.
    juce::String getLoadedWavetableName(int oscIndex) const;

    // The shaped envelopes. Index 0 is AMP ENV; 1..3 are ENV 1..3, which are
    // kept in the same array only because they are the same kind of thing - the
    // two systems stay separate everywhere it matters.
    //
    // Public because the editor owns the editing: it reads a shape, changes it,
    // and hands it back. Message thread.
    static constexpr int kShapedEnvelopeCount = 4;
    void setShapedEnvelope(int index, const px3::BreakpointEnvelope& envelope);
    px3::BreakpointEnvelope getShapedEnvelope(int index) const;

    // The shape actually being played, which is the stored one unless it is
    // still plain ADSR - in which case the parameters describe it and the
    // envelope is built from them, so a knob and a DAW automation lane both
    // still move the curve.
    px3::BreakpointEnvelope currentAmpEnvelope() const;

    //==========================================================================
    // MIDI parameter mapping. See docs/midi-mapping-design.md.
    //
    // All of it is per-instance member state: no statics, no singleton, no
    // global listener. Two instances of the plugin in one project cannot see
    // each other's mappings because there is nothing shared to see through.
    //==========================================================================

    // Arm MIDI learn for a set of parameter IDs. The next CC that MOVES is
    // assigned to all of them. An empty set disarms.
    void setMidiLearnTargets(const juce::StringArray& parameterIds);
    juce::StringArray getMidiLearnTargets() const;
    bool isMidiLearnArmed() const;

    // Which CC drives this parameter, or -1. Cheap enough for the editor to
    // ask once per knob per refresh.
    int getMidiCcForParameter(const juce::String& parameterId) const;
    void clearMidiMappingForParameter(const juce::String& parameterId);
    void clearAllMidiMappings();
    std::vector<px3::MidiMapping> getMidiMappings() const;

    // Drains what the audio thread recorded: assigns if learn is armed, then
    // writes every CC that has MOVED to its destinations.
    //
    // Public because the processor's own timer is a CALLER of this rather than
    // the mechanism - a test with no message loop drives it directly, and so
    // does anything else that needs mappings applied at a known moment.
    void applyPendingMidiMappings();

    // Called by the learn path when an assignment completes, so the editor can
    // leave Select Mode without polling for it.
    std::function<void(int ccNumber)> onMidiMappingAssigned;

    // The four parameters, for a card that has to apply them to a stored shape
    // without disturbing its curves.
    EnvelopeSettings currentAmpEnvelopeSettings() const;
    EnvelopeSettings currentModEnvelopeSettings(int envIndex) const;

    // What the four knobs SAY, whether or not the envelope is switched on.
    // currentModEnvelopeSettings answers a different question - what the voice
    // should run - and substitutes a neutral contour while bypassed, which is
    // right for the DSP and wrong for anything drawing the user's envelope.
    EnvelopeSettings envelopeParameterSettings(int envIndex) const;
    px3::BreakpointEnvelope currentModEnvelope(int envIndex) const;

    // A user table is named, not indexed: the factory list has fixed positions
    // and the user library does not. Empty means "use the factory choice".
    void setUserWavetableName(int oscIndex, const juce::String& name);
    juce::String getUserWavetableName(int oscIndex) const;

    // Imports frames, saves them to the user library and selects them. Message
    // thread - it builds, allocates and writes a file.
    bool importWavetable(int oscIndex,
                         const juce::String& name,
                         const std::vector<px3::FrameSpectrum>& frames,
                         juce::String& error);

    // Set when a preset named a user table that is not on this machine. The UI
    // reads it to say so rather than leaving the wrong sound playing silently.
    juce::String getMissingWavetableName(int oscIndex) const;

    // Message thread. A drawing-sized copy of what an oscillator is playing.
    px3::WavetableDisplay getWavetableDisplay(int oscIndex, int frames, int points) const;

    // Where the scan actually is, modulation included. The display draws this
    // next to the base value so an LFO on the scan reads as a range rather than
    // as an unexplained wobble.
    float getModulatedWavetablePosition(int oscIndex) const;

    // Is anything currently pointed at this parameter? Distinct from "is its
    // modulated value different right now" - an LFO passing through zero is
    // still assigned, and a knob whose arc vanished at every zero crossing
    // would flicker.
    bool isParameterModulated(const juce::String& parameterId) const;

    // The parameter's value with modulation applied, normalised 0..1. Returns
    // -1 when nothing is assigned to it, which is what the knobs draw as "no
    // modulation ring".
    float getModulatedNormalisedValue(juce::RangedAudioParameter& parameter) const;

    // The same value BEFORE the range clamp. Equal to the clamped one unless
    // modulation is driving the parameter past its own range, which is exactly
    // what headroom scaling exists to prevent - so the gap between them is the
    // measurement that matters.
    float getUnclampedModulatedNormalisedValue(juce::RangedAudioParameter& parameter) const;
    juce::AudioParameterFloat& getOscillatorHarmonicParam(int oscIndex, int harmonicIndex) const;
    juce::AudioParameterBool& getSubOscEnabledParam() const;
    juce::AudioParameterFloat& getSubOscPitchParam() const;
    juce::AudioParameterChoice& getSubOscOctaveParam() const;
    juce::AudioParameterChoice& getSubOscWaveformParam() const;
    juce::AudioParameterBool& getFilterEnabledParam(int filterIndex) const;
    juce::AudioParameterFloat& getFilterCutoffParam(int filterIndex) const;
    juce::AudioParameterFloat& getFilterResonanceParam(int filterIndex) const;
    juce::AudioParameterChoice& getFilterTypeParam(int filterIndex) const;

    // Comb mode's controls. Musical terms - a pitch and a decay time - rather
    // than the loop's delay length and feedback gain, which mean different
    // things at every tuning.
    juce::AudioParameterFloat& getFilterCombTuneParam(int filterIndex) const;
    juce::AudioParameterFloat& getFilterCombDecayParam(int filterIndex) const;
    juce::AudioParameterFloat& getFilterCombDampingParam(int filterIndex) const;
    juce::AudioParameterFloat& getFilterCombDispersionParam(int filterIndex) const;
    juce::AudioParameterFloat& getFilterCombDriveParam(int filterIndex) const;
    juce::AudioParameterFloat& getFilterCombMixParam(int filterIndex) const;
    juce::AudioParameterBool& getFilterCombInvertParam(int filterIndex) const;
    juce::AudioParameterFloat& getAttackParam() const;
    juce::AudioParameterFloat& getEnvelopeAttackParam(int envIndex) const;
    juce::AudioParameterFloat& getDecayParam() const;
    juce::AudioParameterFloat& getEnvelopeDecayParam(int envIndex) const;
    juce::AudioParameterFloat& getSustainParam() const;
    juce::AudioParameterFloat& getEnvelopeSustainParam(int envIndex) const;
    juce::AudioParameterFloat& getReleaseParam() const;
    juce::AudioParameterFloat& getEnvelopeReleaseParam(int envIndex) const;
    juce::AudioParameterBool& getAmpEnvEnabledParam() const;
    juce::AudioParameterBool& getEnvelopeEnabledParam(int envIndex) const;
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
    juce::AudioParameterFloat& getMixerLevelParam(int sourceIndex) const;
    juce::AudioParameterFloat& getMixerPanParam(int sourceIndex) const;
    juce::AudioParameterFloat& getMixerSendParam(int sourceIndex) const;
    juce::AudioParameterBool& getMixerMuteParam(int sourceIndex) const;
    // Polarity flip for a mixer channel. Inverting one source against another
    // is how you hear whether they are fighting each other.
    juce::AudioParameterBool& getMixerPhaseInvertParam(int sourceIndex) const;
    juce::AudioParameterBool& getFxReturnPhaseInvertParam() const;

    // ---- bus inserts ------------------------------------------------------
    // One set per bus. Indexed rather than named individually so a third bus
    // is one more entry here and one more createBusInsertParameters call, and
    // nothing else changes: see BusInsertChain.
    static constexpr int kBusInsertCount = 2;   // 0 = dry, 1 = FX
    enum BusInsertSlot { dryBusInsert = 0, fxBusInsert = 1 };

    struct BusInsertParams
    {
        juce::AudioParameterBool* eqEnabled { nullptr };
        // Only bands 1 and 4 have a type; the inner two are always bells, so
        // their entries stay null rather than being one-entry choices.
        std::array<juce::AudioParameterChoice*, px3::kEqBandCount> bandType { {} };
        std::array<juce::AudioParameterFloat*, px3::kEqBandCount> bandFreq { {} };
        std::array<juce::AudioParameterFloat*, px3::kEqBandCount> bandGain { {} };
        std::array<juce::AudioParameterFloat*, px3::kEqBandCount> bandQ { {} };

        juce::AudioParameterBool* compEnabled { nullptr };
        juce::AudioParameterFloat* compInput { nullptr };
        juce::AudioParameterFloat* compOutput { nullptr };
        juce::AudioParameterFloat* compAttack { nullptr };
        juce::AudioParameterFloat* compRelease { nullptr };
        juce::AudioParameterChoice* compRatio { nullptr };
        juce::AudioParameterFloat* compMix { nullptr };
        juce::AudioParameterBool* compLink { nullptr };
        juce::AudioParameterChoice* compMeterMode { nullptr };
    };

    const BusInsertParams& getBusInsertParams(int bus) const
    {
        return busInsertParams[static_cast<std::size_t>(juce::jlimit(0, kBusInsertCount - 1, bus))];
    }

    // Gain reduction for a bus's compressor, in positive decibels, with the
    // meter's own ballistics already applied. Read off an atomic, so the UI can
    // ask at any rate it likes without touching the audio thread.
    float getBusGainReductionDb(int bus) const
    {
        return busInserts[static_cast<std::size_t>(juce::jlimit(0, kBusInsertCount - 1, bus))]
            .getCompressor().gainReductionDb();
    }

    // The spectrum tap for a bus. Inert until a UI switches it on, so an
    // overlay nobody has opened costs the audio thread nothing.
    px3::BusAnalyser& getBusAnalyser(int bus)
    {
        return busInserts[static_cast<std::size_t>(juce::jlimit(0, kBusInsertCount - 1, bus))].getAnalyser();
    }

    // Level either side of a bus's compressor, in dBFS, for the meter when it
    // is switched away from gain reduction.
    float getBusCompressorLevelDb(int bus, bool wantInput) const
    {
        const auto& comp = busInserts[static_cast<std::size_t>(juce::jlimit(0, kBusInsertCount - 1, bus))]
                               .getCompressor();
        return wantInput ? comp.inputLevelDb() : comp.outputLevelDb();
    }

    // The EQ's own view of its response, for the curve display. Reads the live
    // processor rather than recomputing from parameters, so what is drawn is
    // what is running - the smoothing included.
    float getBusEqMagnitudeDb(int bus, float frequencyHz) const
    {
        return busInserts[static_cast<std::size_t>(juce::jlimit(0, kBusInsertCount - 1, bus))]
            .getEq().magnitudeDb(frequencyHz);
    }

    // The dry bus as a mixer channel of its own: the summed sources before the
    // FX return is added. Previously the dry path had no control at all, so the
    // only way to change the balance between dry and wet was to move every
    // source fader.
    juce::AudioParameterFloat& getDryBusGainParam() const;
    juce::AudioParameterFloat& getDryBusPanParam() const;
    juce::AudioParameterBool& getDryBusMuteParam() const;
    juce::AudioParameterBool& getDryBusSoloParam() const;
    juce::AudioParameterBool& getDryBusPhaseInvertParam() const;
    bool dryBusAudible(bool anySolo, bool anySourceSolo, bool drySolo) const;
    juce::AudioParameterBool& getMixerSoloParam(int sourceIndex) const;
    juce::AudioParameterBool& getFxReturnMuteParam() const;
    juce::AudioParameterBool& getFxReturnSoloParam() const;
    juce::AudioParameterFloat& getFxReturnPanParam() const;
    juce::AudioParameterFloat& getReverbAmountParam() const;
    juce::AudioParameterBool& getReverbEnabledParam() const;
    juce::AudioParameterChoice& getReverbAlgorithmParam() const;
    juce::AudioParameterBool& getMoodEnabledParam() const;
    juce::AudioParameterBool& getMoodFreezeParam() const;
    juce::AudioParameterFloat& getMoodMixParam() const;
    juce::AudioParameterFloat& getMoodClockParam() const;
    juce::AudioParameterFloat& getMoodWetTimeParam() const;
    juce::AudioParameterFloat& getMoodWetModifyParam() const;
    juce::AudioParameterFloat& getMoodLoopLengthParam() const;
    juce::AudioParameterFloat& getMoodLoopModifyParam() const;
    juce::AudioParameterFloat& getMoodFeedbackParam() const;
    juce::AudioParameterFloat& getMoodSpreadParam() const;
    juce::AudioParameterFloat& getMoodDegradeParam() const;
    juce::AudioParameterChoice& getMoodRoutingParam() const;
    juce::AudioParameterChoice& getMoodWetModeParam() const;
    juce::AudioParameterChoice& getMoodLoopModeParam() const;

    juce::AudioParameterBool& getDoomEnabledParam() const;
    juce::AudioParameterBool& getDoomFreezeParam() const;
    juce::AudioParameterBool& getDoomLoopActiveParam() const;
    juce::AudioParameterBool& getDoomWetActiveParam() const;
    juce::AudioParameterBool& getDoomLoopHalfParam() const;
    juce::AudioParameterBool& getDoomClockSmoothParam() const;
    juce::AudioParameterFloat& getDoomMixParam() const;
    juce::AudioParameterFloat& getDoomClockParam() const;
    juce::AudioParameterFloat& getDoomLoopLengthParam() const;
    juce::AudioParameterFloat& getDoomLoopModifyParam() const;
    juce::AudioParameterFloat& getDoomOverdubParam() const;
    juce::AudioParameterFloat& getDoomFadeParam() const;
    juce::AudioParameterFloat& getDoomWetTimeParam() const;
    juce::AudioParameterFloat& getDoomWetModifyParam() const;
    juce::AudioParameterFloat& getDoomCrossParam() const;
    juce::AudioParameterFloat& getDoomGlueParam() const;
    juce::AudioParameterFloat& getDoomEqParam() const;
    juce::AudioParameterFloat& getDoomBalanceParam() const;
    juce::AudioParameterFloat& getDoomBlendParam() const;
    juce::AudioParameterFloat& getDoomSpreadParam() const;
    juce::AudioParameterChoice& getDoomRoutingParam() const;
    juce::AudioParameterChoice& getDoomLoopModeParam() const;
    juce::AudioParameterChoice& getDoomWetModeParam() const;
    juce::AudioParameterChoice& getDoomCrossSourceParam() const;

    juce::AudioParameterBool& getLucyEnabledParam() const;
    juce::AudioParameterBool& getLucyFilterInvertParam() const;
    juce::AudioParameterBool& getLucyVerbPostParam() const;
    juce::AudioParameterBool& getLucyFreezeParam() const;
    juce::AudioParameterBool& getLucyFreezeSlushyParam() const;
    juce::AudioParameterBool& getLucyGateParam() const;
    juce::AudioParameterBool& getLucySlowParam() const;
    juce::AudioParameterFloat& getLucyGlobalParam() const;
    juce::AudioParameterFloat& getLucyLossParam() const;
    juce::AudioParameterFloat& getLucySpeedParam() const;
    juce::AudioParameterFloat& getLucyFilterParam() const;
    juce::AudioParameterFloat& getLucyFilterFreqParam() const;
    juce::AudioParameterFloat& getLucyVerbParam() const;
    juce::AudioParameterFloat& getLucyVerbDecayParam() const;
    juce::AudioParameterFloat& getLucyFreezerParam() const;
    juce::AudioParameterFloat& getLucyGateCutoffParam() const;
    juce::AudioParameterFloat& getLucyThresholdParam() const;
    juce::AudioParameterFloat& getLucyAutoGainParam() const;
    juce::AudioParameterFloat& getLucyWeightingParam() const;
    juce::AudioParameterFloat& getLucyGainParam() const;
    juce::AudioParameterFloat& getLucySpreadParam() const;
    juce::AudioParameterChoice& getLucyModeParam() const;
    juce::AudioParameterChoice& getLucyPacketsParam() const;
    juce::AudioParameterChoice& getLucySlopeParam() const;

    juce::AudioParameterBool& getChorusEnabledParam() const;
    juce::AudioParameterFloat& getChorusAmountParam() const;
    juce::AudioParameterFloat& getChorusRateParam() const;
    juce::AudioParameterFloat& getChorusDepthParam() const;
    juce::AudioParameterFloat& getChorusWidthParam() const;
    juce::AudioParameterFloat& getChorusSpreadParam() const;
    juce::AudioParameterFloat& getChorusLowCutParam() const;
    juce::AudioParameterFloat& getChorusFeedbackParam() const;
    juce::AudioParameterFloat& getChorusCharacterParam() const;
    juce::AudioParameterFloat& getChorusMixParam() const;
    juce::AudioParameterFloat& getChorusToneParam() const;
    juce::AudioParameterChoice& getChorusModeParam() const;

    juce::AudioParameterBool& getSpreadEnabledParam() const;
    juce::AudioParameterFloat& getSpreadAmountParam() const;
    juce::AudioParameterFloat& getSpreadWidthParam() const;
    juce::AudioParameterFloat& getSpreadDepthParam() const;
    juce::AudioParameterFloat& getSpreadCenterParam() const;
    juce::AudioParameterFloat& getSpreadLowWidthParam() const;
    juce::AudioParameterFloat& getSpreadHighWidthParam() const;
    juce::AudioParameterFloat& getSpreadLowFreqParam() const;
    juce::AudioParameterFloat& getSpreadHighFreqParam() const;
    juce::AudioParameterFloat& getSpreadMixParam() const;
    juce::AudioParameterFloat& getSpreadToneParam() const;
    juce::AudioParameterChoice& getSpreadModeParam() const;

    // AnalogEngine exposes only its on/off and its archetype. Every tuning
    // constant is internal and reachable through the debug console alone.
    juce::AudioParameterBool& getAnalogEnabledParam() const;
    juce::AudioParameterChoice& getAnalogProfileParam() const;
    juce::AudioParameterBool& getLfoEnabledParam() const;
    juce::AudioParameterBool& getLfoEnabledParam(int lfoIndex) const;
    juce::AudioParameterFloat& getLfoFrequencyParam() const;
    juce::AudioParameterFloat& getLfoFrequencyParam(int lfoIndex) const;
    juce::AudioParameterFloat& getLfoAmountParam() const;
    juce::AudioParameterFloat& getLfoAmountParam(int lfoIndex) const;
    juce::AudioParameterChoice& getLfoWaveformParam() const;
    juce::AudioParameterChoice& getLfoWaveformParam(int lfoIndex) const;
    juce::AudioParameterFloat& getEnvelopeAmountParam() const;
    juce::AudioParameterFloat& getEnvelopeAmountParam(int envIndex) const;
    const juce::StringArray& getLfoAssignmentDisplayNames() const;
    int getLfoAssignmentIndex() const;
    int getLfoAssignmentIndex(int lfoIndex) const;
    juce::String getLfoAssignmentParameterId() const;
    juce::String getLfoAssignmentParameterId(int lfoIndex) const;
    bool setLfoAssignmentIndex(int index, bool notifyHost = true);
    bool setLfoAssignmentIndex(int lfoIndex, int index, bool notifyHost = true);
    bool setLfoAssignmentByParameterId(const juce::String& parameterId, bool notifyHost = true);
    bool setLfoAssignmentByParameterId(int lfoIndex, const juce::String& parameterId, bool notifyHost = true);
    const juce::StringArray& getEnvelopeAssignmentDisplayNames() const;
    int getEnvelopeAssignmentIndex() const;
    int getEnvelopeAssignmentIndex(int envIndex) const;
    juce::String getEnvelopeAssignmentParameterId() const;
    juce::String getEnvelopeAssignmentParameterId(int envIndex) const;
    bool setEnvelopeAssignmentIndex(int index, bool notifyHost = true);
    bool setEnvelopeAssignmentIndex(int envIndex, int index, bool notifyHost = true);
    bool setEnvelopeAssignmentByParameterId(const juce::String& parameterId, bool notifyHost = true);
    bool setEnvelopeAssignmentByParameterId(int envIndex, const juce::String& parameterId, bool notifyHost = true);
    px3::FxOrder getFxProcessingOrder() const;
    void setFxProcessingOrder(const px3::FxOrder& order);
    void setFxProcessingOrderWithReason(const px3::FxOrder& order,
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
    juce::String debugDescribeOrder(const px3::FxOrder& order) const;
    float debugGetLfoPhase() const;
    float debugGetLfoCurrentValue() const;
    float debugGetLfoCurrentValue(int lfoIndex) const;
    float debugGetLfoBaseNormalized() const;
    float debugGetLfoEffectiveNormalized() const;
    float debugGetEnvelopeCurrentValue(int envIndex) const;
    float debugGetEnvelopeContributionNormalized(int envIndex) const;
    float debugGetEnvelopeDestinationBaseNormalized(int envIndex) const;
    float debugGetEnvelopeDestinationEffectiveNormalized(int envIndex) const;
    juce::String debugGetEnvelopeAssignmentName(int envIndex) const;
    float debugGetOscillatorBusRms() const;
    float debugGetDryBusRms() const;
    float debugGetFxBusRms() const;
    float debugGetMasterBusRms() const;
    float debugGetOscillatorBusPeak() const;
    float debugGetOscillatorBusPrePolyPeak() const;
    int debugGetOscillatorBusPrePolyClipSamples() const;
    float debugGetDryBusPeak() const;
    float debugGetFxBusPeak() const;
    float debugGetMasterBusPeak() const;
    float debugGetMasterPreOutputPeak() const;
    int debugGetMasterClipSamples() const;
    float debugGetMixerSourceRms(int sourceIndex) const;
    float debugGetMixerSourcePeak(int sourceIndex) const;
    float debugGetVoicePeak() const;
    float debugGetVoiceSourcePeak(int sourceIndex) const;
    int debugGetActiveVoiceCount() const;
    int debugGetReleasingVoiceCount() const;
    int debugGetNearSilentReleaseVoiceCount() const;
    float debugGetPolyphonyGainApplied() const;
    float debugGetPolyphonyGainTarget() const;
    float debugGetEffectiveVoiceLoad() const;
    float debugGetReleaseEnergyEquivalent() const;
    int debugGetHeldVoiceCount() const;
    bool debugGetPolyGainTailBypassActive() const;
    int debugGetReleaseVoicesPruned() const;
    float debugGetFxReturnRms() const;
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
    void debugSetAnalogTuningValue(const juce::String& key, float value);
    float debugGetAnalogTuningValue(const juce::String& key) const;
    void debugResetAnalogTuning();
    juce::String debugDescribeAnalogEngine() const;
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

    // The preset the editor last loaded. The processor does not use any of it;
    // it is carried here purely so it survives in DAW state, because the editor
    // is destroyed and rebuilt every time the window is closed and reopened.
    struct LoadedPreset
    {
        juce::String name;
        juce::String category;
        juce::String author;
        juce::String filePath;
        bool valid { false };
    };

    LoadedPreset getLoadedPreset() const;
    void setLoadedPreset(const LoadedPreset& preset);

    juce::ValueTree createParameterStateTree() const;
    juce::ValueTree createPresetStateTree() const;
    bool applyParameterStateTree(const juce::ValueTree& state,
                                 juce::String* error = nullptr,
                                 bool restoreUiSessionState = true);

private:
    bool anySourceSoloed() const;
    bool anyChannelSoloed() const;
    bool sourceMuted(int sourceIndex) const;
    bool sourceSoloed(int sourceIndex) const;
    bool sourceDryAudible(int sourceIndex, bool anySolo) const;
    bool sourceSendAudible(int sourceIndex, bool anySolo, bool anySourceSolo, bool fxSolo) const;
    bool fxReturnAudible(bool anySolo, bool anySourceSolo, bool fxSolo) const;

    void updateActiveNotesFromMidi(const juce::MidiBuffer& midiMessages);
    void clearAllActiveNotes();
    void incrementNoteCount(std::size_t index);
    void decrementNoteCount(std::size_t index);
    SubtractiveSettings currentSubtractiveSettings() const;
    SubOscSettings currentSubOscillatorSettings() const;
    std::array<OscillatorLayerSettings, kOscillatorSourceCount> currentOscillatorLayerSettings() const;
    std::array<FilterSettings, kFilterInstanceCount> currentFilterSettings() const;
    // The shape each envelope is actually running.
    //
    // While an envelope is still the four-point skeleton ADSR describes - which
    // is every preset that predates the editor - the PARAMETERS carry the four
    // times and the level, so automation and the knobs under the graph keep
    // driving it, and the stored shape carries the curves. Once a point has
    // been ADDED there is no ADSR left to describe it and the stored envelope
    // is the whole truth.

    LfoSettings currentLfoSettings() const;
    LfoSettings currentLfoSettings(int lfoIndex) const;
    VibeSettings currentVibeSettings() const;
    DelaySettings currentDelaySettings() const;
    ReverbSettings currentReverbSettings() const;
    MoodSettings currentMoodSettings() const;
    DoomSettings currentDoomSettings() const;
    LucySettings currentLucySettings() const;
    ChorusSettings currentChorusSettings() const;
    StereoSpreadSettings currentStereoSpreadSettings() const;

    void updateTransportState();
    void buildLfoAssignableTargets();
    float lfoDepthForParameterId(const juce::String& parameterId) const;
    int getAssignmentIndex(std::atomic<int> const& sourceIndex) const;
    juce::String getAssignmentParameterId(std::atomic<int> const& sourceIndex) const;
    std::atomic<int>& lfoAssignmentAtomic(int lfoIndex);
    std::atomic<int> const& lfoAssignmentAtomic(int lfoIndex) const;
    std::atomic<int>& envelopeAssignmentAtomic(int envIndex);
    std::atomic<int> const& envelopeAssignmentAtomic(int envIndex) const;
    bool setAssignmentIndex(std::atomic<int>& sourceIndex, int index, bool notifyHost, const juce::String& sourceName);
    bool setAssignmentByParameterId(std::atomic<int>& sourceIndex,
                                    const juce::String& parameterId,
                                    bool notifyHost,
                                    const juce::String& sourceName);
    float applyModulationToNormalizedValue(juce::RangedAudioParameter* parameter,
                                           float baseNormalized,
                                           float* outBaseNormalized = nullptr,
                                           float* outEffectiveNormalized = nullptr,
                                           float* outUnclampedNormalized = nullptr) const;
    float currentLfoSignalForBlock(int numSamples);
    float currentLfoSignalForBlock(int lfoIndex, int numSamples);
    void collectModulationEnvelopeValuesFromVoices();

    juce::Synthesiser synth;

    std::array<juce::AudioParameterBool*, kOscillatorSourceCount> oscEnabledParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kOscillatorSourceCount> oscCoarseParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kOscillatorSourceCount> oscFineParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kOscillatorSourceCount> oscPitchParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterChoice*, kOscillatorSourceCount> oscModeParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kOscillatorSourceCount> oscMacroAParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kOscillatorSourceCount> oscMacroBParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kOscillatorSourceCount> oscMacroCParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterChoice*, kOscillatorSourceCount> oscVowelParams { { nullptr, nullptr, nullptr } };

    // WT Position is its OWN parameter rather than one of the macros, and that
    // is the whole reason it can be modulated: buildLfoAssignableTargets builds
    // the destination list from the float parameters that exist, so a real
    // parameter is a modulation destination with no further plumbing, and a
    // value folded into macroA would have no identity to assign to.
    std::array<juce::AudioParameterFloat*, kOscillatorSourceCount> oscWtPositionParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterChoice*, kOscillatorSourceCount> oscWtTableParams { { nullptr, nullptr, nullptr } };
    std::array<std::array<juce::AudioParameterFloat*, 8>, kOscillatorSourceCount> oscHarmonicParams { { { { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr } }, { { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr } }, { { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr } } } };
    juce::AudioParameterBool* subOscEnabledParam { nullptr };
    juce::AudioParameterFloat* subOscPitchParam { nullptr };
    juce::AudioParameterChoice* subOscOctaveParam { nullptr };
    juce::AudioParameterChoice* subOscWaveformParam { nullptr };
    std::array<juce::AudioParameterBool*, kFilterInstanceCount> filterEnabledParams { { nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kFilterInstanceCount> filterCutoffParams { { nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kFilterInstanceCount> filterResonanceParams { { nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kFilterInstanceCount> filterCombTuneParams { { nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kFilterInstanceCount> filterCombDecayParams { { nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kFilterInstanceCount> filterCombDampingParams { { nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kFilterInstanceCount> filterCombDispersionParams { { nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kFilterInstanceCount> filterCombDriveParams { { nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kFilterInstanceCount> filterCombMixParams { { nullptr, nullptr } };
    std::array<juce::AudioParameterBool*, kFilterInstanceCount> filterCombInvertParams { { nullptr, nullptr } };
    std::array<juce::AudioParameterChoice*, kFilterInstanceCount> filterTypeParams { { nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kEnvelopeSourceCount> attackParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kEnvelopeSourceCount> decayParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kEnvelopeSourceCount> sustainParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kEnvelopeSourceCount> releaseParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterBool*, kEnvelopeSourceCount> envelopeEnabledParams { { nullptr, nullptr, nullptr } };
    juce::AudioParameterFloat* attackParam { nullptr };
    juce::AudioParameterFloat* decayParam { nullptr };
    juce::AudioParameterFloat* sustainParam { nullptr };
    juce::AudioParameterFloat* releaseParam { nullptr };
    juce::AudioParameterBool* ampEnvEnabledParam { nullptr };
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
    std::array<juce::AudioParameterFloat*, kMixerSourceCount> mixerLevelParams { { nullptr, nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kMixerSourceCount> mixerPanParams { { nullptr, nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kMixerSourceCount> mixerSendParams { { nullptr, nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterBool*, kMixerSourceCount> mixerMuteParams { { nullptr, nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterBool*, kMixerSourceCount> mixerPhaseInvertParams { { nullptr, nullptr, nullptr, nullptr } };
    juce::AudioParameterBool* fxReturnPhaseInvertParam { nullptr };
    juce::AudioParameterFloat* dryBusGainParam { nullptr };
    juce::AudioParameterFloat* dryBusPanParam { nullptr };
    juce::AudioParameterBool* dryBusMuteParam { nullptr };
    juce::AudioParameterBool* dryBusSoloParam { nullptr };
    juce::AudioParameterBool* dryBusPhaseInvertParam { nullptr };
    std::array<juce::AudioParameterBool*, kMixerSourceCount> mixerSoloParams { { nullptr, nullptr, nullptr, nullptr } };
    juce::AudioParameterBool* fxReturnMuteParam { nullptr };
    juce::AudioParameterBool* fxReturnSoloParam { nullptr };
    juce::AudioParameterFloat* fxReturnPanParam { nullptr };
    juce::AudioParameterFloat* reverbAmountParam { nullptr };
    juce::AudioParameterBool* reverbEnabledParam { nullptr };
    juce::AudioParameterChoice* reverbAlgorithmParam { nullptr };
    juce::AudioParameterBool* moodEnabledParam { nullptr };
    juce::AudioParameterBool* moodFreezeParam { nullptr };
    juce::AudioParameterFloat* moodMixParam { nullptr };
    juce::AudioParameterFloat* moodClockParam { nullptr };
    juce::AudioParameterFloat* moodWetTimeParam { nullptr };
    juce::AudioParameterFloat* moodWetModifyParam { nullptr };
    juce::AudioParameterFloat* moodLoopLengthParam { nullptr };
    juce::AudioParameterFloat* moodLoopModifyParam { nullptr };
    juce::AudioParameterFloat* moodFeedbackParam { nullptr };
    juce::AudioParameterFloat* moodSpreadParam { nullptr };
    juce::AudioParameterFloat* moodDegradeParam { nullptr };
    juce::AudioParameterChoice* moodRoutingParam { nullptr };
    juce::AudioParameterChoice* moodWetModeParam { nullptr };
    juce::AudioParameterChoice* moodLoopModeParam { nullptr };

    juce::AudioParameterBool* doomEnabledParam { nullptr };
    juce::AudioParameterBool* doomFreezeParam { nullptr };
    juce::AudioParameterBool* doomLoopActiveParam { nullptr };
    juce::AudioParameterBool* doomWetActiveParam { nullptr };
    juce::AudioParameterBool* doomLoopHalfParam { nullptr };
    juce::AudioParameterBool* doomClockSmoothParam { nullptr };
    juce::AudioParameterFloat* doomMixParam { nullptr };
    juce::AudioParameterFloat* doomClockParam { nullptr };
    juce::AudioParameterFloat* doomLoopLengthParam { nullptr };
    juce::AudioParameterFloat* doomLoopModifyParam { nullptr };
    juce::AudioParameterFloat* doomOverdubParam { nullptr };
    juce::AudioParameterFloat* doomFadeParam { nullptr };
    juce::AudioParameterFloat* doomWetTimeParam { nullptr };
    juce::AudioParameterFloat* doomWetModifyParam { nullptr };
    juce::AudioParameterFloat* doomCrossParam { nullptr };
    juce::AudioParameterFloat* doomGlueParam { nullptr };
    juce::AudioParameterFloat* doomEqParam { nullptr };
    juce::AudioParameterFloat* doomBalanceParam { nullptr };
    juce::AudioParameterFloat* doomBlendParam { nullptr };
    juce::AudioParameterFloat* doomSpreadParam { nullptr };
    juce::AudioParameterChoice* doomRoutingParam { nullptr };
    juce::AudioParameterChoice* doomLoopModeParam { nullptr };
    juce::AudioParameterChoice* doomWetModeParam { nullptr };
    juce::AudioParameterChoice* doomCrossSourceParam { nullptr };

    juce::AudioParameterBool* lucyEnabledParam { nullptr };
    juce::AudioParameterBool* lucyFilterInvertParam { nullptr };
    juce::AudioParameterBool* lucyVerbPostParam { nullptr };
    juce::AudioParameterBool* lucyFreezeParam { nullptr };
    juce::AudioParameterBool* lucyFreezeSlushyParam { nullptr };
    juce::AudioParameterBool* lucyGateParam { nullptr };
    juce::AudioParameterBool* lucySlowParam { nullptr };
    juce::AudioParameterFloat* lucyGlobalParam { nullptr };
    juce::AudioParameterFloat* lucyLossParam { nullptr };
    juce::AudioParameterFloat* lucySpeedParam { nullptr };
    juce::AudioParameterFloat* lucyFilterParam { nullptr };
    juce::AudioParameterFloat* lucyFilterFreqParam { nullptr };
    juce::AudioParameterFloat* lucyVerbParam { nullptr };
    juce::AudioParameterFloat* lucyVerbDecayParam { nullptr };
    juce::AudioParameterFloat* lucyFreezerParam { nullptr };
    juce::AudioParameterFloat* lucyGateCutoffParam { nullptr };
    juce::AudioParameterFloat* lucyThresholdParam { nullptr };
    juce::AudioParameterFloat* lucyAutoGainParam { nullptr };
    juce::AudioParameterFloat* lucyWeightingParam { nullptr };
    juce::AudioParameterFloat* lucyGainParam { nullptr };
    juce::AudioParameterFloat* lucySpreadParam { nullptr };
    juce::AudioParameterChoice* lucyModeParam { nullptr };
    juce::AudioParameterChoice* lucyPacketsParam { nullptr };
    juce::AudioParameterChoice* lucySlopeParam { nullptr };

    juce::AudioParameterBool* chorusEnabledParam { nullptr };
    juce::AudioParameterFloat* chorusAmountParam { nullptr };
    juce::AudioParameterFloat* chorusRateParam { nullptr };
    juce::AudioParameterFloat* chorusDepthParam { nullptr };
    juce::AudioParameterFloat* chorusWidthParam { nullptr };
    juce::AudioParameterFloat* chorusSpreadParam { nullptr };
    juce::AudioParameterFloat* chorusLowCutParam { nullptr };
    juce::AudioParameterFloat* chorusFeedbackParam { nullptr };
    juce::AudioParameterFloat* chorusCharacterParam { nullptr };
    juce::AudioParameterFloat* chorusMixParam { nullptr };
    juce::AudioParameterFloat* chorusToneParam { nullptr };
    juce::AudioParameterChoice* chorusModeParam { nullptr };

    juce::AudioParameterBool* spreadEnabledParam { nullptr };

    std::array<BusInsertParams, kBusInsertCount> busInsertParams {};
    std::array<px3::BusInsertChain, kBusInsertCount> busInserts {};

    void createBusInsertParameters(int bus, const juce::String& idPrefix, const juce::String& label);
    void updateBusInsertSettings(int bus);
    juce::AudioParameterFloat* spreadAmountParam { nullptr };
    juce::AudioParameterFloat* spreadWidthParam { nullptr };
    juce::AudioParameterFloat* spreadDepthParam { nullptr };
    juce::AudioParameterFloat* spreadCenterParam { nullptr };
    juce::AudioParameterFloat* spreadLowWidthParam { nullptr };
    juce::AudioParameterFloat* spreadHighWidthParam { nullptr };
    juce::AudioParameterFloat* spreadLowFreqParam { nullptr };
    juce::AudioParameterFloat* spreadHighFreqParam { nullptr };
    juce::AudioParameterFloat* spreadMixParam { nullptr };
    juce::AudioParameterFloat* spreadToneParam { nullptr };
    juce::AudioParameterChoice* spreadModeParam { nullptr };

    juce::AudioParameterBool* analogEnabledParam { nullptr };
    juce::AudioParameterChoice* analogProfileParam { nullptr };
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
    std::array<juce::AudioParameterBool*, kLfoSourceCount> lfoEnabledParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kLfoSourceCount> lfoFrequencyParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kLfoSourceCount> lfoAmountParams { { nullptr, nullptr, nullptr } };
    std::array<juce::AudioParameterChoice*, kLfoSourceCount> lfoWaveformParams { { nullptr, nullptr, nullptr } };
    juce::AudioParameterBool* lfoEnabledParam { nullptr };
    juce::AudioParameterFloat* lfoFrequencyParam { nullptr };
    juce::AudioParameterFloat* lfoAmountParam { nullptr };
    juce::AudioParameterChoice* lfoWaveformParam { nullptr };
    std::array<juce::AudioParameterFloat*, kEnvelopeSourceCount> envelopeAmountParams { { nullptr, nullptr, nullptr } };

    struct LfoAssignableTarget
    {
        juce::String parameterId;
        juce::String displayName;
        juce::RangedAudioParameter* parameter { nullptr };
        float normalizedDepth { 0.10f };

        // Scale the source by how much room the base value actually has, rather
        // than letting it swing a fixed amount and clamping what falls outside.
        //
    };

    // One slot per oscillator, each holding an immutable table shared by every
    // voice rather than copied into it - a table is 2.25 MB.
    std::array<px3::WavetableSlot, kOscillatorSourceCount> wavetableSlots;

    // What is actually loaded, against what the parameter asks for. -1 means
    // nothing has been built yet.
    std::array<int, kOscillatorSourceCount> loadedWavetableIndex { { -1, -1, -1 } };
    std::array<juce::String, kOscillatorSourceCount> userWavetableNames;
    std::array<juce::String, kOscillatorSourceCount> missingWavetableNames;

    // AMP ENV at 0, ENV 1..3 after it. Each instance owns its own; nothing is
    // shared between them, which is what keeps editing one from touching
    // another.
    std::array<px3::BreakpointEnvelope, kShapedEnvelopeCount> shapedEnvelopes;

    void handleAsyncUpdate() override;

    std::vector<LfoAssignableTarget> lfoAssignableTargets;
    juce::StringArray lfoAssignmentDisplayNames;
    std::array<std::atomic<int>, kLfoSourceCount> lfoAssignmentIndices { { 0, 0, 0 } };
    std::array<std::atomic<int>, kEnvelopeSourceCount> envelopeAssignmentIndices { { 0, 0, 0 } };

    std::array<std::atomic<int>, PianoKeyboard::totalKeys> activeNoteCounts {};
    std::array<std::atomic<int>, PianoKeyboard::totalKeys> activeNoteVelocities {};
    std::atomic<int> lastMidiNote { -1 };
    std::atomic<int> lastMidiVelocity { 0 };
    std::atomic<int> lastMidiNoteOn { 0 };
    // Virtual keyboard events, handed from the message thread to the audio
    // thread WITHOUT a lock.
    //
    // This was a juce::MidiBuffer behind a CriticalSection, and processBlock
    // took that lock every block. A lock on the audio thread is a real-time
    // violation whatever its hold time: if the message thread is preempted
    // while holding it, the audio thread waits for that thread to be
    // rescheduled, and a missed deadline is a dropout - which is heard as a
    // click.
    //
    // It went unnoticed because it only bites where the on-screen keyboard is
    // used and the message thread is busy, which is the standalone. Play a note
    // there and the same thread that takes this lock is also spawning key
    // sparks - a count proportional to VELOCITY - and repainting, so playing
    // harder makes the contention worse.
    //
    // Single producer, single consumer, fixed capacity: the message thread
    // writes and the audio thread reads, so a plain ring of atomics is enough
    // and neither side ever blocks or allocates. A full ring drops events
    // rather than waiting; 256 is far more than a keyboard can produce between
    // two audio blocks.
    struct VirtualNote
    {
        int note { 0 };
        float velocity { 0.0f };
        bool isNoteOn { false };
    };
    static constexpr int kVirtualNoteCapacity = 256;
    std::array<VirtualNote, kVirtualNoteCapacity> virtualNotes {};
    std::atomic<int> virtualNoteWrite { 0 };
    std::atomic<int> virtualNoteRead { 0 };

    void pushVirtualNote(VirtualNote event);

public:
    // Where the AMP ENV currently is, for the graph to paint.
    //
    // The MOST RECENTLY TRIGGERED sounding voice, chosen by note-start
    // sequence. One envelope is drawn rather than all of them: the graph is a
    // picture of the shape, and stacking sixty-four progress fills on it would
    // say less than one does.
    //
    // Published once per block from the audio thread into plain atomics and
    // read by the editor's timer. Nothing flows the other way.
    // Where the newest sounding voice's envelopes are, for the graphs that draw
    // them. Slot 0 is AMP ENV and 1-3 are ENV 1-3, the same numbering the
    // shaped-envelope accessors use.
    static constexpr int kEnvelopeSlots = 4;
    EnvelopePosition getEnvelopeProgress(int slot) const;

private:
    // One slot per envelope. Written on the audio thread, read on the message
    // thread; each field is independently atomic and `active` is the release
    // store that publishes the rest.
    struct ProgressSlot
    {
        std::atomic<bool> active { false };
        std::atomic<bool> inRelease { false };
        std::atomic<double> held { 0.0 };
        std::atomic<double> released { 0.0 };
        std::atomic<double> sustain { 0.0 };
    };
    std::array<ProgressSlot, kEnvelopeSlots> envelopeProgress;

    //---- MIDI mapping ---------------------------------------------------
    // Written on the audio thread, read on the message thread. The audio
    // thread records and nothing else: it never walks the mapping list, never
    // takes a lock and never allocates, which is why the list below can be a
    // plain vector.
    static constexpr int kMidiCcCount = 128;
    std::array<std::atomic<int>, kMidiCcCount> ccValues {};
    std::array<std::atomic<std::uint32_t>, kMidiCcCount> ccSequence {};
    std::array<std::uint32_t, kMidiCcCount> ccSeenSequence {};

    std::atomic<int> lastTouchedCc { -1 };
    std::atomic<int> lastTouchedChannel { 1 };
    std::atomic<std::uint32_t> lastTouchedSequence { 0 };
    std::uint32_t seenTouchedSequence { 0 };

    // Message thread only.
    std::vector<px3::MidiMapping> midiMappings;
    juce::StringArray midiLearnTargets;

    void recordMidiController(int ccNumber, int channel, int value);
    void assignMidiLearnTo(int ccNumber, int channel);
    void writeParameterFromCc(const juce::String& parameterId, int ccValue);
    juce::RangedAudioParameter* findParameterById(const juce::String& parameterId) const;

    // The message-thread pump. Owned here rather than by the editor, so
    // mappings keep working with the window closed.
    struct MidiMappingTimer final : public juce::Timer
    {
        explicit MidiMappingTimer(PX3SynthAudioProcessor& ownerIn) : owner(ownerIn) {}
        void timerCallback() override { owner.applyPendingMidiMappings(); }
        PX3SynthAudioProcessor& owner;
    };
    MidiMappingTimer midiMappingTimer { *this };
    void publishEnvelopeProgress();

    // Onset capture, for diagnosing a fault that only appears in a real host.
    //
    // Off unless PX3_ONSET_CAPTURE is set in the environment. When on, the
    // first note-on starts recording per-sample: the final output, the amp
    // envelope of the voice that took the note, and how many voices are
    // sounding. Written to the path in PX3_ONSET_CAPTURE once full.
    //
    // The point is that the fault does not reproduce offline. Reading the code
    // has not found it and neither has rendering; this makes the failing
    // environment measurable instead.
    static constexpr int kOnsetCaptureSamples = 8192;
    struct OnsetCapture
    {
        std::array<float, kOnsetCaptureSamples> output {};
        std::array<float, kOnsetCaptureSamples> ampEnvelope {};
        std::array<float, kOnsetCaptureSamples> voiceCount {};
        std::array<float, kOnsetCaptureSamples> attackSeconds {};
        std::array<float, kOnsetCaptureSamples> heldSeconds {};
        int written { 0 };
        bool armed { false };
        bool recording { false };
        bool done { false };
        juce::String path;
    };
    // Allocated only when armed. The arrays are 160 KB and the processor object
    // is 128 KB, so holding them unconditionally would more than double it for
    // a tool that is off in every normal run.
    std::unique_ptr<OnsetCapture> onsetCapture;
    void writeOnsetCapture();

    std::atomic<float> pitchBendNormalized { 0.0f };
    std::atomic<float> modWheelNormalized { 0.0f };
    std::atomic<float> pitchBendActivity { 0.0f };
    std::atomic<float> modWheelActivity { 0.0f };
    std::atomic<int> topMenuViewIndex { 0 };
    // Guarded because the host may serialise state off the message thread while
    // the editor is writing this from it.
    mutable std::mutex loadedPresetMutex;
    LoadedPreset loadedPreset;

    float vibratoPhaseRadians { 0.0f };
    std::array<LfoGenerator, kLfoSourceCount> lfoGenerators;
    std::array<std::atomic<float>, kLfoSourceCount> lfoPhaseForDebug { { 0.0f, 0.0f, 0.0f } };
    std::array<std::atomic<float>, kLfoSourceCount> lfoCurrentValues { { 0.0f, 0.0f, 0.0f } };
    std::array<std::atomic<float>, kEnvelopeSourceCount> modulationEnvelopeValues { { 0.0f, 0.0f, 0.0f } };
    std::array<std::atomic<float>, kEnvelopeSourceCount> debugEnvelopeContributionNormalized { { 0.0f, 0.0f, 0.0f } };
    std::array<std::atomic<float>, kEnvelopeSourceCount> debugEnvelopeDestinationBaseNormalized { { 0.0f, 0.0f, 0.0f } };
    std::array<std::atomic<float>, kEnvelopeSourceCount> debugEnvelopeDestinationEffectiveNormalized { { 0.0f, 0.0f, 0.0f } };
    std::atomic<float> lfoDebugBaseNormalized { 0.0f };
    std::atomic<float> lfoDebugEffectiveNormalized { 0.0f };
    std::atomic<float> debugOscillatorBusRms { 0.0f };
    std::atomic<float> debugDryBusRms { 0.0f };
    std::atomic<float> debugFxBusRms { 0.0f };
    std::atomic<float> debugMasterBusRms { 0.0f };
    std::atomic<float> debugOscillatorBusPeak { 0.0f };
    std::atomic<float> debugOscillatorBusPrePolyPeak { 0.0f };
    std::atomic<int> debugOscillatorBusPrePolyClipSamples { 0 };
    std::atomic<float> debugDryBusPeak { 0.0f };
    std::atomic<float> debugFxBusPeak { 0.0f };
    std::atomic<float> debugMasterBusPeak { 0.0f };
    std::atomic<float> debugMasterPreOutputPeak { 0.0f };
    std::atomic<int> debugMasterClipSamples { 0 };
    std::array<std::atomic<float>, kMixerSourceCount> debugMixerSourceRms { { 0.0f, 0.0f, 0.0f, 0.0f } };
    std::array<std::atomic<float>, kMixerSourceCount> debugMixerSourcePeak { { 0.0f, 0.0f, 0.0f, 0.0f } };
    std::array<std::atomic<float>, kMixerSourceCount> debugVoiceSourcePeak { { 0.0f, 0.0f, 0.0f, 0.0f } };
    std::atomic<float> debugVoicePeak { 0.0f };
    std::atomic<int> debugActiveVoiceCount { 0 };
    std::atomic<int> debugHeldVoiceCount { 0 };
    std::atomic<int> debugReleasingVoiceCount { 0 };
    std::atomic<int> debugNearSilentReleaseVoiceCount { 0 };
    std::atomic<float> debugPolyphonyGainApplied { 1.0f };
    std::atomic<float> debugPolyphonyGainTarget { 1.0f };
    std::atomic<float> debugEffectiveVoiceLoad { 1.0f };
    std::atomic<float> debugReleaseEnergyEquivalent { 0.0f };
    std::atomic<int> debugPolyGainTailBypassActive { 0 };
    std::atomic<int> debugReleaseVoicesPruned { 0 };
    std::atomic<float> debugFxReturnRms { 0.0f };
    std::atomic<float> debugInstanceCpuLoadPercent { 0.0f };

    /*
     * VIBE is a correlated imperfection system. It is intentionally not a
     * single post-distortion. Shared slow processes (PSU, temperature, chaos,
     * drift) are generated once and distributed across multiple DSP points.
     */
    Vibe vibeComponent;
    Delay delayComponent;
    Mood moodComponent;
    px3::Doom doomComponent;
    px3::Lucy lucyComponent;
    px3::Chorus chorusComponent;
    px3::StereoSpread stereoSpreadComponent;
    px3::AnalogEngine analogEngine;
    ::Reverb reverb;

    // Internal routing buses (prepared once, reused per block).
    juce::AudioBuffer<float> oscillatorBusBuffer;
    juce::AudioBuffer<float> dryBusBuffer;
    juce::AudioBuffer<float> fxBusBuffer;
    juce::AudioBuffer<float> masterBusBuffer;
    std::array<SmoothedGate, kMixerSourceCount> sourceDryGateSmoothers;
    std::array<SmoothedGate, kMixerSourceCount> sourceSendGateSmoothers;
    SmoothedGate fxReturnGateSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> fxReturnPanSmoother;
    // Mixer faders, pans and sends are user-facing gains applied per sample, so
    // they are smoothed per sample rather than stepped once per block.
    std::array<SmoothedGain, kMixerSourceCount> sourceLevelSmoothers;
    // Polarity is smoothed from +1 to -1 rather than switched. A hard sign flip
    // is a step discontinuity in the middle of a waveform, which is a click;
    // ramping through zero costs a few milliseconds of dip instead.
    std::array<SmoothedGain, kMixerSourceCount> sourcePhaseSmoothers;
    std::array<float, kMixerSourceCount> sourcePhaseValues { { 1.0f, 1.0f, 1.0f, 1.0f } };
    SmoothedGain fxReturnPhaseSmoother;
    SmoothedGain dryBusGainSmoother;
    SmoothedGain dryBusPhaseSmoother;
    SmoothedGate dryBusGateSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> dryBusPanSmoother;
    std::array<SmoothedGain, kMixerSourceCount> sourcePanLeftSmoothers;
    std::array<SmoothedGain, kMixerSourceCount> sourcePanRightSmoothers;
    std::array<SmoothedGain, kMixerSourceCount> sourceSendSmoothers;
    SmoothedGain fxSendGainSmoother;
    SmoothedGain fxReturnGainSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> polyphonyGainSmoother;
    // Attenuation hold: drops instantly, recovers slowly, so a decaying release
    // tail cannot lift its own polyphony gain while it is still audible.
    float polyphonyGainHold { 1.0f };

    // Preallocated scratch for the release-tail budget. This used to be a
    // std::vector built inside processBlock, which heap-allocated on every block
    // that had any releasing voice.
    std::array<SynthVoice*, kPolyphonyVoiceCount> releaseCandidateScratch { };
    // Voices in pool order, typed. Owned by juce::Synthesiser, not by this
    // array; populated once at construction alongside addVoice.
    std::array<SynthVoice*, kPolyphonyVoiceCount> typedVoices { };
    // Recomputed in prepareToPlay; read once per block.
    std::atomic<int> soundingVoiceBudget { kSoundingVoiceBudgetAtReference };

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
