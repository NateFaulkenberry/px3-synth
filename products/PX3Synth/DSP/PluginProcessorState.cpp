#include "PluginProcessor.h"
#include "PluginProcessorInternals.h"
#include "LfoMode.h"
#include "SubOscMode.h"

#include <cmath>
#include <cstdlib>

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
    const std::lock_guard<std::recursive_mutex> lock(graphAuthoringMutex);
    // This tree is the canonical 0.8 state used by DAW projects and presets.
    juce::ValueTree state(kStateTypeId);
    state.setProperty(kStateVersionId, kCurrentStateVersion, nullptr);
    state.addChild(parameterCatalog.createStateTree(), -1, nullptr);
    juce::ValueTree graphState("MODULATION_GRAPH");
    for (std::size_t slot = 0; slot < primaryGraphRoutes.size(); ++slot)
    {
        const auto& route = primaryGraphRoutes[slot];
        if (route.source < 0) { continue; }
        juce::ValueTree node("PRIMARY_ROUTE");
        node.setProperty("slot", static_cast<int>(slot), nullptr);
        node.setProperty("source", graphSourceId(route.source), nullptr);
        node.setProperty("destination", route.destination, nullptr);
        graphState.addChild(node, -1, nullptr);
    }
    for (int slot = 0; slot < kGraphRouteSlots; ++slot)
    {
        const auto& route = graphRouteConfigurations[static_cast<std::size_t>(slot)];
        if (route.source < 0) { continue; }
        juce::ValueTree node("ROUTE");
        node.setProperty("slot", slot, nullptr);
        node.setProperty("source", graphSourceId(route.source), nullptr);
        node.setProperty("destination", route.destination, nullptr);
        node.setProperty("polarity", static_cast<int>(route.polarity), nullptr);
        node.setProperty("curve", static_cast<int>(route.curve), nullptr);
        graphState.addChild(node, -1, nullptr);
    }
    state.addChild(graphState, -1, nullptr);

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

    // Only oscillators actually using a user table appear, so a preset built
    // entirely from factory tables carries nothing extra.
    {
        juce::ValueTree userWavetables(kUserWavetablesId);
        for (int osc = 0; osc < kOscillatorSourceCount; ++osc)
        {
            const auto name = getUserWavetableName(osc);
            if (name.isEmpty())
            {
                continue;
            }

            juce::ValueTree entry(kUserWavetableNameId);
            entry.setProperty(kUserWavetableOscId, osc, nullptr);
            entry.setProperty(kUserWavetableNameId, name, nullptr);
            entry.setProperty("displayName", getUserWavetableDisplayName(osc), nullptr);
            userWavetables.addChild(entry, -1, nullptr);
        }
        state.addChild(userWavetables, -1, nullptr);
    }

    // The reverb's impulse response, by path. A missing file on another
    // machine leaves the IR mode silent rather than failing the whole state.
    if (reverbImpulseResponsePath.isNotEmpty())
    {
        state.setProperty("reverbImpulseResponse", reverbImpulseResponsePath, nullptr);
    }

    // Envelope shapes, and only the ones the four ADSR parameters cannot
    // already describe.
    {
        juce::ValueTree shapes(kEnvelopeShapesId);
        shapes.setProperty(kEnvelopeShapeVersionId, kEnvelopeShapeVersion, nullptr);

        const auto writePoints = [](juce::ValueTree& into, const px3::BreakpointEnvelope& envelope)
        {
            for (int p = 0; p < envelope.getPointCount(); ++p)
            {
                const auto& point = envelope.getPoint(p);
                juce::ValueTree entry(kEnvelopePointId);
                entry.setProperty(kEnvelopePointTimeId, point.timeSeconds, nullptr);
                entry.setProperty(kEnvelopePointValueId, point.value, nullptr);
                entry.setProperty(kEnvelopePointCurveId, point.curveToNext, nullptr);
                into.addChild(entry, -1, nullptr);
            }
        };

        auto wroteAny = false;
        for (int index = 0; index < kShapedEnvelopeCount; ++index)
        {
            const auto envelope = getShapedEnvelope(index);
            // Both modes' state is written, not only the active one, so a
            // preset saved in ADSR mode with a breakpoint envelope behind it
            // reopens with both and switching reveals exactly what was stored.
            // The retained child carries whichever mode is NOT active - the
            // ADSR put aside while a drawing is being played, or the drawing
            // put aside while the ADSR is. It used to carry the breakpoint
            // shape either way, which in Breakpoint mode wrote a second copy
            // of the active shape and left the stored ADSR's curves nowhere to
            // go: they came back straight.
            const auto inBreakpoint = envelope.isBreakpointMode();
            const auto& retained = inBreakpoint
                                       ? adsrShapes[static_cast<std::size_t>(index)]
                                       : breakpointShapes[static_cast<std::size_t>(index)];
            const auto hasRetained
                = inBreakpoint || breakpointInitialised[static_cast<std::size_t>(index)];

            // A plain straight-line ADSR in ADSR mode says nothing the four
            // parameters do not already say, and is rebuilt from them on load.
            // Anything else - a curve, extra points, Breakpoint mode, or a
            // retained shape waiting to be switched back to - is written.
            if (envelope.isPlainAdsr() && ! envelope.isBreakpointMode() && ! hasRetained)
            {
                continue;
            }

            juce::ValueTree node(kEnvelopeShapeId);
            node.setProperty(kEnvelopeShapeIndexId, index, nullptr);
            node.setProperty(kEnvelopeShapeSustainId, envelope.getSustainPoint(), nullptr);
            node.setProperty(kEnvelopeShapeModeId,
                             envelope.isBreakpointMode() ? "breakpoint" : "adsr", nullptr);

            writePoints(node, envelope);

            // The shape the user had before switching to ADSR, so switching
            // back restores it exactly rather than approximately.
            if (hasRetained)
            {
                juce::ValueTree keep(kEnvelopeRetainedShapeId);
                keep.setProperty(kEnvelopeShapeSustainId, retained.getSustainPoint(), nullptr);
                keep.setProperty(kEnvelopeShapeModeId, inBreakpoint ? "adsr" : "breakpoint", nullptr);
                writePoints(keep, retained);
                node.addChild(keep, -1, nullptr);
            }

            shapes.addChild(node, -1, nullptr);
            wroteAny = true;
        }

        if (wroteAny)
        {
            state.addChild(shapes, -1, nullptr);
        }
    }

    for (int macro = 0; macro < kMacroCount; ++macro)
    {
        for (const auto& destination : macroDestinations[static_cast<std::size_t>(macro)])
        {
            juce::ValueTree node("MACRO_ROUTE");
            node.setProperty("source", graphSourceId(kLfoSourceCount + kEnvelopeSourceCount + macro), nullptr);
            node.setProperty("destination", destination.parameterId, nullptr);
            node.setProperty("depth", destination.depth, nullptr);
            graphState.addChild(node, -1, nullptr);
        }
    }

    // MIDI mappings. Written here because this tree is what a DAW project
    // stores, and removed again in createPresetStateTree because a preset is
    // the sound rather than the user's hardware.
    if (! midiMappings.empty())
    {
        juce::ValueTree mappings(kMidiMappingsId);

        for (const auto& mapping : midiMappings)
        {
            if (! mapping.isValid()) { continue; }

            juce::ValueTree node(kMidiMappingId);
            node.setProperty(kMidiCcId, mapping.ccNumber, nullptr);
            node.setProperty(kMidiChannelId, mapping.learnedChannel, nullptr);

            for (const auto& parameterId : mapping.parameterIds)
            {
                juce::ValueTree destination(kMidiDestinationId);
                destination.setProperty(kMidiParameterId, parameterId, nullptr);
                node.appendChild(destination, nullptr);
            }

            mappings.appendChild(node, nullptr);
        }

        state.appendChild(mappings, nullptr);
    }


    juce::ValueTree subOscState(kSubOscStateId);
    subOscState.setProperty(kSubOscEnabledId, subOscEnabledParam->get(), nullptr);
    subOscState.setProperty(kSubOscWaveformId, subOscWaveformParam->getIndex(), nullptr);
    state.addChild(subOscState, -1, nullptr);

    // ANALOG's on/off is its parameter (fx.analog.enabled); only the engine
    // seed, which is not a parameter, is stored here.
    juce::ValueTree analogState(kAnalogDriftStateId);
    analogState.setProperty(kAnalogDriftSeedId, static_cast<int64_t>(debugGetAnalogDriftSeed()), nullptr);
    state.addChild(analogState, -1, nullptr);

    state.setProperty(kTopMenuViewId, getTopMenuViewIndex(), nullptr);

    if (const auto preset = getLoadedPreset(); preset.valid)
    {
        state.setProperty(kLoadedPresetNameId, preset.name, nullptr);
        state.setProperty(kLoadedPresetCategoryId, preset.category, nullptr);
        state.setProperty(kLoadedPresetAuthorId, preset.author, nullptr);
        state.setProperty(kLoadedPresetPathId, preset.filePath, nullptr);
    }

    return state;
}

juce::ValueTree PX3SynthAudioProcessor::createPresetStateTree() const
{
    auto state = createParameterStateTree();
    state.removeProperty(kTopMenuViewId, nullptr);

    // MIDI mappings stay in the preset. Saving a patch saves the hardware
    // layout that goes with it, so a sound designed around a controller
    // arrives with that controller already wired up.
    // A preset file must not name itself: the identity belongs to the session,
    // not to the sound. Saving it would mean a preset loaded, edited and saved
    // under a new name still claimed to be the old one.
    state.removeProperty(kLoadedPresetNameId, nullptr);
    state.removeProperty(kLoadedPresetCategoryId, nullptr);
    state.removeProperty(kLoadedPresetAuthorId, nullptr);
    state.removeProperty(kLoadedPresetPathId, nullptr);
    return state;
}

bool PX3SynthAudioProcessor::applyParameterStateTree(const juce::ValueTree& state,
                                                     juce::String* error,
                                                     bool restoreUiSessionState)
{
    const std::lock_guard<std::recursive_mutex> lock(graphAuthoringMutex);
    if (!state.isValid() || state.getType() != kStateTypeId)
    {
        if (error != nullptr)
        {
            *error = "State tree is invalid or has unexpected type.";
        }
        return false;
    }

    const auto storedStateVersion = static_cast<int>(state.getProperty(kStateVersionId, 0));
    if (storedStateVersion != kCurrentStateVersion)
    {
        if (error != nullptr)
        {
            *error = "Unsupported PX3 0.8 state schema version.";
        }
        return false;
    }

    std::vector<px3::synth::ParameterCatalog::StateValue> parameterValues;
    juce::String parameterError;
    const auto parameters = state.getChildWithName("PARAMETERS");
    if (! parameterCatalog.readStateValues(parameters, parameterValues, parameterError))
    {
        if (error != nullptr) { *error = parameterError; }
        return false;
    }

    std::array<GraphRouteConfiguration, kGraphRouteSlots> graphConfigurations;
    std::array<GraphRouteConfiguration, kLfoSourceCount + kEnvelopeSourceCount> primaryConfigurations;
    std::array<std::vector<MacroDestination>, kMacroCount> macroConfigurations;
    auto macroRouteCount = 0;
    std::array<bool, kLfoSourceCount + kEnvelopeSourceCount> usedPrimarySlots {};
    std::array<bool, kGraphRouteSlots> usedGraphSlots {};
    const auto graphState = state.getChildWithName("MODULATION_GRAPH");
    const auto parseIndex = [](const juce::ValueTree& node, const char* property, int& output)
    {
        const auto text = node.getProperty(property).toString();
        if (text.isEmpty() || text.length() > 3 || ! text.containsOnly("0123456789")) { return false; }
        output = text.getIntValue();
        return true;
    };
    for (int index = 0; index < graphState.getNumChildren(); ++index)
    {
        const auto node = graphState.getChild(index);
        if (node.getType() == juce::Identifier("MACRO_ROUTE"))
        {
            auto macro = -1;
            for (int candidate = 0; candidate < kMacroCount; ++candidate)
            {
                if (node.getProperty("source").toString()
                    == graphSourceId(kLfoSourceCount + kEnvelopeSourceCount + candidate))
                {
                    macro = candidate;
                    break;
                }
            }
            const auto destination = node.getProperty("destination").toString();
            const auto depthText = node.getProperty("depth").toString().trim();
            const auto* start = depthText.toRawUTF8();
            char* end = nullptr;
            const auto depth = std::strtof(start, &end);
            if (macro < 0 || destination.isEmpty() || depthText.isEmpty() || depthText.length() > 32
                || end != start + depthText.getNumBytesAsUTF8() || ! std::isfinite(depth)
                || depth < -1.0f || depth > 1.0f || ++macroRouteCount > kMacroRouteSlots)
            {
                if (error != nullptr) { *error = "Malformed macro modulation route."; }
                return false;
            }
            auto& routes = macroConfigurations[static_cast<std::size_t>(macro)];
            if (std::any_of(routes.begin(), routes.end(), [&destination](const auto& route) { return route.parameterId == destination; }))
            {
                if (error != nullptr) { *error = "Duplicate macro modulation route."; }
                return false;
            }
            routes.push_back({ destination, depth });
            continue;
        }
        if (node.getType() == juce::Identifier("PRIMARY_ROUTE"))
        {
            int primarySlot = -1;
            if (! parseIndex(node, "slot", primarySlot)
                || ! juce::isPositiveAndBelow(primarySlot, static_cast<int>(primaryConfigurations.size()))
                || usedPrimarySlots[static_cast<std::size_t>(primarySlot)]
                || node.getProperty("source").toString() != graphSourceId(primarySlot))
            {
                if (error != nullptr) { *error = "Malformed primary modulation route."; }
                return false;
            }
            primaryConfigurations[static_cast<std::size_t>(primarySlot)]
                = { primarySlot, node.getProperty("destination").toString() };
            usedPrimarySlots[static_cast<std::size_t>(primarySlot)] = true;
            continue;
        }
        int slot = -1;
        int polarity = -1;
        int curve = -1;
        if (node.getType() != juce::Identifier("ROUTE") || ! parseIndex(node, "slot", slot)
            || ! juce::isPositiveAndBelow(slot, kGraphRouteSlots)
            || usedGraphSlots[static_cast<std::size_t>(slot)]
            || ! parseIndex(node, "polarity", polarity) || polarity > 2
            || ! parseIndex(node, "curve", curve) || curve > 2)
        {
            if (error != nullptr) { *error = "Malformed modulation graph route."; }
            return false;
        }
        auto& configuration = graphConfigurations[static_cast<std::size_t>(slot)];
        for (int source = 0; source < kLfoSourceCount + kEnvelopeSourceCount + kMacroCount; ++source)
        {
            if (node.getProperty("source").toString() == graphSourceId(source))
            {
                configuration.source = source;
                break;
            }
        }
        configuration.destination = node.getProperty("destination").toString();
        configuration.polarity = static_cast<px3::synth::ModulationPolarity>(polarity);
        configuration.curve = static_cast<px3::synth::ModulationCurve>(curve);
        if (configuration.source < 0 || configuration.destination.isEmpty())
        {
            if (error != nullptr) { *error = "Unknown modulation graph endpoint."; }
            return false;
        }
        usedGraphSlots[static_cast<std::size_t>(slot)] = true;
    }
    px3::synth::CompiledModulationGraph graphPlan;
    juce::String graphError;
    if (! compileModulationGraph(graphConfigurations, graphPlan, graphError, &primaryConfigurations, &macroConfigurations))
    {
        if (error != nullptr) { *error = graphError; }
        return false;
    }
    graphRouteConfigurations = std::move(graphConfigurations);
    primaryGraphRoutes = std::move(primaryConfigurations);
    macroDestinations = std::move(macroConfigurations);

    // Validate the complete parameter set before applying any value.
    for (const auto& value : parameterValues)
    {
        if (value.parameter != nullptr)
        {
            value.parameter->setValueNotifyingHost(value.normalizedValue);
            parameterCatalog.rememberLoadedValue(value.parameter, value.normalizedValue);
        }
    }

    auto fxOrderFromState = px3::kDefaultFxOrder;
    auto hasModuleOrder = false;
    auto moduleOrderSource = juce::String("none");

    if (const auto moduleOrder = state.getChildWithName(kModuleOrderId); moduleOrder.isValid())
    {
        // Read what the state names, then let the shared sanitiser fill in any
        // stage the saved order predates. A session written before an effect
        // existed simply gets that effect in its default slot.
        FxOrder fromState {};
        int write = 0;

        for (int i = 0; i < moduleOrder.getNumChildren() && write < kFxStageCount; ++i)
        {
            const auto moduleNode = moduleOrder.getChild(i);
            if (!moduleNode.isValid() || moduleNode.getType() != kModuleEntryId || !moduleNode.hasProperty(kModuleIdProperty))
            {
                continue;
            }

            const auto stage = stageForModuleId(moduleNode.getProperty(kModuleIdProperty).toString());
            if (stage < 0)
            {
                continue;
            }

            fromState[static_cast<std::size_t>(write++)] = stage;
        }

        if (write > 0)
        {
            hasModuleOrder = true;
            moduleOrderSource = "MODULE_ORDER";

            // Pad with a stage the sanitiser will discard as a duplicate, so
            // the unwritten tail cannot read as a run of stage 0.
            for (int i = write; i < kFxStageCount; ++i)
            {
                fromState[static_cast<std::size_t>(i)] = fromState[0];
            }

            fxOrderFromState = sanitizeFxOrder(fromState);
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

    rebuildModulationGraph();

    if (const auto analogState = state.getChildWithName(kAnalogDriftStateId); analogState.isValid())
    {
        if (analogState.hasProperty(kAnalogDriftSeedId))
        {
            debugSetAnalogDriftSeed(static_cast<uint32_t>(juce::jmax<int64_t>(1, static_cast<int64_t>(analogState[kAnalogDriftSeedId]))));
        }
    }

    if (const auto subOscState = state.getChildWithName(kSubOscStateId); subOscState.isValid())
    {
        if (subOscState.hasProperty(kSubOscEnabledId) && subOscEnabledParam != nullptr)
        {
            subOscEnabledParam->setValueNotifyingHost(static_cast<bool>(subOscState[kSubOscEnabledId]) ? 1.0f : 0.0f);
        }

        if (subOscState.hasProperty(kSubOscWaveformId) && subOscWaveformParam != nullptr)
        {
            const auto waveform = px3::clampSubOscWaveformIndex(static_cast<int>(subOscState[kSubOscWaveformId]));
            subOscWaveformParam->setValueNotifyingHost(subOscWaveformParam->convertTo0to1(static_cast<float>(waveform)));
        }
    }


    // MIDI mappings. Restored on both paths, but they mean different things.
    //
    // A DAW session IS the whole truth for this instance, so it is applied
    // whole: whatever it holds, including nothing, is what you get back.
    //
    // A preset is a sound that MAY bring a hardware layout with it. One that
    // does replaces what is there; one that does not leaves your controller
    // alone. The alternative - absent meaning "clear" - would have every
    // factory preset wipe the assignments of anyone who auditioned one.
    {
        const auto mappings = state.getChildWithName(kMidiMappingsId);
        const auto shouldApply = restoreUiSessionState || mappings.isValid();

        if (shouldApply)
        {
            midiMappings.clear();
        }

        if (shouldApply && mappings.isValid())
        {
            for (const auto& node : mappings)
            {
                px3::MidiMapping mapping;
                mapping.ccNumber = juce::jlimit(-1, 127,
                                                static_cast<int>(node.getProperty(kMidiCcId, -1)));
                mapping.learnedChannel = juce::jlimit(1, 16,
                                                      static_cast<int>(node.getProperty(kMidiChannelId, 1)));

                for (const auto& destination : node)
                {
                    const auto parameterId
                        = destination.getProperty(kMidiParameterId, juce::String()).toString();

                    // A destination naming a parameter this build does not
                    // have is dropped and the rest of the mapping still loads.
                    // A state file from a later build, or one whose parameter
                    // was renamed, should cost that mapping a destination -
                    // not the whole session's worth of assignments.
                    if (parameterId.isNotEmpty() && findParameterById(parameterId) != nullptr)
                    {
                        mapping.parameterIds.addIfNotAlreadyThere(parameterId);
                    }
                }

                if (! mapping.isValid()) { continue; }

                // Two nodes claiming one CC are merged rather than kept apart,
                // so the invariant that one CC is one mapping holds however the
                // file was written.
                auto existing = std::find_if(midiMappings.begin(), midiMappings.end(),
                                              [&mapping](const px3::MidiMapping& m)
                                              { return m.ccNumber == mapping.ccNumber; });

                if (existing != midiMappings.end())
                {
                    existing->parameterIds.addArray(mapping.parameterIds);
                    existing->parameterIds.removeDuplicates(false);
                }
                else
                {
                    midiMappings.push_back(mapping);
                }
            }
        }

        // Restored mappings must not fire on the first tick just because the
        // controller happens to sit somewhere: they drive on the next MOVEMENT.
        if (shouldApply)
        {
            for (std::size_t i = 0; i < ccSeenSequence.size(); ++i)
            {
                ccSeenSequence[i] = ccSequence[i].load(std::memory_order_acquire);
            }

            // The audio thread's table follows the list it is a view of. A
            // restored mapping drives from the block its CC next arrives in,
            // exactly as a freshly learned one does.
            rebuildCcRoutes();
        }
    }

    // A session saved by 0.6.0-development may carry an animationsEnabled
    // property. It is deliberately ignored: the preference is global now, and
    // letting an old per-instance value win would put the two instances that
    // restored different sessions back into disagreement - which is the whole
    // thing this stopped being.
    if (restoreUiSessionState && state.hasProperty(kTopMenuViewId))
    {
        setTopMenuViewIndex(static_cast<int>(state.getProperty(kTopMenuViewId)), false);
    }
    else if (restoreUiSessionState)
    {
        setTopMenuViewIndex(0, false);
    }

    if (restoreUiSessionState)
    {
        LoadedPreset preset;
        preset.name = state.getProperty(kLoadedPresetNameId, juce::String()).toString();
        preset.category = state.getProperty(kLoadedPresetCategoryId, juce::String()).toString();
        preset.author = state.getProperty(kLoadedPresetAuthorId, juce::String()).toString();
        preset.filePath = state.getProperty(kLoadedPresetPathId, juce::String()).toString();
        preset.valid = preset.name.isNotEmpty();
        setLoadedPreset(preset);
    }

    if (error != nullptr)
    {
        error->clear();
    }

    // Cleared first: a preset that uses factory tables has to REMOVE whatever
    // user table the previous one selected, and it does that by saying nothing.
    for (int osc = 0; osc < kOscillatorSourceCount; ++osc)
    {
        userWavetableNames[static_cast<std::size_t>(osc)].clear();
        userWavetableDisplayNames[static_cast<std::size_t>(osc)].clear();
        missingWavetableNames[static_cast<std::size_t>(osc)].clear();
    }

    if (const auto irPath = state.getProperty("reverbImpulseResponse").toString(); irPath.isNotEmpty())
    {
        if (loadReverbImpulseResponse(juce::File(irPath)).isNotEmpty()) { reverbImpulseResponsePath = irPath; }
    }
    else
    {
        clearReverbImpulseResponse();
    }

    if (const auto userWavetables = state.getChildWithName(kUserWavetablesId); userWavetables.isValid())
    {
        for (const auto& entry : userWavetables)
        {
            const auto osc = juce::jlimit(0, kOscillatorSourceCount - 1,
                                          static_cast<int>(entry.getProperty(kUserWavetableOscId, 0)));
            userWavetableNames[static_cast<std::size_t>(osc)] =
                entry.getProperty(kUserWavetableNameId).toString();
            userWavetableDisplayNames[static_cast<std::size_t>(osc)] = entry.getProperty(
                "displayName", userWavetableNames[static_cast<std::size_t>(osc)]).toString();
        }
    }

    // Envelope shapes. Cleared first, so a preset built from plain ADSR removes
    // whatever the previous one had shaped - it does that by saying nothing.
    for (int index = 0; index < kShapedEnvelopeCount; ++index)
    {
        setShapedEnvelope(index, px3::BreakpointEnvelope {});
        adsrShapes[static_cast<std::size_t>(index)] = px3::BreakpointEnvelope {};
        breakpointShapes[static_cast<std::size_t>(index)] = px3::BreakpointEnvelope {};
        breakpointInitialised[static_cast<std::size_t>(index)] = false;
    }

    if (const auto shapes = state.getChildWithName(kEnvelopeShapesId); shapes.isValid())
    {
        // A version this build does not know is left alone rather than read
        // wrongly: the envelopes stay ADSR, which is a sound the preset at least
        // partly intended, where misreading points is one that nobody did.
        const auto version = static_cast<int>(shapes.getProperty(kEnvelopeShapeVersionId, 0));
        if (version >= 1 && version <= kEnvelopeShapeVersion)
        {
            for (const auto& node : shapes)
            {
                const auto index = juce::jlimit(0, kShapedEnvelopeCount - 1,
                                                static_cast<int>(node.getProperty(kEnvelopeShapeIndexId, 0)));

                const auto readPoints = [](const juce::ValueTree& from)
                {
                    std::vector<px3::BreakpointEnvelope::Point> points;
                    points.reserve(static_cast<std::size_t>(px3::BreakpointEnvelope::kMaxPoints));

                    for (const auto& entry : from)
                    {
                        if (! entry.hasType(kEnvelopePointId)) { continue; }

                        px3::BreakpointEnvelope::Point point;
                        point.timeSeconds = static_cast<double>(entry.getProperty(kEnvelopePointTimeId, 0.0));
                        point.value = static_cast<double>(entry.getProperty(kEnvelopePointValueId, 0.0));
                        point.curveToNext = static_cast<double>(entry.getProperty(kEnvelopePointCurveId, 0.0));
                        points.push_back(point);
                    }

                    return points;
                };

                const auto loaded = readPoints(node);

                px3::BreakpointEnvelope envelope;
                envelope.setPoints(loaded.data(), static_cast<int>(loaded.size()),
                                   static_cast<int>(node.getProperty(kEnvelopeShapeSustainId, 2)));

                // The mode. A tree written before modes existed records none,
                // and the envelope takes the mode its shape implies - which is
                // what the old implicit rule did, so those presets keep the
                // behaviour they have.
                if (node.hasProperty(kEnvelopeShapeModeId))
                {
                    envelope.setMode(node.getProperty(kEnvelopeShapeModeId).toString() == "breakpoint"
                                         ? px3::BreakpointEnvelope::Mode::breakpoint
                                         : px3::BreakpointEnvelope::Mode::adsr);
                }
                else
                {
                    envelope.setMode(px3::BreakpointEnvelope::impliedModeFor(envelope));
                }

                // The shape waiting to be switched back to, if there is one.
                breakpointInitialised[static_cast<std::size_t>(index)] = false;
                if (const auto keep = node.getChildWithName(kEnvelopeRetainedShapeId); keep.isValid())
                {
                    const auto retainedPoints = readPoints(keep);
                    if (retainedPoints.size() >= 2)
                    {
                        px3::BreakpointEnvelope retained;
                        retained.setPoints(retainedPoints.data(),
                                           static_cast<int>(retainedPoints.size()),
                                           static_cast<int>(keep.getProperty(kEnvelopeShapeSustainId, 2)));
                        // A child with no mode is the breakpoint shape, which
                        // is the only thing this child used to hold.
                        const auto retainedIsAdsr
                            = keep.getProperty(kEnvelopeShapeModeId).toString() == "adsr";
                        retained.setMode(retainedIsAdsr
                                             ? px3::BreakpointEnvelope::Mode::adsr
                                             : px3::BreakpointEnvelope::Mode::breakpoint);

                        if (retainedIsAdsr)
                        {
                            adsrShapes[static_cast<std::size_t>(index)] = retained;
                        }
                        else
                        {
                            breakpointShapes[static_cast<std::size_t>(index)] = retained;
                            breakpointInitialised[static_cast<std::size_t>(index)] = true;
                        }
                    }
                }

                // Versions 1 and 2 could hold a five-point AHDSR skeleton;
                // this build has only the four-point ADSR one, so the hold is
                // collapsed out on the way in.
                //
                // Keyed on the saved VERSION rather than on the shape, and that
                // distinction matters: a user who has added a point to an ADSR
                // also has five points holding at index 3, and is indis-
                // tinguishable from a legacy hold by inspection. Migrating by
                // shape would silently delete their breakpoint.
                auto restored = version < 3 ? px3::withoutHoldStage(envelope) : envelope;

                // Same guarantee as entering the mode. Saved state can carry
                // shapes editing no longer reaches - two bare points, or a
                // collapsed duration - so they are put right on the way in.
                restored.normaliseForBreakpointEditing();
                setShapedEnvelope(index, restored);

                // Whichever mode is NOT active still needs its state, or
                // switching after a reload would find nothing there. The active
                // one is the shape just restored; the other comes from the
                // retained child, or from the ADSR the parameters describe.
                if (restored.isBreakpointMode())
                {
                    breakpointShapes[static_cast<std::size_t>(index)] = restored;
                    breakpointInitialised[static_cast<std::size_t>(index)] = true;
                }
                else
                {
                    adsrShapes[static_cast<std::size_t>(index)] = restored;
                }
            }
        }
    }

    // Both the DAW restore and the preset load land here, and both may have just
    // changed which wavetable each oscillator wants. Building it now, on the
    // thread that called us, means the next block plays the RESTORED table.
    // Leaving it to the async path restores every number correctly and plays the
    // previous table until the message thread catches up, which is a bug that
    // looks like a working restore.
    refreshWavetableSelections();

    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PX3SynthAudioProcessor();
}

PX3SynthAudioProcessor::LoadedPreset PX3SynthAudioProcessor::getLoadedPreset() const
{
    const std::scoped_lock<std::mutex> lock(loadedPresetMutex);
    return loadedPreset;
}

void PX3SynthAudioProcessor::setLoadedPreset(const LoadedPreset& preset)
{
    const std::scoped_lock<std::mutex> lock(loadedPresetMutex);
    loadedPreset = preset;
}

juce::String PX3SynthAudioProcessor::loadReverbImpulseResponse(const juce::File& file)
{
    const auto error = reverb.loadImpulseResponse(file);
    if (error.isEmpty()) { reverbImpulseResponsePath = file.getFullPathName(); }
    return error;
}

void PX3SynthAudioProcessor::clearReverbImpulseResponse()
{
    reverb.clearImpulseResponse();
    reverbImpulseResponsePath.clear();
}
