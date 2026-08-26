#include "PluginProcessor.h"
#include "PluginProcessorInternals.h"
#include "LfoMode.h"
#include "SubOscMode.h"

// File role: plugin state serialization/restoration and ValueTree mapping.
// Preserve IDs and schema compatibility here; avoid mixing runtime DSP updates
// except where state apply must touch live parameter-backed settings.

using namespace px3::processor_internal;

//==============================================================================
// State Serialization And Restore
//==============================================================================
void PX3SynthAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // DAW save path: capture current authoritative state tree -> XML payload.
    auto state = createParameterStateTree();
    const auto order = getFxProcessingOrder();

    if (auto xml = state.createXml())
    {
        copyXmlToBinary(*xml, destData);

        {
            const std::scoped_lock<std::mutex> lock(debugStateMutex);
            debugLastSerializedState = destData;
            debugLastSerializedStateXml = xml->toString();
        }

        debugLogEvent("HOST",
                      "GET_STATE_INFORMATION",
                      "order=" + debugDescribeOrder(order)
                          + " stateVersion=" + juce::String(static_cast<int>(state.getProperty(kStateVersionId, 0)))
                          + " size=" + juce::String(static_cast<int>(destData.getSize())));
    }
}

void PX3SynthAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // DAW restore path: payload -> ValueTree -> parameter/state apply.
    const auto before = getFxProcessingOrder();
    debugLogEvent("HOST",
                  "SET_STATE_INFORMATION_BEGIN",
                  "incomingSize=" + juce::String(sizeInBytes)
                      + " before=" + debugDescribeOrder(before));

    const auto xml = getXmlFromBinary(data, sizeInBytes);

    if (xml == nullptr)
    {
        debugLogEvent("HOST", "SET_STATE_INFORMATION_INVALID", "xml=null");
        return;
    }

    const auto state = juce::ValueTree::fromXml(*xml);

    if (!state.isValid())
    {
        debugLogEvent("HOST", "SET_STATE_INFORMATION_INVALID", "state=invalid");
        return;
    }

    {
        const std::scoped_lock<std::mutex> lock(debugStateMutex);
        debugLastSerializedState.setSize(static_cast<size_t>(juce::jmax(0, sizeInBytes)));
        if (sizeInBytes > 0)
        {
            std::memcpy(debugLastSerializedState.getData(), data, static_cast<size_t>(sizeInBytes));
        }
        debugLastSerializedStateXml = xml->toString();
    }

    juce::String ignoredError;
    applyParameterStateTree(state, &ignoredError);

    const auto after = getFxProcessingOrder();
    debugLogEvent("HOST",
                  "SET_STATE_INFORMATION_END",
                  "incomingSize=" + juce::String(sizeInBytes)
                      + " before=" + debugDescribeOrder(before)
                      + " after=" + debugDescribeOrder(after)
                      + (ignoredError.isNotEmpty() ? " error=" + ignoredError : juce::String()));
}

juce::ValueTree PX3SynthAudioProcessor::createParameterStateTree() const
{
    // This tree is the canonical persisted state used by DAW projects and
    // preset files. Keep fields backward-compatible when extending it.
    juce::ValueTree state(kStateTypeId);
    state.setProperty(kStateVersionId, kCurrentStateVersion, nullptr);

    for (auto* parameter : getParameters())
    {
        if (const auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
        {
            state.setProperty(ranged->getParameterID(), ranged->getValue(), nullptr);
        }
    }

    // Module order is not represented by audio parameters; serialize it here as
    // explicit MODULE_ORDER entries so UI drag order and DSP chain stay stable.
    const auto fxOrder = getFxProcessingOrder();
    juce::ValueTree moduleOrder(kModuleOrderId);
    for (const auto stage : fxOrder)
    {
        juce::ValueTree module(kModuleEntryId);
        module.setProperty(kModuleIdProperty, moduleIdForStage(stage), nullptr);
        moduleOrder.addChild(module, -1, nullptr);
    }
    state.addChild(moduleOrder, -1, nullptr);
    state.setProperty(kModuleOrderRevisionId,
                      static_cast<int64_t>(fxOrderRevision.load(std::memory_order_relaxed)),
                      nullptr);

    // Keep modulation source states in dedicated nodes for backward-compatible evolution.
    juce::ValueTree lfoSources(kLfoSourcesStateId);
    for (int lfoIndex = 0; lfoIndex < kLfoSourceCount; ++lfoIndex)
    {
        juce::ValueTree source(kSourceEntryId);
        source.setProperty(kSourceIndexId, lfoIndex, nullptr);
        source.setProperty(kLfoEnabledId, getLfoEnabledParam(lfoIndex).get(), nullptr);
        source.setProperty(kLfoFrequencyId, getLfoFrequencyParam(lfoIndex).get(), nullptr);
        source.setProperty(kLfoWaveformId, getLfoWaveformParam(lfoIndex).getIndex(), nullptr);
        source.setProperty(kLfoAssignmentId, getLfoAssignmentParameterId(lfoIndex), nullptr);
        lfoSources.addChild(source, -1, nullptr);
    }
    state.addChild(lfoSources, -1, nullptr);

    juce::ValueTree envelopeSources(kEnvelopeSourcesStateId);
    for (int envIndex = 0; envIndex < kEnvelopeSourceCount; ++envIndex)
    {
        juce::ValueTree source(kSourceEntryId);
        source.setProperty(kSourceIndexId, envIndex, nullptr);
        source.setProperty(kEnvelopeAssignmentId, getEnvelopeAssignmentParameterId(envIndex), nullptr);
        envelopeSources.addChild(source, -1, nullptr);
    }
    state.addChild(envelopeSources, -1, nullptr);

    juce::ValueTree subOscState(kSubOscStateId);
    subOscState.setProperty(kSubOscEnabledId, subOscEnabledParam->get(), nullptr);
    subOscState.setProperty(kSubOscLevelId, subOscLevelParam->get(), nullptr);
    subOscState.setProperty(kSubOscPitchId, subOscPitchParam->get(), nullptr);
    subOscState.setProperty(kSubOscOctaveId, subOscOctaveParam->getIndex(), nullptr);
    subOscState.setProperty(kSubOscWaveformId, subOscWaveformParam->getIndex(), nullptr);
    state.addChild(subOscState, -1, nullptr);

    juce::ValueTree vibeState(kVibeStateId);
    vibeState.setProperty(kVibeBypassId, debugGetVibeBypass(), nullptr);
    vibeState.setProperty(kVibeSeedId, static_cast<int64_t>(debugGetVibeSeed()), nullptr);
    state.addChild(vibeState, -1, nullptr);

    state.setProperty(kTopMenuViewId, getTopMenuViewIndex(), nullptr);

    return state;
}

juce::ValueTree PX3SynthAudioProcessor::createPresetStateTree() const
{
    auto state = createParameterStateTree();
    state.removeProperty(kTopMenuViewId, nullptr);
    return state;
}

bool PX3SynthAudioProcessor::applyParameterStateTree(const juce::ValueTree& state,
                                                     juce::String* error,
                                                     bool restoreUiSessionState)
{
    if (!state.isValid() || state.getType() != kStateTypeId)
    {
        if (error != nullptr)
        {
            *error = "State tree is invalid or has unexpected type.";
        }
        return false;
    }

    // Restore all known parameter base values first. These are host-automatable
    // and remain the source of truth for both UI and DSP readers.
    for (auto* parameter : getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
        {
            const auto paramID = ranged->getParameterID();
            if (state.hasProperty(paramID))
            {
                const auto value = static_cast<float>(state[paramID]);
                ranged->setValueNotifyingHost(value);
            }
        }
    }

    std::array<int, 4> fxOrderFromState { { 0, 1, 3, 2 } };
    auto hasModuleOrder = false;
    auto moduleOrderSource = juce::String("none");

    if (const auto moduleOrder = state.getChildWithName(kModuleOrderId); moduleOrder.isValid())
    {
        std::array<bool, 4> seen { { false, false, false, false } };
        int write = 0;

        for (int i = 0; i < moduleOrder.getNumChildren() && write < 4; ++i)
        {
            const auto moduleNode = moduleOrder.getChild(i);
            if (!moduleNode.isValid() || moduleNode.getType() != kModuleEntryId || !moduleNode.hasProperty(kModuleIdProperty))
            {
                continue;
            }

            const auto moduleId = moduleNode.getProperty(kModuleIdProperty).toString();
            const auto stage = stageForModuleId(moduleId);
            if (stage < 0)
            {
                continue;
            }

            if (!seen[static_cast<std::size_t>(stage)])
            {
                fxOrderFromState[static_cast<std::size_t>(write++)] = stage;
                seen[static_cast<std::size_t>(stage)] = true;
            }
        }

        if (write > 0)
        {
            hasModuleOrder = true;
            moduleOrderSource = "MODULE_ORDER";
            for (int stage = 0; stage < 4 && write < 4; ++stage)
            {
                if (!seen[static_cast<std::size_t>(stage)])
                {
                    fxOrderFromState[static_cast<std::size_t>(write++)] = stage;
                }
            }
        }
    }

    // Then restore processing chain order. This is processor-owned state and is
    // intentionally separate from AudioParameter IDs.
    if (hasModuleOrder)
    {
        setFxProcessingOrderWithReason(fxOrderFromState, "HOST", "STATE_RESTORE", -1, -1);
    }
    else
    {
        setFxProcessingOrderWithReason({ { 0, 1, 3, 2 } }, "HOST", "STATE_RESTORE_DEFAULT", -1, -1);
    }

    debugLogEvent("HOST",
                  "APPLY_STATE_TREE",
                  "moduleOrderSource=" + moduleOrderSource
                      + " restoredOrder=" + debugDescribeOrder(getFxProcessingOrder())
                      + " stateVersion=" + juce::String(static_cast<int>(state.getProperty(kStateVersionId, 0))));

    if (state.hasProperty(kModuleOrderRevisionId))
    {
        const auto revision = juce::jmax<int64_t>(0, static_cast<int64_t>(state[kModuleOrderRevisionId]));
        fxOrderRevision.store(static_cast<uint32_t>(revision), std::memory_order_relaxed);
    }

    if (const auto lfoSources = state.getChildWithName(kLfoSourcesStateId); lfoSources.isValid())
    {
        for (int i = 0; i < lfoSources.getNumChildren(); ++i)
        {
            const auto source = lfoSources.getChild(i);
            if (!source.isValid() || source.getType() != kSourceEntryId)
            {
                continue;
            }

            const auto lfoIndex = juce::jlimit(0, kLfoSourceCount - 1, static_cast<int>(source.getProperty(kSourceIndexId, 0)));

            if (source.hasProperty(kLfoEnabledId))
            {
                const auto enabled = static_cast<bool>(source[kLfoEnabledId]);
                auto& enabledParam = getLfoEnabledParam(lfoIndex);
                enabledParam.setValueNotifyingHost(enabledParam.convertTo0to1(enabled));
            }

            if (source.hasProperty(kLfoFrequencyId))
            {
                const auto frequency = juce::jlimit(0.01f, 20.0f, static_cast<float>(source[kLfoFrequencyId]));
                auto& freqParam = getLfoFrequencyParam(lfoIndex);
                freqParam.setValueNotifyingHost(freqParam.convertTo0to1(frequency));
            }

            if (source.hasProperty(kLfoWaveformId))
            {
                const auto waveform = px3::clampLfoWaveformIndex(static_cast<int>(source[kLfoWaveformId]));
                auto& waveformParam = getLfoWaveformParam(lfoIndex);
                waveformParam.setValueNotifyingHost(waveformParam.convertTo0to1(static_cast<float>(waveform)));
            }

            if (source.hasProperty(kLfoAssignmentId))
            {
                setLfoAssignmentByParameterId(lfoIndex, source[kLfoAssignmentId].toString(), false);
            }
        }
    }

    if (const auto envelopeSources = state.getChildWithName(kEnvelopeSourcesStateId); envelopeSources.isValid())
    {
        for (int i = 0; i < envelopeSources.getNumChildren(); ++i)
        {
            const auto source = envelopeSources.getChild(i);
            if (!source.isValid() || source.getType() != kSourceEntryId)
            {
                continue;
            }

            const auto envIndex = juce::jlimit(0, kEnvelopeSourceCount - 1, static_cast<int>(source.getProperty(kSourceIndexId, 0)));
            if (source.hasProperty(kEnvelopeAssignmentId))
            {
                setEnvelopeAssignmentByParameterId(envIndex, source[kEnvelopeAssignmentId].toString(), false);
            }
        }
    }

    if (const auto vibeState = state.getChildWithName(kVibeStateId); vibeState.isValid())
    {
        if (vibeState.hasProperty(kVibeBypassId))
        {
            debugSetVibeBypass(static_cast<bool>(vibeState[kVibeBypassId]));
        }

        if (vibeState.hasProperty(kVibeSeedId))
        {
            debugSetVibeSeed(static_cast<uint32_t>(juce::jmax<int64_t>(1, static_cast<int64_t>(vibeState[kVibeSeedId]))));
        }
    }

    if (const auto subOscState = state.getChildWithName(kSubOscStateId); subOscState.isValid())
    {
        if (subOscState.hasProperty(kSubOscEnabledId) && subOscEnabledParam != nullptr)
        {
            subOscEnabledParam->setValueNotifyingHost(static_cast<bool>(subOscState[kSubOscEnabledId]) ? 1.0f : 0.0f);
        }

        if (subOscState.hasProperty(kSubOscLevelId) && subOscLevelParam != nullptr)
        {
            const auto level = juce::jlimit(0.0f, 1.0f, static_cast<float>(subOscState[kSubOscLevelId]));
            subOscLevelParam->setValueNotifyingHost(subOscLevelParam->convertTo0to1(level));
        }

        if (subOscState.hasProperty(kSubOscPitchId) && subOscPitchParam != nullptr)
        {
            const auto pitch = juce::jlimit(-0.12f, 0.12f, static_cast<float>(subOscState[kSubOscPitchId]));
            subOscPitchParam->setValueNotifyingHost(subOscPitchParam->convertTo0to1(pitch));
        }

        if (subOscState.hasProperty(kSubOscOctaveId) && subOscOctaveParam != nullptr)
        {
            const auto octave = px3::clampSubOscOctaveIndex(static_cast<int>(subOscState[kSubOscOctaveId]));
            subOscOctaveParam->setValueNotifyingHost(subOscOctaveParam->convertTo0to1(static_cast<float>(octave)));
        }

        if (subOscState.hasProperty(kSubOscWaveformId) && subOscWaveformParam != nullptr)
        {
            const auto waveform = px3::clampSubOscWaveformIndex(static_cast<int>(subOscState[kSubOscWaveformId]));
            subOscWaveformParam->setValueNotifyingHost(subOscWaveformParam->convertTo0to1(static_cast<float>(waveform)));
        }
    }

    if (restoreUiSessionState && state.hasProperty(kTopMenuViewId))
    {
        setTopMenuViewIndex(static_cast<int>(state.getProperty(kTopMenuViewId)), false);
    }
    else if (restoreUiSessionState)
    {
        setTopMenuViewIndex(0, false);
    }

    if (error != nullptr)
    {
        error->clear();
    }

    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PX3SynthAudioProcessor();
}
