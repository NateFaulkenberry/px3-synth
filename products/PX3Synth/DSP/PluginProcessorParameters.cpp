#include "PluginProcessor.h"
#include "WavetableFactory.h"
#include "WavetableLibrary.h"
#include "PluginProcessorInternals.h"

// File role: parameter accessors, modulation mapping, and FX order API.
// Do not add audio block orchestration here; keep this focused on parameter
// value translation and host-facing parameter helpers.

using namespace px3::processor_internal;

//==============================================================================
// Parameter Access And Routing
//==============================================================================
namespace
{
// Reflects a value back into 0..1 instead of clamping it: past 1 it walks back
// down, below 0 it walks back up, as many times as it has to. A clamped
// modulation sits flat at the end of the range for as long as it is past it,
// which turns a sine into a square with rounded shoulders; a folded one keeps
// moving.
float foldIntoUnitRange(float value)
{
    if (! std::isfinite(value))
    {
        return 0.0f;
    }

    auto wrapped = std::fmod(value, 2.0f);
    if (wrapped < 0.0f)
    {
        wrapped += 2.0f;
    }
    return wrapped <= 1.0f ? wrapped : 2.0f - wrapped;
}
} // namespace

juce::AudioParameterChoice& PX3SynthAudioProcessor::getLfoClockModeParam(int index) const
{
    return *lfoClockModeParams[static_cast<std::size_t>(juce::jlimit(0, kLfoSourceCount - 1, index))];
}

juce::AudioParameterChoice& PX3SynthAudioProcessor::getLfoClockDivisionParam(int index) const
{
    return *lfoClockDivisionParams[static_cast<std::size_t>(juce::jlimit(0, kLfoSourceCount - 1, index))];
}

bool PX3SynthAudioProcessor::isLfoClockAvailable(int index) const
{
    const auto mode = getLfoClockModeParam(index).getIndex();
    return mode == 0 || (mode == 1 ? hostTempoAvailable.load(std::memory_order_relaxed)
                                 : hostPositionAvailable.load(std::memory_order_relaxed));
}

float PX3SynthAudioProcessor::modulatedParameterValue(juce::AudioParameterFloat* parameter) const
{
    if (parameter == nullptr) { return 0.0f; }
    return parameter->convertFrom0to1(applyModulationToNormalizedValue(
        parameter, static_cast<juce::RangedAudioParameter*>(parameter)->getValue()));
}

float PX3SynthAudioProcessor::applyModulationToNormalizedValue(juce::RangedAudioParameter* parameter,
                                                               float baseNormalized,
                                                               float* outBaseNormalized,
                                                               float* outEffectiveNormalized,
                                                               float* outUnclampedNormalized) const
{
    const auto base = clamp01(baseNormalized);
    auto effective = base;

    if (parameter == nullptr)
    {
        if (outUnclampedNormalized != nullptr)
        {
            *outUnclampedNormalized = base;
        }
        if (outBaseNormalized != nullptr)
        {
            *outBaseNormalized = base;
        }
        if (outEffectiveNormalized != nullptr)
        {
            *outEffectiveNormalized = effective;
        }
        return effective;
    }

    std::array<float, kLfoSourceCount + kEnvelopeSourceCount + kMacroCount> signals {};
    std::array<bool, kLfoSourceCount + kEnvelopeSourceCount + kMacroCount> enabledSources {};
    auto graph = modulationGraph.read();
    if (! graph || ! graph->hasDestination(parameter->getParameterIndex()))
    {
        if (outUnclampedNormalized != nullptr) { *outUnclampedNormalized = base; }
        if (outBaseNormalized != nullptr) { *outBaseNormalized = base; }
        if (outEffectiveNormalized != nullptr) { *outEffectiveNormalized = base; }
        return base;
    }
    auto depths = graph ? graph->depths()
                        : std::array<float, px3::synth::CompiledModulationGraph::routeCapacity> {};
    for (int i = 0; i < kLfoSourceCount; ++i)
    {
        const auto index = static_cast<std::size_t>(i);
        signals[index] = lfoCurrentValues[index].load(std::memory_order_relaxed);
        enabledSources[index] = getLfoEnabledParam(i).get() && isLfoClockAvailable(i);
    }
    // Envelopes are per-voice sources. For a destination that lives inside the
    // voice, each voice adds its own envelopes (VoiceModulation.h), so they are
    // left out here rather than applied as one cross-voice average.
    const auto parameterIndex = parameter->getParameterIndex();
    const auto envelopesAreVoiceLocal = parameterIndex >= 0
                                        && static_cast<std::size_t>(parameterIndex) < voiceModulatedParameter.size()
                                        && voiceModulatedParameter[static_cast<std::size_t>(parameterIndex)];
    for (int i = 0; i < kEnvelopeSourceCount; ++i)
    {
        const auto index = static_cast<std::size_t>(i);
        signals[static_cast<std::size_t>(kLfoSourceCount + i)] = modulationEnvelopeValues[index].load(std::memory_order_relaxed);
        enabledSources[static_cast<std::size_t>(kLfoSourceCount + i)] = getEnvelopeEnabledParam(i).get() && ! envelopesAreVoiceLocal;
    }
    for (int macro = 0; macro < kMacroCount; ++macro)
    {
        signals[static_cast<std::size_t>(kLfoSourceCount + kEnvelopeSourceCount + macro)]
            = macroParams[static_cast<std::size_t>(macro)]->get();
        enabledSources[static_cast<std::size_t>(kLfoSourceCount + kEnvelopeSourceCount + macro)] = true;
    }
    for (int slot = 0; slot < kGraphRouteSlots; ++slot)
    {
        depths[static_cast<std::size_t>(kGraphRouteSlotBase + slot)]
            = graphRouteDepthParams[static_cast<std::size_t>(slot)]->get();
    }
    const auto totalDelta = graph->deltaFor(parameter->getParameterIndex(), base, signals, depths, enabledSources);

    if (outUnclampedNormalized != nullptr)
    {
        *outUnclampedNormalized = base + totalDelta;
    }
    // Folded, not clamped. The pre-fold value is what outUnclampedNormalized
    // reports, so a test can still see how far past the range the sum went.
    effective = foldIntoUnitRange(base + totalDelta);

    if (outBaseNormalized != nullptr)
    {
        *outBaseNormalized = base;
    }
    if (outEffectiveNormalized != nullptr)
    {
        *outEffectiveNormalized = effective;
    }

    return effective;
}

juce::AudioParameterBool& PX3SynthAudioProcessor::getOscillatorEnabledParam(int oscIndex) const
{
    const auto idx = juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex);
    return *oscEnabledParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getOscillatorCoarseParam(int oscIndex) const
{
    const auto idx = juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex);
    return *oscCoarseParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getOscillatorSemitoneParam(int oscIndex) const
{
    return *oscSemitoneParams[static_cast<std::size_t>(juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex))];
}

juce::AudioParameterFloat& PX3SynthAudioProcessor::getSubOscSemitoneParam() const
{
    return *subOscSemitoneParam;
}

juce::AudioParameterFloat& PX3SynthAudioProcessor::getOscillatorFineParam(int oscIndex) const
{
    const auto idx = juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex);
    return *oscFineParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterChoice& PX3SynthAudioProcessor::getOscillatorModeParam(int oscIndex) const
{
    const auto idx = juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex);
    return *oscModeParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getOscillatorMacroAParam(int oscIndex) const
{
    const auto idx = juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex);
    return *oscMacroAParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getOscillatorMacroBParam(int oscIndex) const
{
    const auto idx = juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex);
    return *oscMacroBParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getOscillatorMacroCParam(int oscIndex) const
{
    const auto idx = juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex);
    return *oscMacroCParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterChoice& PX3SynthAudioProcessor::getOscillatorVowelParam(int oscIndex) const
{
    const auto idx = juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex);
    return *oscVowelParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getOscillatorHarmonicParam(int oscIndex, int harmonicIndex) const
{
    const auto oscIdx = juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex);
    const auto harmIdx = juce::jlimit(0, 7, harmonicIndex);
    return *oscHarmonicParams[static_cast<std::size_t>(oscIdx)][static_cast<std::size_t>(harmIdx)];
}
juce::AudioParameterBool& PX3SynthAudioProcessor::getSubOscEnabledParam() const { return *subOscEnabledParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getSubOscCoarseParam() const { return *subOscCoarseParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getSubOscFineParam() const { return *subOscFineParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getSubOscWaveformParam() const { return *subOscWaveformParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getFilterEnabledParam(int filterIndex) const
{
    const auto idx = juce::jlimit(0, kFilterInstanceCount - 1, filterIndex);
    return *filterEnabledParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getFilterCutoffParam(int filterIndex) const
{
    const auto idx = juce::jlimit(0, kFilterInstanceCount - 1, filterIndex);
    return *filterCutoffParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getFilterResonanceParam(int filterIndex) const
{
    const auto idx = juce::jlimit(0, kFilterInstanceCount - 1, filterIndex);
    return *filterResonanceParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterChoice& PX3SynthAudioProcessor::getFilterTypeParam(int filterIndex) const
{
    const auto idx = juce::jlimit(0, kFilterInstanceCount - 1, filterIndex);
    return *filterTypeParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getAttackParam() const { return *attackParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getEnvelopeAttackParam(int envIndex) const
{
    const auto idx = juce::jlimit(0, kEnvelopeSourceCount - 1, envIndex);
    return *attackParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getDecayParam() const { return *decayParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getEnvelopeDecayParam(int envIndex) const
{
    const auto idx = juce::jlimit(0, kEnvelopeSourceCount - 1, envIndex);
    return *decayParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getSustainParam() const { return *sustainParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getEnvelopeSustainParam(int envIndex) const
{
    const auto idx = juce::jlimit(0, kEnvelopeSourceCount - 1, envIndex);
    return *sustainParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getReleaseParam() const { return *releaseParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getEnvelopeReleaseParam(int envIndex) const
{
    const auto idx = juce::jlimit(0, kEnvelopeSourceCount - 1, envIndex);
    return *releaseParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterBool& PX3SynthAudioProcessor::getAmpEnvEnabledParam() const { return *ampEnvEnabledParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getEnvelopeEnabledParam(int envIndex) const
{
    const auto idx = juce::jlimit(0, kEnvelopeSourceCount - 1, envIndex);
    return *envelopeEnabledParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getMasterGainParam() const { return *masterGainParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getAnalogDriftAmountParam() const { return *analogDriftAmountParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getAnalogDriftEnabledParam() const { return *analogDriftEnabledParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getVibeEnabledParam() const { return *vibeEnabledParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getAnalogDriftTypeParam() const { return *analogDriftTypeParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getDelayAmountParam() const { return *delayAmountParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getGranularSyncDivisionParam() const { return *granularSyncDivisionParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getGranularModeParam() const { return *granularModeParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getDelayAlgorithmParam() const { return *delayAlgorithmParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getDelayEnabledParam() const { return *delayEnabledParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getDelayTimeParam() const { return *delayTimeParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getDelayFeedbackParam() const { return *delayFeedbackParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getFxSendGainParam() const { return *fxSendGainParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getFxReturnGainParam() const { return *fxReturnGainParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getMixerLevelParam(int sourceIndex) const
{
    const auto idx = juce::jlimit(0, kMixerSourceCount - 1, sourceIndex);
    return *mixerLevelParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getMixerPanParam(int sourceIndex) const
{
    const auto idx = juce::jlimit(0, kMixerSourceCount - 1, sourceIndex);
    return *mixerPanParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getMixerSendParam(int sourceIndex) const
{
    const auto idx = juce::jlimit(0, kMixerSourceCount - 1, sourceIndex);
    return *mixerSendParams[static_cast<std::size_t>(idx)];
}
namespace
{
int clampFilterIndex(int filterIndex)
{
    return juce::jlimit(0, kFilterInstanceCount - 1, filterIndex);
}
}

juce::AudioParameterFloat& PX3SynthAudioProcessor::getFilterCombTuneParam(int filterIndex) const
{
    return *filterCombTuneParams[static_cast<std::size_t>(clampFilterIndex(filterIndex))];
}

juce::AudioParameterFloat& PX3SynthAudioProcessor::getFilterCombDecayParam(int filterIndex) const
{
    return *filterCombDecayParams[static_cast<std::size_t>(clampFilterIndex(filterIndex))];
}

juce::AudioParameterFloat& PX3SynthAudioProcessor::getFilterCombDampingParam(int filterIndex) const
{
    return *filterCombDampingParams[static_cast<std::size_t>(clampFilterIndex(filterIndex))];
}

juce::AudioParameterFloat& PX3SynthAudioProcessor::getFilterCombDispersionParam(int filterIndex) const
{
    return *filterCombDispersionParams[static_cast<std::size_t>(clampFilterIndex(filterIndex))];
}

juce::AudioParameterFloat& PX3SynthAudioProcessor::getFilterCombDriveParam(int filterIndex) const
{
    return *filterCombDriveParams[static_cast<std::size_t>(clampFilterIndex(filterIndex))];
}

juce::AudioParameterFloat& PX3SynthAudioProcessor::getFilterCombMixParam(int filterIndex) const
{
    return *filterCombMixParams[static_cast<std::size_t>(clampFilterIndex(filterIndex))];
}

juce::AudioParameterBool& PX3SynthAudioProcessor::getFilterCombInvertParam(int filterIndex) const
{
    return *filterCombInvertParams[static_cast<std::size_t>(clampFilterIndex(filterIndex))];
}

juce::AudioParameterBool& PX3SynthAudioProcessor::getMixerPhaseInvertParam(int sourceIndex) const
{
    const auto idx = juce::jlimit(0, kMixerSourceCount - 1, sourceIndex);
    return *mixerPhaseInvertParams[static_cast<std::size_t>(idx)];
}

juce::AudioParameterBool& PX3SynthAudioProcessor::getFxReturnPhaseInvertParam() const
{
    return *fxReturnPhaseInvertParam;
}

juce::AudioParameterBool& PX3SynthAudioProcessor::getMixerMuteParam(int sourceIndex) const
{
    const auto idx = juce::jlimit(0, kMixerSourceCount - 1, sourceIndex);
    return *mixerMuteParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterBool& PX3SynthAudioProcessor::getMixerSoloParam(int sourceIndex) const
{
    const auto idx = juce::jlimit(0, kMixerSourceCount - 1, sourceIndex);
    return *mixerSoloParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterBool& PX3SynthAudioProcessor::getFxReturnMuteParam() const { return *fxReturnMuteParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getFxReturnSoloParam() const { return *fxReturnSoloParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getFxReturnPanParam() const { return *fxReturnPanParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getReverbAmountParam() const { return *reverbAmountParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getReverbSizeParam() const { return *reverbSizeParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getReverbDecayParam() const { return *reverbDecayParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getReverbDampingParam() const { return *reverbDampingParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getReverbPreDelayParam() const { return *reverbPreDelayParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getReverbModDepthParam() const { return *reverbModDepthParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getReverbModRateParam() const { return *reverbModRateParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getReverbWidthParam() const { return *reverbWidthParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getReverbCloudFeedbackParam() const { return *reverbCloudFeedbackParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getReverbCloudDiffusionParam() const { return *reverbCloudDiffusionParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getReverbEnabledParam() const { return *reverbEnabledParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getReverbAlgorithmParam() const { return *reverbAlgorithmParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getMoodEnabledParam() const { return *moodEnabledParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getMoodFreezeParam() const { return *moodFreezeParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getMoodMixParam() const { return *moodMixParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getMoodClockParam() const { return *moodClockParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getMoodWetTimeParam() const { return *moodWetTimeParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getMoodWetModifyParam() const { return *moodWetModifyParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getMoodLoopLengthParam() const { return *moodLoopLengthParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getMoodLoopModifyParam() const { return *moodLoopModifyParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getMoodFeedbackParam() const { return *moodFeedbackParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getMoodSpreadParam() const { return *moodSpreadParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getMoodDegradeParam() const { return *moodDegradeParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getMoodRoutingParam() const { return *moodRoutingParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getMoodWetModeParam() const { return *moodWetModeParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getMoodLoopModeParam() const { return *moodLoopModeParam; }

juce::AudioParameterBool& PX3SynthAudioProcessor::getDoomEnabledParam() const { return *doomEnabledParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getDoomFreezeParam() const { return *doomFreezeParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getDoomLoopActiveParam() const { return *doomLoopActiveParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getDoomWetActiveParam() const { return *doomWetActiveParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getDoomLoopHalfParam() const { return *doomLoopHalfParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getDoomClockSmoothParam() const { return *doomClockSmoothParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getDoomMixParam() const { return *doomMixParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getDoomClockParam() const { return *doomClockParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getDoomLoopLengthParam() const { return *doomLoopLengthParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getDoomLoopModifyParam() const { return *doomLoopModifyParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getDoomOverdubParam() const { return *doomOverdubParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getDoomFadeParam() const { return *doomFadeParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getDoomWetTimeParam() const { return *doomWetTimeParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getDoomWetModifyParam() const { return *doomWetModifyParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getDoomCrossParam() const { return *doomCrossParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getDoomGlueParam() const { return *doomGlueParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getDoomEqParam() const { return *doomEqParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getDoomBalanceParam() const { return *doomBalanceParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getDoomBlendParam() const { return *doomBlendParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getDoomSpreadParam() const { return *doomSpreadParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getDoomRoutingParam() const { return *doomRoutingParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getDoomLoopModeParam() const { return *doomLoopModeParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getDoomWetModeParam() const { return *doomWetModeParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getDoomCrossSourceParam() const { return *doomCrossSourceParam; }

juce::AudioParameterBool& PX3SynthAudioProcessor::getLucyEnabledParam() const { return *lucyEnabledParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getLucyFilterInvertParam() const { return *lucyFilterInvertParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getLucyVerbPostParam() const { return *lucyVerbPostParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getLucyFreezeParam() const { return *lucyFreezeParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getLucyGateParam() const { return *lucyGateParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getLucySlowParam() const { return *lucySlowParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLucyGlobalParam() const { return *lucyGlobalParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLucyLossParam() const { return *lucyLossParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLucySpeedParam() const { return *lucySpeedParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLucyFilterParam() const { return *lucyFilterParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLucyFilterFreqParam() const { return *lucyFilterFreqParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLucyVerbParam() const { return *lucyVerbParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLucyVerbDecayParam() const { return *lucyVerbDecayParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLucyFreezerParam() const { return *lucyFreezerParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLucyGateThresholdParam() const { return *lucyGateThresholdParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLucyLimiterThresholdParam() const { return *lucyLimiterThresholdParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLucyAutoGainParam() const { return *lucyAutoGainParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLucyLossGainParam() const { return *lucyLossGainParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLucySpreadParam() const { return *lucySpreadParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getLucyModeParam() const { return *lucyModeParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getLucyPacketsParam() const { return *lucyPacketsParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getLucySlopeParam() const { return *lucySlopeParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getLucyWeightingParam() const { return *lucyWeightingParam; }

juce::AudioParameterBool& PX3SynthAudioProcessor::getChorusEnabledParam() const { return *chorusEnabledParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getChorusAmountParam() const { return *chorusAmountParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getChorusRateParam() const { return *chorusRateParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getChorusDepthParam() const { return *chorusDepthParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getChorusWidthParam() const { return *chorusWidthParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getChorusSpreadParam() const { return *chorusSpreadParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getChorusLowCutParam() const { return *chorusLowCutParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getChorusFeedbackParam() const { return *chorusFeedbackParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getChorusCharacterParam() const { return *chorusCharacterParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getChorusMixParam() const { return *chorusMixParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getChorusToneParam() const { return *chorusToneParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getChorusModeParam() const { return *chorusModeParam; }

juce::AudioParameterBool& PX3SynthAudioProcessor::getSpreadEnabledParam() const { return *spreadEnabledParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getSpreadAmountParam() const { return *spreadAmountParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getSpreadWidthParam() const { return *spreadWidthParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getSpreadDepthParam() const { return *spreadDepthParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getSpreadCenterParam() const { return *spreadCenterParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getSpreadLowWidthParam() const { return *spreadLowWidthParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getSpreadHighWidthParam() const { return *spreadHighWidthParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getSpreadLowFreqParam() const { return *spreadLowFreqParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getSpreadHighFreqParam() const { return *spreadHighFreqParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getSpreadMixParam() const { return *spreadMixParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getSpreadToneParam() const { return *spreadToneParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getSpreadModeParam() const { return *spreadModeParam; }

juce::AudioParameterBool& PX3SynthAudioProcessor::getAnalogEnabledParam() const { return *analogEnabledParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getFxSeparateOutputParam() const { return *fxSeparateOutputParam; }
bool PX3SynthAudioProcessor::isParameterModulated(const juce::String& parameterId) const
{
    // A macro counts as modulation for this purpose: the question this answers
    // is "does the knob need a moving ring", and a macro moves the value just
    // as an LFO does. Without this the ring stayed dark and a macro-driven
    // parameter looked untouched however far the macro was turned.
    if (getMacroMaskForParameter(parameterId) != 0)
    {
        return true;
    }

    const auto* entry = parameterCatalog.find(parameterId);
    auto graph = modulationGraph.read();
    if (! graph || entry == nullptr) { return false; }

    // A route counts only while its source is switched on: an LFO or envelope
    // that is off is not modulating anything, whatever it is patched to.
    auto live = false;
    graph->forEachRouteTo(entry->parameter->getParameterIndex(),
                          [this, &live](const px3::synth::ModulationRoute& route,
                                        const px3::synth::ModulationSourceDescriptor&)
    {
        if (route.slot < kGraphRouteSlotBase) { return; }
        if (route.source < kLfoSourceCount) { live = live || getLfoEnabledParam(route.source).get(); }
        else if (route.source < kLfoSourceCount + kEnvelopeSourceCount)
        {
            live = live || getEnvelopeEnabledParam(route.source - kLfoSourceCount).get();
        }
        else { live = true; }
    });
    return live;
}

float PX3SynthAudioProcessor::getModulatedNormalisedValue(juce::RangedAudioParameter& parameter) const
{
    if (! isParameterModulated(parameter.getParameterID()))
    {
        return -1.0f;
    }
    return juce::jlimit(0.0f, 1.0f,
                        applyModulationToNormalizedValue(&parameter, parameter.getValue()));
}

float PX3SynthAudioProcessor::getUnclampedModulatedNormalisedValue(
    juce::RangedAudioParameter& parameter) const
{
    if (! isParameterModulated(parameter.getParameterID()))
    {
        return -1.0f;
    }

    float unclamped = 0.0f;
    applyModulationToNormalizedValue(&parameter, parameter.getValue(), nullptr, nullptr, &unclamped);
    return unclamped;
}

juce::AudioParameterFloat& PX3SynthAudioProcessor::getOscillatorWtPositionParam(int oscIndex) const
{
    const auto idx = juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex);
    return *oscWtPositionParams[static_cast<std::size_t>(idx)];
}

juce::AudioParameterChoice& PX3SynthAudioProcessor::getOscillatorWtTableParam(int oscIndex) const
{
    const auto idx = juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex);
    return *oscWtTableParams[static_cast<std::size_t>(idx)];
}

void PX3SynthAudioProcessor::loadFactoryWavetable(int oscIndex, int tableIndex)
{
    const auto idx = juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex);
    // Built here, on the message thread, and published as an immutable table -
    // the audio thread never sees this allocation.
    if (auto table = px3::buildFactoryWavetable(tableIndex))
    {
        wavetableSlots[static_cast<std::size_t>(idx)].publish(std::move(table));
        loadedWavetableIndex[static_cast<std::size_t>(idx)] = tableIndex;
    }
}

void PX3SynthAudioProcessor::refreshWavetableSelections()
{
    for (int osc = 0; osc < kOscillatorSourceCount; ++osc)
    {
        const auto index = static_cast<std::size_t>(osc);
        const auto userName = userWavetableNames[index];

        if (userName.isNotEmpty())
        {
            // Already loaded? getLoadedWavetableName is the authority, because
            // the loaded index says nothing about user tables.
            if (getLoadedWavetableName(osc) == userName)
            {
                continue;
            }

            if (auto table = px3::WavetableLibrary::load(userName))
            {
                wavetableSlots[index].publish(std::move(table));
                loadedWavetableIndex[index] = -1;
                missingWavetableNames[index].clear();
                continue;
            }

            // A preset that names a table this machine does not have. Fall back
            // to the factory selection and REMEMBER what was missing - falling
            // back silently leaves the user with a preset that sounds wrong and
            // nothing to explain why.
            missingWavetableNames[index] = userName;
            userWavetableNames[index].clear();
            loadedWavetableIndex[index] = -1;
        }

        const auto wanted = getOscillatorWtTableParam(osc).getIndex();
        if (loadedWavetableIndex[index] == wanted)
        {
            continue;
        }

        if (auto table = px3::buildFactoryWavetable(wanted))
        {
            wavetableSlots[index].publish(std::move(table));
            loadedWavetableIndex[index] = wanted;
        }
    }
}

void PX3SynthAudioProcessor::setUserWavetableName(int oscIndex, const juce::String& name)
{
    const auto idx = static_cast<std::size_t>(juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex));
    userWavetableNames[idx] = name;
    userWavetableDisplayNames[idx] = name;
    missingWavetableNames[idx].clear();
    refreshWavetableSelections();
}

juce::String PX3SynthAudioProcessor::getUserWavetableName(int oscIndex) const
{
    return userWavetableNames[static_cast<std::size_t>(
        juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex))];
}

juce::String PX3SynthAudioProcessor::getUserWavetableDisplayName(int oscIndex) const
{
    const auto index = static_cast<std::size_t>(juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex));
    return userWavetableDisplayNames[index].isNotEmpty() ? userWavetableDisplayNames[index] : userWavetableNames[index];
}

juce::String PX3SynthAudioProcessor::getMissingWavetableName(int oscIndex) const
{
    return missingWavetableNames[static_cast<std::size_t>(
        juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex))];
}

bool PX3SynthAudioProcessor::importWavetable(int oscIndex,
                                             const juce::String& name,
                                             const std::vector<px3::FrameSpectrum>& frames,
                                             juce::String& error)
{
    if (! px3::WavetableLibrary::save(name, frames, error))
    {
        return false;
    }

    // Saved before selected, so a table that cannot be written is never the one
    // playing - otherwise it works until the session is reopened.
    setUserWavetableName(oscIndex, name);
    return true;
}

void PX3SynthAudioProcessor::handleAsyncUpdate()
{
    refreshWavetableSelections();
    serviceLazyEffects();
    collectRetiredWavetables();
    writeOnsetCapture();
}

void PX3SynthAudioProcessor::writeOnsetCapture()
{
    // Message thread. The audio thread only fills the arrays and flags them.
    if (onsetCapture == nullptr || ! onsetCapture->done
        || onsetCapture->path.isEmpty()) { return; }

    const juce::File file(onsetCapture->path);
    file.deleteFile();

    juce::String text;
    text << "sample\toutput\tampEnvelope\tvoices\tattackSeconds\theldSeconds\n";
    for (int i = 0; i < onsetCapture->written; ++i)
    {
        const auto index = static_cast<std::size_t>(i);
        text << i << "\t" << juce::String(onsetCapture->output[index], 8)
             << "\t" << juce::String(onsetCapture->ampEnvelope[index], 8)
             << "\t" << juce::String(static_cast<int>(onsetCapture->voiceCount[index]))
             << "\t" << juce::String(onsetCapture->attackSeconds[index], 4)
             << "\t" << juce::String(onsetCapture->heldSeconds[index], 6) << "\n";
    }

    file.replaceWithText(text);
    onsetCapture->path.clear();   // once only
}

juce::String PX3SynthAudioProcessor::getLoadedWavetableName(int oscIndex) const
{
    const auto idx = juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex);
    const auto* table = wavetableSlots[static_cast<std::size_t>(idx)].current();
    return table != nullptr ? table->getName() : juce::String();
}

px3::WavetableDisplay PX3SynthAudioProcessor::getWavetableDisplay(int oscIndex,
                                                                  int frames,
                                                                  int points) const
{
    px3::WavetableDisplay display;
    const auto idx = juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex);
    const auto* table = wavetableSlots[static_cast<std::size_t>(idx)].current();
    if (table == nullptr)
    {
        return display;
    }

    display.name = table->getName();
    display.category = table->getCategory();
    display.fromUserLibrary = table->getCategory() == "USER";

    const auto wantedFrames = juce::jlimit(2, table->getFrameCount(), frames);
    const auto wantedPoints = juce::jlimit(8, 2048, points);

    // Drawn from the BRIGHTEST level, so the picture shows the waveform the
    // table actually holds rather than whichever band-limited version the
    // currently playing note happens to have selected.
    const auto length = table->getLevelLength(0);

    display.frames.reserve(static_cast<std::size_t>(wantedFrames));
    for (int f = 0; f < wantedFrames; ++f)
    {
        const auto sourceFrame = wantedFrames > 1
                                   ? f * (table->getFrameCount() - 1) / (wantedFrames - 1)
                                   : 0;
        const auto* samples = table->getFrame(0, sourceFrame);

        std::vector<float> row(static_cast<std::size_t>(wantedPoints), 0.0f);
        for (int i = 0; i < wantedPoints; ++i)
        {
            row[static_cast<std::size_t>(i)] =
                samples[static_cast<std::size_t>(
                    static_cast<long long>(i) * length / wantedPoints)];
        }
        display.frames.push_back(std::move(row));
    }

    return display;
}

void PX3SynthAudioProcessor::collectRetiredWavetables()
{
    for (auto& slot : wavetableSlots)
    {
        slot.collectRetired();
    }
}

juce::AudioParameterChoice& PX3SynthAudioProcessor::getAnalogProfileParam() const { return *analogProfileParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getLfoEnabledParam() const { return getLfoEnabledParam(0); }
juce::AudioParameterBool& PX3SynthAudioProcessor::getLfoEnabledParam(int lfoIndex) const
{
    const auto idx = juce::jlimit(0, kLfoSourceCount - 1, lfoIndex);
    return *lfoEnabledParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLfoFrequencyParam() const { return getLfoFrequencyParam(0); }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLfoFrequencyParam(int lfoIndex) const
{
    const auto idx = juce::jlimit(0, kLfoSourceCount - 1, lfoIndex);
    return *lfoFrequencyParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterChoice& PX3SynthAudioProcessor::getLfoWaveformParam() const { return getLfoWaveformParam(0); }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getLfoWaveformParam(int lfoIndex) const
{
    const auto idx = juce::jlimit(0, kLfoSourceCount - 1, lfoIndex);
    return *lfoWaveformParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLfoRampTimeParam(int lfoIndex) const
{
    const auto idx = juce::jlimit(0, kLfoSourceCount - 1, lfoIndex);
    return *lfoRampTimeParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterBool& PX3SynthAudioProcessor::getLfoKeySyncParam(int lfoIndex) const
{
    const auto idx = juce::jlimit(0, kLfoSourceCount - 1, lfoIndex);
    return *lfoKeySyncParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getOscillatorPitchModParam(int oscIndex) const
{
    const auto idx = juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex);
    return *oscPitchModParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getSubOscPitchModParam() const { return *subOscPitchModParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getFilterRoutingParam() const { return *filterRoutingParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getFilterParallelBalanceParam() const { return *filterParallelBalanceParam; }
int PX3SynthAudioProcessor::getTopMenuViewIndex() const
{
    return juce::jlimit(0, kTopMenuViewCount - 1,
                        topMenuViewIndex.load(std::memory_order_relaxed));
}

void PX3SynthAudioProcessor::setTopMenuViewIndex(int index, bool notifyHost)
{
    const auto clamped = juce::jlimit(0, kTopMenuViewCount - 1, index);
    topMenuViewIndex.store(clamped, std::memory_order_relaxed);

    if (notifyHost)
    {
        updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
    }
}

bool PX3SynthAudioProcessor::sourceMuted(int sourceIndex) const
{
    const auto idx = juce::jlimit(0, kMixerSourceCount - 1, sourceIndex);
    return mixerMuteParams[static_cast<std::size_t>(idx)] != nullptr
           && mixerMuteParams[static_cast<std::size_t>(idx)]->get();
}

bool PX3SynthAudioProcessor::sourceSoloed(int sourceIndex) const
{
    const auto idx = juce::jlimit(0, kMixerSourceCount - 1, sourceIndex);
    return mixerSoloParams[static_cast<std::size_t>(idx)] != nullptr
           && mixerSoloParams[static_cast<std::size_t>(idx)]->get();
}

bool PX3SynthAudioProcessor::anySourceSoloed() const
{
    for (int i = 0; i < kMixerSourceCount; ++i)
    {
        if (sourceSoloed(i))
        {
            return true;
        }
    }
    return false;
}

bool PX3SynthAudioProcessor::anyChannelSoloed() const
{
    const auto fxSolo = fxReturnSoloParam != nullptr && fxReturnSoloParam->get();
    return anySourceSoloed() || fxSolo;
}

bool PX3SynthAudioProcessor::sourceDryAudible(int sourceIndex, bool anySolo) const
{
    if (sourceMuted(sourceIndex))
    {
        return false;
    }

    if (anySolo)
    {
        return sourceSoloed(sourceIndex);
    }

    return true;
}

bool PX3SynthAudioProcessor::sourceSendAudible(int sourceIndex, bool anySolo, bool anySourceSolo, bool fxSolo) const
{
    if (sourceMuted(sourceIndex))
    {
        return false;
    }

    if (!anySolo)
    {
        return true;
    }

    if (anySourceSolo)
    {
        return fxSolo && sourceSoloed(sourceIndex);
    }

    return fxSolo;
}

juce::AudioParameterFloat& PX3SynthAudioProcessor::getDryBusGainParam() const
{
    return *dryBusGainParam;
}

juce::AudioParameterFloat& PX3SynthAudioProcessor::getDryBusPanParam() const
{
    return *dryBusPanParam;
}

juce::AudioParameterBool& PX3SynthAudioProcessor::getDryBusMuteParam() const
{
    return *dryBusMuteParam;
}

juce::AudioParameterBool& PX3SynthAudioProcessor::getDryBusSoloParam() const
{
    return *dryBusSoloParam;
}

juce::AudioParameterBool& PX3SynthAudioProcessor::getDryBusPhaseInvertParam() const
{
    return *dryBusPhaseInvertParam;
}

bool PX3SynthAudioProcessor::dryBusAudible(bool anySolo, bool anySourceSolo, bool drySolo) const
{
    if (dryBusMuteParam != nullptr && dryBusMuteParam->get())
    {
        return false;
    }

    if (!anySolo)
    {
        return true;
    }

    // Soloing a SOURCE has to leave the dry bus open, or the solo would mute
    // the very path the soloed source is heard through. The dry channel is
    // silenced by a solo only when something else is soloed and it is not.
    return drySolo || anySourceSolo;
}

bool PX3SynthAudioProcessor::fxReturnAudible(bool anySolo, bool anySourceSolo, bool fxSolo) const
{
    if (fxReturnMuteParam != nullptr && fxReturnMuteParam->get())
    {
        return false;
    }

    if (!anySolo)
    {
        return true;
    }

    // Once anything is soloed the FX return is audible only if the FX channel
    // itself is soloed. This does not depend on whether a source is also soloed.
    juce::ignoreUnused(anySourceSolo);
    return fxSolo;
}

px3::FxOrder PX3SynthAudioProcessor::getFxProcessingOrder() const
{
    // Stored order is packed atomically; sanitize on read so malformed legacy or
    // duplicate values always recover to a valid permutation.
    const auto packed = fxProcessingOrderPacked.load(std::memory_order_relaxed);
    const auto raw = unpackFxOrder(packed);

    return sanitizeFxOrder(raw);
}

void PX3SynthAudioProcessor::setFxProcessingOrder(const px3::FxOrder& order)
{
    setFxProcessingOrderWithReason(order, "UNKNOWN", "UNSPECIFIED", -1, -1);
}

void PX3SynthAudioProcessor::setFxProcessingOrderWithReason(const px3::FxOrder& order,
                                                                const juce::String& source,
                                                                const juce::String& reason,
                                                                int fromIndex,
                                                                int toIndex)
{
    // Authoritative module order lives in the processor (not UI). UI drag-drop
    // requests are sanitized and committed here so DSP, state save, and debug
    // diagnostics all observe the same canonical order.
    const auto sanitized = sanitizeFxOrder(order);

    const auto packed = packFxOrder(sanitized);
    const auto previous = fxProcessingOrderPacked.load(std::memory_order_relaxed);
    if (packed == previous)
    {
        return;
    }

    const auto beforeOrder = getFxProcessingOrder();
    const auto oldRevision = fxOrderRevision.load(std::memory_order_relaxed);
    fxProcessingOrderPacked.store(packed, std::memory_order_relaxed);
    const auto newRevision = fxOrderRevision.fetch_add(1u, std::memory_order_relaxed) + 1u;
    const auto afterOrder = getFxProcessingOrder();

    debugLogEvent(source,
                  "MODULE_ORDER_CHANGED",
                  "reason=" + reason
                      + " fromIndex=" + juce::String(fromIndex)
                      + " toIndex=" + juce::String(toIndex)
                      + " oldOrder=" + debugDescribeOrder(beforeOrder)
                      + " newOrder=" + debugDescribeOrder(afterOrder)
                      + " oldHash=" + juce::String(static_cast<int64_t>(previous))
                      + " newHash=" + juce::String(static_cast<int64_t>(packed))
                      + " gen=" + juce::String(static_cast<int64_t>(oldRevision))
                      + "->" + juce::String(static_cast<int64_t>(newRevision)));

    // Module order is part of plugin state but not an automatable parameter.
    // Explicit host notifications ensure project dirty-state and save prompts
    // stay accurate after UI drag reorder operations.
    updateHostDisplay();
    updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
    updateHostDisplay(juce::AudioProcessor::ChangeDetails().withProgramChanged(true));
}


//==============================================================================
// Macro control system. See docs/macro-system-design.md.
//==============================================================================

juce::String PX3SynthAudioProcessor::macroParameterId(int macroIndex)
{
    return "mod.macro" + juce::String(juce::jlimit(0, kMacroCount - 1, macroIndex) + 1) + ".value";
}

juce::String PX3SynthAudioProcessor::macroDisplayName(int macroIndex)
{
    return "MACRO " + juce::String(juce::jlimit(0, kMacroCount - 1, macroIndex) + 1);
}

juce::AudioParameterFloat& PX3SynthAudioProcessor::getMacroParam(int macroIndex) const
{
    return *macroParams[static_cast<std::size_t>(juce::jlimit(0, kMacroCount - 1, macroIndex))];
}

bool PX3SynthAudioProcessor::setMacroDestinationDepth(int macroIndex,
                                                      const juce::String& parameterId,
                                                      float depth)
{
    const std::lock_guard<std::recursive_mutex> lock(graphAuthoringMutex);
    if (! juce::isPositiveAndBelow(macroIndex, kMacroCount) || ! std::isfinite(depth)) { return false; }

    auto& list = macroDestinations[static_cast<std::size_t>(macroIndex)];
    const auto entry = std::find_if(list.begin(), list.end(),
                                    [&parameterId](const MacroDestination& destination)
                                    { return destination.parameterId == parameterId; });

    if (entry == list.end()) { return false; }

    const auto previous = entry->depth;
    entry->depth = juce::jlimit(-1.0f, 1.0f, depth);

    // The audio thread reads the resolved table, not this list, so the edit is
    // not audible until the table is rebuilt. Rebuilding here rather than
    // leaving it to the caller is what stops a depth that shows on screen and
    // is not in the sound.
    if (! rebuildModulationGraph()) { entry->depth = previous; return false; }
    return true;
}

float PX3SynthAudioProcessor::getMacroDestinationDepth(int macroIndex,
                                                       const juce::String& parameterId) const
{
    const std::lock_guard<std::recursive_mutex> lock(graphAuthoringMutex);
    if (! juce::isPositiveAndBelow(macroIndex, kMacroCount)) { return 0.0f; }

    const auto& list = macroDestinations[static_cast<std::size_t>(macroIndex)];
    const auto entry = std::find_if(list.begin(), list.end(),
                                    [&parameterId](const MacroDestination& destination)
                                    { return destination.parameterId == parameterId; });

    return entry != list.end() ? entry->depth : 0.0f;
}

bool PX3SynthAudioProcessor::isMacroDestination(int macroIndex,
                                                const juce::String& parameterId) const
{
    const std::lock_guard<std::recursive_mutex> lock(graphAuthoringMutex);
    if (! juce::isPositiveAndBelow(macroIndex, kMacroCount)) { return false; }

    const auto& list = macroDestinations[static_cast<std::size_t>(macroIndex)];
    return std::any_of(list.begin(), list.end(),
                       [&parameterId](const MacroDestination& destination)
                       { return destination.parameterId == parameterId; });
}

bool PX3SynthAudioProcessor::toggleMacroDestination(int macroIndex,
                                                    const juce::String& parameterId)
{
    const std::lock_guard<std::recursive_mutex> lock(graphAuthoringMutex);
    if (! juce::isPositiveAndBelow(macroIndex, kMacroCount) || parameterId.isEmpty())
    {
        return false;
    }

    // A macro cannot drive a macro. Not a scope decision so much as a loop
    // waiting to happen.
    for (int macro = 0; macro < kMacroCount; ++macro)
    {
        if (parameterId == macroParameterId(macro)) { return false; }
    }

    if (findParameterById(parameterId) == nullptr) { return false; }

    auto& list = macroDestinations[static_cast<std::size_t>(macroIndex)];
    const auto previous = list;
    const auto existing = std::find_if(list.begin(), list.end(),
                                       [&parameterId](const MacroDestination& destination)
                                       { return destination.parameterId == parameterId; });

    auto assigned = false;
    if (existing != list.end())
    {
        list.erase(existing);
    }
    else
    {
        // Full depth, positive, which is what a macro did before there was a
        // depth editor. New assignments therefore sound exactly as they always
        // did, and only a deliberate edit changes that.
        list.push_back({ parameterId, kMacroDepthDefault });
        assigned = true;
    }

    if (! rebuildModulationGraph()) { list = previous; return ! assigned; }
    return assigned;
}

std::vector<PX3SynthAudioProcessor::MacroDestination>
PX3SynthAudioProcessor::getMacroDestinations(int macroIndex) const
{
    const std::lock_guard<std::recursive_mutex> lock(graphAuthoringMutex);
    if (! juce::isPositiveAndBelow(macroIndex, kMacroCount)) { return {}; }
    return macroDestinations[static_cast<std::size_t>(macroIndex)];
}

void PX3SynthAudioProcessor::clearMacroDestinations(int macroIndex)
{
    const std::lock_guard<std::recursive_mutex> lock(graphAuthoringMutex);
    if (! juce::isPositiveAndBelow(macroIndex, kMacroCount)) { return; }

    auto& routes = macroDestinations[static_cast<std::size_t>(macroIndex)];
    const auto previous = routes;
    routes.clear();
    if (! rebuildModulationGraph()) { routes = previous; }
}

int PX3SynthAudioProcessor::getMacroMaskForParameter(const juce::String& parameterId) const
{
    const std::lock_guard<std::recursive_mutex> lock(graphAuthoringMutex);
    auto mask = 0;
    for (int macro = 0; macro < kMacroCount; ++macro)
    {
        if (isMacroDestination(macro, parameterId)) { mask |= (1 << macro); }
    }
    return mask;
}

bool PX3SynthAudioProcessor::isGraphDestination(const juce::String& id) const
{
    const auto* entry = parameterCatalog.find(id);
    return entry != nullptr && entry->modulationDestination;
}

bool PX3SynthAudioProcessor::setGraphRoute(int slot, const GraphRouteConfiguration& route, juce::String& error)
{
    const std::lock_guard<std::recursive_mutex> lock(graphAuthoringMutex);
    if (! juce::isPositiveAndBelow(slot, kGraphRouteSlots))
    {
        error = "Unknown modulation route slot.";
        return false;
    }
    auto candidate = graphRouteConfigurations;
    candidate[static_cast<std::size_t>(slot)] = route;
    px3::synth::CompiledModulationGraph plan;
    if (! compileModulationGraph(candidate, plan, error)) { return false; }
    if (! modulationGraph.publish(plan))
    {
        error = "Modulation graph buffers are busy; retry the edit.";
        return false;
    }
    graphRouteConfigurations = std::move(candidate);
    updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
    return true;
}

PX3SynthAudioProcessor::GraphRouteConfiguration PX3SynthAudioProcessor::getGraphRoute(int slot) const
{
    const std::lock_guard<std::recursive_mutex> lock(graphAuthoringMutex);
    return juce::isPositiveAndBelow(slot, kGraphRouteSlots)
               ? graphRouteConfigurations[static_cast<std::size_t>(slot)] : GraphRouteConfiguration {};
}

juce::AudioParameterFloat& PX3SynthAudioProcessor::getGraphRouteDepthParam(int slot) const
{
    return *graphRouteDepthParams[static_cast<std::size_t>(juce::jlimit(0, kGraphRouteSlots - 1, slot))];
}

juce::String PX3SynthAudioProcessor::graphSourceName(int source)
{
    if (juce::isPositiveAndBelow(source, kLfoSourceCount)) { return "LFO " + juce::String(source + 1); }
    source -= kLfoSourceCount;
    if (juce::isPositiveAndBelow(source, kEnvelopeSourceCount)) { return "ENV " + juce::String(source + 1); }
    source -= kEnvelopeSourceCount;
    return juce::isPositiveAndBelow(source, kMacroCount) ? macroDisplayName(source) : juce::String();
}

juce::String PX3SynthAudioProcessor::graphSourceId(int source)
{
    if (juce::isPositiveAndBelow(source, kLfoSourceCount)) { return "mod.lfo" + juce::String(source + 1); }
    source -= kLfoSourceCount;
    if (juce::isPositiveAndBelow(source, kEnvelopeSourceCount)) { return juce::String("mod.env") + juce::String(source + 1); }
    source -= kEnvelopeSourceCount;
    return juce::isPositiveAndBelow(source, kMacroCount) ? "mod.macro" + juce::String(source + 1) : juce::String();
}

bool PX3SynthAudioProcessor::rebuildModulationGraph()
{
    px3::synth::CompiledModulationGraph plan;
    juce::String error;
    return compileModulationGraph(graphRouteConfigurations, plan, error) && modulationGraph.publish(plan);
}

bool PX3SynthAudioProcessor::compileModulationGraph(
    const std::array<GraphRouteConfiguration, kGraphRouteSlots>& configurations,
    px3::synth::CompiledModulationGraph& plan, juce::String& graphError,
    const std::array<std::vector<MacroDestination>, kMacroCount>* macros) const
{
    const std::lock_guard<std::recursive_mutex> lock(graphAuthoringMutex);
    using namespace px3::synth;
    std::array<ModulationSourceDescriptor, kLfoSourceCount + kEnvelopeSourceCount + kMacroCount> sources {};
    for (int source = 0; source < kLfoSourceCount; ++source) { sources[static_cast<std::size_t>(source)].bipolar = true; }
    std::array<ModulationDestinationDescriptor, CompiledModulationGraph::destinationCapacity> destinations {};
    for (int source = 0; source < kLfoSourceCount; ++source)
    {
        destinations[static_cast<std::size_t>(getLfoFrequencyParam(source).getParameterIndex())].controllingSource = source;
        destinations[static_cast<std::size_t>(getLfoRampTimeParam(source).getParameterIndex())].controllingSource = source;
    }
    for (int source = 0; source < kEnvelopeSourceCount; ++source)
    {
        const auto index = static_cast<std::size_t>(source);
        for (auto* parameter : { attackParams[index], decayParams[index], sustainParams[index], releaseParams[index] })
        {
            destinations[static_cast<std::size_t>(parameter->getParameterIndex())].controllingSource = kLfoSourceCount + source;
        }
    }
    std::vector<ModulationRoute> routes;
    auto slot = kMacroRouteSlotBase;
    const auto& macroConfigurations = macros != nullptr ? *macros : macroDestinations;
    for (int macro = 0; macro < kMacroCount; ++macro)
    {
        for (const auto& destination : macroConfigurations[static_cast<std::size_t>(macro)])
        {
            auto* entry = parameterCatalog.find(destination.parameterId);
            auto* target = entry != nullptr ? entry->parameter : nullptr;
            if (target == nullptr || slot >= kMacroRouteSlotBase + kMacroRouteSlots)
            {
                graphError = "Macro modulation route has an unavailable endpoint or exceeds capacity.";
                return false;
            }
            routes.push_back({ slot++, kLfoSourceCount + kEnvelopeSourceCount + macro,
                               target->getParameterIndex(), destination.depth });
        }
    }
    for (int index = 0; index < kGraphRouteSlots; ++index)
    {
        const auto& configuration = configurations[static_cast<std::size_t>(index)];
        if (configuration.source == -1 && configuration.destination.isEmpty()) { continue; }
        const auto* entry = parameterCatalog.find(configuration.destination);
        auto* target = entry != nullptr ? dynamic_cast<juce::RangedAudioParameter*>(entry->parameter) : nullptr;
        const auto eligible = isGraphDestination(configuration.destination);
        if (! eligible)
        {
            graphError = "Modulation destination is unavailable: " + configuration.destination;
            return false;
        }
        routes.push_back({ kGraphRouteSlotBase + index,
                           configuration.source, target->getParameterIndex(), 0.0f,
                           configuration.polarity, configuration.curve });
    }
    std::string error;
    const auto compiled = plan.compile(sources, { destinations.data(), static_cast<std::size_t>(getParameters().size()) }, routes, error);
    graphError = error;
    return compiled;
}


void PX3SynthAudioProcessor::initialiseVoiceModulationTargets()
{
    using T = px3::synth::VoiceModTarget;
    auto set = [this](T target, juce::AudioParameterFloat& parameter)
    {
        voiceModTargets[static_cast<std::size_t>(target)] = &parameter;
        const auto index = parameter.getParameterIndex();
        if (index >= 0 && static_cast<std::size_t>(index) < voiceModulatedParameter.size())
        {
            voiceModulatedParameter[static_cast<std::size_t>(index)] = true;
        }
    };
    set(T::filter1Cutoff, getFilterCutoffParam(0));
    set(T::filter2Cutoff, getFilterCutoffParam(1));
    set(T::filter1Resonance, getFilterResonanceParam(0));
    set(T::filter2Resonance, getFilterResonanceParam(1));
    for (int osc = 0; osc < kOscillatorSourceCount; ++osc)
    {
        set(static_cast<T>(static_cast<int>(T::osc1Fine) + osc), getOscillatorFineParam(osc));
        set(static_cast<T>(static_cast<int>(T::osc1PitchMod) + osc), getOscillatorPitchModParam(osc));
        set(static_cast<T>(static_cast<int>(T::osc1MacroA) + osc), getOscillatorMacroAParam(osc));
        set(static_cast<T>(static_cast<int>(T::osc1MacroB) + osc), getOscillatorMacroBParam(osc));
        set(static_cast<T>(static_cast<int>(T::osc1MacroC) + osc), getOscillatorMacroCParam(osc));
        set(static_cast<T>(static_cast<int>(T::osc1WtPosition) + osc), getOscillatorWtPositionParam(osc));
    }
}

void PX3SynthAudioProcessor::buildVoiceModulationPlan(px3::synth::VoiceModulationPlan& plan) const
{
    plan.active = false;
    auto graph = modulationGraph.read();
    if (! graph) { return; }

    // The same depth table the global evaluation uses.
    auto depths = graph->depths();
    for (int slot = 0; slot < kGraphRouteSlots; ++slot)
    {
        depths[static_cast<std::size_t>(kGraphRouteSlotBase + slot)]
            = graphRouteDepthParams[static_cast<std::size_t>(slot)]->get();
    }

    for (int target = 0; target < px3::synth::kVoiceModTargetCount; ++target)
    {
        auto& destination = plan.destinations[static_cast<std::size_t>(target)];
        destination.routeCount = 0;
        auto* parameter = voiceModTargets[static_cast<std::size_t>(target)];
        if (parameter == nullptr) { continue; }

        const auto base = juce::jlimit(0.0f, 1.0f, static_cast<juce::RangedAudioParameter*>(parameter)->getValue());
        graph->forEachRouteTo(parameter->getParameterIndex(), [&](const px3::synth::ModulationRoute& route,
                                                                  const px3::synth::ModulationSourceDescriptor& source)
        {
            const auto envelope = route.source - kLfoSourceCount;
            if (envelope < 0 || envelope >= kEnvelopeSourceCount || ! getEnvelopeEnabledParam(envelope).get()
                || destination.routeCount >= px3::synth::kVoiceModRoutesPerTarget)
            {
                return;
            }
            auto& voiceRoute = destination.routes[static_cast<std::size_t>(destination.routeCount++)];
            voiceRoute.envelope = envelope;
            voiceRoute.depth = static_cast<std::size_t>(route.slot) < depths.size()
                                   ? depths[static_cast<std::size_t>(route.slot)] : route.depth;
            voiceRoute.sourceBipolar = source.bipolar;
            voiceRoute.polarity = route.polarity;
            voiceRoute.curve = route.curve;
        });
        if (destination.routeCount == 0) { continue; }

        float unfolded = base;
        applyModulationToNormalizedValue(parameter, base, nullptr, nullptr, &unfolded);
        destination.base = base;
        destination.unfolded = unfolded;
        destination.start = parameter->range.start;
        destination.end = parameter->range.end;
        destination.skew = parameter->range.skew;
        plan.active = true;
    }
}
