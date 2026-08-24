#include "PluginProcessor.h"
#include "PluginProcessorInternals.h"
#include "FilterMode.h"
#include "LfoMode.h"
#include "OscillatorMode.h"
#include "SubOscMode.h"

#include "PluginEditor.h"
#include <cmath>

// File role: core processor orchestration and JUCE lifecycle entry points.
// Keep high-level runtime flow here (prepare/process/editor), and place
// domain-specific helpers in PluginProcessor*.cpp companion files.

using namespace px3::processor_internal;

PX3SynthAudioProcessor::PX3SynthAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    const auto instanceNumber = kInstanceCounter.fetch_add(1u, std::memory_order_relaxed) + 1u;
    debugInstanceId = "PX3-INSTANCE-" + juce::String(static_cast<int>(instanceNumber)).paddedLeft('0', 2);
    debugProcessorCreatedTime = nowTimestamp();

    fxProcessingOrderPacked.store(packFxOrder({ { 0, 1, 2 } }), std::memory_order_relaxed);
    fxOrderRevision.store(0u, std::memory_order_relaxed);

    oscSineParam = new juce::AudioParameterFloat("oscSine", "Osc Sine", juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f);
    oscSawParam = new juce::AudioParameterFloat("oscSaw", "Osc Saw", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    oscSquareParam = new juce::AudioParameterFloat("oscSquare", "Osc Square", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    oscModeParam = new juce::AudioParameterChoice("oscMode",
                                                   "Oscillator Mode",
                                                   px3::oscillatorModeChoices(),
                                                   0);
    oscMacroAParam = new juce::AudioParameterFloat("oscMacroA", "Osc Macro A", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f);
    oscMacroBParam = new juce::AudioParameterFloat("oscMacroB", "Osc Macro B", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f);
    oscMacroCParam = new juce::AudioParameterFloat("oscMacroC", "Osc Macro C", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f);
    oscVowelParam = new juce::AudioParameterChoice("oscVowel", "Osc Vowel", juce::StringArray { "A", "E", "I", "O", "U" }, 0);
    oscHarmonicParams = { {
        new juce::AudioParameterFloat("oscH1", "Osc Harmonic 1", juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f),
        new juce::AudioParameterFloat("oscH2", "Osc Harmonic 2", juce::NormalisableRange<float>(0.0f, 1.0f), 0.7f),
        new juce::AudioParameterFloat("oscH3", "Osc Harmonic 3", juce::NormalisableRange<float>(0.0f, 1.0f), 0.45f),
        new juce::AudioParameterFloat("oscH4", "Osc Harmonic 4", juce::NormalisableRange<float>(0.0f, 1.0f), 0.3f),
        new juce::AudioParameterFloat("oscH5", "Osc Harmonic 5", juce::NormalisableRange<float>(0.0f, 1.0f), 0.2f),
        new juce::AudioParameterFloat("oscH6", "Osc Harmonic 6", juce::NormalisableRange<float>(0.0f, 1.0f), 0.14f),
        new juce::AudioParameterFloat("oscH7", "Osc Harmonic 7", juce::NormalisableRange<float>(0.0f, 1.0f), 0.1f),
        new juce::AudioParameterFloat("oscH8", "Osc Harmonic 8", juce::NormalisableRange<float>(0.0f, 1.0f), 0.07f)
    } };
    subOscEnabledParam = new juce::AudioParameterBool("subOscEnabled", "Sub Osc Enabled", false);
    subOscLevelParam = new juce::AudioParameterFloat("subOscLevel", "Sub Osc Level", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    subOscOctaveParam = new juce::AudioParameterChoice("subOscOctave",
                                                        "Sub Osc Octave",
                                                        px3::subOscOctaveChoices(),
                                                        1);
    subOscWaveformParam = new juce::AudioParameterChoice("subOscWaveform",
                                                          "Sub Osc Waveform",
                                                          px3::subOscWaveformChoices(),
                                                          1);
    filterCutoffParam = new juce::AudioParameterFloat("filterCutoff", "Filter Cutoff", juce::NormalisableRange<float>(80.0f, 18000.0f, 1.0f, 0.35f), 12000.0f);
    filterResonanceParam = new juce::AudioParameterFloat("filterResonance", "Filter Resonance", juce::NormalisableRange<float>(0.25f, 2.2f), 0.8f);
    filterTypeParam = new juce::AudioParameterChoice("filterType",
                                                          "Filter Type",
                                                          px3::filterModeChoices(),
                                                          0);
    attackParam = new juce::AudioParameterFloat("ampAttack", "Amp Attack", juce::NormalisableRange<float>(0.001f, 3.0f, 0.001f, 0.45f), 0.005f);
    decayParam = new juce::AudioParameterFloat("ampDecay", "Amp Decay", juce::NormalisableRange<float>(0.005f, 4.0f, 0.001f, 0.45f), 0.050f);
    sustainParam = new juce::AudioParameterFloat("ampSustain", "Amp Sustain", juce::NormalisableRange<float>(0.0f, 1.0f), 0.8f);
    releaseParam = new juce::AudioParameterFloat("ampRelease", "Amp Release", juce::NormalisableRange<float>(0.010f, 5.0f, 0.001f, 0.45f), 0.100f);
    masterGainParam = new juce::AudioParameterFloat("masterGain", "Master Gain", juce::NormalisableRange<float>(0.0f, 1.0f), 0.6f);

    vibeAmountParam = new juce::AudioParameterFloat("vibeAmount", "Vibe", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    vibeEnabledParam = new juce::AudioParameterBool("vibeEnabled", "Vibe Enabled", true);
    vibeTypeParam = new juce::AudioParameterChoice("vibeType",
                                                    "Vibe Type",
                                                    kVibeTypeChoices,
                                                    0);
    delayAmountParam = new juce::AudioParameterFloat("delayAmount", "Delay Amount", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    granularSyncDivisionParam = new juce::AudioParameterChoice("granularSyncDivision",
                                                                "Granular Sync",
                                                                juce::StringArray { "Free", "1 Bar", "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T" },
                                                                0);
    granularModeParam = new juce::AudioParameterChoice("granularMode",
                                                        "Granular Mode",
                                                        juce::StringArray { "CLASSIC", "CLOUD", "SHIMMER", "RHYTHMIC" },
                                                        0);
    delayAlgorithmParam = new juce::AudioParameterChoice("delayAlgorithm",
                                                          "Delay Algorithm",
                                                          juce::StringArray { "Granular", "Tape", "Analog/BBD", "Ping-Pong", "Stereo", "Modulated", "Diffusion" },
                                                          0);
    delayEnabledParam = new juce::AudioParameterBool("delayEnabled", "Delay Enabled", true);
    delayTimeParam = new juce::AudioParameterFloat("delayTime", "Delay Time", juce::NormalisableRange<float>(0.0f, 1.0f), 0.35f);
    delayFeedbackParam = new juce::AudioParameterFloat("delayFeedback", "Delay Feedback", juce::NormalisableRange<float>(0.0f, 1.0f), 0.38f);
    reverbAmountParam = new juce::AudioParameterFloat("reverbAmount", "Reverb", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    reverbEnabledParam = new juce::AudioParameterBool("reverbEnabled", "Reverb Enabled", true);
    reverbAlgorithmParam = new juce::AudioParameterChoice("reverbAlgorithm",
                                                           "Reverb Mode",
                                                           juce::StringArray { "ROOM", "PLATE", "HALL", "CLOUD" },
                                                           0);
    reverbSizeParam = new juce::AudioParameterFloat("reverbSize", "Reverb Size", juce::NormalisableRange<float>(0.0f, 1.0f), 0.52f);
    reverbDecayParam = new juce::AudioParameterFloat("reverbDecay", "Reverb Decay", juce::NormalisableRange<float>(0.0f, 1.0f), 0.48f);
    reverbDampingParam = new juce::AudioParameterFloat("reverbDamping", "Reverb Damping", juce::NormalisableRange<float>(0.0f, 1.0f), 0.46f);
    reverbPreDelayParam = new juce::AudioParameterFloat("reverbPreDelay", "Reverb PreDelay", juce::NormalisableRange<float>(0.0f, 1.0f), 0.08f);
    reverbModDepthParam = new juce::AudioParameterFloat("reverbModDepth", "Reverb Mod Depth", juce::NormalisableRange<float>(0.0f, 1.0f), 0.24f);
    reverbModRateParam = new juce::AudioParameterFloat("reverbModRate", "Reverb Mod Rate", juce::NormalisableRange<float>(0.0f, 1.0f), 0.18f);
    reverbWidthParam = new juce::AudioParameterFloat("reverbWidth", "Reverb Width", juce::NormalisableRange<float>(0.0f, 1.0f), 0.86f);
    reverbCloudFeedbackParam = new juce::AudioParameterFloat("reverbCloudFeedback", "Reverb Cloud Feedback", juce::NormalisableRange<float>(0.0f, 1.0f), 0.62f);
    reverbCloudDiffusionParam = new juce::AudioParameterFloat("reverbCloudDiffusion", "Reverb Cloud Diffusion", juce::NormalisableRange<float>(0.0f, 1.0f), 0.54f);
    pitchBendRangeParam = new juce::AudioParameterInt("pitchBendRange",
                                                       "Pitch Bend Range",
                                                       1,
                                                       24,
                                                       2);
    lfoFrequencyParam = new juce::AudioParameterFloat("lfoFrequency",
                                                       "LFO Frequency",
                                                       juce::NormalisableRange<float>(0.01f, 20.0f, 0.0001f, 0.30f),
                                                       1.0f);
    lfoWaveformParam = new juce::AudioParameterChoice("lfoWaveform",
                                                       "LFO Waveform",
                                                       px3::lfoWaveformChoices(),
                                                       0);

    addParameter(oscSineParam);
    addParameter(oscSawParam);
    addParameter(oscSquareParam);
    addParameter(oscModeParam);
    addParameter(oscMacroAParam);
    addParameter(oscMacroBParam);
    addParameter(oscMacroCParam);
    addParameter(oscVowelParam);
    for (auto* harmonicParam : oscHarmonicParams)
    {
        addParameter(harmonicParam);
    }
    addParameter(subOscEnabledParam);
    addParameter(subOscLevelParam);
    addParameter(subOscOctaveParam);
    addParameter(subOscWaveformParam);
    addParameter(filterCutoffParam);
    addParameter(filterResonanceParam);
    addParameter(filterTypeParam);
    addParameter(attackParam);
    addParameter(decayParam);
    addParameter(sustainParam);
    addParameter(releaseParam);
    addParameter(masterGainParam);
    addParameter(vibeAmountParam);
    addParameter(vibeEnabledParam);
    addParameter(vibeTypeParam);
    addParameter(delayAmountParam);
    addParameter(granularSyncDivisionParam);
    addParameter(granularModeParam);
    addParameter(delayAlgorithmParam);
    addParameter(delayEnabledParam);
    addParameter(delayTimeParam);
    addParameter(delayFeedbackParam);
    addParameter(reverbAmountParam);
    addParameter(reverbEnabledParam);
    addParameter(reverbAlgorithmParam);
    addParameter(reverbSizeParam);
    addParameter(reverbDecayParam);
    addParameter(reverbDampingParam);
    addParameter(reverbPreDelayParam);
    addParameter(reverbModDepthParam);
    addParameter(reverbModRateParam);
    addParameter(reverbWidthParam);
    addParameter(reverbCloudFeedbackParam);
    addParameter(reverbCloudDiffusionParam);
    addParameter(pitchBendRangeParam);
    addParameter(lfoFrequencyParam);
    addParameter(lfoWaveformParam);

    buildLfoAssignableTargets();

    const auto initialEnvelope = currentEnvelopeSettings();
    const auto initialFilter = currentFilterSettings();
    const auto initialSubtractive = currentSubtractiveSettings();
    const auto initialSubOsc = currentSubOscillatorSettings();
    const auto initialOscillator = currentOscillatorSettings();

    for (int voice = 0; voice < 16; ++voice)
    {
        auto* synthVoice = new SynthVoice();
        synthVoice->setVoiceIndex(voice);
        synthVoice->setEnvelope(initialEnvelope);
        synthVoice->setFilterSettings(initialFilter);
        synthVoice->setSubtractiveSettings(initialSubtractive);
        synthVoice->setSubOscillatorSettings(initialSubOsc);
        synthVoice->setOscillatorSettings(initialOscillator);
        synth.addVoice(synthVoice);
    }

    synth.addSound(new SynthSound());
    clearAllActiveNotes();

    debugLogEvent("LIFECYCLE", "PROCESSOR_CREATED",
                  "id=" + debugInstanceId + " order=" + debugDescribeOrder(getFxProcessingOrder()));
}

PX3SynthAudioProcessor::~PX3SynthAudioProcessor()
{
    debugLogEvent("LIFECYCLE", "PROCESSOR_DESTROYED",
                  "id=" + debugInstanceId + " order=" + debugDescribeOrder(getFxProcessingOrder()));
}

const juce::String PX3SynthAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PX3SynthAudioProcessor::acceptsMidi() const
{
    return true;
}

bool PX3SynthAudioProcessor::producesMidi() const
{
    return false;
}

bool PX3SynthAudioProcessor::isMidiEffect() const
{
    return false;
}

double PX3SynthAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int PX3SynthAudioProcessor::getNumPrograms()
{
    return 1;
}

int PX3SynthAudioProcessor::getCurrentProgram()
{
    return 0;
}

void PX3SynthAudioProcessor::setCurrentProgram(int)
{
}

const juce::String PX3SynthAudioProcessor::getProgramName(int)
{
    return {};
}

void PX3SynthAudioProcessor::changeProgramName(int, const juce::String&)
{
}

void PX3SynthAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate(sampleRate);
    lfoGenerator.prepare(sampleRate);
    lfoGenerator.setSettings(currentLfoSettings());
    vibeComponent.prepare(sampleRate, synth.getNumVoices(), vibeComponent.getSeed());
    delayComponent.prepare(sampleRate);
    reverbComponent.prepare(sampleRate);

    const auto envelope = currentEnvelopeSettings();
    const auto filter = currentFilterSettings();
    const auto subtractive = currentSubtractiveSettings();
    const auto subOsc = currentSubOscillatorSettings();
    const auto oscillator = currentOscillatorSettings();

    for (int voiceIndex = 0; voiceIndex < synth.getNumVoices(); ++voiceIndex)
    {
        if (auto* voice = dynamic_cast<SynthVoice*>(synth.getVoice(voiceIndex)))
        {
            voice->setEnvelope(envelope);
            voice->setFilterSettings(filter);
            voice->setSubtractiveSettings(subtractive);
            voice->setSubOscillatorSettings(subOsc);
            voice->setOscillatorSettings(oscillator);
        }
    }
    juce::ignoreUnused(samplesPerBlock);
}

void PX3SynthAudioProcessor::releaseResources()
{
}

bool PX3SynthAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
           || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

float PX3SynthAudioProcessor::currentLfoSignalForBlock(int numSamples)
{
    // Keep one global LFO instance evaluated once per block. The generator is
    // generic and destination-agnostic; routing is handled elsewhere.
    lfoGenerator.setSettings(currentLfoSettings());
    const auto signal = lfoGenerator.getMidpointSignalAndAdvance(numSamples);

    lfoPhaseForDebug.store(lfoGenerator.getPhaseRadians(), std::memory_order_relaxed);
    lfoCurrentValue.store(signal, std::memory_order_relaxed);
    return signal;
}

void PX3SynthAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
    {
        buffer.clear(channel, 0, buffer.getNumSamples());
    }

    juce::MidiBuffer combinedMidi;
    combinedMidi.addEvents(midiMessages, 0, buffer.getNumSamples(), 0);

    {
        // Virtual keyboard events are produced on the message thread. Lock scope
        // is kept minimal so the audio thread only copies and clears queued MIDI.
        const juce::ScopedLock lock(virtualMidiLock);
        combinedMidi.addEvents(virtualMidiMessages, 0, -1, 0);
        virtualMidiMessages.clear();
    }

    midiMessages.swapWith(combinedMidi);
    updateActiveNotesFromMidi(midiMessages);

    const auto pitchBend = juce::jlimit(-1.0f, 1.0f, pitchBendNormalized.load(std::memory_order_relaxed));
    const auto modWheel = juce::jlimit(0.0f, 1.0f, modWheelNormalized.load(std::memory_order_relaxed));
    const auto bendRange = static_cast<float>(pitchBendRangeParam->get());
    constexpr float vibratoRateHz = 5.0f;
    constexpr float vibratoMaxDepthSemitones = 1.0f;

    const auto blockLfoSignal = currentLfoSignalForBlock(buffer.getNumSamples());
    const auto lfoAssignedIndex = getLfoAssignmentIndex();
    if (lfoAssignedIndex > 0 && lfoAssignedIndex < static_cast<int>(lfoAssignableTargets.size()))
    {
        const auto& target = lfoAssignableTargets[static_cast<std::size_t>(lfoAssignedIndex)];
        if (target.parameter != nullptr)
        {
            float baseNorm = 0.0f;
            float effectiveNorm = 0.0f;
            juce::ignoreUnused(applyLfoToNormalizedValue(target.parameter,
                                                         target.parameter->getValue(),
                                                         blockLfoSignal,
                                                         &baseNorm,
                                                         &effectiveNorm));
            lfoDebugBaseNormalized.store(baseNorm, std::memory_order_relaxed);
            lfoDebugEffectiveNormalized.store(effectiveNorm, std::memory_order_relaxed);
        }
    }
    else
    {
        lfoDebugBaseNormalized.store(0.0f, std::memory_order_relaxed);
        lfoDebugEffectiveNormalized.store(0.0f, std::memory_order_relaxed);
    }

    const auto envelope = currentEnvelopeSettings();
    const auto filter = currentFilterSettings();
    const auto subtractive = currentSubtractiveSettings();
    const auto subOsc = currentSubOscillatorSettings();
    const auto oscillator = currentOscillatorSettings();
    vibeComponent.updateForBlock(currentVibeSettings(), buffer.getNumSamples());
    const auto vibeShared = vibeComponent.getSharedState();
    const auto vibeTuning = vibeComponent.getTuning();
    const auto vibeBypass = vibeComponent.isBypassed();
    const auto vibeAmount = vibeBypass ? 0.0f : vibeComponent.getEffectiveAmount();
    for (int voiceIndex = 0; voiceIndex < synth.getNumVoices(); ++voiceIndex)
    {
        if (auto* voice = dynamic_cast<SynthVoice*>(synth.getVoice(voiceIndex)))
        {
            voice->setEnvelope(envelope);
            voice->setFilterSettings(filter);
            voice->setSubtractiveSettings(subtractive);
            voice->setSubOscillatorSettings(subOsc);
            voice->setOscillatorSettings(oscillator);
            voice->setPerformanceModulation(pitchBend,
                                            modWheel,
                                            bendRange,
                                            vibratoPhaseRadians,
                                            vibratoRateHz,
                                            vibratoMaxDepthSemitones);
            const auto voiceVariation = vibeComponent.getVoiceVariation(voiceIndex);
            voice->setVibeState(vibeAmount, vibeBypass, vibeShared, voiceVariation, vibeTuning);
        }
    }

    synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());
    updateTransportState();

    delayComponent.updateForBlock(currentDelaySettings());
    reverbComponent.updateForBlock(currentReverbSettings(), buffer.getNumSamples());
    const auto fxOrder = getFxProcessingOrder();

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
        auto stageL = buffer.getSample(0, sample);
        auto stageR = (buffer.getNumChannels() > 1) ? buffer.getSample(1, sample) : stageL;

        for (const auto stage : fxOrder)
        {
            switch (stage)
            {
                case 0: // VIBE (distributed in voice stage)
                    break;

                case 1: // Delay
                    delayComponent.processSampleFrame(stageL, stageR, stageL, stageR);
                    break;

                case 2: // Reverb
                    reverbComponent.processSampleFrame(stageL, stageR, stageL, stageR);
                    break;

                default:
                    break;
            }
        }

        buffer.setSample(0, sample, stageL);
        if (buffer.getNumChannels() > 1)
        {
            buffer.setSample(1, sample, stageR);
        }
    }
    reverbComponent.applyPostBlockCompensation(buffer);
}

bool PX3SynthAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* PX3SynthAudioProcessor::createEditor()
{
    debugLogEvent("LIFECYCLE", "CREATE_EDITOR", "order=" + debugDescribeOrder(getFxProcessingOrder()));
    return new PX3SynthAudioProcessorEditor(*this);
}

