#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <cmath>

namespace
{
constexpr double kMaxIsaacDelaySeconds = 4.0;
constexpr double kMaxMoonDelaySeconds = 0.35;

float divisionBeatsForIndex(int index)
{
    // 0 = free-running; 1+ are beat subdivisions.
    static constexpr std::array<float, 8> kBeatDivisions { 0.0f, 4.0f, 2.0f, 1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f };
    const auto clamped = juce::jlimit(0, static_cast<int>(kBeatDivisions.size()) - 1, index);
    return kBeatDivisions[static_cast<std::size_t>(clamped)];
}

inline float clamp01(float v)
{
    return juce::jlimit(0.0f, 1.0f, v);
}

inline float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

inline float smoothstep(float x)
{
    const auto t = clamp01(x);
    return t * t * (3.0f - 2.0f * t);
}
}

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

    robAmountParam = new juce::AudioParameterFloat("robAmount", "Harmonic Drive", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    isaacAmountParam = new juce::AudioParameterFloat("isaacAmount", "Granular Delay", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    granularSyncDivisionParam = new juce::AudioParameterChoice("granularSyncDivision",
                                                                "Granular Sync",
                                                                juce::StringArray { "Free", "1 Bar", "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T" },
                                                                0);
    reverbAmountParam = new juce::AudioParameterFloat("reverbAmount", "Reverb", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    reverbAlgorithmParam = new juce::AudioParameterChoice("reverbAlgorithm",
                                                           "Reverb Algorithm",
                                                           juce::StringArray { "Hall", "Plate", "Room", "Cavern", "Moon" },
                                                           0);
    pitchBendRangeParam = new juce::AudioParameterInt("pitchBendRange",
                                                       "Pitch Bend Range",
                                                       1,
                                                       24,
                                                       2);

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
    addParameter(robAmountParam);
    addParameter(isaacAmountParam);
    addParameter(granularSyncDivisionParam);
    addParameter(reverbAmountParam);
    addParameter(reverbAlgorithmParam);
    addParameter(pitchBendRangeParam);

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

    prepareIsaacEngine(sampleRate);
    prepareReverbEngine(sampleRate);

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

    updateActiveNotesFromMidi(midiMessages);

    const auto pitchBend = juce::jlimit(-1.0f, 1.0f, pitchBendNormalized.load(std::memory_order_relaxed));
    const auto modWheel = juce::jlimit(0.0f, 1.0f, modWheelNormalized.load(std::memory_order_relaxed));
    const auto bendRange = static_cast<float>(pitchBendRangeParam->get());
    constexpr float vibratoRateHz = 5.0f;
    constexpr float vibratoMaxDepthSemitones = 1.0f;

    const auto envelope = currentEnvelopeSettings();
    const auto subtractive = currentSubtractiveSettings();

    for (int voiceIndex = 0; voiceIndex < synth.getNumVoices(); ++voiceIndex)
    {
        if (auto* voice = dynamic_cast<SynthVoice*>(synth.getVoice(voiceIndex)))
        {
            voice->setEnvelope(envelope);
            voice->setSubtractiveSettings(subtractive);
            voice->setPerformanceModulation(pitchBend,
                                            modWheel,
                                            bendRange,
                                            vibratoPhaseRadians,
                                            vibratoRateHz,
                                            vibratoMaxDepthSemitones);
        }
    }

    synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());
    updateTransportState();

    const auto robAmount = clamp01(robAmountParam->get());
    const auto isaacAmount = clamp01(isaacAmountParam->get());
    const auto syncDivisionIndex = granularSyncDivisionParam->getIndex();
    const auto reverbAmount = clamp01(reverbAmountParam->get());
    const auto reverbAlgorithmIndex = reverbAlgorithmParam->getIndex();

    if (reverbAmount > 0.0001f)
    {
        const auto a = smoothstep(reverbAmount);
        juce::Reverb::Parameters p;
        p.freezeMode = 0.0f;
        p.width = 0.86f;
        p.damping = 0.44f;

        switch (juce::jlimit(0, 4, reverbAlgorithmIndex))
        {
            case 0: // Hall
                p.roomSize = 0.72f;
                p.damping = 0.38f;
                p.width = 0.92f;
                break;
            case 1: // Plate
                p.roomSize = 0.58f;
                p.damping = 0.52f;
                p.width = 0.88f;
                break;
            case 2: // Room
                p.roomSize = 0.40f;
                p.damping = 0.62f;
                p.width = 0.76f;
                break;
            case 3: // Cavern
                p.roomSize = 0.94f;
                p.damping = 0.22f;
                p.width = 1.0f;
                break;
            case 4: // Moon
                p.roomSize = 0.83f;
                p.damping = 0.28f;
                p.width = 1.0f;
                break;
            default:
                break;
        }

        p.wetLevel = lerp(0.02f, 0.80f, std::pow(a, 1.25f));
        p.dryLevel = lerp(1.0f, 0.18f, std::pow(a, 1.05f));
        reverb.setParameters(p);
    }

    const auto blockPhaseAdvance = juce::MathConstants<float>::twoPi * vibratoRateHz
                                   * (static_cast<float>(buffer.getNumSamples()) / static_cast<float>(juce::jmax(1.0, currentSampleRateHz)));
    vibratoPhaseRadians += blockPhaseAdvance;
    while (vibratoPhaseRadians >= juce::MathConstants<float>::twoPi)
    {
        vibratoPhaseRadians -= juce::MathConstants<float>::twoPi;
    }

    const auto activityDecay = std::exp(-static_cast<float>(buffer.getNumSamples())
                                        / (static_cast<float>(juce::jmax(1.0, currentSampleRateHz)) * 0.25f));
    pitchBendActivity.store(pitchBendActivity.load(std::memory_order_relaxed) * activityDecay, std::memory_order_relaxed);
    modWheelActivity.store(modWheelActivity.load(std::memory_order_relaxed) * activityDecay, std::memory_order_relaxed);

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        auto inL = buffer.getSample(0, sample);
        auto inR = (buffer.getNumChannels() > 1) ? buffer.getSample(1, sample) : inL;

        inL = processRobSample(inL, 0, robAmount);
        inR = processRobSample(inR, 1, robAmount);

        float outL = inL;
        float outR = inR;
        processIsaacGranularSample(inL, inR, isaacAmount, syncDivisionIndex, outL, outR);
        processReverbSampleFrame(outL, outR, reverbAmount, reverbAlgorithmIndex, outL, outR);

        buffer.setSample(0, sample, outL);
        if (buffer.getNumChannels() > 1)
        {
            buffer.setSample(1, sample, outR);
        }
    }
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

juce::AudioParameterFloat& SynthProjectAudioProcessor::getOscSineParam() const { return *oscSineParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getOscSawParam() const { return *oscSawParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getOscSquareParam() const { return *oscSquareParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getFilterCutoffParam() const { return *filterCutoffParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getFilterResonanceParam() const { return *filterResonanceParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getAttackParam() const { return *attackParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getDecayParam() const { return *decayParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getSustainParam() const { return *sustainParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getReleaseParam() const { return *releaseParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getMasterGainParam() const { return *masterGainParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getRobAmountParam() const { return *robAmountParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getIsaacAmountParam() const { return *isaacAmountParam; }
juce::AudioParameterChoice& SynthProjectAudioProcessor::getGranularSyncDivisionParam() const { return *granularSyncDivisionParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getReverbAmountParam() const { return *reverbAmountParam; }
juce::AudioParameterChoice& SynthProjectAudioProcessor::getReverbAlgorithmParam() const { return *reverbAlgorithmParam; }
juce::AudioParameterInt& SynthProjectAudioProcessor::getPitchBendRangeParam() const { return *pitchBendRangeParam; }

float SynthProjectAudioProcessor::copyPitchBendNormalized() const
{
    return pitchBendNormalized.load(std::memory_order_relaxed);
}

float SynthProjectAudioProcessor::copyModWheelNormalized() const
{
    return modWheelNormalized.load(std::memory_order_relaxed);
}

float SynthProjectAudioProcessor::copyPitchBendActivity() const
{
    return pitchBendActivity.load(std::memory_order_relaxed);
}

float SynthProjectAudioProcessor::copyModWheelActivity() const
{
    return modWheelActivity.load(std::memory_order_relaxed);
}

void SynthProjectAudioProcessor::setPitchBendNormalizedFromUI(float normalized)
{
    const auto value = juce::jlimit(-1.0f, 1.0f, normalized);
    const auto previous = pitchBendNormalized.load(std::memory_order_relaxed);
    if (std::abs(value - previous) > 0.0005f)
    {
        pitchBendActivity.store(1.0f, std::memory_order_relaxed);
    }
    pitchBendNormalized.store(value, std::memory_order_relaxed);
}

void SynthProjectAudioProcessor::setModWheelNormalizedFromUI(float normalized)
{
    const auto value = juce::jlimit(0.0f, 1.0f, normalized);
    const auto previous = modWheelNormalized.load(std::memory_order_relaxed);
    if (std::abs(value - previous) > 0.0005f)
    {
        modWheelActivity.store(1.0f, std::memory_order_relaxed);
    }
    modWheelNormalized.store(value, std::memory_order_relaxed);
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

void SynthProjectAudioProcessor::prepareIsaacEngine(double sampleRate)
{
    currentSampleRateHz = juce::jmax(8000.0, sampleRate);
    isaacBufferSize = static_cast<int>(std::ceil(currentSampleRateHz * kMaxIsaacDelaySeconds));
    isaacBufferSize = juce::jmax(1, isaacBufferSize);

    for (auto& channelBuffer : isaacDelayBuffer)
    {
        channelBuffer.assign(static_cast<std::size_t>(isaacBufferSize), 0.0f);
    }

    for (auto& grain : isaacGrains)
    {
        grain = Grain {};
    }

    isaacWritePos = 0;
    isaacSpawnCounter = 0;
    isaacPanPhase = 0.0f;
    isaacFeedbackFilter = { { 0.0f, 0.0f } };
}

void SynthProjectAudioProcessor::prepareReverbEngine(double sampleRate)
{
    juce::ignoreUnused(sampleRate);

    reverb.reset();

    moonBufferSize = juce::jmax(1, static_cast<int>(std::ceil(currentSampleRateHz * kMaxMoonDelaySeconds)));
    moonWritePos = 0;
    moonPhase = 0.0f;

    for (auto& channelBuffer : moonDelayBuffer)
    {
        channelBuffer.assign(static_cast<std::size_t>(moonBufferSize), 0.0f);
    }
}

void SynthProjectAudioProcessor::updateTransportState()
{
    currentBpm = 120.0;
    currentTimelineSeconds = 0.0;

    if (auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            if (const auto bpm = position->getBpm())
            {
                currentBpm = juce::jmax(20.0, *bpm);
            }

            if (const auto time = position->getTimeInSeconds())
            {
                currentTimelineSeconds = *time;
            }
            else if (const auto ppq = position->getPpqPosition())
            {
                currentTimelineSeconds = (*ppq * 60.0) / currentBpm;
            }
        }
    }
}

float SynthProjectAudioProcessor::processRobSample(float x, int channel, float robAmount)
{
    if (robAmount <= 0.0001f)
    {
        return x;
    }

    const auto warm = smoothstep(robAmount);

    robDcState[static_cast<std::size_t>(channel)] += 0.0045f * (x - robDcState[static_cast<std::size_t>(channel)]);
    const auto hp = x - robDcState[static_cast<std::size_t>(channel)] * (0.70f * warm);

    const auto drive = 1.0f + warm * 6.8f;
    const auto pre = hp * drive;
    const auto asym = pre + (0.18f * warm) * pre * pre;

    const auto satA = std::tanh(asym * 0.85f);
    const auto satB = asym / (1.0f + std::abs(asym));
    auto colored = satA * (0.62f + 0.20f * warm) + satB * (0.38f - 0.12f * warm);

    robToneState[static_cast<std::size_t>(channel)] += 0.09f * (colored - robToneState[static_cast<std::size_t>(channel)]);
    colored = 0.72f * colored + 0.28f * robToneState[static_cast<std::size_t>(channel)];

    const auto wetMix = 0.12f + 0.78f * warm;
    const auto parallel = juce::jmap(warm, x, colored);
    const auto mixed = x * (1.0f - wetMix * 0.72f) + parallel * wetMix;

    const auto levelComp = 1.0f / (1.0f + 0.58f * warm);
    return mixed * levelComp;
}

float SynthProjectAudioProcessor::readDelaySample(int channel, float readPos) const
{
    const auto& buffer = isaacDelayBuffer[static_cast<std::size_t>(channel)];
    const auto rp = readPos >= 0.0f ? readPos : readPos + static_cast<float>(isaacBufferSize);
    const auto i0 = static_cast<int>(rp) % isaacBufferSize;
    const auto i1 = (i0 + 1) % isaacBufferSize;
    const auto frac = rp - static_cast<float>(i0);
    return buffer[static_cast<std::size_t>(i0)] + (buffer[static_cast<std::size_t>(i1)] - buffer[static_cast<std::size_t>(i0)]) * frac;
}

void SynthProjectAudioProcessor::spawnIsaacGrain(float amount, int syncDivisionIndex)
{
    for (auto& grain : isaacGrains)
    {
        if (grain.active)
        {
            continue;
        }

        grain.active = true;

        const auto a = smoothstep(amount);
        const auto macro = std::pow(a, 0.62f);
        const auto grainMs = lerp(35.0f, 170.0f, a);
        grain.lengthSamples = juce::jmax(24, static_cast<int>(std::round(grainMs * 0.001f * static_cast<float>(currentSampleRateHz))));
        grain.ageSamples = 0;

        const std::array<int, 7> intervals { -12, -7, -5, 0, 5, 7, 12 };
        const auto chooseWide = juce::Random::getSystemRandom().nextFloat() < (0.22f + 0.70f * macro);
        const auto idx = chooseWide ? juce::Random::getSystemRandom().nextInt(static_cast<int>(intervals.size())) : 3;
        const auto semitone = static_cast<float>(intervals[static_cast<std::size_t>(idx)]);
        const auto micro = (juce::Random::getSystemRandom().nextFloat() - 0.5f) * (0.16f + 0.26f * macro);
        grain.increment = std::pow(2.0f, (semitone + micro) / 12.0f);

        const auto secPerBeat = static_cast<float>(60.0 / currentBpm);
        auto beatDelay = lerp(0.125f, 0.75f, macro);
        const auto syncBeats = divisionBeatsForIndex(syncDivisionIndex);
        const auto syncEnabled = syncDivisionIndex > 0 && syncBeats > 0.0f;
        if (syncEnabled)
        {
            beatDelay = syncBeats;
        }
        const auto baseDelaySamples = beatDelay * secPerBeat * static_cast<float>(currentSampleRateHz);
        const auto jitterWidth = syncEnabled ? (0.01f + 0.05f * macro) : (0.06f + 0.22f * macro);
        const auto jitter = (juce::Random::getSystemRandom().nextFloat() - 0.5f) * jitterWidth * baseDelaySamples;

        auto readPos = static_cast<float>(isaacWritePos) - (baseDelaySamples + jitter);
        while (readPos < 0.0f)
        {
            readPos += static_cast<float>(isaacBufferSize);
        }
        while (readPos >= static_cast<float>(isaacBufferSize))
        {
            readPos -= static_cast<float>(isaacBufferSize);
        }
        grain.readPos = readPos;

        isaacPanPhase += lerp(0.13f, 0.36f, macro);
        if (isaacPanPhase > juce::MathConstants<float>::twoPi)
        {
            isaacPanPhase -= juce::MathConstants<float>::twoPi;
        }

        const auto panSpread = lerp(0.14f, 0.88f, macro);
        grain.pan = 0.5f + std::sin(isaacPanPhase) * 0.5f * panSpread;
        grain.gain = lerp(0.11f, 0.30f, macro);
        return;
    }
}

void SynthProjectAudioProcessor::processIsaacGranularSample(float inL,
                                                            float inR,
                                                            float amount,
                                                            int syncDivisionIndex,
                                                            float& outL,
                                                            float& outR)
{
    if (isaacBufferSize <= 1)
    {
        outL = inL;
        outR = inR;
        return;
    }

    if (amount <= 0.0001f)
    {
        isaacDelayBuffer[0][static_cast<std::size_t>(isaacWritePos)] = inL;
        isaacDelayBuffer[1][static_cast<std::size_t>(isaacWritePos)] = inR;
        isaacWritePos = (isaacWritePos + 1) % isaacBufferSize;
        outL = inL;
        outR = inR;
        return;
    }

    const auto a = smoothstep(amount);
    const auto macro = std::pow(a, 0.62f);
    const auto secPerBeat = static_cast<float>(60.0 / currentBpm);
    auto spawnEverySec = lerp(0.085f, 0.028f, macro) * secPerBeat * 2.0f;
    const auto syncBeats = divisionBeatsForIndex(syncDivisionIndex);
    if (syncDivisionIndex > 0 && syncBeats > 0.0f)
    {
        // In sync mode, lock spawn cadence to a musical fraction of the selected division.
        spawnEverySec = juce::jmax(0.010f, secPerBeat * syncBeats * lerp(0.38f, 0.25f, macro));
    }
    const auto spawnEverySamples = juce::jmax(12, static_cast<int>(std::round(spawnEverySec * static_cast<float>(currentSampleRateHz))));

    if (++isaacSpawnCounter >= spawnEverySamples)
    {
        isaacSpawnCounter = 0;
        spawnIsaacGrain(a, syncDivisionIndex);
    }

    float wetL = 0.0f;
    float wetR = 0.0f;

    for (auto& grain : isaacGrains)
    {
        if (!grain.active)
        {
            continue;
        }

        const auto age = static_cast<float>(grain.ageSamples);
        const auto len = static_cast<float>(juce::jmax(1, grain.lengthSamples));
        const auto phase = age / len;

        if (phase >= 1.0f)
        {
            grain.active = false;
            continue;
        }

        const auto window = 0.5f - 0.5f * std::cos(phase * juce::MathConstants<float>::twoPi);
        const auto g = grain.gain * window;

        const auto left = readDelaySample(0, grain.readPos);
        const auto right = readDelaySample(1, grain.readPos);
        const auto mono = 0.5f * (left + right);

        const auto pan = clamp01(grain.pan);
        const auto panAngle = pan * juce::MathConstants<float>::halfPi;
        const auto gainL = std::cos(panAngle);
        const auto gainR = std::sin(panAngle);

        wetL += mono * g * gainL;
        wetR += mono * g * gainR;

        grain.readPos += grain.increment;
        while (grain.readPos >= static_cast<float>(isaacBufferSize))
        {
            grain.readPos -= static_cast<float>(isaacBufferSize);
        }

        ++grain.ageSamples;
    }

    const auto dampCoeff = lerp(0.19f, 0.06f, macro);
    isaacFeedbackFilter[0] += dampCoeff * (wetL - isaacFeedbackFilter[0]);
    isaacFeedbackFilter[1] += dampCoeff * (wetR - isaacFeedbackFilter[1]);

    wetL = std::tanh((wetL * 0.82f + isaacFeedbackFilter[0] * 0.18f) * (1.0f + 0.25f * macro));
    wetR = std::tanh((wetR * 0.82f + isaacFeedbackFilter[1] * 0.18f) * (1.0f + 0.25f * macro));

    const auto feedback = lerp(0.16f, 0.74f, macro);
    const auto writeL = inL + wetL * feedback;
    const auto writeR = inR + wetR * feedback;

    isaacDelayBuffer[0][static_cast<std::size_t>(isaacWritePos)] = writeL;
    isaacDelayBuffer[1][static_cast<std::size_t>(isaacWritePos)] = writeR;
    isaacWritePos = (isaacWritePos + 1) % isaacBufferSize;

    const auto wetMix = lerp(0.08f, 0.92f, std::pow(macro, 1.02f));
    const auto dryMix = lerp(0.95f, 0.12f, macro);

    outL = inL * dryMix + wetL * wetMix;
    outR = inR * dryMix + wetR * wetMix;
}

float SynthProjectAudioProcessor::readMoonDelaySample(int channel, float readPos) const
{
    const auto& buffer = moonDelayBuffer[static_cast<std::size_t>(channel)];
    auto rp = readPos;

    while (rp < 0.0f)
    {
        rp += static_cast<float>(moonBufferSize);
    }
    while (rp >= static_cast<float>(moonBufferSize))
    {
        rp -= static_cast<float>(moonBufferSize);
    }

    const auto i0 = static_cast<int>(rp) % moonBufferSize;
    const auto i1 = (i0 + 1) % moonBufferSize;
    const auto frac = rp - static_cast<float>(i0);
    return buffer[static_cast<std::size_t>(i0)]
           + (buffer[static_cast<std::size_t>(i1)] - buffer[static_cast<std::size_t>(i0)]) * frac;
}

void SynthProjectAudioProcessor::processReverbSampleFrame(float inL,
                                                          float inR,
                                                          float amount,
                                                          int algorithmIndex,
                                                          float& outL,
                                                          float& outR)
{
    if (amount <= 0.0001f)
    {
        outL = inL;
        outR = inR;
        return;
    }

    const auto a = smoothstep(amount);

    float wetL = inL;
    float wetR = inR;
    reverb.processStereo(&wetL, &wetR, 1);

    if (algorithmIndex == 4 && moonBufferSize > 2)
    {
        const auto baseSamples = static_cast<float>(currentSampleRateHz * (0.055 + 0.120 * a));
        moonPhase += 0.0017f + 0.0024f * a;
        if (moonPhase > juce::MathConstants<float>::twoPi)
        {
            moonPhase -= juce::MathConstants<float>::twoPi;
        }

        const auto detuneA = std::sin(moonPhase) * (0.8f + 4.6f * a);
        const auto detuneB = std::sin(moonPhase * 0.79f + 1.4f) * (0.6f + 3.8f * a);

        const auto readAL = static_cast<float>(moonWritePos) - (baseSamples + detuneA);
        const auto readAR = static_cast<float>(moonWritePos) - (baseSamples * 0.92f - detuneB);
        const auto readBL = static_cast<float>(moonWritePos) - (baseSamples * 1.37f - detuneB * 0.8f);
        const auto readBR = static_cast<float>(moonWritePos) - (baseSamples * 1.18f + detuneA * 0.6f);

        const auto reflL = 0.62f * readMoonDelaySample(0, readAL) + 0.38f * readMoonDelaySample(1, readBL);
        const auto reflR = 0.62f * readMoonDelaySample(1, readAR) + 0.38f * readMoonDelaySample(0, readBR);

        const auto moonMix = 0.20f + 0.60f * a;
        wetL = wetL * (1.0f - moonMix) + reflL * moonMix;
        wetR = wetR * (1.0f - moonMix) + reflR * moonMix;

        moonDelayBuffer[0][static_cast<std::size_t>(moonWritePos)] = std::tanh(inL + wetL * (0.28f + 0.26f * a));
        moonDelayBuffer[1][static_cast<std::size_t>(moonWritePos)] = std::tanh(inR + wetR * (0.28f + 0.26f * a));
        moonWritePos = (moonWritePos + 1) % moonBufferSize;
    }

    outL = wetL;
    outR = wetR;
}

bool SynthProjectAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* SynthProjectAudioProcessor::createEditor()
{
    return new SynthProjectAudioProcessorEditor(*this);
}

void SynthProjectAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state("PX3_STATE");

    for (auto* parameter : getParameters())
    {
        if (const auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
        {
            state.setProperty(withID->paramID, parameter->getValue(), nullptr);
        }
    }

    if (auto xml = state.createXml())
    {
        copyXmlToBinary(*xml, destData);
    }
}

void SynthProjectAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary(data, sizeInBytes);

    if (xml == nullptr)
    {
        return;
    }

    const auto state = juce::ValueTree::fromXml(*xml);

    if (!state.isValid())
    {
        return;
    }

    for (auto* parameter : getParameters())
    {
        if (const auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
        {
            if (state.hasProperty(withID->paramID))
            {
                const auto value = static_cast<float>(state[withID->paramID]);
                parameter->setValueNotifyingHost(value);
            }
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SynthProjectAudioProcessor();
}
