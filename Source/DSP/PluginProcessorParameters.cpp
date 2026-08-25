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
                                         lfoAssignmentIndex.load(std::memory_order_relaxed));

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
juce::AudioParameterChoice& PX3SynthAudioProcessor::getSubOscOctaveParam() const { return *subOscOctaveParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getSubOscWaveformParam() const { return *subOscWaveformParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getFilterCutoffParam() const { return *filterCutoffParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getFilterResonanceParam() const { return *filterResonanceParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getFilterTypeParam() const { return *filterTypeParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getAttackParam() const { return *attackParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getDecayParam() const { return *decayParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getSustainParam() const { return *sustainParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getReleaseParam() const { return *releaseParam; }
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
juce::AudioParameterFloat& PX3SynthAudioProcessor::getReverbAmountParam() const { return *reverbAmountParam; }
juce::AudioParameterBool& PX3SynthAudioProcessor::getReverbEnabledParam() const { return *reverbEnabledParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getReverbAlgorithmParam() const { return *reverbAlgorithmParam; }
juce::AudioParameterInt& PX3SynthAudioProcessor::getPitchBendRangeParam() const { return *pitchBendRangeParam; }
juce::AudioParameterFloat& PX3SynthAudioProcessor::getLfoFrequencyParam() const { return *lfoFrequencyParam; }
juce::AudioParameterChoice& PX3SynthAudioProcessor::getLfoWaveformParam() const { return *lfoWaveformParam; }

int PX3SynthAudioProcessor::getTopMenuViewIndex() const
{
    return juce::jlimit(0, 4, topMenuViewIndex.load(std::memory_order_relaxed));
}

void PX3SynthAudioProcessor::setTopMenuViewIndex(int index, bool notifyHost)
{
    const auto clamped = juce::jlimit(0, 4, index);
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

int PX3SynthAudioProcessor::getLfoAssignmentIndex() const
{
    return juce::jlimit(0,
                        juce::jmax(0, static_cast<int>(lfoAssignableTargets.size()) - 1),
                        lfoAssignmentIndex.load(std::memory_order_relaxed));
}

juce::String PX3SynthAudioProcessor::getLfoAssignmentParameterId() const
{
    const auto index = getLfoAssignmentIndex();
    if (index <= 0 || index >= static_cast<int>(lfoAssignableTargets.size()))
    {
        return "none";
    }

    return lfoAssignableTargets[static_cast<std::size_t>(index)].parameterId;
}

bool PX3SynthAudioProcessor::setLfoAssignmentIndex(int index, bool notifyHost)
{
    if (lfoAssignableTargets.empty())
    {
        lfoAssignmentIndex.store(0, std::memory_order_relaxed);
        return false;
    }

    const auto clamped = juce::jlimit(0,
                                      static_cast<int>(lfoAssignableTargets.size()) - 1,
                                      index);
    lfoAssignmentIndex.store(clamped, std::memory_order_relaxed);

    if (notifyHost)
    {
        updateHostDisplay();
        updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
    }

    debugLogEvent("LFO",
                  "ASSIGNMENT_CHANGED",
                  "index=" + juce::String(clamped)
                      + " id=" + getLfoAssignmentParameterId());
    return true;
}

bool PX3SynthAudioProcessor::setLfoAssignmentByParameterId(const juce::String& parameterId, bool notifyHost)
{
    if (parameterId.isEmpty() || parameterId.equalsIgnoreCase("none"))
    {
        return setLfoAssignmentIndex(0, notifyHost);
    }

    for (int i = 0; i < static_cast<int>(lfoAssignableTargets.size()); ++i)
    {
        if (lfoAssignableTargets[static_cast<std::size_t>(i)].parameterId.equalsIgnoreCase(parameterId))
        {
            return setLfoAssignmentIndex(i, notifyHost);
        }
    }

    return false;
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
               || id.equalsIgnoreCase("lfoWaveform")
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

    lfoAssignmentIndex.store(0, std::memory_order_relaxed);
}

float PX3SynthAudioProcessor::lfoDepthForParameterId(const juce::String& parameterId) const
{
    // Depths are intentionally conservative by default to avoid abrupt jumps on
    // sensitive controls. Specific musical targets get tuned overrides.
    if (parameterId.equalsIgnoreCase("masterGain"))
    {
        return 0.30f;
    }

    if (parameterId.equalsIgnoreCase("filterResonance")
        || parameterId.equalsIgnoreCase("delayFeedback")
        || parameterId.equalsIgnoreCase("reverbAmount"))
    {
        return 0.12f;
    }

    if (parameterId.equalsIgnoreCase("filterCutoff")
        || parameterId.equalsIgnoreCase("delayTime")
        || parameterId.equalsIgnoreCase("reverbDecay"))
    {
        return 0.22f;
    }

    return 0.10f;
}

std::array<int, 3> PX3SynthAudioProcessor::getFxProcessingOrder() const
{
    // Stored order is packed atomically; sanitize on read so malformed legacy or
    // duplicate values always recover to a valid permutation.
    const auto packed = fxProcessingOrderPacked.load(std::memory_order_relaxed);
    const auto raw = unpackFxOrder(packed);

    std::array<int, 3> sanitized { { 0, 1, 2 } };
    std::array<bool, 3> seen { { false, false, false } };

    int write = 0;
    for (int i = 0; i < 3; ++i)
    {
        const auto stage = juce::jlimit(0, 2, raw[static_cast<std::size_t>(i)]);
        if (!seen[static_cast<std::size_t>(stage)])
        {
            sanitized[static_cast<std::size_t>(write++)] = stage;
            seen[static_cast<std::size_t>(stage)] = true;
        }
    }

    for (int stage = 0; stage < 3; ++stage)
    {
        if (!seen[static_cast<std::size_t>(stage)] && write < 3)
        {
            sanitized[static_cast<std::size_t>(write++)] = stage;
        }
    }

    return sanitized;
}

void PX3SynthAudioProcessor::setFxProcessingOrder(const std::array<int, 3>& order)
{
    setFxProcessingOrderWithReason(order, "UNKNOWN", "UNSPECIFIED", -1, -1);
}

void PX3SynthAudioProcessor::setFxProcessingOrderWithReason(const std::array<int, 3>& order,
                                                                const juce::String& source,
                                                                const juce::String& reason,
                                                                int fromIndex,
                                                                int toIndex)
{
    // Authoritative module order lives in the processor (not UI). UI drag-drop
    // requests are sanitized and committed here so DSP, state save, and debug
    // diagnostics all observe the same canonical order.
    std::array<int, 3> sanitized { { 0, 1, 2 } };
    std::array<bool, 3> seen { { false, false, false } };

    int write = 0;
    for (const auto stageIn : order)
    {
        const auto stage = juce::jlimit(0, 2, stageIn);
        if (!seen[static_cast<std::size_t>(stage)] && write < 3)
        {
            sanitized[static_cast<std::size_t>(write++)] = stage;
            seen[static_cast<std::size_t>(stage)] = true;
        }
    }

    for (int stage = 0; stage < 3 && write < 3; ++stage)
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

