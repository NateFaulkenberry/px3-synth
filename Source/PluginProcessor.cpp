#include "PluginProcessor.h"

#include "PluginEditor.h"

SynthProjectAudioProcessor::SynthProjectAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    oscSineParam = new juce::AudioParameterFloat("oscSine", "Osc Sine", juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f);
    oscSawParam = new juce::AudioParameterFloat("oscSaw", "Osc Saw", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    oscSquareParam = new juce::AudioParameterFloat("oscSquare", "Osc Square", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    filterCutoffParam = new juce::AudioParameterFloat("filterCutoff", "Filter Cutoff", juce::NormalisableRange<float>(80.0f, 18000.0f, 1.0f, 0.35f), 12000.0f);
    filterResonanceParam = new juce::AudioParameterFloat("filterResonance", "Filter Resonance", juce::NormalisableRange<float>(0.25f, 2.2f), 0.8f);
    attackParam = new juce::AudioParameterFloat("ampAttack", "Amp Attack", juce::NormalisableRange<float>(0.001f, 3.0f, 0.001f, 0.45f), 0.005f);
    decayParam = new juce::AudioParameterFloat("ampDecay", "Amp Decay", juce::NormalisableRange<float>(0.005f, 4.0f, 0.001f, 0.45f), 0.050f);
    sustainParam = new juce::AudioParameterFloat("ampSustain", "Amp Sustain", juce::NormalisableRange<float>(0.0f, 1.0f), 0.8f);
    releaseParam = new juce::AudioParameterFloat("ampRelease", "Amp Release", juce::NormalisableRange<float>(0.010f, 5.0f, 0.001f, 0.45f), 0.100f);
    masterGainParam = new juce::AudioParameterFloat("masterGain", "Master Gain", juce::NormalisableRange<float>(0.0f, 1.0f), 0.6f);

    addParameter(oscSineParam);
    addParameter(oscSawParam);
    addParameter(oscSquareParam);
    addParameter(filterCutoffParam);
    addParameter(filterResonanceParam);
    addParameter(attackParam);
    addParameter(decayParam);
    addParameter(sustainParam);
    addParameter(releaseParam);
    addParameter(masterGainParam);

    const auto initialEnvelope = currentEnvelopeSettings();
    const auto initialSubtractive = currentSubtractiveSettings();

    for (int voice = 0; voice < 16; ++voice)
    {
        auto* synthVoice = new SynthVoice();
        synthVoice->setEnvelope(initialEnvelope);
        synthVoice->setSubtractiveSettings(initialSubtractive);
        synth.addVoice(synthVoice);
    }

    synth.addSound(new SynthSound());
    clearAllActiveNotes();
}

SynthProjectAudioProcessor::~SynthProjectAudioProcessor() = default;

const juce::String SynthProjectAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SynthProjectAudioProcessor::acceptsMidi() const
{
    return true;
}

bool SynthProjectAudioProcessor::producesMidi() const
{
    return false;
}

bool SynthProjectAudioProcessor::isMidiEffect() const
{
    return false;
}

double SynthProjectAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SynthProjectAudioProcessor::getNumPrograms()
{
    return 1;
}

int SynthProjectAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SynthProjectAudioProcessor::setCurrentProgram(int)
{
}

const juce::String SynthProjectAudioProcessor::getProgramName(int)
{
    return {};
}

void SynthProjectAudioProcessor::changeProgramName(int, const juce::String&)
{
}

void SynthProjectAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate(sampleRate);

    const auto envelope = currentEnvelopeSettings();
    const auto subtractive = currentSubtractiveSettings();

    for (int voiceIndex = 0; voiceIndex < synth.getNumVoices(); ++voiceIndex)
    {
        if (auto* voice = dynamic_cast<SynthVoice*>(synth.getVoice(voiceIndex)))
        {
            voice->setEnvelope(envelope);
            voice->setSubtractiveSettings(subtractive);
        }
    }

    juce::ignoreUnused(samplesPerBlock);
}

void SynthProjectAudioProcessor::releaseResources()
{
}

bool SynthProjectAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
           || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SynthProjectAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
    {
        buffer.clear(channel, 0, buffer.getNumSamples());
    }

    // Audio thread safety: only lock-free atomic note-state updates here for UI feedback.
    updateActiveNotesFromMidi(midiMessages);

    const auto envelope = currentEnvelopeSettings();
    const auto subtractive = currentSubtractiveSettings();

    for (int voiceIndex = 0; voiceIndex < synth.getNumVoices(); ++voiceIndex)
    {
        if (auto* voice = dynamic_cast<SynthVoice*>(synth.getVoice(voiceIndex)))
        {
            voice->setEnvelope(envelope);
            voice->setSubtractiveSettings(subtractive);
        }
    }

    synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());
}

void SynthProjectAudioProcessor::updateActiveNotesFromMidi(const juce::MidiBuffer& midiMessages)
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
            continue;
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

void SynthProjectAudioProcessor::clearAllActiveNotes()
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

void SynthProjectAudioProcessor::incrementNoteCount(std::size_t index)
{
    activeNoteCounts[index].fetch_add(1, std::memory_order_relaxed);
}

void SynthProjectAudioProcessor::decrementNoteCount(std::size_t index)
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

std::array<bool, PianoKeyboard::totalKeys> SynthProjectAudioProcessor::copyActiveNoteStates() const
{
    std::array<bool, PianoKeyboard::totalKeys> states {};

    for (std::size_t i = 0; i < states.size(); ++i)
    {
        states[i] = activeNoteCounts[i].load(std::memory_order_relaxed) > 0;
    }

    return states;
}

std::array<float, PianoKeyboard::totalKeys> SynthProjectAudioProcessor::copyActiveNoteVelocities() const
{
    std::array<float, PianoKeyboard::totalKeys> velocities {};

    for (std::size_t i = 0; i < velocities.size(); ++i)
    {
        const auto midiVelocity = activeNoteVelocities[i].load(std::memory_order_relaxed);
        velocities[i] = juce::jlimit(0.0f, 1.0f, static_cast<float>(midiVelocity) / 127.0f);
    }

    return velocities;
}

SynthProjectAudioProcessor::MidiStatus SynthProjectAudioProcessor::copyMidiStatus() const
{
    MidiStatus status;
    status.noteNumber = lastMidiNote.load(std::memory_order_relaxed);
    status.velocity = lastMidiVelocity.load(std::memory_order_relaxed);
    status.noteOn = lastMidiNoteOn.load(std::memory_order_relaxed) != 0;
    return status;
}

juce::AudioParameterFloat& SynthProjectAudioProcessor::getOscSineParam() const
{
    return *oscSineParam;
}

juce::AudioParameterFloat& SynthProjectAudioProcessor::getOscSawParam() const
{
    return *oscSawParam;
}

juce::AudioParameterFloat& SynthProjectAudioProcessor::getOscSquareParam() const
{
    return *oscSquareParam;
}

juce::AudioParameterFloat& SynthProjectAudioProcessor::getFilterCutoffParam() const
{
    return *filterCutoffParam;
}

juce::AudioParameterFloat& SynthProjectAudioProcessor::getFilterResonanceParam() const
{
    return *filterResonanceParam;
}

juce::AudioParameterFloat& SynthProjectAudioProcessor::getAttackParam() const
{
    return *attackParam;
}

juce::AudioParameterFloat& SynthProjectAudioProcessor::getDecayParam() const
{
    return *decayParam;
}

juce::AudioParameterFloat& SynthProjectAudioProcessor::getSustainParam() const
{
    return *sustainParam;
}

juce::AudioParameterFloat& SynthProjectAudioProcessor::getReleaseParam() const
{
    return *releaseParam;
}

juce::AudioParameterFloat& SynthProjectAudioProcessor::getMasterGainParam() const
{
    return *masterGainParam;
}

SubtractiveSettings SynthProjectAudioProcessor::currentSubtractiveSettings() const
{
    SubtractiveSettings settings;
    settings.sineMix = oscSineParam->get();
    settings.sawMix = oscSawParam->get();
    settings.squareMix = oscSquareParam->get();
    settings.filterCutoffHz = filterCutoffParam->get();
    settings.filterResonanceQ = filterResonanceParam->get();
    settings.masterGain = masterGainParam->get();
    return settings;
}

EnvelopeSettings SynthProjectAudioProcessor::currentEnvelopeSettings() const
{
    EnvelopeSettings settings;
    settings.attackSeconds = attackParam->get();
    settings.decaySeconds = decayParam->get();
    settings.sustainLevel = sustainParam->get();
    settings.releaseSeconds = releaseParam->get();
    return settings;
}

bool SynthProjectAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* SynthProjectAudioProcessor::createEditor()
{
    return new SynthProjectAudioProcessorEditor(*this);
}

void SynthProjectAudioProcessor::getStateInformation(juce::MemoryBlock&)
{
}

void SynthProjectAudioProcessor::setStateInformation(const void*, int)
{
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SynthProjectAudioProcessor();
}
