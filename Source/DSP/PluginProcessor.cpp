#include "PluginProcessor.h"
#include "PluginProcessorInternals.h"
#include "OscillatorMode.h"

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
    filterCutoffParam = new juce::AudioParameterFloat("filterCutoff", "Filter Cutoff", juce::NormalisableRange<float>(80.0f, 18000.0f, 1.0f, 0.35f), 12000.0f);
    filterResonanceParam = new juce::AudioParameterFloat("filterResonance", "Filter Resonance", juce::NormalisableRange<float>(0.25f, 2.2f), 0.8f);
     filterTypeParam = new juce::AudioParameterChoice("filterType",
                                                                        "Filter Type",
                                                                        juce::StringArray { "LP12", "LP24", "HP12", "HP24", "BandPass", "Notch", "AllPass" },
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
    sourceEngineParam = new juce::AudioParameterChoice("sourceEngine",
                                                        "Source Engine",
                                                        juce::StringArray { "Image", "Audio" },
                                                        0);
    imagePositionParam = new juce::AudioParameterFloat("imagePosition", "Image Position", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f);
    imageAnimateParam = new juce::AudioParameterFloat("imageAnimate", "Image Animate", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    imageRateParam = new juce::AudioParameterFloat("imageRate", "Image Rate", juce::NormalisableRange<float>(0.01f, 4.0f, 0.001f, 0.32f), 0.2f);
    imageAnimModeParam = new juce::AudioParameterChoice("imageAnimMode", "Image Animation Mode", juce::StringArray { "Forward", "Reverse", "Ping Pong" }, 2);
    imageAnimSyncParam = new juce::AudioParameterChoice("imageAnimSync",
                                                         "Image Animation Sync",
                                                         juce::StringArray { "Free", "1 Bar", "1/2", "1/4", "1/8", "1/16" },
                                                         0);
    imageTargetParam = new juce::AudioParameterChoice("imageTarget",
                                                       "Image Target",
                                                       juce::StringArray { "Vibe", "Delay", "Reverb" },
                                                       0);
    audioPositionParam = new juce::AudioParameterFloat("audioPosition", "Audio Position", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f);
    audioGrainParam = new juce::AudioParameterFloat("audioGrain", "Audio Grain", juce::NormalisableRange<float>(0.0f, 1.0f), 0.45f);
    audioTextureParam = new juce::AudioParameterFloat("audioTexture", "Audio Texture", juce::NormalisableRange<float>(0.0f, 1.0f), 0.35f);
    audioAnimateParam = new juce::AudioParameterFloat("audioAnimate", "Audio Animate", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    audioRateParam = new juce::AudioParameterFloat("audioRate", "Audio Rate", juce::NormalisableRange<float>(0.01f, 4.0f, 0.001f, 0.32f), 0.22f);
    audioAnimModeParam = new juce::AudioParameterChoice("audioAnimMode",
                                                         "Audio Animation Mode",
                                                         juce::StringArray { "Forward", "Reverse", "Ping Pong" },
                                                         2);
    audioAnimSyncParam = new juce::AudioParameterChoice("audioAnimSync",
                                                         "Audio Animation Sync",
                                                         juce::StringArray { "Free", "1 Bar", "1/2", "1/4", "1/8", "1/16" },
                                                         0);
    audioTargetParam = new juce::AudioParameterChoice("audioTarget",
                                                       "Audio Target",
                                                       juce::StringArray { "Vibe", "Delay", "Reverb" },
                                                       1);
    pitchBendRangeParam = new juce::AudioParameterInt("pitchBendRange",
                                                       "Pitch Bend Range",
                                                       1,
                                                       24,
                                                       2);
    lfoFrequencyParam = new juce::AudioParameterFloat("lfoFrequency",
                                                       "LFO Frequency",
                                                       juce::NormalisableRange<float>(0.01f, 20.0f, 0.0001f, 0.30f),
                                                       1.0f);

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
    addParameter(sourceEngineParam);
    addParameter(imagePositionParam);
    addParameter(imageAnimateParam);
    addParameter(imageRateParam);
    addParameter(imageAnimModeParam);
    addParameter(imageAnimSyncParam);
    addParameter(imageTargetParam);
    addParameter(audioPositionParam);
    addParameter(audioGrainParam);
    addParameter(audioTextureParam);
    addParameter(audioAnimateParam);
    addParameter(audioRateParam);
    addParameter(audioAnimModeParam);
    addParameter(audioAnimSyncParam);
    addParameter(audioTargetParam);
    addParameter(pitchBendRangeParam);
    addParameter(lfoFrequencyParam);

    buildLfoAssignableTargets();

    const auto initialEnvelope = currentEnvelopeSettings();
    const auto initialSubtractive = currentSubtractiveSettings();
    const auto initialOscillator = currentOscillatorSettings();

    for (int voice = 0; voice < 16; ++voice)
    {
        auto* synthVoice = new SynthVoice();
        synthVoice->setVoiceIndex(voice);
        synthVoice->setEnvelope(initialEnvelope);
        synthVoice->setSubtractiveSettings(initialSubtractive);
        synthVoice->setOscillatorSettings(initialOscillator);
        synth.addVoice(synthVoice);
    }

    synth.addSound(new SynthSound());
    clearAllActiveNotes();
    applyVibeTypeProfile(vibeTypeParam->getIndex());

    auto initialTable = createDefaultImageWavetable();
    if (initialTable != nullptr)
    {
        installImageWavetable(std::move(initialTable), juce::Image());
    }

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
    const auto sr = static_cast<float>(juce::jmax(1.0, sampleRate));
    constexpr float delayControlTauSec = 0.008f;
    constexpr float reverbAmountTauSec = 0.020f;
    delayControlSmoothingCoeff = 1.0f - std::exp(-1.0f / (sr * delayControlTauSec));
    reverbAmountSmoothingCoeff = 1.0f - std::exp(-1.0f / (sr * reverbAmountTauSec));
    delayAmountSmoothed = clamp01(delayAmountParam->get());
    delayTimeControlSmoothed = clamp01(delayTimeParam->get());
    delayFeedbackControlSmoothed = clamp01(delayFeedbackParam->get());
    reverbAmountSmoothed = reverbEnabledParam->get() ? clamp01(reverbAmountParam->get()) : 0.0f;
    vibeEngine.prepare(sampleRate, synth.getNumVoices(), vibeSeed.load(std::memory_order_relaxed));
    vibeLastAppliedSeed.store(vibeSeed.load(std::memory_order_relaxed), std::memory_order_relaxed);
    applyVibeTypeProfile(vibeTypeParam->getIndex());

    const auto envelope = currentEnvelopeSettings();
    const auto subtractive = currentSubtractiveSettings();
    const auto oscillator = currentOscillatorSettings();

    for (int voiceIndex = 0; voiceIndex < synth.getNumVoices(); ++voiceIndex)
    {
        if (auto* voice = dynamic_cast<SynthVoice*>(synth.getVoice(voiceIndex)))
        {
            voice->setEnvelope(envelope);
            voice->setSubtractiveSettings(subtractive);
            voice->setOscillatorSettings(oscillator);
        }
    }

    prepareReverbEngine(sampleRate);

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
    // ONE LFO architecture: evaluate once per block (lightweight) and use the
    // midpoint phase so block-rate modulation feels stable during automation.
    const auto frequencyHz = juce::jlimit(0.01f, 20.0f, lfoFrequencyParam->get());
    const auto startPhase = lfoPhaseRadians;
    const auto samples = juce::jmax(1, numSamples);
    const auto halfBlockAdvance = juce::MathConstants<float>::twoPi * frequencyHz
                                  * (static_cast<float>(samples) * 0.5f / static_cast<float>(juce::jmax(1.0, currentSampleRateHz)));
    const auto signal = std::sin(startPhase + halfBlockAdvance);

    const auto fullAdvance = juce::MathConstants<float>::twoPi * frequencyHz
                             * (static_cast<float>(samples) / static_cast<float>(juce::jmax(1.0, currentSampleRateHz)));
    lfoPhaseRadians += fullAdvance;
    while (lfoPhaseRadians >= juce::MathConstants<float>::twoPi)
    {
        lfoPhaseRadians -= juce::MathConstants<float>::twoPi;
    }

    lfoPhaseForDebug.store(lfoPhaseRadians, std::memory_order_relaxed);
    lfoCurrentValue.store(signal, std::memory_order_relaxed);
    return signal;
}

int PX3SynthAudioProcessor::sanitizeVibeTypeIndex(int typeIndex) const
{
    return juce::jlimit(0, juce::jmax(0, vibeTypeParam->choices.size() - 1), typeIndex);
}

void PX3SynthAudioProcessor::applyVibeTypeProfile(int typeIndex)
{
    const auto clamped = sanitizeVibeTypeIndex(typeIndex);
    if (clamped == vibeTypeLastApplied.load(std::memory_order_relaxed))
    {
        return;
    }

    struct Profile
    {
        float oscillatorDrift;
        float voiceVariation;
        float filterVariation;
        float saturation;
        float noise;
        float psuMovement;
        float vcaNonlinearity;
        float waveformAsymmetry;
        float temperatureDrift;
        float correlatedChaos;
    };

    static const std::array<Profile, 6> profiles {
        {
            // Warm: balanced analog movement and harmonic softening.
            { 0.55f, 0.55f, 0.45f, 0.40f, 0.25f, 0.38f, 0.42f, 0.32f, 0.40f, 0.50f },
            // Hot: stronger drive/nonlinearity with faster-feeling motion.
            { 0.62f, 0.66f, 0.56f, 0.74f, 0.36f, 0.60f, 0.78f, 0.68f, 0.48f, 0.72f },
            // Cool: cleaner, lower saturation/noise with restrained instability.
            { 0.35f, 0.34f, 0.30f, 0.18f, 0.08f, 0.22f, 0.18f, 0.14f, 0.24f, 0.20f },
            // Vintage: larger drift/PSU movement and added noise.
            { 0.74f, 0.72f, 0.52f, 0.48f, 0.52f, 0.72f, 0.44f, 0.40f, 0.76f, 0.64f },
            // Clean: minimal imperfections with slight organic motion.
            { 0.16f, 0.14f, 0.12f, 0.08f, 0.03f, 0.10f, 0.08f, 0.06f, 0.10f, 0.08f },
            // LoFi: noisy, unstable and asymmetrical by design.
            { 0.68f, 0.82f, 0.62f, 0.56f, 0.84f, 0.70f, 0.52f, 0.74f, 0.58f, 0.78f }
        }
    };

    const auto& p = profiles[static_cast<std::size_t>(clamped)];
    vibeTuneOscDrift.store(p.oscillatorDrift, std::memory_order_relaxed);
    vibeTuneVoiceVar.store(p.voiceVariation, std::memory_order_relaxed);
    vibeTuneFilterVar.store(p.filterVariation, std::memory_order_relaxed);
    vibeTuneSaturation.store(p.saturation, std::memory_order_relaxed);
    vibeTuneNoise.store(p.noise, std::memory_order_relaxed);
    vibeTunePsu.store(p.psuMovement, std::memory_order_relaxed);
    vibeTuneVca.store(p.vcaNonlinearity, std::memory_order_relaxed);
    vibeTuneAsym.store(p.waveformAsymmetry, std::memory_order_relaxed);
    vibeTuneTemp.store(p.temperatureDrift, std::memory_order_relaxed);
    vibeTuneChaos.store(p.correlatedChaos, std::memory_order_relaxed);
    vibeTypeLastApplied.store(clamped, std::memory_order_relaxed);
}

void PX3SynthAudioProcessor::updateVibeStateForBlock(int numSamples, float lfoSignal)
{
    const auto seed = vibeSeed.load(std::memory_order_relaxed);
    if (seed != vibeLastAppliedSeed.load(std::memory_order_relaxed))
    {
        vibeEngine.setSeed(seed);
        vibeLastAppliedSeed.store(seed, std::memory_order_relaxed);
    }

    VibeEngine::Tuning t;
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

    vibeEngine.setTuning(t);
    vibeEngine.setBypass(debugGetVibeBypass());
    const auto globalAmount = applyLfoToNormalizedValue(vibeAmountParam,
                                                         static_cast<juce::RangedAudioParameter*>(vibeAmountParam)->getValue(),
                                                         lfoSignal);
    vibeEngine.setGlobalAmount(juce::jlimit(0.0f, 1.0f, globalAmount));
    vibeEngine.advance(numSamples);
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
    const auto vibeEnabled = vibeEnabledParam->get();
    constexpr float vibratoRateHz = 5.0f;
    constexpr float vibratoMaxDepthSemitones = 1.0f;

    applyVibeTypeProfile(vibeTypeParam->getIndex());
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
    const auto subtractive = currentSubtractiveSettings();
    const auto oscillator = currentOscillatorSettings();
    updateVibeStateForBlock(buffer.getNumSamples(), blockLfoSignal);
    const auto vibeShared = vibeEngine.getSharedState();
    const auto vibeTuning = debugGetVibeTuning();
    const auto vibeBypass = debugGetVibeBypass();
    const auto vibeAmount = vibeBypass ? 0.0f : vibeEngine.getEffectiveAmount();
    const auto currentImagePosition = updateImageAnimationPosition(buffer.getNumSamples());
    const auto currentAudioPosition = updateAudioAnimationPosition(buffer.getNumSamples());
    const auto wavetableForBlock = std::atomic_load(&activeImageWavetable);
    const auto audioSourceForBlock = std::atomic_load(&activeAudioSource);
    const auto requestedSourceMode = sourceEngineParam->getIndex() == 1 ? ExternalSourceMode::audio : ExternalSourceMode::image;
    const auto wavetableModeActive = oscillator.modeIndex == 8;
    // WAVETABLE mode reserves the image engine for oscillator generation only.
    const auto sourceMode = wavetableModeActive ? ExternalSourceMode::image : requestedSourceMode;

    AudioGranularSettings granularSettings;
    granularSettings.enabled = sourceMode == ExternalSourceMode::audio;
    granularSettings.position = currentAudioPosition;
    granularSettings.grainSize = juce::jlimit(0.0f, 1.0f, audioGrainParam->get());
    granularSettings.texture = juce::jlimit(0.0f, 1.0f, audioTextureParam->get());
    granularSettings.rootMidiNote = 60;

    for (int voiceIndex = 0; voiceIndex < synth.getNumVoices(); ++voiceIndex)
    {
        if (auto* voice = dynamic_cast<SynthVoice*>(synth.getVoice(voiceIndex)))
        {
            voice->setEnvelope(envelope);
            voice->setSubtractiveSettings(subtractive);
            voice->setOscillatorSettings(oscillator);
            voice->setPerformanceModulation(pitchBend,
                                            modWheel,
                                            bendRange,
                                            vibratoPhaseRadians,
                                            vibratoRateHz,
                                            vibratoMaxDepthSemitones);
            const auto vv = vibeEngine.getVoiceVariation(voiceIndex);
            VibeVoiceVariation voiceVariation;
            voiceVariation.pitchCents = vv.pitchCents;
            voiceVariation.cutoffOffset = vv.cutoffOffset;
            voiceVariation.resonanceOffset = vv.resonanceOffset;
            voiceVariation.gainOffset = vv.gainOffset;
            voiceVariation.asymmetryBias = vv.asymmetryBias;
            voiceVariation.saturationBias = vv.saturationBias;
            VibeSharedState voiceShared;
            voiceShared.oscillatorDrift = vibeShared.oscillatorDrift;
            voiceShared.psu = vibeShared.psu;
            voiceShared.temperature = vibeShared.temperature;
            voiceShared.chaos = vibeShared.chaos;
            voice->setVibeState(vibeAmount, vibeBypass, voiceShared, voiceVariation, vibeTuning);
            voice->setExternalSourceMode(sourceMode);
            voice->setImageWavetable(wavetableForBlock, currentImagePosition);
            voice->setAudioGranularSource(audioSourceForBlock, granularSettings);
        }
    }

    synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());
    updateTransportState();

    const auto vibeAmountBase = applyLfoToNormalizedValue(vibeAmountParam,
                                                          static_cast<juce::RangedAudioParameter*>(vibeAmountParam)->getValue(),
                                                          blockLfoSignal);
    const auto delayAmountBase = applyLfoToNormalizedValue(delayAmountParam,
                                                            static_cast<juce::RangedAudioParameter*>(delayAmountParam)->getValue(),
                                                            blockLfoSignal);
    const auto syncDivisionIndex = granularSyncDivisionParam->getIndex();
    const auto granularModeIndex = granularModeParam->getIndex();
    const auto delayAlgorithmIndex = delayAlgorithmParam->getIndex();
    const auto delayEnabled = delayEnabledParam->get();
    const auto delayTimeControl = applyLfoToNormalizedValue(delayTimeParam,
                                                            static_cast<juce::RangedAudioParameter*>(delayTimeParam)->getValue(),
                                                            blockLfoSignal);
    const auto delayFeedbackControl = applyLfoToNormalizedValue(delayFeedbackParam,
                                                                static_cast<juce::RangedAudioParameter*>(delayFeedbackParam)->getValue(),
                                                                blockLfoSignal);
    const auto reverbAmountBase = applyLfoToNormalizedValue(reverbAmountParam,
                                                            static_cast<juce::RangedAudioParameter*>(reverbAmountParam)->getValue(),
                                                            blockLfoSignal);
    const auto reverbEnabled = reverbEnabledParam->get();
    const auto fxOrder = getFxProcessingOrder();

    if (delayAlgorithmIndex != lastDelayAlgorithmIndex
        || granularModeIndex != lastGranularModeIndex)
    {
        lastDelayAlgorithmIndex = delayAlgorithmIndex;
        lastGranularModeIndex = granularModeIndex;
        isaacSpawnCounter = 0;
        isaacRhythmicStepIndex = 0;
        isaacRhythmicSamplesUntilNext = 0;
        isaacRhythmicSwingToggle = false;
        isaacFeedbackFilter = { { 0.0f, 0.0f } };
        isaacShimmerSmooth = { { 0.0f, 0.0f } };
        clearGranularDiffusionState();
        for (auto& grain : isaacGrains)
        {
            grain.active = false;
        }
    }
    const auto reverbAlgorithmIndex = reverbAlgorithmParam->getIndex();
    const auto imageTargetIndex = imageTargetParam->getIndex();
    const auto audioTargetIndex = audioTargetParam->getIndex();

    const auto imageControlRaw = computeImageTargetControlSignal(currentImagePosition, buffer.getNumSamples());
    auto audioControlRaw = juce::jlimit(0.0f, 1.0f, currentAudioPosition);
    const auto audioTexture = applyLfoToNormalizedValue(audioTextureParam,
                                                        static_cast<juce::RangedAudioParameter*>(audioTextureParam)->getValue(),
                                                        blockLfoSignal);
    const auto audioGrain = applyLfoToNormalizedValue(audioGrainParam,
                                                      static_cast<juce::RangedAudioParameter*>(audioGrainParam)->getValue(),
                                                      blockLfoSignal);
    const auto audioAnimate = applyLfoToNormalizedValue(audioAnimateParam,
                                                        static_cast<juce::RangedAudioParameter*>(audioAnimateParam)->getValue(),
                                                        blockLfoSignal);
    const auto imageAnimate = applyLfoToNormalizedValue(imageAnimateParam,
                                                        static_cast<juce::RangedAudioParameter*>(imageAnimateParam)->getValue(),
                                                        blockLfoSignal);
    if (audioSourceForBlock != nullptr && !audioSourceForBlock->waveformPreview.empty())
    {
        const auto& preview = audioSourceForBlock->waveformPreview;
        const auto previewPos = juce::jlimit(0.0f,
                                             1.0f,
                                             currentAudioPosition + (audioTexture - 0.5f) * 0.10f);
        const auto sampleIndex = juce::jlimit(0,
                                              static_cast<int>(preview.size()) - 1,
                                              static_cast<int>(std::lround(previewPos * static_cast<float>(preview.size() - 1))));
        audioControlRaw = juce::jlimit(0.0f, 1.0f, preview[static_cast<std::size_t>(sampleIndex)]);
    }

    const auto controlSmoothingSec = 0.06f;
    const auto controlBlend = 1.0f - std::exp(-static_cast<float>(buffer.getNumSamples())
                                              / (static_cast<float>(juce::jmax(1.0, currentSampleRateHz)) * controlSmoothingSec));
    imageTargetControlSmoothed += (imageControlRaw - imageTargetControlSmoothed) * controlBlend;

    // Add stepped/chopped modulation to create stronger granular-like throwing into assigned targets.
    const auto imageQuantSteps = 5 + static_cast<int>(std::lround(imageAnimate * 20.0f));
    const auto imageQuantized = std::floor(imageTargetControlSmoothed * static_cast<float>(imageQuantSteps - 1))
                              / static_cast<float>(juce::jmax(1, imageQuantSteps - 1));
    const auto imageBitMix = juce::jlimit(0.0f, 0.94f, 0.32f + imageAnimate * 0.62f);
    const auto imagePulseHz = 3.0f + imageAnimate * 18.0f;
    const auto imagePulse = 0.5f + 0.5f * std::sin(static_cast<float>(currentTimelineSeconds * static_cast<double>(imagePulseHz))
                                                   * juce::MathConstants<float>::twoPi);
    const auto imageBurst = imagePulse > 0.80f ? 1.0f : 0.0f;
    const auto imageGrainyControl = juce::jlimit(0.0f,
                                                 1.0f,
                                                 lerp(imageTargetControlSmoothed, imageQuantized, imageBitMix)
                                                     * (0.62f + 0.60f * imagePulse)
                                                     + imageBurst * (0.12f + 0.18f * imageAnimate));

    const auto audioQuantSteps = 4 + static_cast<int>(std::lround(audioGrain * 22.0f));
    const auto audioQuantized = std::floor(audioControlRaw * static_cast<float>(audioQuantSteps - 1))
                              / static_cast<float>(juce::jmax(1, audioQuantSteps - 1));
    const auto chopperHz = 5.6f + audioTexture * 19.0f + audioAnimate * 7.0f;
    const auto chopperPhase = static_cast<float>(currentTimelineSeconds * static_cast<double>(chopperHz))
                            + audioControlRaw * 13.0f;
    const auto chopper = 0.5f + 0.5f * std::sin(chopperPhase * juce::MathConstants<float>::twoPi);
    const auto chopperShaped = std::pow(chopper, 4.6f - 3.2f * audioGrain);
    const auto audioBurstPhase = static_cast<float>(currentTimelineSeconds * static_cast<double>(2.5f + audioTexture * 8.0f));
    const auto audioBurst = (std::sin(audioBurstPhase * juce::MathConstants<float>::twoPi + audioControlRaw * 9.0f) > 0.83f) ? 1.0f : 0.0f;
    const auto audioControlSmoothed = juce::jlimit(0.0f,
                                                   1.0f,
                                                   audioQuantized * (0.38f + 0.96f * chopperShaped)
                                                       + audioBurst * (0.20f + 0.30f * audioAnimate));

    const auto imageScale = lerp(0.06f, 2.58f, imageGrainyControl);
    const auto audioScale = lerp(0.04f, 2.82f, audioControlSmoothed);

    float driveScaleTarget = 1.0f;
    float granularScaleTarget = 1.0f;
    float reverbScaleTarget = 1.0f;

    if (!wavetableModeActive)
    {
        switch (juce::jlimit(0, 2, imageTargetIndex))
        {
            case 0:
                driveScaleTarget = imageScale;
                break;
            case 1:
                granularScaleTarget = imageScale * (1.0f + 0.38f * imageBurst);
                break;
            case 2:
                reverbScaleTarget = imageScale;
                break;
            default:
                break;
        }
    }

    if (sourceMode == ExternalSourceMode::audio)
    {
        switch (juce::jlimit(0, 2, audioTargetIndex))
        {
            case 0:
                driveScaleTarget *= audioScale;
                break;
            case 1:
                granularScaleTarget *= audioScale * (1.08f + 0.66f * audioBurst);
                break;
            case 2:
                reverbScaleTarget *= audioScale;
                break;
            default:
                break;
        }
    }

    driveScaleTarget = juce::jlimit(0.04f, 2.95f, driveScaleTarget);
    granularScaleTarget = juce::jlimit(0.04f, 3.05f, granularScaleTarget);
    reverbScaleTarget = juce::jlimit(0.04f, 2.72f, reverbScaleTarget);

    const auto routeSmoothingSec = 0.024f;
    const auto routeBlend = 1.0f - std::exp(-static_cast<float>(buffer.getNumSamples())
                                            / (static_cast<float>(juce::jmax(1.0, currentSampleRateHz)) * routeSmoothingSec));
    imageDriveScaleSmoothed += (driveScaleTarget - imageDriveScaleSmoothed) * routeBlend;
    imageGranularScaleSmoothed += (granularScaleTarget - imageGranularScaleSmoothed) * routeBlend;
    imageReverbScaleSmoothed += (reverbScaleTarget - imageReverbScaleSmoothed) * routeBlend;

    const auto vibeAmountRouted = clamp01(vibeAmountBase * imageDriveScaleSmoothed);
    const auto delayAmount = clamp01(delayAmountBase * imageGranularScaleSmoothed);
    const auto reverbAmount = clamp01(reverbAmountBase * imageReverbScaleSmoothed);

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

    double reverbPreEnergy = 0.0;
    double reverbPostEnergy = 0.0;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto reverbAmountTarget = reverbEnabled ? reverbAmount : 0.0f;
        const auto reverbSmoothCoeff = juce::jlimit(0.0001f, 1.0f, reverbAmountSmoothingCoeff);
        reverbAmountSmoothed += reverbSmoothCoeff * (reverbAmountTarget - reverbAmountSmoothed);
        const auto reverbAmountForSample = clamp01(reverbAmountSmoothed);

        auto stageL = buffer.getSample(0, sample);
        auto stageR = (buffer.getNumChannels() > 1) ? buffer.getSample(1, sample) : stageL;

        for (const auto stage : fxOrder)
        {
            switch (stage)
            {
                case 0: // VIBE (distributed in voice stage)
                    juce::ignoreUnused(vibeEnabled, vibeAmountRouted);
                    break;

                case 1: // Delay
                    if (delayEnabled)
                    {
                        float delayedL = stageL;
                        float delayedR = stageR;
                        processDelayAlgorithmSample(stageL,
                                                    stageR,
                                                    delayAmount,
                                                    delayAlgorithmIndex,
                                                    delayTimeControl,
                                                    delayFeedbackControl,
                                                    syncDivisionIndex,
                                                    delayedL,
                                                    delayedR);
                        stageL = delayedL;
                        stageR = delayedR;
                    }
                    break;

                case 2: // Reverb
                    if (reverbAmountForSample > 0.0001f)
                    {
                        const auto reverbInL = stageL;
                        const auto reverbInR = stageR;
                        processReverbSampleFrame(stageL, stageR, reverbAmountForSample, reverbAlgorithmIndex, stageL, stageR);

                        reverbPreEnergy += 0.5 * (static_cast<double>(reverbInL) * static_cast<double>(reverbInL)
                                                  + static_cast<double>(reverbInR) * static_cast<double>(reverbInR));
                        reverbPostEnergy += 0.5 * (static_cast<double>(stageL) * static_cast<double>(stageL)
                                                   + static_cast<double>(stageR) * static_cast<double>(stageR));
                    }
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

    if (reverbAmountSmoothed > 0.0001f && buffer.getNumSamples() > 0)
    {
        const auto invN = 1.0 / static_cast<double>(buffer.getNumSamples());
        const auto preRms = static_cast<float>(std::sqrt(juce::jmax(1.0e-12, reverbPreEnergy * invN)));
        const auto postRms = static_cast<float>(std::sqrt(juce::jmax(1.0e-12, reverbPostEnergy * invN)));
        const auto rawComp = juce::jlimit(0.72f, 1.45f, preRms / juce::jmax(1.0e-5f, postRms));

        const auto compBlend = smoothstep(clamp01(reverbAmountSmoothed));
        const auto targetComp = 1.0f + (rawComp - 1.0f) * compBlend;
        reverbOutputCompGain += 0.03f * (targetComp - reverbOutputCompGain);
    }
    else
    {
        reverbOutputCompGain += 0.02f * (1.0f - reverbOutputCompGain);
    }

    if (std::abs(reverbOutputCompGain - 1.0f) > 0.001f)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            buffer.applyGain(channel, 0, buffer.getNumSamples(), reverbOutputCompGain);
        }
    }
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

