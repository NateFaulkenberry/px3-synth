#include "PluginProcessor.h"
#include "PluginProcessorInternals.h"

// File role: parameter accessors, modulation mapping, and FX order API.
// Do not add audio block orchestration here; keep this focused on parameter
// value translation and host-facing parameter helpers.

using namespace px3::processor_internal;

//==============================================================================
// Parameter Access And Routing
//==============================================================================
float PX3SynthAudioProcessor::applyLfoToNormalizedValue(juce::RangedAudioParameter* parameter,
                                                             float baseNormalized,
                                                             float lfoSignal,
                                                             float* outBaseNormalized,
                                                             float* outEffectiveNormalized) const
{
    // `base` is the host-visible parameter value (automation/presets/state).
    // `effective` is the transient DSP value after modulation. We never write
    // `effective` back into parameters so DAW automation lanes stay deterministic.
    const auto base = clamp01(baseNormalized);
    auto effective = base;

    if (parameter == nullptr)
    {
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

    const auto assignment = juce::jlimit(0,
                                         juce::jmax(0, static_cast<int>(lfoAssignableTargets.size()) - 1),
                                         lfoAssignmentAtomic(0).load(std::memory_order_relaxed));

    if (assignment > 0 && assignment < static_cast<int>(lfoAssignableTargets.size()))
    {
        const auto& target = lfoAssignableTargets[static_cast<std::size_t>(assignment)];
        const auto sameId = target.parameterId.equalsIgnoreCase(parameter->getParameterID());
        const auto samePointer = (target.parameter == parameter);
        if (sameId || samePointer)
        {
            effective = clamp01(base + target.normalizedDepth * lfoSignal);
        }
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

float PX3SynthAudioProcessor::applyModulationToNormalizedValue(juce::RangedAudioParameter* parameter,
                                                               float baseNormalized,
                                                               float* outBaseNormalized,
                                                               float* outEffectiveNormalized) const
{
    const auto base = clamp01(baseNormalized);
    auto effective = base;

    if (parameter == nullptr)
    {
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

    const auto applySource = [&](std::atomic<int> const& assignmentIndex,
                                 float signal,
                                 float amount)
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
            effective = clamp01(effective + target.normalizedDepth * (signal * amount));
        }
    };

    for (int i = 0; i < kLfoSourceCount; ++i)
    {
        const auto index = static_cast<std::size_t>(i);
        applySource(lfoAssignmentAtomic(i),
                    lfoCurrentValues[index].load(std::memory_order_relaxed),
                    getLfoAmountParam(i).get());
    }
    for (int i = 0; i < kEnvelopeSourceCount; ++i)
    {
        const auto index = static_cast<std::size_t>(i);
        applySource(envelopeAssignmentAtomic(i),
                    modulationEnvelopeValues[index].load(std::memory_order_relaxed),
                    getEnvelopeAmountParam(i).get());
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

juce::AudioParameterBool& PX3SynthAudioProcessor::getOscillatorEnabledParam(int oscIndex) const
{
    const auto idx = juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex);
    return *oscEnabledParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getOscillatorLevelParam(int oscIndex) const
{
    const auto idx = juce::jlimit(0, kOscillatorSourceCount - 1, oscIndex);
    return *oscLevelParams[static_cast<std::size_t>(idx)];
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
juce::AudioParameterFloat& PX3SynthAudioProcessor::getSubOscLevelParam() const { return *subOscLevelParam; }
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
juce::AudioParameterFloat& PX3SynthAudioProcessor::getAttackParam(int envIndex) const
{
    const auto idx = juce::jlimit(0, kEnvelopeSourceCount - 1, envIndex);
    return *attackParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getDecayParam() const { return *decayParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getDecayParam(int envIndex) const
{
    const auto idx = juce::jlimit(0, kEnvelopeSourceCount - 1, envIndex);
    return *decayParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getSustainParam() const { return *sustainParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getSustainParam(int envIndex) const
{
    const auto idx = juce::jlimit(0, kEnvelopeSourceCount - 1, envIndex);
    return *sustainParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& PX3SynthAudioProcessor::getReleaseParam() const { return *releaseParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getReleaseParam(int envIndex) const
{
    const auto idx = juce::jlimit(0, kEnvelopeSourceCount - 1, envIndex);
    return *releaseParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterBool& PX3SynthAudioProcessor::getAmpEnvEnabledParam() const { return *ampEnvEnabledParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getAmpEnvEnabledParam(int envIndex) const
{
    const auto idx = juce::jlimit(0, kEnvelopeSourceCount - 1, envIndex);
    return *ampEnvEnabledParams[static_cast<std::size_t>(idx)];
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
juce::AudioParameterBool& PX3SynthAudioProcessor::getMoodTrueBypassParam() const { return *moodTrueBypassParam; }
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
juce::AudioParameterInt& PX3SynthAudioProcessor::getPitchBendRangeParam() const { return *pitchBendRangeParam; }
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

    if (anySourceSolo)
    {
        return fxSolo;
    }

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
    // Depths are intentionally conservative by default to avoid abrupt jumps on
    // sensitive controls. Specific musical targets get tuned overrides.
    if (parameterId.equalsIgnoreCase("masterGain"))
    {
        return 0.30f;
    }

    if (parameterId.containsIgnoreCase("Resonance")
        || parameterId.equalsIgnoreCase("delayFeedback")
        || parameterId.equalsIgnoreCase("reverbAmount"))
    {
        return 0.12f;
    }

    if (parameterId.containsIgnoreCase("Cutoff")
        || parameterId.equalsIgnoreCase("delayTime")
        || parameterId.equalsIgnoreCase("reverbDecay"))
    {
        return 0.22f;
    }

    return 0.10f;
}

std::array<int, 4> PX3SynthAudioProcessor::getFxProcessingOrder() const
{
    // Stored order is packed atomically; sanitize on read so malformed legacy or
    // duplicate values always recover to a valid permutation.
    const auto packed = fxProcessingOrderPacked.load(std::memory_order_relaxed);
    const auto raw = unpackFxOrder(packed);

    std::array<int, 4> sanitized { { 0, 1, 3, 2 } };
    std::array<bool, 4> seen { { false, false, false, false } };

    int write = 0;
    for (int i = 0; i < 4; ++i)
    {
        const auto stage = juce::jlimit(0, 3, raw[static_cast<std::size_t>(i)]);
        if (!seen[static_cast<std::size_t>(stage)])
        {
            sanitized[static_cast<std::size_t>(write++)] = stage;
            seen[static_cast<std::size_t>(stage)] = true;
        }
    }

    for (int stage = 0; stage < 4; ++stage)
    {
        if (!seen[static_cast<std::size_t>(stage)] && write < 4)
        {
            sanitized[static_cast<std::size_t>(write++)] = stage;
        }
    }

    return sanitized;
}

void PX3SynthAudioProcessor::setFxProcessingOrder(const std::array<int, 4>& order)
{
    setFxProcessingOrderWithReason(order, "UNKNOWN", "UNSPECIFIED", -1, -1);
}

void PX3SynthAudioProcessor::setFxProcessingOrderWithReason(const std::array<int, 4>& order,
                                                                const juce::String& source,
                                                                const juce::String& reason,
                                                                int fromIndex,
                                                                int toIndex)
{
    // Authoritative module order lives in the processor (not UI). UI drag-drop
    // requests are sanitized and committed here so DSP, state save, and debug
    // diagnostics all observe the same canonical order.
    std::array<int, 4> sanitized { { 0, 1, 3, 2 } };
    std::array<bool, 4> seen { { false, false, false, false } };

    int write = 0;
    for (const auto stageIn : order)
    {
        const auto stage = juce::jlimit(0, 3, stageIn);
        if (!seen[static_cast<std::size_t>(stage)] && write < 4)
        {
            sanitized[static_cast<std::size_t>(write++)] = stage;
            seen[static_cast<std::size_t>(stage)] = true;
        }
    }

    for (int stage = 0; stage < 4 && write < 4; ++stage)
    {
        if (!seen[static_cast<std::size_t>(stage)])
        {
            sanitized[static_cast<std::size_t>(write++)] = stage;
        }
    }

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

