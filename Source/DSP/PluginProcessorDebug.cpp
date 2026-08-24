#include "PluginProcessor.h"
#include "PluginProcessorInternals.h"

// File role: debug instrumentation, snapshots, and diagnostic helpers.
// Keep optional/debug-only introspection here so production audio path code
// remains easier to navigate in the core and domain-specific files.

using namespace px3::processor_internal;

//==============================================================================
// Debug And Diagnostics
//==============================================================================
float PX3SynthAudioProcessor::debugGetVibeGlobalAmount() const
{
    return juce::jlimit(0.0f, 1.0f, vibeAmountParam->get());
}

float PX3SynthAudioProcessor::debugGetVibeEffectiveAmount() const
{
    return juce::jlimit(0.0f, 1.0f, vibeEngine.getEffectiveAmount());
}

bool PX3SynthAudioProcessor::debugGetVibeBypass() const
{
    return !vibeEnabledParam->get();
}

uint32_t PX3SynthAudioProcessor::debugGetVibeSeed() const
{
    return vibeSeed.load(std::memory_order_relaxed);
}

VibeTuning PX3SynthAudioProcessor::debugGetVibeTuning() const
{
    VibeTuning t;
    t.oscillatorDrift = juce::jlimit(0.0f, 1.0f, vibeTuneOscDrift.load(std::memory_order_relaxed));
    t.voiceVariation = juce::jlimit(0.0f, 1.0f, vibeTuneVoiceVar.load(std::memory_order_relaxed));
    t.filterVariation = juce::jlimit(0.0f, 1.0f, vibeTuneFilterVar.load(std::memory_order_relaxed));
    t.saturation = juce::jlimit(0.0f, 1.0f, vibeTuneSaturation.load(std::memory_order_relaxed));
    t.noise = juce::jlimit(0.0f, 1.0f, vibeTuneNoise.load(std::memory_order_relaxed));
    t.psuMovement = juce::jlimit(0.0f, 1.0f, vibeTunePsu.load(std::memory_order_relaxed));
    t.vcaNonlinearity = juce::jlimit(0.0f, 1.0f, vibeTuneVca.load(std::memory_order_relaxed));
    t.waveformAsymmetry = juce::jlimit(0.0f, 1.0f, vibeTuneAsym.load(std::memory_order_relaxed));
    t.temperatureDrift = juce::jlimit(0.0f, 1.0f, vibeTuneTemp.load(std::memory_order_relaxed));
    t.correlatedChaos = juce::jlimit(0.0f, 1.0f, vibeTuneChaos.load(std::memory_order_relaxed));
    return t;
}

void PX3SynthAudioProcessor::debugSetVibeBypass(bool shouldBypass)
{
    const auto enabledValue = shouldBypass ? 0.0f : 1.0f;
    vibeEnabledParam->setValueNotifyingHost(enabledValue);
}

void PX3SynthAudioProcessor::debugSetVibeSeed(uint32_t seed)
{
    vibeSeed.store(seed == 0u ? 1u : seed, std::memory_order_relaxed);
}

void PX3SynthAudioProcessor::debugSetVibeTuningValue(const juce::String& key, float value)
{
    const auto v = juce::jlimit(0.0f, 1.0f, value);
    if (key.equalsIgnoreCase("oscillatorDrift")) vibeTuneOscDrift.store(v, std::memory_order_relaxed);
    else if (key.equalsIgnoreCase("voiceVariation")) vibeTuneVoiceVar.store(v, std::memory_order_relaxed);
    else if (key.equalsIgnoreCase("filterVariation")) vibeTuneFilterVar.store(v, std::memory_order_relaxed);
    else if (key.equalsIgnoreCase("saturation")) vibeTuneSaturation.store(v, std::memory_order_relaxed);
    else if (key.equalsIgnoreCase("noise")) vibeTuneNoise.store(v, std::memory_order_relaxed);
    else if (key.equalsIgnoreCase("psuMovement")) vibeTunePsu.store(v, std::memory_order_relaxed);
    else if (key.equalsIgnoreCase("vcaNonlinearity")) vibeTuneVca.store(v, std::memory_order_relaxed);
    else if (key.equalsIgnoreCase("waveformAsymmetry")) vibeTuneAsym.store(v, std::memory_order_relaxed);
    else if (key.equalsIgnoreCase("temperatureDrift")) vibeTuneTemp.store(v, std::memory_order_relaxed);
    else if (key.equalsIgnoreCase("correlatedChaos")) vibeTuneChaos.store(v, std::memory_order_relaxed);
}

float PX3SynthAudioProcessor::debugGetVibeTuningValue(const juce::String& key) const
{
    if (key.equalsIgnoreCase("oscillatorDrift")) return vibeTuneOscDrift.load(std::memory_order_relaxed);
    if (key.equalsIgnoreCase("voiceVariation")) return vibeTuneVoiceVar.load(std::memory_order_relaxed);
    if (key.equalsIgnoreCase("filterVariation")) return vibeTuneFilterVar.load(std::memory_order_relaxed);
    if (key.equalsIgnoreCase("saturation")) return vibeTuneSaturation.load(std::memory_order_relaxed);
    if (key.equalsIgnoreCase("noise")) return vibeTuneNoise.load(std::memory_order_relaxed);
    if (key.equalsIgnoreCase("psuMovement")) return vibeTunePsu.load(std::memory_order_relaxed);
    if (key.equalsIgnoreCase("vcaNonlinearity")) return vibeTuneVca.load(std::memory_order_relaxed);
    if (key.equalsIgnoreCase("waveformAsymmetry")) return vibeTuneAsym.load(std::memory_order_relaxed);
    if (key.equalsIgnoreCase("temperatureDrift")) return vibeTuneTemp.load(std::memory_order_relaxed);
    if (key.equalsIgnoreCase("correlatedChaos")) return vibeTuneChaos.load(std::memory_order_relaxed);
    return 0.0f;
}

juce::String PX3SynthAudioProcessor::debugGetInstanceId() const
{
    return debugInstanceId;
}

juce::String PX3SynthAudioProcessor::debugGetProcessorCreatedTime() const
{
    return debugProcessorCreatedTime;
}

juce::String PX3SynthAudioProcessor::debugNowTimestamp() const
{
    return nowTimestamp();
}

void PX3SynthAudioProcessor::debugNotifyEditorCreated(void* editorPtr)
{
    debugLogEvent("LIFECYCLE",
                  "EDITOR_CREATED",
                  "editor=" + juce::String::toHexString(reinterpret_cast<juce::int64>(editorPtr))
                      + " order=" + debugDescribeOrder(getFxProcessingOrder()));
}

void PX3SynthAudioProcessor::debugNotifyEditorDestroyed(void* editorPtr)
{
    debugLogEvent("LIFECYCLE",
                  "EDITOR_DESTROYED",
                  "editor=" + juce::String::toHexString(reinterpret_cast<juce::int64>(editorPtr))
                      + " order=" + debugDescribeOrder(getFxProcessingOrder()));
}

void PX3SynthAudioProcessor::debugLogEvent(const juce::String& source,
                                               const juce::String& event,
                                               const juce::String& details)
{
    const auto line = "[" + nowTimestamp() + "] SOURCE=" + source + " EVENT=" + event
                      + (details.isNotEmpty() ? " " + details : juce::String());

    const std::scoped_lock<std::mutex> lock(debugStateMutex);
    debugEventLogLines.add(line);
    constexpr int maxLines = 600;
    if (debugEventLogLines.size() > maxLines)
    {
        debugEventLogLines.removeRange(0, debugEventLogLines.size() - maxLines);
    }
}

void PX3SynthAudioProcessor::debugClearEventLog()
{
    const std::scoped_lock<std::mutex> lock(debugStateMutex);
    debugEventLogLines.clear();
}

juce::String PX3SynthAudioProcessor::debugGetEventLogText() const
{
    const std::scoped_lock<std::mutex> lock(debugStateMutex);
    return debugEventLogLines.joinIntoString("\n");
}

int PX3SynthAudioProcessor::debugGetLastSerializedStateSize() const
{
    const std::scoped_lock<std::mutex> lock(debugStateMutex);
    return static_cast<int>(debugLastSerializedState.getSize());
}

juce::String PX3SynthAudioProcessor::debugGetLastSerializedStateXml() const
{
    const std::scoped_lock<std::mutex> lock(debugStateMutex);
    return debugLastSerializedStateXml;
}

juce::MemoryBlock PX3SynthAudioProcessor::debugGetLastSerializedStateCopy() const
{
    const std::scoped_lock<std::mutex> lock(debugStateMutex);
    return debugLastSerializedState;
}

bool PX3SynthAudioProcessor::debugRestoreLastSerializedState(juce::String& report)
{
    const auto snapshot = debugGetLastSerializedStateCopy();
    if (snapshot.getSize() == 0)
    {
        report = "No serialized state captured yet.";
        return false;
    }

    const auto before = getFxProcessingOrder();
    setStateInformation(snapshot.getData(), static_cast<int>(snapshot.getSize()));
    const auto after = getFxProcessingOrder();

    report = "RESTORE_LAST_SERIALIZED_STATE\n"
             "size=" + juce::String(static_cast<int>(snapshot.getSize())) + "\n"
             "before=" + debugDescribeOrder(before) + "\n"
             "after=" + debugDescribeOrder(after);
    debugLogEvent("DEBUG_PANEL", "RESTORE_LAST_SERIALIZED_STATE", report.replaceCharacters("\n", " | "));
    return true;
}

bool PX3SynthAudioProcessor::debugRoundTripCurrentState(juce::String& report)
{
    juce::MemoryBlock block;
    getStateInformation(block);
    if (block.getSize() == 0)
    {
        report = "Round trip failed: empty serialized block.";
        return false;
    }

    const auto xml = getXmlFromBinary(block.getData(), static_cast<int>(block.getSize()));
    if (xml == nullptr)
    {
        report = "Round trip failed: cannot decode XML from state block.";
        return false;
    }

    const auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid())
    {
        report = "Round trip failed: invalid ValueTree decoded.";
        return false;
    }

    const auto currentOrder = getFxProcessingOrder();
    std::array<int, 3> decodedOrder { { 0, 1, 2 } };
    if (const auto moduleOrder = state.getChildWithName(kModuleOrderId); moduleOrder.isValid())
    {
        std::array<bool, 3> seen { { false, false, false } };
        int write = 0;
        for (int i = 0; i < moduleOrder.getNumChildren() && write < 3; ++i)
        {
            const auto node = moduleOrder.getChild(i);
            if (!node.isValid() || !node.hasProperty(kModuleIdProperty))
            {
                continue;
            }

            const auto stage = stageForModuleId(node.getProperty(kModuleIdProperty).toString());
            if (stage >= 0 && !seen[static_cast<std::size_t>(stage)])
            {
                decodedOrder[static_cast<std::size_t>(write++)] = stage;
                seen[static_cast<std::size_t>(stage)] = true;
            }
        }
    }

    auto serializedLfoFrequency = lfoFrequencyParam->get();
    auto serializedLfoAssignment = juce::String("none");
    if (const auto lfoState = state.getChildWithName(kLfoStateId); lfoState.isValid())
    {
        if (lfoState.hasProperty(kLfoFrequencyId))
        {
            serializedLfoFrequency = juce::jlimit(0.01f, 20.0f, static_cast<float>(lfoState[kLfoFrequencyId]));
        }
        if (lfoState.hasProperty(kLfoAssignmentId))
        {
            serializedLfoAssignment = lfoState[kLfoAssignmentId].toString();
        }
    }

    const auto frequencyMatches = std::abs(serializedLfoFrequency - lfoFrequencyParam->get()) <= 0.0005f;
    const auto assignmentMatches = serializedLfoAssignment.equalsIgnoreCase(getLfoAssignmentParameterId());

    auto serializedAttack = attackParam->get();
    auto serializedDecay = decayParam->get();
    auto serializedSustain = sustainParam->get();
    auto serializedRelease = releaseParam->get();
    if (state.hasProperty("ampAttack")) serializedAttack = static_cast<float>(state["ampAttack"]);
    if (state.hasProperty("ampDecay")) serializedDecay = static_cast<float>(state["ampDecay"]);
    if (state.hasProperty("ampSustain")) serializedSustain = static_cast<float>(state["ampSustain"]);
    if (state.hasProperty("ampRelease")) serializedRelease = static_cast<float>(state["ampRelease"]);

    const auto attackMatches = std::abs(serializedAttack - attackParam->get()) <= 0.0005f;
    const auto decayMatches = std::abs(serializedDecay - decayParam->get()) <= 0.0005f;
    const auto sustainMatches = std::abs(serializedSustain - sustainParam->get()) <= 0.0005f;
    const auto releaseMatches = std::abs(serializedRelease - releaseParam->get()) <= 0.0005f;

    const auto pass = (debugDescribeOrder(currentOrder) == debugDescribeOrder(decodedOrder))
                   && frequencyMatches
                   && assignmentMatches
                   && attackMatches
                   && decayMatches
                   && sustainMatches
                   && releaseMatches;
    report = "TEST_STATE_ROUND_TRIP\n"
             "before=" + debugDescribeOrder(currentOrder) + "\n"
             "serialized=" + debugDescribeOrder(decodedOrder) + "\n"
             "lfoFrequencyCurrent=" + juce::String(lfoFrequencyParam->get(), 4) + "\n"
             "lfoFrequencySerialized=" + juce::String(serializedLfoFrequency, 4) + "\n"
             "lfoAssignmentCurrent=" + getLfoAssignmentParameterId() + "\n"
             "lfoAssignmentSerialized=" + serializedLfoAssignment + "\n"
             "attackCurrent=" + juce::String(attackParam->get(), 6) + "\n"
             "attackSerialized=" + juce::String(serializedAttack, 6) + "\n"
             "decayCurrent=" + juce::String(decayParam->get(), 6) + "\n"
             "decaySerialized=" + juce::String(serializedDecay, 6) + "\n"
             "sustainCurrent=" + juce::String(sustainParam->get(), 6) + "\n"
             "sustainSerialized=" + juce::String(serializedSustain, 6) + "\n"
             "releaseCurrent=" + juce::String(releaseParam->get(), 6) + "\n"
             "releaseSerialized=" + juce::String(serializedRelease, 6) + "\n"
             "size=" + juce::String(static_cast<int>(block.getSize())) + "\n"
             "result=" + juce::String(pass ? "PASS" : "FAIL");

    debugLogEvent("DEBUG_PANEL", "TEST_STATE_ROUND_TRIP", report.replaceCharacters("\n", " | "));
    return pass;
}

uint32_t PX3SynthAudioProcessor::debugGetModuleOrderGeneration() const
{
    return fxOrderRevision.load(std::memory_order_relaxed);
}

uint32_t PX3SynthAudioProcessor::debugGetModuleOrderHash() const
{
    return fxProcessingOrderPacked.load(std::memory_order_relaxed);
}

juce::String PX3SynthAudioProcessor::debugDescribeOrder(const std::array<int, 3>& order) const
{
    return formatOrderString(order);
}

float PX3SynthAudioProcessor::debugGetLfoPhase() const
{
    const auto wrapped = std::fmod(lfoPhaseForDebug.load(std::memory_order_relaxed), juce::MathConstants<float>::twoPi);
    return wrapped < 0.0f ? wrapped + juce::MathConstants<float>::twoPi : wrapped;
}

float PX3SynthAudioProcessor::debugGetLfoCurrentValue() const
{
    return lfoCurrentValue.load(std::memory_order_relaxed);
}

float PX3SynthAudioProcessor::debugGetLfoBaseNormalized() const
{
    return lfoDebugBaseNormalized.load(std::memory_order_relaxed);
}

float PX3SynthAudioProcessor::debugGetLfoEffectiveNormalized() const
{
    return lfoDebugEffectiveNormalized.load(std::memory_order_relaxed);
}

juce::String PX3SynthAudioProcessor::debugGetLfoAssignmentName() const
{
    const auto index = getLfoAssignmentIndex();
    if (index <= 0 || index >= static_cast<int>(lfoAssignableTargets.size()))
    {
        return "None";
    }

    return lfoAssignableTargets[static_cast<std::size_t>(index)].displayName
        + " [" + lfoAssignableTargets[static_cast<std::size_t>(index)].parameterId + "]";
}

