#include "PluginProcessor.h"
#include "PluginProcessorInternals.h"

// File role: MIDI and performance-control ingestion/state bridging.
// Keep note/controller parsing and UI bridge state here, not DSP effect
// processing or state serialization logic.

using namespace px3::processor_internal;

//==============================================================================
// MIDI And Performance Input
//==============================================================================
void PX3SynthAudioProcessor::updateActiveNotesFromMidi(const juce::MidiBuffer& midiMessages)
{
    const auto toMidiVelocity = [](const juce::MidiMessage& message)
    {
        const auto raw = static_cast<float>(message.getVelocity());
        if (raw <= 1.0f)
        {
            return juce::jlimit(0, 127, static_cast<int>(std::lround(raw * 127.0f)));
        }

        return juce::jlimit(0, 127, static_cast<int>(std::lround(raw)));
    };

    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();

        if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            clearAllActiveNotes();
            lastMidiNote.store(-1, std::memory_order_relaxed);
            lastMidiVelocity.store(0, std::memory_order_relaxed);
            lastMidiNoteOn.store(0, std::memory_order_relaxed);

            if (message.isAllSoundOff())
            {
                pitchBendNormalized.store(0.0f, std::memory_order_relaxed);
                modWheelNormalized.store(0.0f, std::memory_order_relaxed);
                pitchBendActivity.store(1.0f, std::memory_order_relaxed);
                modWheelActivity.store(1.0f, std::memory_order_relaxed);
            }

            continue;
        }

        if (message.isController())
        {
            // Recorded for MIDI mapping before anything else looks at it, and
            // without consuming it: CC 1 still drives the mod wheel below, and
            // a user who maps CC 1 to a cutoff gets both.
            recordMidiController(message.getControllerNumber(),
                                 message.getChannel(),
                                 message.getControllerValue());

            if (message.getControllerNumber() == 1)
            {
                const auto newValue = juce::jlimit(0.0f, 1.0f, static_cast<float>(message.getControllerValue()) / 127.0f);
                const auto previous = modWheelNormalized.load(std::memory_order_relaxed);

                if (std::abs(newValue - previous) > 0.0005f)
                {
                    modWheelActivity.store(1.0f, std::memory_order_relaxed);
                }

                modWheelNormalized.store(newValue, std::memory_order_relaxed);
            }
            else if (message.getControllerNumber() == 121)
            {
                // Reset All Controllers
                pitchBendNormalized.store(0.0f, std::memory_order_relaxed);
                modWheelNormalized.store(0.0f, std::memory_order_relaxed);
                pitchBendActivity.store(1.0f, std::memory_order_relaxed);
                modWheelActivity.store(1.0f, std::memory_order_relaxed);
            }
        }

        if (message.isPitchWheel())
        {
            const auto wheel = message.getPitchWheelValue();
            const auto newValue = juce::jlimit(-1.0f, 1.0f,
                                               (static_cast<float>(wheel) - 8192.0f) / 8192.0f);
            const auto previous = pitchBendNormalized.load(std::memory_order_relaxed);

            if (std::abs(newValue - previous) > 0.0005f)
            {
                pitchBendActivity.store(1.0f, std::memory_order_relaxed);
            }

            pitchBendNormalized.store(newValue, std::memory_order_relaxed);
        }

        if (message.isNoteOnOrOff())
        {
            const auto midiNote = message.getNoteNumber();

            if (midiNote >= PianoKeyboard::firstMidiNote && midiNote <= PianoKeyboard::lastMidiNote)
            {
                const auto index = static_cast<std::size_t>(midiNote - PianoKeyboard::firstMidiNote);

                if (message.isNoteOn())
                {
                    incrementNoteCount(index);
                    activeNoteVelocities[index].store(toMidiVelocity(message), std::memory_order_relaxed);
                }
                else
                {
                    decrementNoteCount(index);

                    if (activeNoteCounts[index].load(std::memory_order_relaxed) <= 0)
                    {
                        activeNoteVelocities[index].store(0, std::memory_order_relaxed);
                    }
                }

                lastMidiNote.store(midiNote, std::memory_order_relaxed);
                lastMidiVelocity.store(toMidiVelocity(message), std::memory_order_relaxed);
                lastMidiNoteOn.store(message.isNoteOn() ? 1 : 0, std::memory_order_relaxed);
            }
        }
    }
}

void PX3SynthAudioProcessor::clearAllActiveNotes()
{
    for (auto& noteCount : activeNoteCounts)
    {
        noteCount.store(0, std::memory_order_relaxed);
    }

    for (auto& velocity : activeNoteVelocities)
    {
        velocity.store(0, std::memory_order_relaxed);
    }
}

void PX3SynthAudioProcessor::incrementNoteCount(std::size_t index)
{
    activeNoteCounts[index].fetch_add(1, std::memory_order_relaxed);
}

void PX3SynthAudioProcessor::decrementNoteCount(std::size_t index)
{
    auto current = activeNoteCounts[index].load(std::memory_order_relaxed);

    while (current > 0)
    {
        if (activeNoteCounts[index].compare_exchange_weak(current,
                                                          current - 1,
                                                          std::memory_order_relaxed,
                                                          std::memory_order_relaxed))
        {
            break;
        }
    }
}

void PX3SynthAudioProcessor::pushVirtualNote(VirtualNote event)
{
    // Message thread only. Publishes with a release so the audio thread's
    // acquire sees the slot's contents, never just the index.
    const auto write = virtualNoteWrite.load(std::memory_order_relaxed);
    const auto next = (write + 1) % kVirtualNoteCapacity;

    if (next == virtualNoteRead.load(std::memory_order_acquire))
    {
        return;   // full - drop rather than block, which is the whole point
    }

    virtualNotes[static_cast<std::size_t>(write)] = event;
    virtualNoteWrite.store(next, std::memory_order_release);
}

void PX3SynthAudioProcessor::queueVirtualKeyboardNoteOn(int midiNote, float velocityNorm)
{
    const auto boundedNote = juce::jlimit(PianoKeyboard::firstMidiNote,
                                          PianoKeyboard::lastMidiNote, midiNote);
    pushVirtualNote({ boundedNote, juce::jlimit(0.0f, 1.0f, velocityNorm), true });
}

void PX3SynthAudioProcessor::queueVirtualKeyboardNoteOff(int midiNote)
{
    const auto boundedNote = juce::jlimit(PianoKeyboard::firstMidiNote,
                                          PianoKeyboard::lastMidiNote, midiNote);
    pushVirtualNote({ boundedNote, 0.0f, false });
}

std::array<bool, PianoKeyboard::totalKeys> PX3SynthAudioProcessor::copyActiveNoteStates() const
{
    std::array<bool, PianoKeyboard::totalKeys> states {};

    for (std::size_t i = 0; i < states.size(); ++i)
    {
        states[i] = activeNoteCounts[i].load(std::memory_order_relaxed) > 0;
    }

    return states;
}

std::array<float, PianoKeyboard::totalKeys> PX3SynthAudioProcessor::copyActiveNoteVelocities() const
{
    std::array<float, PianoKeyboard::totalKeys> velocities {};

    for (std::size_t i = 0; i < velocities.size(); ++i)
    {
        const auto midiVelocity = activeNoteVelocities[i].load(std::memory_order_relaxed);
        velocities[i] = juce::jlimit(0.0f, 1.0f, static_cast<float>(midiVelocity) / 127.0f);
    }

    return velocities;
}

PX3SynthAudioProcessor::MidiStatus PX3SynthAudioProcessor::copyMidiStatus() const
{
    MidiStatus status;
    status.noteNumber = lastMidiNote.load(std::memory_order_relaxed);
    status.velocity = lastMidiVelocity.load(std::memory_order_relaxed);
    status.noteOn = lastMidiNoteOn.load(std::memory_order_relaxed) != 0;
    return status;
}

float PX3SynthAudioProcessor::copyPitchBendNormalized() const
{
    return pitchBendNormalized.load(std::memory_order_relaxed);
}

float PX3SynthAudioProcessor::copyModWheelNormalized() const
{
    return modWheelNormalized.load(std::memory_order_relaxed);
}

float PX3SynthAudioProcessor::copyPitchBendActivity() const
{
    return pitchBendActivity.load(std::memory_order_relaxed);
}

float PX3SynthAudioProcessor::copyModWheelActivity() const
{
    return modWheelActivity.load(std::memory_order_relaxed);
}

void PX3SynthAudioProcessor::setPitchBendNormalizedFromUI(float normalized)
{
    const auto value = juce::jlimit(-1.0f, 1.0f, normalized);
    const auto previous = pitchBendNormalized.load(std::memory_order_relaxed);
    if (std::abs(value - previous) > 0.0005f)
    {
        pitchBendActivity.store(1.0f, std::memory_order_relaxed);
    }
    pitchBendNormalized.store(value, std::memory_order_relaxed);
}

void PX3SynthAudioProcessor::setModWheelNormalizedFromUI(float normalized)
{
    const auto value = juce::jlimit(0.0f, 1.0f, normalized);
    const auto previous = modWheelNormalized.load(std::memory_order_relaxed);
    if (std::abs(value - previous) > 0.0005f)
    {
        modWheelActivity.store(1.0f, std::memory_order_relaxed);
    }
    modWheelNormalized.store(value, std::memory_order_relaxed);
}


//==============================================================================
// MIDI parameter mapping. See docs/midi-mapping-design.md.
//==============================================================================

void PX3SynthAudioProcessor::recordMidiController(int ccNumber, int channel, int value)
{
    // Audio thread. Records and nothing else - two relaxed stores and two
    // release stores per CC message, no allocation, no lock, and the message
    // is not consumed: it goes on to the synth exactly as before, so note
    // input, mod wheel and pitch bend are untouched.
    if (! juce::isPositiveAndBelow(ccNumber, kMidiCcCount))
    {
        return;
    }

    const auto index = static_cast<std::size_t>(ccNumber);
    ccValues[index].store(juce::jlimit(0, 127, value), std::memory_order_relaxed);
    ccSequence[index].fetch_add(1, std::memory_order_release);

    lastTouchedCc.store(ccNumber, std::memory_order_relaxed);
    lastTouchedChannel.store(juce::jlimit(1, 16, channel), std::memory_order_relaxed);
    lastTouchedSequence.fetch_add(1, std::memory_order_release);
}

void PX3SynthAudioProcessor::setMidiLearnTargets(const juce::StringArray& parameterIds)
{
    midiLearnTargets = parameterIds;

    // Start from where the audio thread is NOW. Without this, a CC that moved
    // before the user shift-clicked anything would be waiting in the sequence
    // counter and would assign itself the instant learn was armed.
    seenTouchedSequence = lastTouchedSequence.load(std::memory_order_acquire);
}

juce::StringArray PX3SynthAudioProcessor::getMidiLearnTargets() const
{
    return midiLearnTargets;
}

bool PX3SynthAudioProcessor::isMidiLearnArmed() const
{
    return ! midiLearnTargets.isEmpty();
}

int PX3SynthAudioProcessor::getMidiCcForParameter(const juce::String& parameterId) const
{
    for (const auto& mapping : midiMappings)
    {
        if (mapping.parameterIds.contains(parameterId))
        {
            return mapping.ccNumber;
        }
    }

    return -1;
}

void PX3SynthAudioProcessor::clearMidiMappingForParameter(const juce::String& parameterId)
{
    for (auto entry = midiMappings.begin(); entry != midiMappings.end();)
    {
        entry->parameterIds.removeString(parameterId);

        // A mapping with nothing left to drive is not a mapping.
        entry = entry->parameterIds.isEmpty() ? midiMappings.erase(entry) : entry + 1;
    }
}

void PX3SynthAudioProcessor::clearAllMidiMappings()
{
    midiMappings.clear();
}

std::vector<px3::MidiMapping> PX3SynthAudioProcessor::getMidiMappings() const
{
    return midiMappings;
}

void PX3SynthAudioProcessor::assignMidiLearnTo(int ccNumber, int channel)
{
    // One parameter has one CC. Anything being assigned here leaves whatever
    // mapping it was in first, so "which CC drives this knob" always has a
    // single answer.
    for (const auto& parameterId : midiLearnTargets)
    {
        clearMidiMappingForParameter(parameterId);
    }

    px3::MidiMapping* existing = nullptr;
    for (auto& mapping : midiMappings)
    {
        if (mapping.ccNumber == ccNumber)
        {
            existing = &mapping;
            break;
        }
    }

    if (existing == nullptr)
    {
        px3::MidiMapping created;
        created.ccNumber = ccNumber;
        created.learnedChannel = channel;
        midiMappings.push_back(created);
        existing = &midiMappings.back();
    }

    for (const auto& parameterId : midiLearnTargets)
    {
        // Only IDs that name something. A mapping to a parameter that does not
        // exist would be dead weight that persists forever.
        if (findParameterById(parameterId) != nullptr)
        {
            existing->parameterIds.addIfNotAlreadyThere(parameterId);
        }
    }

    if (existing->parameterIds.isEmpty())
    {
        clearMidiMappingForParameter({});
        midiMappings.erase(std::remove_if(midiMappings.begin(), midiMappings.end(),
                                          [](const px3::MidiMapping& m)
                                          { return m.parameterIds.isEmpty(); }),
                           midiMappings.end());
    }

    midiLearnTargets.clear();

    if (onMidiMappingAssigned != nullptr)
    {
        onMidiMappingAssigned(ccNumber);
    }
}

juce::RangedAudioParameter*
PX3SynthAudioProcessor::findParameterById(const juce::String& parameterId) const
{
    for (auto* parameter : getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
        {
            if (ranged->getParameterID() == parameterId)
            {
                return ranged;
            }
        }
    }

    return nullptr;
}

void PX3SynthAudioProcessor::writeParameterFromCc(const juce::String& parameterId, int ccValue)
{
    auto* parameter = findParameterById(parameterId);
    if (parameter == nullptr)
    {
        return;   // named something that no longer exists; the rest still runs
    }

    // The CC's 0..127 spans the destination's own NORMALISED range, so a
    // cutoff in hertz and a resonance in 0..1 each get a full sweep in their
    // own units. This is the same call a knob's attachment makes, so the DAW
    // sees an ordinary parameter change and the knob moves because its
    // attachment observes the parameter - there is no second value anywhere.
    const auto normalised = juce::jlimit(0.0f, 1.0f, static_cast<float>(ccValue) / 127.0f);

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(normalised);
    parameter->endChangeGesture();
}

void PX3SynthAudioProcessor::applyPendingMidiMappings()
{
    // Message thread. Parameter writes call listeners and, in a plugin, call
    // into the host: doing that from the audio thread is how a real-time path
    // acquires a lock. The cost is that a CC lands on the next tick rather
    // than sample-accurately, which is recorded in the design document.
    if (isMidiLearnArmed())
    {
        const auto touched = lastTouchedSequence.load(std::memory_order_acquire);
        if (touched != seenTouchedSequence)
        {
            seenTouchedSequence = touched;
            const auto cc = lastTouchedCc.load(std::memory_order_relaxed);
            const auto channel = lastTouchedChannel.load(std::memory_order_relaxed);

            if (juce::isPositiveAndBelow(cc, kMidiCcCount))
            {
                assignMidiLearnTo(cc, channel);

                // The CC that taught the mapping does not also drive it in the
                // same tick: the knob would jump to wherever the controller
                // happened to be, which is not what the user asked for by
                // touching it. Its next movement does.
                ccSeenSequence[static_cast<std::size_t>(cc)]
                    = ccSequence[static_cast<std::size_t>(cc)].load(std::memory_order_acquire);
            }
        }
    }

    if (midiMappings.empty())
    {
        return;
    }

    for (const auto& mapping : midiMappings)
    {
        if (! juce::isPositiveAndBelow(mapping.ccNumber, kMidiCcCount))
        {
            continue;
        }

        const auto index = static_cast<std::size_t>(mapping.ccNumber);
        const auto sequence = ccSequence[index].load(std::memory_order_acquire);

        // Only when the controller has actually MOVED. Writing every mapped
        // parameter every tick would fight the user's own hand on the knob and
        // overwrite any automation the host is playing back.
        if (sequence == ccSeenSequence[index])
        {
            continue;
        }

        ccSeenSequence[index] = sequence;
        const auto value = ccValues[index].load(std::memory_order_relaxed);

        for (const auto& parameterId : mapping.parameterIds)
        {
            writeParameterFromCc(parameterId, value);
        }
    }
}
