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

    float totalDelta = 0.0f;
    const auto parameterId = parameter->getParameterID();

    const auto accumulateSourceDelta = [&](std::atomic<int> const& assignmentIndex,
                                           float signal,
                                           float amount,
                                           bool bipolar)
    {
        const auto assignment = juce::jlimit(0,
                                             juce::jmax(0, static_cast<int>(lfoAssignableTargets.size()) - 1),
                                             assignmentIndex.load(std::memory_order_relaxed));
        if (assignment <= 0 || assignment >= static_cast<int>(lfoAssignableTargets.size()))
        {
            return;
        }

        const auto& target = lfoAssignableTargets[static_cast<std::size_t>(assignment)];
        const auto sameId = target.parameterId.equalsIgnoreCase(parameter->getParameterID());
        const auto samePointer = (target.parameter == parameter);
        if (sameId || samePointer)
        {
            // The room the base actually has, in the direction this source can
            // travel. Scaling by it means full amount arrives exactly at the end
            // of the range and turns around there, instead of driving past it
            // and being clamped flat - which turns a sine into a square with
            // rounded shoulders and holds every other shape at its limit.
            //
            // The two source kinds need different room. An LFO is bipolar and
            // swings both ways, so it gets the NEARER side and stays centred on
            // the base. An envelope only ever travels one way, decided by the
            // sign of its amount, so it gets the whole of that side and can
            // still reach the end of the range from anywhere.
            // Scale the swing to the room the base value actually leaves,
            // rather than letting the sum run past the range and be clamped.
            //
            // Clamping is not wrong in itself, but it turns a sine into a
            // square with rounded shoulders and holds every other shape at its
            // limit: at base 0.5 and full amount the value spent 65.6% of every
            // cycle pinned at an end, measured, in stalls of 661 ms. Scaling
            // arrives at the boundary exactly and turns around there.
            //
            // The two source kinds need different room. An LFO is bipolar, so
            // it needs the SAME headroom on both sides to stay centred on the
            // base - the nearer side is what it gets. An envelope is unipolar
            // and only ever travels one way, so it gets that whole side.
            //
            // This applies to every destination. It began scoped to the
            // wavetable scan, where a clamped LFO is most visible because the
            // stack stops moving, but the same flattening was happening on
            // cutoff, pitch and the rest where it was only audible.
            const auto headroom = bipolar
                                    ? juce::jmin(base, 1.0f - base)
                                    : (amount >= 0.0f ? 1.0f - base : base);

            totalDelta += target.normalizedDepth * headroom * (signal * amount);
        }
    };

    for (int i = 0; i < kLfoSourceCount; ++i)
    {
        const auto index = static_cast<std::size_t>(i);
        // Every LFO shape is bipolar - sine, triangle, saw and square all run
        // -1..+1 - so they all centre on the base.
        accumulateSourceDelta(lfoAssignmentAtomic(i),
                              lfoCurrentValues[index].load(std::memory_order_relaxed),
                              getLfoAmountParam(i).get(),
                              true);
    }
    for (int i = 0; i < kEnvelopeSourceCount; ++i)
    {
        const auto index = static_cast<std::size_t>(i);
        const auto envSignal = juce::jlimit(0.0f,
                                            1.0f,
                                            modulationEnvelopeValues[index].load(std::memory_order_relaxed));
        const auto envAmount = juce::jlimit(-1.0f, 1.0f, getEnvelopeAmountParam(i).get());

        accumulateSourceDelta(envelopeAssignmentAtomic(i), envSignal, envAmount, false);
    }

    // Applied in normalized space, then clamped once. This preserves additive
    // behaviour across multiple sources - and the pre-clamp value is reported
    // so a test can tell "modulation stayed in range" from "modulation was cut
    // off at the edge", which look identical afterwards.
    if (outUnclampedNormalized != nullptr)
    {
        *outUnclampedNormalized = base + totalDelta;
    }
    effective = clamp01(base + totalDelta);

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
juce::AudioParameterFloat& PX3SynthAudioProcessor::getOscillatorFineParam(int oscIndex) const
{
    const auto idx = juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex);
    return *oscFineParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getOscillatorPitchParam(int oscIndex) const
{
    const auto idx = juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex);
    return *oscPitchParams[static_cast<std::size_t>(idx)];
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
juce::AudioParameterFloat& PX3SynthAudioProcessor::getSubOscPitchParam() const { return *subOscPitchParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getSubOscOctaveParam() const { return *subOscOctaveParam; }
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
juce::AudioParameterFloat& PX3SynthAudioProcessor::getVibeAmountParam() const { return *vibeAmountParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getVibeEnabledParam() const { return *vibeEnabledParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getVibeTypeParam() const { return *vibeTypeParam; }
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
juce::AudioParameterBool& PX3SynthAudioProcessor::getLucyFreezeParam() const { return *lucyFreezeParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getLucyFreezeSlushyParam() const { return *lucyFreezeSlushyParam; }
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
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLucyGateCutoffParam() const { return *lucyGateCutoffParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLucyThresholdParam() const { return *lucyThresholdParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLucyAutoGainParam() const { return *lucyAutoGainParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLucyWeightingParam() const { return *lucyWeightingParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLucyGainParam() const { return *lucyGainParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLucySpreadParam() const { return *lucySpreadParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getLucyModeParam() const { return *lucyModeParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getLucyPacketsParam() const { return *lucyPacketsParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getLucySlopeParam() const { return *lucySlopeParam; }

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
bool PX3SynthAudioProcessor::isParameterModulated(const juce::String& parameterId) const
{
    const auto pointsAt = [this, &parameterId](std::atomic<int> const& assignmentIndex)
    {
        const auto assignment = juce::jlimit(0,
                                             juce::jmax(0, static_cast<int>(lfoAssignableTargets.size()) - 1),
                                             assignmentIndex.load(std::memory_order_relaxed));
        if (assignment <= 0 || assignment >= static_cast<int>(lfoAssignableTargets.size()))
        {
            return false;
        }
        return lfoAssignableTargets[static_cast<std::size_t>(assignment)]
            .parameterId.equalsIgnoreCase(parameterId);
    };

    for (int i = 0; i < kLfoSourceCount; ++i)
    {
        if (pointsAt(lfoAssignmentAtomic(i)) && getLfoEnabledParam(i).get())
        {
            return true;
        }
    }
    for (int i = 0; i < kEnvelopeSourceCount; ++i)
    {
        if (pointsAt(envelopeAssignmentAtomic(i)) && getEnvelopeEnabledParam(i).get())
        {
            return true;
        }
    }
    return false;
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
    missingWavetableNames[idx].clear();
    refreshWavetableSelections();
}

juce::String PX3SynthAudioProcessor::getUserWavetableName(int oscIndex) const
{
    return userWavetableNames[static_cast<std::size_t>(
        juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex))];
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
    collectRetiredWavetables();
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
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLfoAmountParam() const { return getLfoAmountParam(0); }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLfoAmountParam(int lfoIndex) const
{
    const auto idx = juce::jlimit(0, kLfoSourceCount - 1, lfoIndex);
    return *lfoAmountParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterChoice& PX3SynthAudioProcessor::getLfoWaveformParam() const { return getLfoWaveformParam(0); }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getLfoWaveformParam(int lfoIndex) const
{
    const auto idx = juce::jlimit(0, kLfoSourceCount - 1, lfoIndex);
    return *lfoWaveformParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getEnvelopeAmountParam() const { return getEnvelopeAmountParam(0); }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getEnvelopeAmountParam(int envIndex) const
{
    const auto idx = juce::jlimit(0, kEnvelopeSourceCount - 1, envIndex);
    return *envelopeAmountParams[static_cast<std::size_t>(idx)];
}
std::atomic<int>& PX3SynthAudioProcessor::lfoAssignmentAtomic(int lfoIndex)
{
    const auto idx = juce::jlimit(0, kLfoSourceCount - 1, lfoIndex);
    return lfoAssignmentIndices[static_cast<std::size_t>(idx)];
}

std::atomic<int> const& PX3SynthAudioProcessor::lfoAssignmentAtomic(int lfoIndex) const
{
    const auto idx = juce::jlimit(0, kLfoSourceCount - 1, lfoIndex);
    return lfoAssignmentIndices[static_cast<std::size_t>(idx)];
}

std::atomic<int>& PX3SynthAudioProcessor::envelopeAssignmentAtomic(int envIndex)
{
    const auto idx = juce::jlimit(0, kEnvelopeSourceCount - 1, envIndex);
    return envelopeAssignmentIndices[static_cast<std::size_t>(idx)];
}

std::atomic<int> const& PX3SynthAudioProcessor::envelopeAssignmentAtomic(int envIndex) const
{
    const auto idx = juce::jlimit(0, kEnvelopeSourceCount - 1, envIndex);
    return envelopeAssignmentIndices[static_cast<std::size_t>(idx)];
}


int PX3SynthAudioProcessor::getTopMenuViewIndex() const
{
    return juce::jlimit(0, 5, topMenuViewIndex.load(std::memory_order_relaxed));
}

void PX3SynthAudioProcessor::setTopMenuViewIndex(int index, bool notifyHost)
{
    const auto clamped = juce::jlimit(0, 5, index);
    topMenuViewIndex.store(clamped, std::memory_order_relaxed);

    if (notifyHost)
    {
        updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
    }
}

const juce::StringArray& PX3SynthAudioProcessor::getLfoAssignmentDisplayNames() const
{
    return lfoAssignmentDisplayNames;
}

int PX3SynthAudioProcessor::getAssignmentIndex(std::atomic<int> const& sourceIndex) const
{
    return juce::jlimit(0,
                        juce::jmax(0, static_cast<int>(lfoAssignableTargets.size()) - 1),
                        sourceIndex.load(std::memory_order_relaxed));
}

juce::String PX3SynthAudioProcessor::getAssignmentParameterId(std::atomic<int> const& sourceIndex) const
{
    const auto index = getAssignmentIndex(sourceIndex);
    if (index <= 0 || index >= static_cast<int>(lfoAssignableTargets.size()))
    {
        return "none";
    }

    return lfoAssignableTargets[static_cast<std::size_t>(index)].parameterId;
}

bool PX3SynthAudioProcessor::setAssignmentIndex(std::atomic<int>& sourceIndex,
                                                int index,
                                                bool notifyHost,
                                                const juce::String& sourceName)
{
    if (lfoAssignableTargets.empty())
    {
        sourceIndex.store(0, std::memory_order_relaxed);
        return false;
    }

    const auto clamped = juce::jlimit(0,
                                      static_cast<int>(lfoAssignableTargets.size()) - 1,
                                      index);
    sourceIndex.store(clamped, std::memory_order_relaxed);

    if (notifyHost)
    {
        updateHostDisplay();
        updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
    }

    debugLogEvent(sourceName,
                  "ASSIGNMENT_CHANGED",
                  "index=" + juce::String(clamped)
                      + " id=" + getAssignmentParameterId(sourceIndex));
    return true;
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

bool PX3SynthAudioProcessor::setAssignmentByParameterId(std::atomic<int>& sourceIndex,
                                                        const juce::String& parameterId,
                                                        bool notifyHost,
                                                        const juce::String& sourceName)
{
    if (parameterId.isEmpty() || parameterId.equalsIgnoreCase("none"))
    {
        return setAssignmentIndex(sourceIndex, 0, notifyHost, sourceName);
    }

    for (int i = 0; i < static_cast<int>(lfoAssignableTargets.size()); ++i)
    {
        if (lfoAssignableTargets[static_cast<std::size_t>(i)].parameterId.equalsIgnoreCase(parameterId))
        {
            return setAssignmentIndex(sourceIndex, i, notifyHost, sourceName);
        }
    }

    return false;
}

int PX3SynthAudioProcessor::getLfoAssignmentIndex() const
{
    return getLfoAssignmentIndex(0);
}

int PX3SynthAudioProcessor::getLfoAssignmentIndex(int lfoIndex) const
{
    return getAssignmentIndex(lfoAssignmentAtomic(lfoIndex));
}

juce::String PX3SynthAudioProcessor::getLfoAssignmentParameterId() const
{
    return getLfoAssignmentParameterId(0);
}

juce::String PX3SynthAudioProcessor::getLfoAssignmentParameterId(int lfoIndex) const
{
    return getAssignmentParameterId(lfoAssignmentAtomic(lfoIndex));
}

bool PX3SynthAudioProcessor::setLfoAssignmentIndex(int index, bool notifyHost)
{
    return setLfoAssignmentIndex(0, index, notifyHost);
}

bool PX3SynthAudioProcessor::setLfoAssignmentIndex(int lfoIndex, int index, bool notifyHost)
{
    const auto sourceName = "LFO" + juce::String(juce::jlimit(0, kLfoSourceCount - 1, lfoIndex) + 1);
    return setAssignmentIndex(lfoAssignmentAtomic(lfoIndex), index, notifyHost, sourceName);
}

bool PX3SynthAudioProcessor::setLfoAssignmentByParameterId(const juce::String& parameterId, bool notifyHost)
{
    return setLfoAssignmentByParameterId(0, parameterId, notifyHost);
}

bool PX3SynthAudioProcessor::setLfoAssignmentByParameterId(int lfoIndex,
                                                           const juce::String& parameterId,
                                                           bool notifyHost)
{
    const auto sourceName = "LFO" + juce::String(juce::jlimit(0, kLfoSourceCount - 1, lfoIndex) + 1);
    return setAssignmentByParameterId(lfoAssignmentAtomic(lfoIndex), parameterId, notifyHost, sourceName);
}

const juce::StringArray& PX3SynthAudioProcessor::getEnvelopeAssignmentDisplayNames() const
{
    return lfoAssignmentDisplayNames;
}

int PX3SynthAudioProcessor::getEnvelopeAssignmentIndex() const
{
    return getEnvelopeAssignmentIndex(0);
}

int PX3SynthAudioProcessor::getEnvelopeAssignmentIndex(int envIndex) const
{
    return getAssignmentIndex(envelopeAssignmentAtomic(envIndex));
}

juce::String PX3SynthAudioProcessor::getEnvelopeAssignmentParameterId() const
{
    return getEnvelopeAssignmentParameterId(0);
}

juce::String PX3SynthAudioProcessor::getEnvelopeAssignmentParameterId(int envIndex) const
{
    return getAssignmentParameterId(envelopeAssignmentAtomic(envIndex));
}

bool PX3SynthAudioProcessor::setEnvelopeAssignmentIndex(int index, bool notifyHost)
{
    return setEnvelopeAssignmentIndex(0, index, notifyHost);
}

bool PX3SynthAudioProcessor::setEnvelopeAssignmentIndex(int envIndex, int index, bool notifyHost)
{
    const auto sourceName = "ENV" + juce::String(juce::jlimit(0, kEnvelopeSourceCount - 1, envIndex) + 1);
    return setAssignmentIndex(envelopeAssignmentAtomic(envIndex), index, notifyHost, sourceName);
}

bool PX3SynthAudioProcessor::setEnvelopeAssignmentByParameterId(const juce::String& parameterId, bool notifyHost)
{
    return setEnvelopeAssignmentByParameterId(0, parameterId, notifyHost);
}

bool PX3SynthAudioProcessor::setEnvelopeAssignmentByParameterId(int envIndex,
                                                                const juce::String& parameterId,
                                                                bool notifyHost)
{
    const auto sourceName = "ENV" + juce::String(juce::jlimit(0, kEnvelopeSourceCount - 1, envIndex) + 1);
    return setAssignmentByParameterId(envelopeAssignmentAtomic(envIndex), parameterId, notifyHost, sourceName);
}

void PX3SynthAudioProcessor::buildLfoAssignableTargets()
{
    // This list is the authoritative mapping between UI assignment index and
    // processor parameter targets. It is built from existing float parameters
    // so new automatable controls become assignable without custom plumbing.
    lfoAssignableTargets.clear();
    lfoAssignmentDisplayNames.clear();

    lfoAssignableTargets.push_back({ "none", "None", nullptr, 0.0f });
    lfoAssignmentDisplayNames.add("None");

    // Source levels are exposed to modulation under stable canonical IDs that
    // are deliberately NOT the IDs of the mixer parameters they drive. Presets
    // store an assignment by name, so these names are part of the saved format
    // and must not be renamed to follow the mixer parameter IDs.
    const auto addCanonicalTarget = [this](const juce::String& canonicalId,
                                           const juce::String& displayName,
                                           juce::RangedAudioParameter& runtimeParam)
    {
        lfoAssignableTargets.push_back({ canonicalId, displayName, &runtimeParam,
                                         lfoDepthForParameterId(canonicalId) });
        lfoAssignmentDisplayNames.add(displayName);
    };

    addCanonicalTarget("subOscLevel", "Sub Osc Level", getMixerLevelParam(mixerSub));
    addCanonicalTarget("osc1Level", "Osc 1 Level", getMixerLevelParam(mixerOsc1));
    addCanonicalTarget("osc2Level", "Osc 2 Level", getMixerLevelParam(mixerOsc2));
    addCanonicalTarget("osc3Level", "Osc 3 Level", getMixerLevelParam(mixerOsc3));

    const auto shouldExclude = [](const juce::String& id)
    {
        // Exclude controls that define modulation behavior itself, rather than
        // being destinations of modulation.
                return id.equalsIgnoreCase("lfoFrequency")
                         || id.equalsIgnoreCase("lfoAmount")
                         || id.equalsIgnoreCase("lfoEnabled")
                             || id.equalsIgnoreCase("lfoWaveform")
                             || id.containsIgnoreCase("lfo2")
                             || id.containsIgnoreCase("lfo3")
                             || id.equalsIgnoreCase("envAmount")
                             || id.equalsIgnoreCase("env2Amount")
                             || id.equalsIgnoreCase("env3Amount")
                             || id.equalsIgnoreCase("ampAttack")
                             || id.equalsIgnoreCase("ampDecay")
                             || id.equalsIgnoreCase("ampSustain")
                             || id.equalsIgnoreCase("ampRelease")
                             || id.equalsIgnoreCase("ampEnvEnabled")
                             || id.containsIgnoreCase("env1Attack")
                             || id.containsIgnoreCase("env1Decay")
                             || id.containsIgnoreCase("env1Sustain")
                             || id.containsIgnoreCase("env1Release")
                             || id.containsIgnoreCase("env1Enabled")
                             || id.containsIgnoreCase("env2Attack")
                             || id.containsIgnoreCase("env2Decay")
                             || id.containsIgnoreCase("env2Sustain")
                             || id.containsIgnoreCase("env2Release")
                             || id.containsIgnoreCase("env2Enabled")
                             || id.containsIgnoreCase("env3Attack")
                             || id.containsIgnoreCase("env3Decay")
                             || id.containsIgnoreCase("env3Sustain")
                             || id.containsIgnoreCase("env3Release")
                             || id.containsIgnoreCase("env3Enabled")
               || id.equalsIgnoreCase("pitchBendRange");
    };

    for (auto* parameter : getParameters())
    {
        auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(parameter);
        if (floatParam == nullptr)
        {
            continue;
        }

        const auto id = floatParam->getParameterID();
        if (shouldExclude(id))
        {
            continue;
        }

        lfoAssignableTargets.push_back({ id,
                                         floatParam->getName(64),
                                         floatParam,
                                         lfoDepthForParameterId(id) });

        lfoAssignmentDisplayNames.add(floatParam->getName(64));
    }

    for (auto& assignment : lfoAssignmentIndices)
    {
        assignment.store(0, std::memory_order_relaxed);
    }
    for (auto& assignment : envelopeAssignmentIndices)
    {
        assignment.store(0, std::memory_order_relaxed);
    }
}

float PX3SynthAudioProcessor::lfoDepthForParameterId(const juce::String& parameterId) const
{
    juce::ignoreUnused(parameterId);
    // Source amount is already user-scaled (-100%..+100%).
    // Use full normalized depth here so routing is clearly audible and
    // modulation behavior is consistent across destinations.
    return 1.0f;
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

