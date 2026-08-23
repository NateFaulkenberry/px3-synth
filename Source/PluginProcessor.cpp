#include "PluginProcessor.h"

#include "PluginEditor.h"
#include <cmath>
#include <memory>

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

class ImageLoadJob final : public juce::ThreadPoolJob
{
public:
    ImageLoadJob(SynthProjectAudioProcessor& ownerIn, juce::File fileIn, int serialIn)
        : juce::ThreadPoolJob("PX3 Image Load"), owner(ownerIn), file(std::move(fileIn)), serial(serialIn)
    {
    }

    JobStatus runJob() override
    {
        if (!file.existsAsFile())
        {
            owner.notifyImageLoadError();
            return jobHasFinished;
        }

        auto input = std::unique_ptr<juce::InputStream>(file.createInputStream());
        if (input == nullptr)
        {
            owner.notifyImageLoadError();
            return jobHasFinished;
        }

        auto image = juce::ImageFileFormat::loadFrom(*input);
        if (!image.isValid() || image.getWidth() <= 0 || image.getHeight() <= 0)
        {
            owner.notifyImageLoadError();
            return jobHasFinished;
        }

        auto wavetable = owner.buildImageWavetableFromImage(image);
        if (wavetable == nullptr)
        {
            owner.notifyImageLoadError();
            return jobHasFinished;
        }

        owner.completeImageLoad(serial, std::move(wavetable), image, file.getFullPathName());

        return jobHasFinished;
    }

private:
    SynthProjectAudioProcessor& owner;
    juce::File file;
    int serial { 0 };
};

class AudioLoadJob final : public juce::ThreadPoolJob
{
public:
    AudioLoadJob(SynthProjectAudioProcessor& ownerIn, juce::File fileIn, int serialIn)
        : juce::ThreadPoolJob("PX3 Audio Load"), owner(ownerIn), file(std::move(fileIn)), serial(serialIn)
    {
    }

    JobStatus runJob() override
    {
        if (!file.existsAsFile())
        {
            owner.notifyAudioLoadError();
            return jobHasFinished;
        }

        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (reader == nullptr)
        {
            owner.notifyAudioLoadError();
            return jobHasFinished;
        }

        const auto lengthInSamples = static_cast<int64_t>(reader->lengthInSamples);
        if (lengthInSamples <= 0)
        {
            owner.notifyAudioLoadError();
            return jobHasFinished;
        }

        constexpr int64_t maxSamples = static_cast<int64_t>(44100 * 240);
        const auto boundedLength = juce::jmin(lengthInSamples, maxSamples);
        const auto numChannels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));

        auto data = std::make_shared<AudioSourceData>();
        data->samples.setSize(numChannels, static_cast<int>(boundedLength));

        if (!reader->read(&data->samples, 0, static_cast<int>(boundedLength), 0, true, numChannels > 1))
        {
            owner.notifyAudioLoadError();
            return jobHasFinished;
        }

        data->sampleRate = reader->sampleRate > 1.0 ? reader->sampleRate : 44100.0;
        data->numChannels = numChannels;
        data->numSamples = static_cast<int>(boundedLength);

        float peak = 0.0f;
        for (int ch = 0; ch < data->numChannels; ++ch)
        {
            const auto* ptr = data->samples.getReadPointer(ch);
            for (int i = 0; i < data->numSamples; ++i)
            {
                peak = juce::jmax(peak, std::abs(ptr[i]));
            }
        }
        data->peak = juce::jmax(0.0001f, peak);

        constexpr int previewPoints = 320;
        data->waveformPreview.assign(previewPoints, 0.0f);
        const auto hop = juce::jmax(1, data->numSamples / previewPoints);
        for (int i = 0; i < previewPoints; ++i)
        {
            const auto start = i * hop;
            const auto end = juce::jmin(data->numSamples, start + hop);
            float acc = 0.0f;
            int count = 0;
            for (int s = start; s < end; ++s)
            {
                float mono = 0.0f;
                for (int ch = 0; ch < data->numChannels; ++ch)
                {
                    mono += data->samples.getSample(ch, s);
                }
                mono /= static_cast<float>(data->numChannels);
                acc += std::abs(mono);
                ++count;
            }

            data->waveformPreview[static_cast<std::size_t>(i)] = count > 0 ? acc / static_cast<float>(count) : 0.0f;
        }

        owner.completeAudioLoad(serial, std::move(data), file.getFullPathName());
        return jobHasFinished;
    }

private:
    SynthProjectAudioProcessor& owner;
    juce::File file;
    int serial { 0 };
};

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
    for (int i = 0; i < 3; ++i)
    {
        fxProcessingOrder[static_cast<std::size_t>(i)].store(i, std::memory_order_relaxed);
    }

    oscSineParam = new juce::AudioParameterFloat("oscSine", "Osc Sine", juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f);
    oscSawParam = new juce::AudioParameterFloat("oscSaw", "Osc Saw", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    oscSquareParam = new juce::AudioParameterFloat("oscSquare", "Osc Square", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    oscModeParam = new juce::AudioParameterChoice("oscMode",
                                                   "Oscillator Mode",
                                                   juce::StringArray { "SINE", "SAW", "SQUARE", "TRIANGLE", "NOISE", "PINK NOISE", "SUPER SAW", "PWM", "WAVETABLE", "ADDITIVE", "FORMANT", "FM", "HARD SYNC", "KARPLUS", "ORGAN", "DIGITAL", "PHYSICAL", "ROB", "ISAAC", "PX3" },
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

    robAmountParam = new juce::AudioParameterFloat("robAmount", "Harmonic Drive", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
    robEnabledParam = new juce::AudioParameterBool("robEnabled", "Harmonic Drive Enabled", true);
    robModeParam = new juce::AudioParameterChoice("robMode",
                                                   "Harmonic Drive Mode",
                                                   juce::StringArray { "Default Drive", "Tape Saturation", "Tube Warmth", "Distortion Pedal" },
                                                   0);
    isaacAmountParam = new juce::AudioParameterFloat("isaacAmount", "Granular Delay", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f);
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
                                                       juce::StringArray { "Harmonic Drive", "Delay", "Reverb" },
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
                                                       juce::StringArray { "Harmonic Drive", "Delay", "Reverb" },
                                                       1);
    pitchBendRangeParam = new juce::AudioParameterInt("pitchBendRange",
                                                       "Pitch Bend Range",
                                                       1,
                                                       24,
                                                       2);
    fxOrderSlot0Param = new juce::AudioParameterInt("fxOrderSlot0", "FX Order Slot 0", 0, 2, 0);
    fxOrderSlot1Param = new juce::AudioParameterInt("fxOrderSlot1", "FX Order Slot 1", 0, 2, 1);
    fxOrderSlot2Param = new juce::AudioParameterInt("fxOrderSlot2", "FX Order Slot 2", 0, 2, 2);

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
    addParameter(robAmountParam);
    addParameter(robEnabledParam);
    addParameter(robModeParam);
    addParameter(isaacAmountParam);
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
    addParameter(fxOrderSlot0Param);
    addParameter(fxOrderSlot1Param);
    addParameter(fxOrderSlot2Param);

    const auto initialEnvelope = currentEnvelopeSettings();
    const auto initialSubtractive = currentSubtractiveSettings();
    const auto initialOscillator = currentOscillatorSettings();

    for (int voice = 0; voice < 16; ++voice)
    {
        auto* synthVoice = new SynthVoice();
        synthVoice->setEnvelope(initialEnvelope);
        synthVoice->setSubtractiveSettings(initialSubtractive);
        synthVoice->setOscillatorSettings(initialOscillator);
        synth.addVoice(synthVoice);
    }

    synth.addSound(new SynthSound());
    clearAllActiveNotes();

    auto initialTable = createDefaultImageWavetable();
    if (initialTable != nullptr)
    {
        installImageWavetable(std::move(initialTable), juce::Image());
    }
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

    juce::MidiBuffer combinedMidi;
    combinedMidi.addEvents(midiMessages, 0, buffer.getNumSamples(), 0);

    {
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

    const auto envelope = currentEnvelopeSettings();
    const auto subtractive = currentSubtractiveSettings();
    const auto oscillator = currentOscillatorSettings();
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
            voice->setExternalSourceMode(sourceMode);
            voice->setImageWavetable(wavetableForBlock, currentImagePosition);
            voice->setAudioGranularSource(audioSourceForBlock, granularSettings);
        }
    }

    synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());
    updateTransportState();

    const auto robAmountBase = clamp01(robAmountParam->get());
    const auto robEnabled = robEnabledParam->get();
    const auto robModeIndex = robModeParam->getIndex();
    const auto isaacAmountBase = clamp01(isaacAmountParam->get());
    const auto syncDivisionIndex = granularSyncDivisionParam->getIndex();
    const auto granularModeIndex = granularModeParam->getIndex();
    const auto delayAlgorithmIndex = delayAlgorithmParam->getIndex();
    const auto delayEnabled = delayEnabledParam->get();
    const auto delayTimeControl = clamp01(delayTimeParam->get());
    const auto delayFeedbackControl = clamp01(delayFeedbackParam->get());
    const auto reverbAmountBase = clamp01(reverbAmountParam->get());
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
    const auto audioTexture = juce::jlimit(0.0f, 1.0f, audioTextureParam->get());
    const auto audioGrain = juce::jlimit(0.0f, 1.0f, audioGrainParam->get());
    const auto audioAnimate = juce::jlimit(0.0f, 1.0f, audioAnimateParam->get());
    const auto imageAnimate = juce::jlimit(0.0f, 1.0f, imageAnimateParam->get());
    if (audioSourceForBlock != nullptr && !audioSourceForBlock->waveformPreview.empty())
    {
        const auto& preview = audioSourceForBlock->waveformPreview;
        const auto previewPos = juce::jlimit(0.0f,
                                             1.0f,
                                             currentAudioPosition + (juce::jlimit(0.0f, 1.0f, audioTextureParam->get()) - 0.5f) * 0.10f);
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

    const auto robAmount = clamp01(robAmountBase * imageDriveScaleSmoothed);
    const auto isaacAmount = clamp01(isaacAmountBase * imageGranularScaleSmoothed);
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
        auto stageL = buffer.getSample(0, sample);
        auto stageR = (buffer.getNumChannels() > 1) ? buffer.getSample(1, sample) : stageL;

        for (const auto stage : fxOrder)
        {
            switch (stage)
            {
                case 0: // Harmonic Drive
                    if (robEnabled)
                    {
                        stageL = processRobSample(stageL, 0, robAmount, robModeIndex);
                        stageR = processRobSample(stageR, 1, robAmount, robModeIndex);
                    }
                    break;

                case 1: // Delay
                    if (delayEnabled)
                    {
                        float delayedL = stageL;
                        float delayedR = stageR;
                        processDelayAlgorithmSample(stageL,
                                                    stageR,
                                                    isaacAmount,
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
                    if (reverbEnabled)
                    {
                        const auto reverbInL = stageL;
                        const auto reverbInR = stageR;
                        processReverbSampleFrame(stageL, stageR, reverbAmount, reverbAlgorithmIndex, stageL, stageR);

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

    if (reverbEnabled && reverbAmount > 0.0001f && buffer.getNumSamples() > 0)
    {
        const auto invN = 1.0 / static_cast<double>(buffer.getNumSamples());
        const auto preRms = static_cast<float>(std::sqrt(juce::jmax(1.0e-12, reverbPreEnergy * invN)));
        const auto postRms = static_cast<float>(std::sqrt(juce::jmax(1.0e-12, reverbPostEnergy * invN)));
        const auto rawComp = juce::jlimit(0.72f, 1.45f, preRms / juce::jmax(1.0e-5f, postRms));

        const auto compBlend = smoothstep(reverbAmount);
        const auto targetComp = 1.0f + (rawComp - 1.0f) * compBlend;
        reverbOutputCompGain += 0.12f * (targetComp - reverbOutputCompGain);
    }
    else
    {
        reverbOutputCompGain += 0.08f * (1.0f - reverbOutputCompGain);
    }

    if (std::abs(reverbOutputCompGain - 1.0f) > 0.001f)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            buffer.applyGain(channel, 0, buffer.getNumSamples(), reverbOutputCompGain);
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

void SynthProjectAudioProcessor::queueVirtualKeyboardNoteOn(int midiNote, float velocityNorm)
{
    const auto boundedNote = juce::jlimit(PianoKeyboard::firstMidiNote, PianoKeyboard::lastMidiNote, midiNote);
    const auto boundedVelocity = juce::jlimit(0.0f, 1.0f, velocityNorm);
    const juce::ScopedLock lock(virtualMidiLock);
    virtualMidiMessages.addEvent(juce::MidiMessage::noteOn(1, boundedNote, boundedVelocity), 0);
}

void SynthProjectAudioProcessor::queueVirtualKeyboardNoteOff(int midiNote)
{
    const auto boundedNote = juce::jlimit(PianoKeyboard::firstMidiNote, PianoKeyboard::lastMidiNote, midiNote);
    const juce::ScopedLock lock(virtualMidiLock);
    virtualMidiMessages.addEvent(juce::MidiMessage::noteOff(1, boundedNote), 0);
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
juce::AudioParameterChoice& SynthProjectAudioProcessor::getOscillatorModeParam() const { return *oscModeParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getOscMacroAParam() const { return *oscMacroAParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getOscMacroBParam() const { return *oscMacroBParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getOscMacroCParam() const { return *oscMacroCParam; }
juce::AudioParameterChoice& SynthProjectAudioProcessor::getOscVowelParam() const { return *oscVowelParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getOscHarmonicParam(int harmonicIndex) const
{
    const auto idx = juce::jlimit(0, 7, harmonicIndex);
    return *oscHarmonicParams[static_cast<std::size_t>(idx)];
}
juce::AudioParameterFloat& SynthProjectAudioProcessor::getFilterCutoffParam() const { return *filterCutoffParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getFilterResonanceParam() const { return *filterResonanceParam; }
juce::AudioParameterChoice& SynthProjectAudioProcessor::getFilterTypeParam() const { return *filterTypeParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getAttackParam() const { return *attackParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getDecayParam() const { return *decayParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getSustainParam() const { return *sustainParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getReleaseParam() const { return *releaseParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getMasterGainParam() const { return *masterGainParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getRobAmountParam() const { return *robAmountParam; }
juce::AudioParameterBool& SynthProjectAudioProcessor::getRobEnabledParam() const { return *robEnabledParam; }
juce::AudioParameterChoice& SynthProjectAudioProcessor::getRobModeParam() const { return *robModeParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getIsaacAmountParam() const { return *isaacAmountParam; }
juce::AudioParameterChoice& SynthProjectAudioProcessor::getGranularSyncDivisionParam() const { return *granularSyncDivisionParam; }
juce::AudioParameterChoice& SynthProjectAudioProcessor::getGranularModeParam() const { return *granularModeParam; }
juce::AudioParameterChoice& SynthProjectAudioProcessor::getDelayAlgorithmParam() const { return *delayAlgorithmParam; }
juce::AudioParameterBool& SynthProjectAudioProcessor::getDelayEnabledParam() const { return *delayEnabledParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getDelayTimeParam() const { return *delayTimeParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getDelayFeedbackParam() const { return *delayFeedbackParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getReverbAmountParam() const { return *reverbAmountParam; }
juce::AudioParameterBool& SynthProjectAudioProcessor::getReverbEnabledParam() const { return *reverbEnabledParam; }
juce::AudioParameterChoice& SynthProjectAudioProcessor::getReverbAlgorithmParam() const { return *reverbAlgorithmParam; }
juce::AudioParameterChoice& SynthProjectAudioProcessor::getSourceEngineParam() const { return *sourceEngineParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getImagePositionParam() const { return *imagePositionParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getImageAnimateParam() const { return *imageAnimateParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getImageRateParam() const { return *imageRateParam; }
juce::AudioParameterChoice& SynthProjectAudioProcessor::getImageAnimModeParam() const { return *imageAnimModeParam; }
juce::AudioParameterChoice& SynthProjectAudioProcessor::getImageAnimSyncParam() const { return *imageAnimSyncParam; }
juce::AudioParameterChoice& SynthProjectAudioProcessor::getImageTargetParam() const { return *imageTargetParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getAudioPositionParam() const { return *audioPositionParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getAudioGrainParam() const { return *audioGrainParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getAudioTextureParam() const { return *audioTextureParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getAudioAnimateParam() const { return *audioAnimateParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getAudioRateParam() const { return *audioRateParam; }
juce::AudioParameterChoice& SynthProjectAudioProcessor::getAudioAnimModeParam() const { return *audioAnimModeParam; }
juce::AudioParameterChoice& SynthProjectAudioProcessor::getAudioAnimSyncParam() const { return *audioAnimSyncParam; }
juce::AudioParameterChoice& SynthProjectAudioProcessor::getAudioTargetParam() const { return *audioTargetParam; }
juce::AudioParameterInt& SynthProjectAudioProcessor::getPitchBendRangeParam() const { return *pitchBendRangeParam; }

std::array<int, 3> SynthProjectAudioProcessor::getFxProcessingOrder() const
{
    if (fxOrderSlot0Param != nullptr && fxOrderSlot1Param != nullptr && fxOrderSlot2Param != nullptr)
    {
        const std::array<int, 3> fromParams {
            { fxOrderSlot0Param->get(), fxOrderSlot1Param->get(), fxOrderSlot2Param->get() }
        };

        std::array<int, 3> sanitizedFromParams { { 0, 1, 2 } };
        std::array<bool, 3> seenFromParams { { false, false, false } };
        int writeParam = 0;
        for (const auto stageIn : fromParams)
        {
            const auto stage = juce::jlimit(0, 2, stageIn);
            if (!seenFromParams[static_cast<std::size_t>(stage)] && writeParam < 3)
            {
                sanitizedFromParams[static_cast<std::size_t>(writeParam++)] = stage;
                seenFromParams[static_cast<std::size_t>(stage)] = true;
            }
        }
        for (int stage = 0; stage < 3 && writeParam < 3; ++stage)
        {
            if (!seenFromParams[static_cast<std::size_t>(stage)])
            {
                sanitizedFromParams[static_cast<std::size_t>(writeParam++)] = stage;
            }
        }

        return sanitizedFromParams;
    }

    std::array<int, 3> sanitized { { 0, 1, 2 } };
    std::array<bool, 3> seen { { false, false, false } };

    int write = 0;
    for (int i = 0; i < 3; ++i)
    {
        const auto stage = juce::jlimit(0,
                                        2,
                                        fxProcessingOrder[static_cast<std::size_t>(i)].load(std::memory_order_relaxed));
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

void SynthProjectAudioProcessor::setFxProcessingOrder(const std::array<int, 3>& order)
{
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

    for (int i = 0; i < 3; ++i)
    {
        fxProcessingOrder[static_cast<std::size_t>(i)].store(sanitized[static_cast<std::size_t>(i)],
                                                             std::memory_order_relaxed);
    }

    if (!updatingFxOrderParams
        && fxOrderSlot0Param != nullptr
        && fxOrderSlot1Param != nullptr
        && fxOrderSlot2Param != nullptr)
    {
        updatingFxOrderParams = true;

        const auto setSlotIfChanged = [this](juce::AudioParameterInt* param, int value)
        {
            if (param == nullptr || param->get() == value)
            {
                return;
            }

            const auto normalized = param->convertTo0to1(static_cast<float>(value));

            int paramIndex = -1;
            const auto& params = getParameters();
            for (std::size_t i = 0; i < params.size(); ++i)
            {
                if (params[i] == param)
                {
                    paramIndex = static_cast<int>(i);
                    break;
                }
            }

            if (paramIndex >= 0)
            {
                beginParameterChangeGesture(paramIndex);
                setParameterNotifyingHost(paramIndex, normalized);
                endParameterChangeGesture(paramIndex);
            }
            else
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost(normalized);
                param->endChangeGesture();
            }
        };

        setSlotIfChanged(fxOrderSlot0Param, sanitized[0]);
        setSlotIfChanged(fxOrderSlot1Param, sanitized[1]);
        setSlotIfChanged(fxOrderSlot2Param, sanitized[2]);

        updatingFxOrderParams = false;
    }

    // Explicitly notify host display/state tracking after order updates.
    // Some hosts are conservative about marking dirty for non-audio UI actions.
    updateHostDisplay();
}

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

void SynthProjectAudioProcessor::requestImageLoadAsync(const juce::File& imageFile)
{
    if (!imageFile.existsAsFile())
    {
        imageLoadErrorFlag.store(true, std::memory_order_relaxed);
        return;
    }

    imageLoadErrorFlag.store(false, std::memory_order_relaxed);
    imageLoadedFromDisk.store(false, std::memory_order_relaxed);

    const auto serial = imageLoadRequestSerial.fetch_add(1, std::memory_order_relaxed) + 1;
    imageLoadThreadPool.removeAllJobs(true, 500);
    imageLoadThreadPool.addJob(new ImageLoadJob(*this, imageFile, serial), true);
}

void SynthProjectAudioProcessor::disableImageEngine()
{
    // In WAVETABLE mode, image is reserved for oscillator generation and cannot be disabled.
    if (oscModeParam != nullptr && oscModeParam->getIndex() == 8)
    {
        return;
    }

    if (sourceEngineParam != nullptr)
    {
        sourceEngineParam->setValueNotifyingHost(sourceEngineParam->convertTo0to1(1.0f));
    }

    if (imageAnimateParam != nullptr)
    {
        imageAnimateParam->setValueNotifyingHost(imageAnimateParam->convertTo0to1(0.0f));
    }
}

void SynthProjectAudioProcessor::resetImageEngine()
{
    if (imagePositionParam != nullptr)
    {
        imagePositionParam->setValueNotifyingHost(imagePositionParam->convertTo0to1(0.5f));
    }
    if (imageAnimateParam != nullptr)
    {
        imageAnimateParam->setValueNotifyingHost(imageAnimateParam->convertTo0to1(0.0f));
    }
    if (imageRateParam != nullptr)
    {
        imageRateParam->setValueNotifyingHost(imageRateParam->convertTo0to1(0.2f));
    }
    if (imageAnimModeParam != nullptr)
    {
        imageAnimModeParam->setValueNotifyingHost(imageAnimModeParam->convertTo0to1(2.0f));
    }
    if (imageAnimSyncParam != nullptr)
    {
        imageAnimSyncParam->setValueNotifyingHost(imageAnimSyncParam->convertTo0to1(0.0f));
    }
    if (imageTargetParam != nullptr)
    {
        imageTargetParam->setValueNotifyingHost(imageTargetParam->convertTo0to1(0.0f));
    }

    if (auto defaultTable = createDefaultImageWavetable())
    {
        installImageWavetable(std::move(defaultTable), juce::Image());
    }

    imageLoadedFromDisk.store(false, std::memory_order_relaxed);
    imageLoadErrorFlag.store(false, std::memory_order_relaxed);

    {
        const std::scoped_lock<std::mutex> lock(imagePreviewMutex);
        imagePreview = {};
    }
    {
        const std::scoped_lock<std::mutex> lock(imageStateMutex);
        lastLoadedImagePath.clear();
    }
}

std::shared_ptr<ImageWavetable> SynthProjectAudioProcessor::buildImageWavetableFromImage(const juce::Image& sourceImage) const
{
    return createImageWavetableFromImage(sourceImage);
}

int SynthProjectAudioProcessor::getImageLoadRequestSerial() const
{
    return imageLoadRequestSerial.load(std::memory_order_relaxed);
}

void SynthProjectAudioProcessor::notifyImageLoadError()
{
    imageLoadErrorFlag.store(true, std::memory_order_relaxed);
}

void SynthProjectAudioProcessor::completeImageLoad(int serial,
                                                   std::shared_ptr<ImageWavetable> wavetable,
                                                   const juce::Image& preview,
                                                   const juce::String& sourcePath)
{
    if (serial != imageLoadRequestSerial.load(std::memory_order_relaxed) || wavetable == nullptr)
    {
        return;
    }

    installImageWavetable(std::move(wavetable), preview);
    imageLoadedFromDisk.store(true, std::memory_order_relaxed);

    const std::scoped_lock<std::mutex> lock(imageStateMutex);
    lastLoadedImagePath = sourcePath;
}

bool SynthProjectAudioProcessor::copyImagePreview(juce::Image& imageOut) const
{
    const std::scoped_lock<std::mutex> lock(imagePreviewMutex);
    if (!imagePreview.isValid())
    {
        imageOut = {};
        return false;
    }

    imageOut = imagePreview.createCopy();
    return imageOut.isValid();
}

bool SynthProjectAudioProcessor::hasLoadedImage() const
{
    return imageLoadedFromDisk.load(std::memory_order_relaxed);
}

bool SynthProjectAudioProcessor::consumeImageLoadErrorFlag()
{
    return imageLoadErrorFlag.exchange(false, std::memory_order_relaxed);
}

float SynthProjectAudioProcessor::copyCurrentImagePosition() const
{
    return currentImagePositionNorm.load(std::memory_order_relaxed);
}

std::vector<float> SynthProjectAudioProcessor::copyCurrentImageWaveformPreview(int sampleCount) const
{
    const auto clampedCount = juce::jlimit(64, 2048, sampleCount);
    std::vector<float> waveform(static_cast<std::size_t>(clampedCount), 0.0f);

    auto table = std::atomic_load(&activeImageWavetable);
    if (table == nullptr || table->frames <= 0 || table->samplesPerFrame <= 1)
    {
        return waveform;
    }

    const auto pos = juce::jlimit(0.0f, 1.0f, currentImagePositionNorm.load(std::memory_order_relaxed));
    const auto framePos = pos * static_cast<float>(table->frames - 1);
    const auto f0 = static_cast<int>(framePos);
    const auto fracF = framePos - static_cast<float>(f0);

    for (int i = 0; i < clampedCount; ++i)
    {
        const auto phase = static_cast<float>(i) / static_cast<float>(clampedCount);
        const auto samplePos = phase * static_cast<float>(table->samplesPerFrame);
        const auto s0 = static_cast<int>(samplePos);
        const auto fracX = samplePos - static_cast<float>(s0);

        const auto readFrame = [table, s0, fracX](int frame)
        {
            const auto a = table->getSample(0, frame, s0);
            const auto b = table->getSample(0, frame, s0 + 1);
            return a + (b - a) * fracX;
        };

        const auto wa = readFrame(f0);
        const auto wb = readFrame((f0 + 1) % table->frames);
        waveform[static_cast<std::size_t>(i)] = wa + (wb - wa) * fracF;
    }

    return waveform;
}

void SynthProjectAudioProcessor::requestAudioLoadAsync(const juce::File& audioFile)
{
    if (!audioFile.existsAsFile())
    {
        audioLoadErrorFlag.store(true, std::memory_order_relaxed);
        return;
    }

    audioLoadErrorFlag.store(false, std::memory_order_relaxed);
    audioLoadedFromDisk.store(false, std::memory_order_relaxed);

    const auto serial = audioLoadRequestSerial.fetch_add(1, std::memory_order_relaxed) + 1;
    audioLoadThreadPool.removeAllJobs(true, 500);
    audioLoadThreadPool.addJob(new AudioLoadJob(*this, audioFile, serial), true);
}

void SynthProjectAudioProcessor::disableAudioEngine()
{
    if (sourceEngineParam != nullptr)
    {
        sourceEngineParam->setValueNotifyingHost(sourceEngineParam->convertTo0to1(0.0f));
    }

    if (audioAnimateParam != nullptr)
    {
        audioAnimateParam->setValueNotifyingHost(audioAnimateParam->convertTo0to1(0.0f));
    }
}

void SynthProjectAudioProcessor::resetAudioEngine()
{
    if (audioPositionParam != nullptr)
    {
        audioPositionParam->setValueNotifyingHost(audioPositionParam->convertTo0to1(0.5f));
    }
    if (audioGrainParam != nullptr)
    {
        audioGrainParam->setValueNotifyingHost(audioGrainParam->convertTo0to1(0.45f));
    }
    if (audioTextureParam != nullptr)
    {
        audioTextureParam->setValueNotifyingHost(audioTextureParam->convertTo0to1(0.35f));
    }
    if (audioAnimateParam != nullptr)
    {
        audioAnimateParam->setValueNotifyingHost(audioAnimateParam->convertTo0to1(0.0f));
    }
    if (audioRateParam != nullptr)
    {
        audioRateParam->setValueNotifyingHost(audioRateParam->convertTo0to1(0.22f));
    }
    if (audioAnimModeParam != nullptr)
    {
        audioAnimModeParam->setValueNotifyingHost(audioAnimModeParam->convertTo0to1(2.0f));
    }
    if (audioAnimSyncParam != nullptr)
    {
        audioAnimSyncParam->setValueNotifyingHost(audioAnimSyncParam->convertTo0to1(0.0f));
    }
    if (audioTargetParam != nullptr)
    {
        audioTargetParam->setValueNotifyingHost(audioTargetParam->convertTo0to1(1.0f));
    }

    std::atomic_store(&activeAudioSource, std::shared_ptr<const AudioSourceData>());
    audioLoadedFromDisk.store(false, std::memory_order_relaxed);
    audioLoadErrorFlag.store(false, std::memory_order_relaxed);

    {
        const std::scoped_lock<std::mutex> lock(imageStateMutex);
        audioWaveformPreview.clear();
        lastLoadedAudioPath.clear();
    }
}

void SynthProjectAudioProcessor::notifyAudioLoadError()
{
    audioLoadErrorFlag.store(true, std::memory_order_relaxed);
}

void SynthProjectAudioProcessor::completeAudioLoad(int serial,
                                                   std::shared_ptr<AudioSourceData> source,
                                                   const juce::String& sourcePath)
{
    if (serial != audioLoadRequestSerial.load(std::memory_order_relaxed) || source == nullptr)
    {
        return;
    }

    std::atomic_store(&activeAudioSource, std::static_pointer_cast<const AudioSourceData>(source));
    audioLoadedFromDisk.store(true, std::memory_order_relaxed);

    {
        const std::scoped_lock<std::mutex> lock(imageStateMutex);
        lastLoadedAudioPath = sourcePath;
        audioWaveformPreview = source->waveformPreview;
    }
}

bool SynthProjectAudioProcessor::consumeAudioLoadErrorFlag()
{
    return audioLoadErrorFlag.exchange(false, std::memory_order_relaxed);
}

bool SynthProjectAudioProcessor::copyCurrentAudioWaveformPreview(std::vector<float>& waveformOut) const
{
    const std::scoped_lock<std::mutex> lock(imageStateMutex);
    waveformOut = audioWaveformPreview;
    return !waveformOut.empty();
}

float SynthProjectAudioProcessor::copyCurrentAudioPosition() const
{
    return currentAudioPositionNorm.load(std::memory_order_relaxed);
}

bool SynthProjectAudioProcessor::hasLoadedAudio() const
{
    return audioLoadedFromDisk.load(std::memory_order_relaxed);
}

SubtractiveSettings SynthProjectAudioProcessor::currentSubtractiveSettings() const
{
    SubtractiveSettings settings;
    settings.sineMix = oscSineParam->get();
    settings.sawMix = oscSawParam->get();
    settings.squareMix = oscSquareParam->get();
    settings.imageMix = 0.35f;
    settings.filterCutoffHz = filterCutoffParam->get();
    settings.filterResonanceQ = filterResonanceParam->get();
        settings.filterTypeIndex = filterTypeParam->getIndex();
    settings.masterGain = masterGainParam->get();
    return settings;
}

OscillatorSettings SynthProjectAudioProcessor::currentOscillatorSettings() const
{
    OscillatorSettings settings;
    settings.modeIndex = oscModeParam->getIndex();
    settings.macroA = clamp01(oscMacroAParam->get());
    settings.macroB = clamp01(oscMacroBParam->get());
    settings.macroC = clamp01(oscMacroCParam->get());
    settings.vowelIndex = oscVowelParam->getIndex();
    for (std::size_t i = 0; i < settings.harmonics.size(); ++i)
    {
        if (oscHarmonicParams[i] != nullptr)
        {
            settings.harmonics[i] = clamp01(oscHarmonicParams[i]->get());
        }
    }
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

    const auto diffSizeA = juce::jmax(8, static_cast<int>(std::round(currentSampleRateHz * 0.0097)));
    const auto diffSizeB = juce::jmax(8, static_cast<int>(std::round(currentSampleRateHz * 0.0153)));
    for (auto& line : isaacDiffusionLineA)
    {
        line.assign(static_cast<std::size_t>(diffSizeA), 0.0f);
    }
    for (auto& line : isaacDiffusionLineB)
    {
        line.assign(static_cast<std::size_t>(diffSizeB), 0.0f);
    }
    isaacDiffusionIndexA = { { 0, 0 } };
    isaacDiffusionIndexB = { { 0, 0 } };

    isaacWritePos = 0;
    isaacSpawnCounter = 0;
    isaacRhythmicStepIndex = 0;
    isaacRhythmicSamplesUntilNext = 0;
    isaacRhythmicSwingToggle = false;
    isaacPanPhase = 0.0f;
    delayModPhase = 0.0f;
    lastDelayAlgorithmIndex = -1;
    lastGranularModeIndex = -1;
    isaacFeedbackFilter = { { 0.0f, 0.0f } };
}

void SynthProjectAudioProcessor::prepareReverbEngine(double sampleRate)
{
    juce::ignoreUnused(sampleRate);

    reverb.reset();
    reverbOutputCompGain = 1.0f;

    moonBufferSize = juce::jmax(1, static_cast<int>(std::ceil(currentSampleRateHz * kMaxMoonDelaySeconds)));
    moonWritePos = 0;
    moonPhase = 0.0f;

    for (auto& channelBuffer : moonDelayBuffer)
    {
        channelBuffer.assign(static_cast<std::size_t>(moonBufferSize), 0.0f);
    }

    const auto maxPreDelaySamples = juce::jmax(8, static_cast<int>(std::round(currentSampleRateHz * 0.30)));
    for (auto& line : reverbPreDelayLines)
    {
        resizeReverbLine(line, maxPreDelaySamples);
    }

    static constexpr std::array<float, 6> plateDelaySeconds { { 0.0113f, 0.0089f, 0.0721f, 0.0887f, 0.0317f, 0.0439f } };
    for (std::size_t i = 0; i < plateLines.size(); ++i)
    {
        resizeReverbLine(plateLines[i], juce::jmax(16, static_cast<int>(std::round(currentSampleRateHz * plateDelaySeconds[i] * 1.8f))));
        plateLines[i].modPhase = static_cast<float>(i) * 0.67f;
        plateLines[i].lpState = 0.0f;
    }
    plateTankState = { { 0.0f, 0.0f } };

    static constexpr std::array<float, 8> hallDelaySeconds { { 0.030f, 0.037f, 0.041f, 0.047f, 0.053f, 0.059f, 0.067f, 0.079f } };
    for (std::size_t i = 0; i < hallLines.size(); ++i)
    {
        resizeReverbLine(hallLines[i], juce::jmax(32, static_cast<int>(std::round(currentSampleRateHz * hallDelaySeconds[i] * 2.2f))));
        hallLines[i].modPhase = static_cast<float>(i) * 0.53f;
        hallLines[i].lpState = 0.0f;
    }
    hallReadCache.fill(0.0f);

    static constexpr std::array<float, 8> cloudDelaySeconds { { 0.049f, 0.061f, 0.073f, 0.089f, 0.104f, 0.127f, 0.013f, 0.017f } };
    for (std::size_t i = 0; i < cloudLines.size(); ++i)
    {
        resizeReverbLine(cloudLines[i], juce::jmax(32, static_cast<int>(std::round(currentSampleRateHz * cloudDelaySeconds[i] * 2.6f))));
        cloudLines[i].modPhase = static_cast<float>(i) * 0.91f;
        cloudLines[i].lpState = 0.0f;
    }
    cloudReadCache.fill(0.0f);
}

float SynthProjectAudioProcessor::imageSyncBeatsForIndex(int index) const
{
    static constexpr std::array<float, 6> beatDivisions { 0.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f };
    const auto clamped = juce::jlimit(0, static_cast<int>(beatDivisions.size()) - 1, index);
    return beatDivisions[static_cast<std::size_t>(clamped)];
}

float SynthProjectAudioProcessor::audioSyncBeatsForIndex(int index) const
{
    static constexpr std::array<float, 6> beatDivisions { 0.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f };
    const auto clamped = juce::jlimit(0, static_cast<int>(beatDivisions.size()) - 1, index);
    return beatDivisions[static_cast<std::size_t>(clamped)];
}

float SynthProjectAudioProcessor::updateImageAnimationPosition(int samplesThisBlock)
{
    const auto basePos = juce::jlimit(0.0f, 1.0f, imagePositionParam->get());
    const auto animAmount = juce::jlimit(0.0f, 1.0f, imageAnimateParam->get());

    if (animAmount <= 0.0001f)
    {
        currentImagePositionNorm.store(basePos, std::memory_order_relaxed);
        return basePos;
    }

    const auto sec = static_cast<float>(samplesThisBlock) / static_cast<float>(juce::jmax(1.0, currentSampleRateHz));
    auto rateHz = juce::jlimit(0.01f, 4.0f, imageRateParam->get());
    const auto syncIndex = imageAnimSyncParam->getIndex();
    if (syncIndex > 0)
    {
        const auto beats = imageSyncBeatsForIndex(syncIndex);
        if (beats > 0.0f)
        {
            const auto cycleSec = static_cast<float>((60.0 / juce::jmax(20.0, currentBpm)) * beats);
            rateHz = 1.0f / juce::jmax(0.05f, cycleSec);
        }
    }

    const auto mode = imageAnimModeParam->getIndex();
    const auto delta = rateHz * sec;

    if (mode == 0)
    {
        imageAnimPhase += delta;
        while (imageAnimPhase > 1.0f)
        {
            imageAnimPhase -= 1.0f;
        }
    }
    else if (mode == 1)
    {
        imageAnimPhase -= delta;
        while (imageAnimPhase < 0.0f)
        {
            imageAnimPhase += 1.0f;
        }
    }
    else
    {
        imageAnimPhase += delta * static_cast<float>(imageAnimDirection);
        if (imageAnimPhase >= 1.0f)
        {
            imageAnimPhase = 1.0f;
            imageAnimDirection = -1;
        }
        else if (imageAnimPhase <= 0.0f)
        {
            imageAnimPhase = 0.0f;
            imageAnimDirection = 1;
        }
    }

    const auto sweep = animAmount * 0.5f;
    const auto animated = juce::jlimit(0.0f, 1.0f, basePos + (imageAnimPhase - 0.5f) * (2.0f * sweep));
    currentImagePositionNorm.store(animated, std::memory_order_relaxed);
    return animated;
}

float SynthProjectAudioProcessor::updateAudioAnimationPosition(int samplesThisBlock)
{
    const auto basePos = juce::jlimit(0.0f, 1.0f, audioPositionParam->get());
    const auto animAmount = juce::jlimit(0.0f, 1.0f, audioAnimateParam->get());

    if (animAmount <= 0.0001f)
    {
        currentAudioPositionNorm.store(basePos, std::memory_order_relaxed);
        return basePos;
    }

    const auto sec = static_cast<float>(samplesThisBlock) / static_cast<float>(juce::jmax(1.0, currentSampleRateHz));
    auto rateHz = juce::jlimit(0.01f, 4.0f, audioRateParam->get());
    const auto syncIndex = audioAnimSyncParam->getIndex();
    if (syncIndex > 0)
    {
        const auto beats = audioSyncBeatsForIndex(syncIndex);
        if (beats > 0.0f)
        {
            const auto cycleSec = static_cast<float>((60.0 / juce::jmax(20.0, currentBpm)) * beats);
            rateHz = 1.0f / juce::jmax(0.05f, cycleSec);
        }
    }

    const auto mode = audioAnimModeParam->getIndex();
    const auto delta = rateHz * sec;

    if (mode == 0)
    {
        audioAnimPhase += delta;
        while (audioAnimPhase > 1.0f)
        {
            audioAnimPhase -= 1.0f;
        }
    }
    else if (mode == 1)
    {
        audioAnimPhase -= delta;
        while (audioAnimPhase < 0.0f)
        {
            audioAnimPhase += 1.0f;
        }
    }
    else
    {
        audioAnimPhase += delta * static_cast<float>(audioAnimDirection);
        if (audioAnimPhase >= 1.0f)
        {
            audioAnimPhase = 1.0f;
            audioAnimDirection = -1;
        }
        else if (audioAnimPhase <= 0.0f)
        {
            audioAnimPhase = 0.0f;
            audioAnimDirection = 1;
        }
    }

    const auto sweep = animAmount * 0.5f;
    const auto animated = juce::jlimit(0.0f, 1.0f, basePos + (audioAnimPhase - 0.5f) * (2.0f * sweep));
    currentAudioPositionNorm.store(animated, std::memory_order_relaxed);
    return animated;
}

float SynthProjectAudioProcessor::computeImageTargetControlSignal(float imagePositionNorm, int samplesThisBlock)
{
    auto table = std::atomic_load(&activeImageWavetable);
    if (table == nullptr || table->frames <= 0 || table->samplesPerFrame <= 1)
    {
        return 0.5f;
    }

    const auto clampedPos = juce::jlimit(0.0f, 1.0f, imagePositionNorm);
    const auto framePos = clampedPos * static_cast<float>(table->frames - 1);
    const auto f0 = static_cast<int>(framePos);
    const auto fracF = framePos - static_cast<float>(f0);
    const auto f1 = (f0 + 1) % table->frames;

    const auto sec = static_cast<float>(samplesThisBlock) / static_cast<float>(juce::jmax(1.0, currentSampleRateHz));
    const auto scanRateHz = lerp(0.35f, 6.0f, juce::jlimit(0.0f, 1.0f, imageRateParam->get() * 0.25f));
    imageTargetScanPhase += sec * scanRateHz;
    while (imageTargetScanPhase >= 1.0f)
    {
        imageTargetScanPhase -= 1.0f;
    }

    const auto samplePos = imageTargetScanPhase * static_cast<float>(table->samplesPerFrame);
    const auto s0 = static_cast<int>(samplePos);
    const auto fracX = samplePos - static_cast<float>(s0);

    const auto readFrame = [table, s0, fracX](int frame)
    {
        const auto a = table->getSample(0, frame, s0);
        const auto b = table->getSample(0, frame, s0 + 1);
        return a + (b - a) * fracX;
    };

    const auto wa = readFrame(f0);
    const auto wb = readFrame(f1);
    const auto sample = wa + (wb - wa) * fracF;

    return clamp01(0.5f + 0.5f * sample);
}

std::shared_ptr<ImageWavetable> SynthProjectAudioProcessor::createDefaultImageWavetable() const
{
    auto table = std::make_shared<ImageWavetable>();
    table->frames = 128;
    table->samplesPerFrame = 2048;
    table->mipLevels = 6;
    table->data.resize(static_cast<std::size_t>(table->frames * table->samplesPerFrame * table->mipLevels), 0.0f);

    for (int frame = 0; frame < table->frames; ++frame)
    {
        const auto morph = static_cast<float>(frame) / static_cast<float>(table->frames - 1);
        for (int i = 0; i < table->samplesPerFrame; ++i)
        {
            const auto phase = static_cast<float>(i) / static_cast<float>(table->samplesPerFrame);
            const auto sine = std::sin(phase * juce::MathConstants<float>::twoPi);
            const auto saw = phase * 2.0f - 1.0f;
            const auto val = (1.0f - morph) * sine + morph * saw;
            const auto idx = static_cast<std::size_t>(frame * table->samplesPerFrame + i);
            table->data[idx] = val * 0.82f;
        }
    }

    for (int mip = 1; mip < table->mipLevels; ++mip)
    {
        for (int frame = 0; frame < table->frames; ++frame)
        {
            const auto prevBase = static_cast<std::size_t>(((mip - 1) * table->frames + frame) * table->samplesPerFrame);
            const auto curBase = static_cast<std::size_t>((mip * table->frames + frame) * table->samplesPerFrame);

            for (int i = 0; i < table->samplesPerFrame; ++i)
            {
                const auto a = table->data[prevBase + static_cast<std::size_t>((i - 1 + table->samplesPerFrame) % table->samplesPerFrame)];
                const auto b = table->data[prevBase + static_cast<std::size_t>(i)];
                const auto c = table->data[prevBase + static_cast<std::size_t>((i + 1) % table->samplesPerFrame)];
                table->data[curBase + static_cast<std::size_t>(i)] = (a + 2.0f * b + c) * 0.25f;
            }
        }
    }

    return table;
}

std::shared_ptr<ImageWavetable> SynthProjectAudioProcessor::createImageWavetableFromImage(const juce::Image& sourceImage) const
{
    if (!sourceImage.isValid() || sourceImage.getWidth() <= 0 || sourceImage.getHeight() <= 0)
    {
        return nullptr;
    }

    auto table = std::make_shared<ImageWavetable>();
    table->frames = 128;
    table->samplesPerFrame = 2048;
    table->mipLevels = 6;
    table->data.resize(static_cast<std::size_t>(table->frames * table->samplesPerFrame * table->mipLevels), 0.0f);

    juce::Image analysisImage(juce::Image::ARGB, table->samplesPerFrame, table->frames, true);
    {
        juce::Graphics g(analysisImage);
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(sourceImage,
                    juce::Rectangle<float>(0.0f,
                                           0.0f,
                                           static_cast<float>(table->samplesPerFrame),
                                           static_cast<float>(table->frames)));
    }

    constexpr float normalizationBlend = 0.68f;
    for (int frame = 0; frame < table->frames; ++frame)
    {
        auto base = static_cast<std::size_t>(frame * table->samplesPerFrame);
        float mean = 0.0f;

        for (int i = 0; i < table->samplesPerFrame; ++i)
        {
            const auto c = analysisImage.getPixelAt(i, frame);
            const auto lum = c.getPerceivedBrightness();
            const auto amp = lum * 2.0f - 1.0f;
            table->data[base + static_cast<std::size_t>(i)] = amp;
            mean += amp;
        }

        mean /= static_cast<float>(table->samplesPerFrame);

        float peak = 0.0001f;
        for (int i = 0; i < table->samplesPerFrame; ++i)
        {
            auto v = table->data[base + static_cast<std::size_t>(i)] - mean;
            const auto left = table->data[base + static_cast<std::size_t>((i - 1 + table->samplesPerFrame) % table->samplesPerFrame)] - mean;
            const auto right = table->data[base + static_cast<std::size_t>((i + 1) % table->samplesPerFrame)] - mean;
            v = v * 0.70f + (left + right) * 0.15f;
            table->data[base + static_cast<std::size_t>(i)] = v;
            peak = juce::jmax(peak, std::abs(v));
        }

        const auto hardNorm = 0.85f / peak;
        const auto gain = 1.0f + (hardNorm - 1.0f) * normalizationBlend;

        for (int i = 0; i < table->samplesPerFrame; ++i)
        {
            table->data[base + static_cast<std::size_t>(i)]
                = juce::jlimit(-0.95f, 0.95f, table->data[base + static_cast<std::size_t>(i)] * gain);
        }
    }

    for (int mip = 1; mip < table->mipLevels; ++mip)
    {
        for (int frame = 0; frame < table->frames; ++frame)
        {
            const auto prevBase = static_cast<std::size_t>(((mip - 1) * table->frames + frame) * table->samplesPerFrame);
            const auto curBase = static_cast<std::size_t>((mip * table->frames + frame) * table->samplesPerFrame);

            for (int i = 0; i < table->samplesPerFrame; ++i)
            {
                const auto a = table->data[prevBase + static_cast<std::size_t>((i - 1 + table->samplesPerFrame) % table->samplesPerFrame)];
                const auto b = table->data[prevBase + static_cast<std::size_t>(i)];
                const auto c = table->data[prevBase + static_cast<std::size_t>((i + 1) % table->samplesPerFrame)];
                table->data[curBase + static_cast<std::size_t>(i)] = (a + 2.0f * b + c) * 0.25f;
            }
        }
    }

    return table;
}

void SynthProjectAudioProcessor::installImageWavetable(std::shared_ptr<ImageWavetable> newTable, const juce::Image& sourcePreview)
{
    if (newTable == nullptr)
    {
        return;
    }

    std::atomic_store(&activeImageWavetable, std::static_pointer_cast<const ImageWavetable>(newTable));

    if (sourcePreview.isValid())
    {
        std::scoped_lock<std::mutex> lock(imagePreviewMutex);
        imagePreview = sourcePreview.createCopy();
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

float SynthProjectAudioProcessor::processRobSample(float x, int channel, float robAmount, int modeIndex)
{
    if (robAmount <= 0.0001f)
    {
        return x;
    }

    const auto mode = juce::jlimit(0, 3, modeIndex);
    const auto warm = smoothstep(robAmount);
    auto& dcState = robDcState[static_cast<std::size_t>(channel)];
    auto& toneState = robToneState[static_cast<std::size_t>(channel)];
    static constexpr std::array<float, 4> kModeTargetTrim { 1.00f, 1.07f, 1.02f, 0.86f };
    const auto trim = 1.0f + (kModeTargetTrim[static_cast<std::size_t>(mode)] - 1.0f) * warm;

    if (mode == 0)
    {
        dcState += 0.0045f * (x - dcState);
        const auto hp = x - dcState * (0.70f * warm);

        const auto drive = 1.0f + warm * 6.8f;
        const auto pre = hp * drive;
        const auto asym = pre + (0.18f * warm) * pre * pre;

        const auto satA = std::tanh(asym * 0.85f);
        const auto satB = asym / (1.0f + std::abs(asym));
        auto colored = satA * (0.62f + 0.20f * warm) + satB * (0.38f - 0.12f * warm);

        toneState += 0.09f * (colored - toneState);
        colored = 0.72f * colored + 0.28f * toneState;

        const auto wetMix = 0.12f + 0.78f * warm;
        const auto parallel = juce::jmap(warm, x, colored);
        const auto mixed = x * (1.0f - wetMix * 0.72f) + parallel * wetMix;

        const auto levelComp = 1.0f / (1.0f + 0.58f * warm);
        return mixed * levelComp * trim;
    }

    if (mode == 1)
    {
        // Tape: softer saturation, mild compression, and slightly darkened top end.
        dcState += 0.0038f * (x - dcState);
        const auto hp = x - dcState * (0.64f * warm);
        const auto drive = 1.0f + 3.6f * warm;
        const auto compressed = std::tanh(hp * drive) * (0.85f + 0.10f * warm);

        toneState += (0.035f + 0.020f * warm) * (compressed - toneState);
        const auto dark = compressed * (0.58f + 0.10f * warm) + toneState * (0.42f - 0.10f * warm);
        const auto wowPhase = static_cast<float>(currentTimelineSeconds * 0.9 + static_cast<double>(channel) * 0.7);
        const auto wow = dark + std::sin(wowPhase) * (0.0018f + 0.0035f * warm);

        const auto mix = 0.18f + 0.66f * warm;
        const auto out = x + (wow - x) * mix;
        return out * (1.0f / (1.0f + 0.34f * warm)) * trim;
    }

    if (mode == 2)
    {
        // Tube: asymmetry and even-harmonic emphasis with gentler clipping.
        dcState += 0.0042f * (x - dcState);
        const auto hp = x - dcState * (0.55f * warm);
        const auto drive = 1.0f + 5.2f * warm;
        const auto pre = hp * drive;
        const auto asym = pre + (0.30f * warm + 0.04f) * pre * pre;
        const auto sat = std::tanh(asym * (0.70f + 0.24f * warm));

        toneState += (0.060f + 0.050f * warm) * (sat - toneState);
        const auto body = 0.64f * sat + 0.36f * toneState;
        const auto mix = 0.16f + 0.74f * warm;
        const auto out = x * (1.0f - mix * 0.62f) + body * mix;
        return out * (1.0f / (1.0f + 0.40f * warm)) * trim;
    }

    // Distortion pedal: aggressive clipping with tighter low-end and bright bite.
    dcState += 0.0065f * (x - dcState);
    const auto hp = x - dcState * (0.86f * warm);
    const auto pre = hp * (1.0f + 18.0f * warm);
    const auto clipped = juce::jlimit(-0.88f, 0.88f, pre);
    const auto shaped = std::tanh(clipped * (1.5f + 0.8f * warm));

    toneState += (0.18f + 0.10f * warm) * (shaped - toneState);
    const auto bite = shaped + (shaped - toneState) * (0.18f + 0.34f * warm);
    const auto mix = 0.26f + 0.70f * warm;
    const auto out = x * (1.0f - mix * 0.50f) + bite * mix;
    return out * (1.0f / (1.0f + 0.66f * warm)) * trim;
}

float SynthProjectAudioProcessor::readDelaySample(int channel, float readPos) const
{
    const auto& buffer = isaacDelayBuffer[static_cast<std::size_t>(channel)];
    auto rp = readPos;
    while (rp < 0.0f)
    {
        rp += static_cast<float>(isaacBufferSize);
    }
    while (rp >= static_cast<float>(isaacBufferSize))
    {
        rp -= static_cast<float>(isaacBufferSize);
    }

    const auto i0 = static_cast<int>(rp) % isaacBufferSize;
    const auto i1 = (i0 + 1) % isaacBufferSize;
    const auto frac = rp - static_cast<float>(i0);
    return buffer[static_cast<std::size_t>(i0)] + (buffer[static_cast<std::size_t>(i1)] - buffer[static_cast<std::size_t>(i0)]) * frac;
}

float SynthProjectAudioProcessor::sanitizeAudioSample(float x) const
{
    if (!std::isfinite(x))
    {
        return 0.0f;
    }

    return juce::jlimit(-4.0f, 4.0f, x);
}

void SynthProjectAudioProcessor::clearGranularDiffusionState()
{
    for (auto& line : isaacDiffusionLineA)
    {
        std::fill(line.begin(), line.end(), 0.0f);
    }
    for (auto& line : isaacDiffusionLineB)
    {
        std::fill(line.begin(), line.end(), 0.0f);
    }
    isaacDiffusionIndexA = { { 0, 0 } };
    isaacDiffusionIndexB = { { 0, 0 } };
}

float SynthProjectAudioProcessor::processAllpassSample(float x,
                                                       std::vector<float>& line,
                                                       int& index,
                                                       float feedback) const
{
    if (line.empty())
    {
        return x;
    }

    auto& d = line[static_cast<std::size_t>(index)];
    const auto g = juce::jlimit(0.0f, 0.82f, feedback);
    const auto y = -g * x + d;
    d = x + g * y;

    ++index;
    if (index >= static_cast<int>(line.size()))
    {
        index = 0;
    }

    return y;
}

void SynthProjectAudioProcessor::processGranularDiffusion(float& wetL,
                                                          float& wetR,
                                                          float diffusionAmount,
                                                          float stereoAmount)
{
    const auto d = juce::jlimit(0.0f, 1.0f, diffusionAmount);
    if (d <= 0.0001f)
    {
        return;
    }

    const auto gA = lerp(0.38f, 0.74f, d);
    const auto gB = lerp(0.32f, 0.68f, d);

    auto dl = processAllpassSample(wetL, isaacDiffusionLineA[0], isaacDiffusionIndexA[0], gA);
    dl = processAllpassSample(dl, isaacDiffusionLineB[0], isaacDiffusionIndexB[0], gB);

    auto dr = processAllpassSample(wetR, isaacDiffusionLineA[1], isaacDiffusionIndexA[1], gA);
    dr = processAllpassSample(dr, isaacDiffusionLineB[1], isaacDiffusionIndexB[1], gB);

    const auto width = juce::jlimit(0.0f, 1.0f, stereoAmount);
    const auto cross = 0.08f + 0.28f * d;
    const auto widenedL = dl * (1.0f - cross) + dr * cross;
    const auto widenedR = dr * (1.0f - cross) + dl * cross;
    wetL = lerp(wetL, widenedL, d * (0.50f + 0.40f * width));
    wetR = lerp(wetR, widenedR, d * (0.50f + 0.40f * width));
}

void SynthProjectAudioProcessor::renderActiveGranularGrains(float& wetL, float& wetR)
{
    wetL = 0.0f;
    wetR = 0.0f;

    constexpr float twoPi = juce::MathConstants<float>::twoPi;

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

        const auto window = 0.5f - 0.5f * std::cos(phase * twoPi);
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

        grain.readPos += grain.reverse ? -std::abs(grain.increment) : std::abs(grain.increment);
        while (grain.readPos < 0.0f)
        {
            grain.readPos += static_cast<float>(isaacBufferSize);
        }
        while (grain.readPos >= static_cast<float>(isaacBufferSize))
        {
            grain.readPos -= static_cast<float>(isaacBufferSize);
        }

        ++grain.ageSamples;
    }
}

void SynthProjectAudioProcessor::spawnIsaacGrain(float amount,
                                                 float timeControl,
                                                 float feedbackControl,
                                                 int syncDivisionIndex,
                                                 GranularMode mode,
                                                 int rhythmicStep)
{
    for (auto& grain : isaacGrains)
    {
        if (grain.active)
        {
            continue;
        }

        auto& random = juce::Random::getSystemRandom();
        grain.active = true;
        grain.reverse = false;

        const auto a = smoothstep(amount);
        const auto macro = std::pow(a, 0.62f);
        const auto sizeCtrl = juce::jlimit(0.0f, 1.0f, timeControl);
        const auto feedbackCtrl = juce::jlimit(0.0f, 1.0f, feedbackControl);

        float grainMs = lerp(35.0f, 170.0f, a);
        float semitone = 0.0f;
        float pitchMicro = 0.0f;
        float reverseChance = 0.0f;
        float panSpread = lerp(0.14f, 0.88f, macro);
        float baseDelayBeats = lerp(0.125f, 0.75f, macro);
        float jitterWidth = 0.06f + 0.22f * macro;
        float gain = lerp(0.11f, 0.30f, macro);

        if (mode == GranularMode::classic)
        {
            const std::array<int, 7> intervals { -12, -7, -5, 0, 5, 7, 12 };
            const auto chooseWide = random.nextFloat() < (0.22f + 0.70f * macro);
            const auto idx = chooseWide ? random.nextInt(static_cast<int>(intervals.size())) : 3;
            semitone = static_cast<float>(intervals[static_cast<std::size_t>(idx)]);
            pitchMicro = (random.nextFloat() - 0.5f) * (0.16f + 0.26f * macro);
            reverseChance = 0.0f;
        }
        else if (mode == GranularMode::cloud)
        {
            const std::array<int, 9> intervals { 0, 0, 0, 7, -7, 12, -12, 5, -5 };
            semitone = static_cast<float>(intervals[static_cast<std::size_t>(random.nextInt(static_cast<int>(intervals.size())))]);
            pitchMicro = (random.nextFloat() - 0.5f) * (0.10f + 0.20f * macro);
            grainMs = lerp(45.0f, 260.0f, sizeCtrl);
            panSpread = lerp(0.35f, 0.98f, a);
            reverseChance = 0.08f + 0.52f * feedbackCtrl;
            baseDelayBeats = lerp(0.18f, 0.95f, sizeCtrl);
            jitterWidth = 0.03f + 0.14f * a;
            gain = lerp(0.08f, 0.22f, macro);
        }
        else if (mode == GranularMode::shimmer)
        {
            const std::array<int, 5> shimmerIntervals { 12, 7, 5, 19, 24 };
            const auto idx = juce::jlimit(0,
                                          static_cast<int>(shimmerIntervals.size()) - 1,
                                          static_cast<int>(std::round(sizeCtrl * static_cast<float>(shimmerIntervals.size() - 1))));
            semitone = static_cast<float>(shimmerIntervals[static_cast<std::size_t>(idx)]);
            pitchMicro = (random.nextFloat() - 0.5f) * 0.10f;
            grainMs = lerp(70.0f, 230.0f, sizeCtrl);
            panSpread = lerp(0.24f, 0.80f, a);
            reverseChance = 0.04f + 0.20f * feedbackCtrl;
            baseDelayBeats = lerp(0.25f, 1.0f, sizeCtrl);
            jitterWidth = 0.01f + 0.06f * a;
            gain = lerp(0.10f, 0.24f, macro);
        }
        else
        {
            static constexpr std::array<std::array<int, 8>, 3> pitchPatterns { {
                { 0, 7, 12, 7, 0, 7, 12, 7 },
                { 0, 12, 0, 7, 0, 12, 0, 7 },
                { 0, -12, 12, 0, 0, -12, 12, 0 }
            } };
            const auto patternIndex = juce::jlimit(0, 2, static_cast<int>(std::floor(sizeCtrl * 2.99f)));
            semitone = static_cast<float>(pitchPatterns[static_cast<std::size_t>(patternIndex)][static_cast<std::size_t>(rhythmicStep & 7)]);
            pitchMicro = 0.0f;
            grainMs = lerp(22.0f, 110.0f, sizeCtrl);
            panSpread = lerp(0.30f, 0.92f, a);
            reverseChance = juce::jlimit(0.0f, 0.9f, feedbackCtrl);
            baseDelayBeats = juce::jmax(0.0625f, divisionBeatsForIndex(syncDivisionIndex));
            jitterWidth = 0.0f;
            gain = lerp(0.10f, 0.26f, macro);
        }

        grain.lengthSamples = juce::jmax(24,
                                         static_cast<int>(std::round(grainMs * 0.001f * static_cast<float>(currentSampleRateHz))));
        grain.ageSamples = 0;
        grain.reverse = random.nextFloat() < reverseChance;

        const auto ratio = std::pow(2.0f, (semitone + pitchMicro) / 12.0f);
        grain.increment = juce::jlimit(0.25f, 4.0f, std::abs(ratio));

        const auto secPerBeat = static_cast<float>(60.0 / juce::jmax(20.0, currentBpm));
        const auto syncBeats = divisionBeatsForIndex(syncDivisionIndex);
        const auto syncEnabled = syncDivisionIndex > 0 && syncBeats > 0.0f;
        if (syncEnabled)
        {
            baseDelayBeats = syncBeats;
            if (mode == GranularMode::rhythmic)
            {
                jitterWidth = 0.0f;
            }
            else
            {
                jitterWidth = juce::jmin(jitterWidth, 0.07f);
            }
        }

        const auto baseDelaySamples = baseDelayBeats * secPerBeat * static_cast<float>(currentSampleRateHz);
        const auto jitter = (random.nextFloat() - 0.5f) * jitterWidth * baseDelaySamples;
        auto readPos = static_cast<float>(isaacWritePos) - (baseDelaySamples + jitter);
        while (readPos < 0.0f)
        {
            readPos += static_cast<float>(isaacBufferSize);
        }
        while (readPos >= static_cast<float>(isaacBufferSize))
        {
            readPos -= static_cast<float>(isaacBufferSize);
        }

        if (grain.reverse)
        {
            readPos += static_cast<float>(grain.lengthSamples) * grain.increment;
            while (readPos >= static_cast<float>(isaacBufferSize))
            {
                readPos -= static_cast<float>(isaacBufferSize);
            }
        }
        grain.readPos = readPos;

        isaacPanPhase += lerp(0.13f, 0.36f, macro);
        if (isaacPanPhase > juce::MathConstants<float>::twoPi)
        {
            isaacPanPhase -= juce::MathConstants<float>::twoPi;
        }

        if (mode == GranularMode::rhythmic)
        {
            const auto alt = ((rhythmicStep & 1) == 0) ? -1.0f : 1.0f;
            grain.pan = 0.5f + alt * 0.5f * panSpread * 0.82f;
        }
        else
        {
            grain.pan = 0.5f + std::sin(isaacPanPhase) * 0.5f * panSpread;
        }
        grain.gain = gain;
        return;
    }
}

void SynthProjectAudioProcessor::processIsaacGranularSample(float inL,
                                                            float inR,
                                                            float amount,
                                                            float timeControl,
                                                            float feedbackControl,
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

    const auto dryL = sanitizeAudioSample(inL);
    const auto dryR = sanitizeAudioSample(inR);

    if (amount <= 0.0001f)
    {
        isaacDelayBuffer[0][static_cast<std::size_t>(isaacWritePos)] = dryL;
        isaacDelayBuffer[1][static_cast<std::size_t>(isaacWritePos)] = dryR;
        isaacWritePos = (isaacWritePos + 1) % isaacBufferSize;
        outL = dryL;
        outR = dryR;
        return;
    }

    const auto mode = static_cast<GranularMode>(juce::jlimit(0, 3, granularModeParam->getIndex()));
    const auto a = smoothstep(amount);
    const auto macro = std::pow(a, 0.62f);
    const auto secPerBeat = static_cast<float>(60.0 / juce::jmax(20.0, currentBpm));
    const auto syncBeats = divisionBeatsForIndex(syncDivisionIndex);

    if (mode == GranularMode::rhythmic)
    {
        if (isaacRhythmicSamplesUntilNext <= 0)
        {
            const std::array<std::array<int, 16>, 3> triggerPatterns { {
                { 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 0 },
                { 1, 0, 1, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 1, 0, 0 },
                { 1, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1 }
            } };

            const auto patternIndex = juce::jlimit(0, 2, static_cast<int>(std::floor(timeControl * 2.99f)));
            const auto step = isaacRhythmicStepIndex & 15;
            const auto shouldTrigger = triggerPatterns[static_cast<std::size_t>(patternIndex)][static_cast<std::size_t>(step)] != 0;
            const auto densityChance = juce::jlimit(0.0f, 1.0f, lerp(0.15f, 0.98f, a));

            if (shouldTrigger || juce::Random::getSystemRandom().nextFloat() < densityChance * 0.22f)
            {
                const auto layers = 1 + (juce::Random::getSystemRandom().nextFloat() < densityChance * 0.26f ? 1 : 0);
                for (int i = 0; i < layers; ++i)
                {
                    spawnIsaacGrain(amount,
                                    timeControl,
                                    feedbackControl,
                                    syncDivisionIndex,
                                    mode,
                                    step + i);
                }
            }

            const auto baseBeats = syncDivisionIndex > 0 && syncBeats > 0.0f
                                       ? syncBeats
                                       : lerp(1.0f, 0.125f, juce::jlimit(0.0f, 1.0f, timeControl));
            const auto swingAmount = juce::jlimit(0.0f, 0.48f, feedbackControl * 0.42f);
            auto stepBeats = baseBeats;
            if (isaacRhythmicSwingToggle)
            {
                stepBeats *= (1.0f + swingAmount);
            }
            else
            {
                stepBeats *= (1.0f - swingAmount);
            }
            isaacRhythmicSwingToggle = !isaacRhythmicSwingToggle;
            isaacRhythmicStepIndex = (isaacRhythmicStepIndex + 1) & 15;
            isaacRhythmicSamplesUntilNext = juce::jmax(8,
                                                       static_cast<int>(std::round(stepBeats * secPerBeat
                                                                                   * static_cast<float>(currentSampleRateHz))));
        }

        --isaacRhythmicSamplesUntilNext;
    }
    else
    {
        auto spawnEverySec = lerp(0.085f, 0.028f, macro) * secPerBeat * 2.0f;
        if (mode == GranularMode::cloud)
        {
            spawnEverySec = lerp(0.040f, 0.009f, a) * lerp(1.08f, 0.72f, juce::jlimit(0.0f, 1.0f, timeControl));
        }
        else if (mode == GranularMode::shimmer)
        {
            spawnEverySec = lerp(0.072f, 0.016f, a);
        }

        if (syncDivisionIndex > 0 && syncBeats > 0.0f)
        {
            auto syncMul = lerp(0.38f, 0.25f, macro);
            if (mode == GranularMode::cloud)
            {
                syncMul = lerp(0.22f, 0.14f, a);
            }
            else if (mode == GranularMode::shimmer)
            {
                syncMul = lerp(0.44f, 0.28f, a);
            }
            spawnEverySec = juce::jmax(0.008f, secPerBeat * syncBeats * syncMul);
        }

        const auto spawnEverySamples = juce::jmax(8,
                                                   static_cast<int>(std::round(spawnEverySec * static_cast<float>(currentSampleRateHz))));
        if (++isaacSpawnCounter >= spawnEverySamples)
        {
            isaacSpawnCounter = 0;
            const auto layerCount = mode == GranularMode::cloud
                                        ? 1 + (juce::Random::getSystemRandom().nextFloat() < a * 0.62f ? 1 : 0)
                                        : 1;
            for (int i = 0; i < layerCount; ++i)
            {
                spawnIsaacGrain(amount,
                                timeControl,
                                feedbackControl,
                                syncDivisionIndex,
                                mode,
                                0);
            }
        }
    }

    float wetL = 0.0f;
    float wetR = 0.0f;
    renderActiveGranularGrains(wetL, wetR);

    float feedback = lerp(0.16f, 0.74f, macro);
    float diffusion = 0.0f;
    float stereo = 0.5f;
    float dampCoeff = lerp(0.19f, 0.06f, macro);

    if (mode == GranularMode::cloud)
    {
        feedback = juce::jlimit(0.0f, 0.86f, 0.20f + 0.66f * feedbackControl);
        diffusion = juce::jlimit(0.0f, 1.0f, 0.22f + 0.72f * feedbackControl);
        stereo = juce::jlimit(0.0f, 1.0f, 0.35f + 0.60f * timeControl);
        dampCoeff = lerp(0.14f, 0.04f, a);
    }
    else if (mode == GranularMode::shimmer)
    {
        feedback = juce::jlimit(0.0f, 0.92f, 0.36f + 0.56f * feedbackControl);
        diffusion = juce::jlimit(0.0f, 1.0f, 0.28f + 0.58f * a);
        stereo = juce::jlimit(0.0f, 1.0f, 0.24f + 0.56f * a);
        dampCoeff = lerp(0.11f, 0.03f, a);
    }
    else if (mode == GranularMode::rhythmic)
    {
        feedback = juce::jlimit(0.0f, 0.80f, 0.10f + 0.70f * feedbackControl);
        diffusion = juce::jlimit(0.0f, 0.40f, 0.05f + 0.35f * a);
        stereo = juce::jlimit(0.0f, 1.0f, 0.35f + 0.54f * a);
        dampCoeff = lerp(0.18f, 0.07f, a);
    }

    processGranularDiffusion(wetL, wetR, diffusion, stereo);

    isaacFeedbackFilter[0] += dampCoeff * (wetL - isaacFeedbackFilter[0]);
    isaacFeedbackFilter[1] += dampCoeff * (wetR - isaacFeedbackFilter[1]);

    if (mode == GranularMode::shimmer)
    {
        const auto shimmerTone = 1.0f + 0.32f * a;
        wetL = std::tanh((wetL * 0.76f + isaacFeedbackFilter[0] * 0.24f) * shimmerTone);
        wetR = std::tanh((wetR * 0.76f + isaacFeedbackFilter[1] * 0.24f) * shimmerTone);
    }
    else
    {
        wetL = std::tanh((wetL * 0.82f + isaacFeedbackFilter[0] * 0.18f) * (1.0f + 0.25f * macro));
        wetR = std::tanh((wetR * 0.82f + isaacFeedbackFilter[1] * 0.18f) * (1.0f + 0.25f * macro));
    }

    const auto writeL = sanitizeAudioSample(dryL + wetL * feedback);
    const auto writeR = sanitizeAudioSample(dryR + wetR * feedback);

    isaacDelayBuffer[0][static_cast<std::size_t>(isaacWritePos)] = writeL;
    isaacDelayBuffer[1][static_cast<std::size_t>(isaacWritePos)] = writeR;
    isaacWritePos = (isaacWritePos + 1) % isaacBufferSize;

    auto wetMix = lerp(0.08f, 0.92f, std::pow(macro, 1.02f));
    auto dryMix = lerp(0.95f, 0.12f, macro);

    if (mode == GranularMode::cloud)
    {
        wetMix = lerp(0.14f, 0.97f, a);
        dryMix = lerp(0.90f, 0.08f, a);
    }
    else if (mode == GranularMode::shimmer)
    {
        wetMix = lerp(0.16f, 0.94f, a);
        dryMix = lerp(0.92f, 0.10f, a);
    }
    else if (mode == GranularMode::rhythmic)
    {
        wetMix = lerp(0.10f, 0.90f, a);
        dryMix = lerp(0.96f, 0.22f, a);
    }

    outL = sanitizeAudioSample(dryL * dryMix + wetL * wetMix);
    outR = sanitizeAudioSample(dryR * dryMix + wetR * wetMix);
}

void SynthProjectAudioProcessor::processDelayAlgorithmSample(float inL,
                                                             float inR,
                                                             float amount,
                                                             int algorithmIndex,
                                                             float timeControl,
                                                             float feedbackControl,
                                                             int syncDivisionIndex,
                                                             float& outL,
                                                             float& outR)
{
    const auto algo = juce::jlimit(0, 6, algorithmIndex);

    if (algo == 0)
    {
        processIsaacGranularSample(inL,
                                   inR,
                                   amount,
                                   timeControl,
                                   feedbackControl,
                                   syncDivisionIndex,
                                   outL,
                                   outR);
        return;
    }

    if (isaacBufferSize <= 1)
    {
        outL = inL;
        outR = inR;
        return;
    }

    const auto a = smoothstep(amount);
    const auto secPerBeat = static_cast<float>(60.0 / currentBpm);
    float baseDelaySec = lerp(0.04f, 1.25f, timeControl);
    const auto syncBeats = divisionBeatsForIndex(syncDivisionIndex);
    if (syncDivisionIndex > 0 && syncBeats > 0.0f)
    {
        baseDelaySec = secPerBeat * syncBeats;
    }

    auto feedback = juce::jlimit(0.0f, 0.95f, lerp(0.05f, 0.92f, feedbackControl));
    if (algo == 2) // BBD can get unstable quickly; keep a stricter cap
    {
        feedback = juce::jmin(feedback, 0.82f);
    }

    delayModPhase += juce::MathConstants<float>::twoPi * (0.18f + 0.65f * timeControl)
                     / static_cast<float>(juce::jmax(1.0, currentSampleRateHz));
    while (delayModPhase >= juce::MathConstants<float>::twoPi)
    {
        delayModPhase -= juce::MathConstants<float>::twoPi;
    }

    auto delaySamplesL = baseDelaySec * static_cast<float>(currentSampleRateHz);
    auto delaySamplesR = delaySamplesL;

    if (algo == 2) // Analog/BBD
    {
        delaySamplesL = juce::jlimit(20.0f,
                                     static_cast<float>(isaacBufferSize - 2),
                                     lerp(0.02f, 0.55f, timeControl) * static_cast<float>(currentSampleRateHz));
        delaySamplesR = delaySamplesL;
    }
    else if (algo == 4) // Stereo delay
    {
        delaySamplesL *= 0.82f;
        delaySamplesR *= 1.28f;
    }
    else if (algo == 5) // Modulated delay
    {
        const auto modDepthSamples = (6.0f + 26.0f * a);
        const auto modA = std::sin(delayModPhase);
        const auto modB = std::sin(delayModPhase * 1.31f + 1.2f);
        delaySamplesL += modDepthSamples * modA;
        delaySamplesR += modDepthSamples * modB;
    }

    delaySamplesL = juce::jlimit(10.0f, static_cast<float>(isaacBufferSize - 2), delaySamplesL);
    delaySamplesR = juce::jlimit(10.0f, static_cast<float>(isaacBufferSize - 2), delaySamplesR);

    const auto readPosL = static_cast<float>(isaacWritePos) - delaySamplesL;
    const auto readPosR = static_cast<float>(isaacWritePos) - delaySamplesR;

    auto wetL = readDelaySample(0, readPosL);
    auto wetR = readDelaySample(1, readPosR);

    if (algo == 1) // Tape
    {
        const auto wow = std::sin(delayModPhase * 0.7f) * (0.003f + 0.007f * a);
        wetL = std::tanh((wetL + wow) * (0.95f + 0.35f * a));
        wetR = std::tanh((wetR - wow) * (0.95f + 0.35f * a));
        isaacFeedbackFilter[0] += 0.05f * (wetL - isaacFeedbackFilter[0]);
        isaacFeedbackFilter[1] += 0.05f * (wetR - isaacFeedbackFilter[1]);
        wetL = wetL * 0.62f + isaacFeedbackFilter[0] * 0.38f;
        wetR = wetR * 0.62f + isaacFeedbackFilter[1] * 0.38f;
    }
    else if (algo == 2) // Analog/BBD
    {
        isaacFeedbackFilter[0] += 0.03f * (wetL - isaacFeedbackFilter[0]);
        isaacFeedbackFilter[1] += 0.03f * (wetR - isaacFeedbackFilter[1]);
        wetL = wetL * 0.52f + isaacFeedbackFilter[0] * 0.48f;
        wetR = wetR * 0.52f + isaacFeedbackFilter[1] * 0.48f;
    }
    else if (algo == 3) // Ping-Pong
    {
        wetL = readDelaySample(1, readPosL);
        wetR = readDelaySample(0, readPosR);
    }
    else if (algo == 6) // Diffusion
    {
        const auto tapA = readDelaySample(0, readPosL - delaySamplesL * 0.23f);
        const auto tapB = readDelaySample(1, readPosR - delaySamplesR * 0.17f);
        const auto tapC = readDelaySample(0, readPosL - delaySamplesL * 0.37f);
        const auto tapD = readDelaySample(1, readPosR - delaySamplesR * 0.31f);
        wetL = 0.50f * wetL + 0.24f * tapA + 0.18f * tapB + 0.08f * tapD;
        wetR = 0.50f * wetR + 0.24f * tapB + 0.18f * tapC + 0.08f * tapA;
    }

    float writeL = inL;
    float writeR = inR;

    if (algo == 3) // Ping-Pong cross feedback
    {
        writeL += wetR * feedback;
        writeR += wetL * feedback;
    }
    else
    {
        writeL += wetL * feedback;
        writeR += wetR * feedback;
    }

    if (algo == 1 || algo == 2)
    {
        const auto sat = algo == 1 ? 0.85f : 1.05f;
        writeL = std::tanh(writeL * sat);
        writeR = std::tanh(writeR * sat);
    }

    isaacDelayBuffer[0][static_cast<std::size_t>(isaacWritePos)] = writeL;
    isaacDelayBuffer[1][static_cast<std::size_t>(isaacWritePos)] = writeR;
    isaacWritePos = (isaacWritePos + 1) % isaacBufferSize;

    const auto wetMix = lerp(0.06f, 0.86f, std::pow(a, 0.95f));
    const auto dryMix = 1.0f - wetMix * 0.88f;

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

void SynthProjectAudioProcessor::resizeReverbLine(ReverbDelayLine& line, int size)
{
    line.buffer.assign(static_cast<std::size_t>(juce::jmax(2, size)), 0.0f);
    line.writePos = 0;
    line.lpState = 0.0f;
}

float SynthProjectAudioProcessor::readReverbLine(const ReverbDelayLine& line, float delaySamples) const
{
    if (line.buffer.empty())
    {
        return 0.0f;
    }

    const auto size = static_cast<int>(line.buffer.size());
    auto readPos = static_cast<float>(line.writePos) - delaySamples;
    while (readPos < 0.0f)
    {
        readPos += static_cast<float>(size);
    }
    while (readPos >= static_cast<float>(size))
    {
        readPos -= static_cast<float>(size);
    }

    const auto i0 = static_cast<int>(readPos);
    const auto i1 = (i0 + 1) % size;
    const auto frac = readPos - static_cast<float>(i0);
    return line.buffer[static_cast<std::size_t>(i0)]
           + (line.buffer[static_cast<std::size_t>(i1)] - line.buffer[static_cast<std::size_t>(i0)]) * frac;
}

void SynthProjectAudioProcessor::writeReverbLine(ReverbDelayLine& line, float sample)
{
    if (line.buffer.empty())
    {
        return;
    }

    line.buffer[static_cast<std::size_t>(line.writePos)] = sample;
    line.writePos = (line.writePos + 1) % static_cast<int>(line.buffer.size());
}

float SynthProjectAudioProcessor::processReverbAllpass(ReverbDelayLine& line, float in, float delaySamples, float gain)
{
    const auto delayed = readReverbLine(line, delaySamples);
    const auto v = in - gain * delayed;
    writeReverbLine(line, v);
    return delayed + gain * v;
}

float SynthProjectAudioProcessor::processReverbDelay(ReverbDelayLine& line, float in, float delaySamples)
{
    const auto delayed = readReverbLine(line, delaySamples);
    writeReverbLine(line, in);
    return delayed;
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

    const auto mode = juce::jlimit(0, 3, algorithmIndex);
    const auto mix = smoothstep(amount);
    const auto size = juce::jlimit(0.0f, 1.0f, reverbSizeParam != nullptr ? reverbSizeParam->get() : 0.52f);
    const auto decayControl = juce::jlimit(0.0f, 1.0f, reverbDecayParam != nullptr ? reverbDecayParam->get() : 0.48f);
    const auto damping = juce::jlimit(0.0f, 1.0f, reverbDampingParam != nullptr ? reverbDampingParam->get() : 0.46f);
    const auto preDelay = juce::jlimit(0.0f, 1.0f, reverbPreDelayParam != nullptr ? reverbPreDelayParam->get() : 0.08f);
    const auto modDepth = juce::jlimit(0.0f, 1.0f, reverbModDepthParam != nullptr ? reverbModDepthParam->get() : 0.24f);
    const auto modRate = juce::jlimit(0.0f, 1.0f, reverbModRateParam != nullptr ? reverbModRateParam->get() : 0.18f);
    const auto width = juce::jlimit(0.0f, 1.0f, reverbWidthParam != nullptr ? reverbWidthParam->get() : 0.86f);
    const auto cloudFeedback = juce::jlimit(0.0f, 1.0f, reverbCloudFeedbackParam != nullptr ? reverbCloudFeedbackParam->get() : 0.62f);
    const auto cloudDiffusion = juce::jlimit(0.0f, 1.0f, reverbCloudDiffusionParam != nullptr ? reverbCloudDiffusionParam->get() : 0.54f);

    const auto predelaySamples = 1.0f + preDelay * static_cast<float>(currentSampleRateHz) * 0.30f;
    const auto inPredelayedL = processReverbDelay(reverbPreDelayLines[0], inL, predelaySamples);
    const auto inPredelayedR = processReverbDelay(reverbPreDelayLines[1], inR, predelaySamples);

    float wetL = 0.0f;
    float wetR = 0.0f;

    if (mode == 0)
    {
        juce::Reverb::Parameters p;
        p.freezeMode = 0.0f;
        p.roomSize = lerp(0.30f, 0.68f, size);
        p.damping = lerp(0.75f, 0.30f, damping);
        p.width = lerp(0.35f, 1.0f, width);
        p.wetLevel = 1.0f;
        p.dryLevel = 0.0f;
        reverb.setParameters(p);

        wetL = inPredelayedL;
        wetR = inPredelayedR;
        reverb.processStereo(&wetL, &wetR, 1);
    }
    else if (mode == 1)
    {
        const auto monoIn = 0.5f * (inPredelayedL + inPredelayedR);
        auto x = processReverbAllpass(plateLines[0], monoIn, lerp(160.0f, 520.0f, size), 0.72f);
        x = processReverbAllpass(plateLines[1], x, lerp(130.0f, 420.0f, size), 0.68f);

        const auto decay = juce::jlimit(0.0f, 0.95f, lerp(0.40f, 0.92f, decayControl));
        const auto dampCoeff = lerp(0.35f, 0.02f, damping);
        const auto modHz = lerp(0.08f, 1.4f, modRate);
        const auto modSamples = 2.0f + 14.0f * modDepth;

        const auto step = juce::MathConstants<float>::twoPi * modHz / static_cast<float>(juce::jmax(1.0, currentSampleRateHz));
        plateLines[4].modPhase += step;
        plateLines[5].modPhase += step * 1.13f;
        if (plateLines[4].modPhase > juce::MathConstants<float>::twoPi) plateLines[4].modPhase -= juce::MathConstants<float>::twoPi;
        if (plateLines[5].modPhase > juce::MathConstants<float>::twoPi) plateLines[5].modPhase -= juce::MathConstants<float>::twoPi;

        const auto tankInA = x + plateTankState[1] * decay;
        const auto tankInB = x + plateTankState[0] * decay;

        auto a = processReverbAllpass(plateLines[4], tankInA, lerp(380.0f, 980.0f, size) + std::sin(plateLines[4].modPhase) * modSamples, 0.62f);
        a = processReverbDelay(plateLines[2], a, lerp(1800.0f, 3900.0f, size));
        plateLines[2].lpState += dampCoeff * (a - plateLines[2].lpState);
        plateTankState[0] = plateLines[2].lpState;

        auto b = processReverbAllpass(plateLines[5], tankInB, lerp(420.0f, 1120.0f, size) + std::sin(plateLines[5].modPhase) * modSamples, 0.60f);
        b = processReverbDelay(plateLines[3], b, lerp(2100.0f, 4300.0f, size));
        plateLines[3].lpState += dampCoeff * (b - plateLines[3].lpState);
        plateTankState[1] = plateLines[3].lpState;

        wetL = 0.66f * plateTankState[0] + 0.34f * readReverbLine(plateLines[3], lerp(970.0f, 1740.0f, size));
        wetR = 0.66f * plateTankState[1] + 0.34f * readReverbLine(plateLines[2], lerp(1030.0f, 1830.0f, size));
    }
    else if (mode == 2)
    {
        const auto monoIn = 0.5f * (inPredelayedL + inPredelayedR);
        const auto decay = juce::jlimit(0.0f, 0.96f, lerp(0.45f, 0.95f, decayControl));
        const auto dampCoeff = lerp(0.28f, 0.015f, damping);
        const auto modHz = lerp(0.05f, 0.80f, modRate);
        const auto modSamples = 1.0f + 8.0f * modDepth;
        const auto modStep = juce::MathConstants<float>::twoPi * modHz / static_cast<float>(juce::jmax(1.0, currentSampleRateHz));

        float sum = 0.0f;
        for (auto& line : hallLines)
        {
            line.modPhase += modStep;
            if (line.modPhase > juce::MathConstants<float>::twoPi)
            {
                line.modPhase -= juce::MathConstants<float>::twoPi;
            }
        }

        for (int i = 0; i < 8; ++i)
        {
            const auto baseDelay = lerp(1200.0f + static_cast<float>(i) * 260.0f,
                                        5200.0f + static_cast<float>(i) * 540.0f,
                                        size);
            const auto mod = std::sin(hallLines[static_cast<std::size_t>(i)].modPhase + static_cast<float>(i) * 0.47f) * modSamples;
            const auto read = readReverbLine(hallLines[static_cast<std::size_t>(i)], baseDelay + mod);
            hallLines[static_cast<std::size_t>(i)].lpState += dampCoeff * (read - hallLines[static_cast<std::size_t>(i)].lpState);
            hallReadCache[static_cast<std::size_t>(i)] = hallLines[static_cast<std::size_t>(i)].lpState;
            sum += hallReadCache[static_cast<std::size_t>(i)];
        }

        const auto householder = 2.0f / 8.0f;
        for (int i = 0; i < 8; ++i)
        {
            const auto fb = (-hallReadCache[static_cast<std::size_t>(i)] + householder * sum) * decay;
            const auto inputInject = monoIn * (0.24f + 0.06f * static_cast<float>((i & 1) != 0));
            writeReverbLine(hallLines[static_cast<std::size_t>(i)], sanitizeAudioSample(inputInject + fb));
        }

        wetL = hallReadCache[0] * 0.27f + hallReadCache[2] * 0.23f + hallReadCache[5] * 0.20f + hallReadCache[7] * 0.18f;
        wetR = hallReadCache[1] * 0.27f + hallReadCache[3] * 0.23f + hallReadCache[4] * 0.20f + hallReadCache[6] * 0.18f;
    }
    else
    {
        const auto monoIn = 0.5f * (inPredelayedL + inPredelayedR);
        const auto decay = juce::jlimit(0.0f, 0.985f, lerp(0.55f, 0.985f, decayControl));
        const auto fb = juce::jlimit(0.0f, 0.985f, lerp(0.45f, 0.985f, cloudFeedback));
        const auto dampCoeff = lerp(0.22f, 0.010f, damping);
        const auto modHz = lerp(0.03f, 0.65f, modRate);
        const auto modSamples = 2.0f + 18.0f * modDepth;
        const auto modStep = juce::MathConstants<float>::twoPi * modHz / static_cast<float>(juce::jmax(1.0, currentSampleRateHz));

        for (int i = 0; i < 6; ++i)
        {
            auto& line = cloudLines[static_cast<std::size_t>(i)];
            line.modPhase += modStep * (1.0f + 0.11f * static_cast<float>(i));
            if (line.modPhase > juce::MathConstants<float>::twoPi)
            {
                line.modPhase -= juce::MathConstants<float>::twoPi;
            }

            const auto baseDelay = lerp(1800.0f + static_cast<float>(i) * 410.0f,
                                        9200.0f + static_cast<float>(i) * 960.0f,
                                        size);
            const auto mod = std::sin(line.modPhase + static_cast<float>(i) * 0.73f) * modSamples;
            const auto delayed = readReverbLine(line, baseDelay + mod);
            line.lpState += dampCoeff * (delayed - line.lpState);
            cloudReadCache[static_cast<std::size_t>(i)] = line.lpState;
        }

        for (int i = 0; i < 6; ++i)
        {
            const auto a = cloudReadCache[static_cast<std::size_t>((i + 1) % 6)];
            const auto b = cloudReadCache[static_cast<std::size_t>((i + 3) % 6)];
            const auto write = monoIn * 0.20f + (a * 0.58f + b * 0.42f) * fb * decay;
            writeReverbLine(cloudLines[static_cast<std::size_t>(i)], sanitizeAudioSample(write));
        }

        auto sumL = cloudReadCache[0] * 0.35f + cloudReadCache[2] * 0.28f + cloudReadCache[4] * 0.24f;
        auto sumR = cloudReadCache[1] * 0.35f + cloudReadCache[3] * 0.28f + cloudReadCache[5] * 0.24f;

        const auto diffAmt = juce::jlimit(0.0f, 1.0f, cloudDiffusion);
        sumL = processReverbAllpass(cloudLines[6], sumL, lerp(120.0f, 520.0f, diffAmt), 0.70f);
        sumR = processReverbAllpass(cloudLines[7], sumR, lerp(150.0f, 610.0f, diffAmt), 0.68f);

        wetL = sumL;
        wetR = sumR;
    }

    // Width/decorrelation stage shared by all modes.
    const auto mid = 0.5f * (wetL + wetR);
    const auto side = 0.5f * (wetL - wetR);
    const auto widthAmt = lerp(0.35f, 1.35f, width);
    wetL = mid + side * widthAmt;
    wetR = mid - side * widthAmt;

    const auto dryGain = 1.0f - mix * 0.88f;
    outL = sanitizeAudioSample(inL * dryGain + wetL * mix);
    outR = sanitizeAudioSample(inR * dryGain + wetR * mix);
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
    auto state = createParameterStateTree();

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

    juce::String ignoredError;
    applyParameterStateTree(state, &ignoredError);
}

juce::ValueTree SynthProjectAudioProcessor::createParameterStateTree() const
{
    juce::ValueTree state("PX3_STATE");

    for (auto* parameter : getParameters())
    {
        if (const auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
        {
            state.setProperty(ranged->getParameterID(), ranged->getValue(), nullptr);
        }
    }

    {
        const std::scoped_lock<std::mutex> lock(imageStateMutex);
        state.setProperty("imagePath", lastLoadedImagePath, nullptr);
        state.setProperty("audioPath", lastLoadedAudioPath, nullptr);
    }

    const auto fxOrder = getFxProcessingOrder();
    for (int i = 0; i < 3; ++i)
    {
        state.setProperty(juce::String("fxOrder") + juce::String(i),
                          fxOrder[static_cast<std::size_t>(i)],
                          nullptr);
    }

    return state;
}

bool SynthProjectAudioProcessor::applyParameterStateTree(const juce::ValueTree& state, juce::String* error)
{
    if (!state.isValid() || state.getType().toString() != "PX3_STATE")
    {
        if (error != nullptr)
        {
            *error = "State tree is invalid or has unexpected type.";
        }
        return false;
    }

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

    std::array<int, 3> fxOrderFromState { { 0, 1, 2 } };
    bool hasFxOrderState = false;
    for (int i = 0; i < 3; ++i)
    {
        const auto propertyName = juce::String("fxOrder") + juce::String(i);
        if (state.hasProperty(propertyName))
        {
            hasFxOrderState = true;
            fxOrderFromState[static_cast<std::size_t>(i)] = static_cast<int>(state[propertyName]);
        }
    }
    if (hasFxOrderState)
    {
        setFxProcessingOrder(fxOrderFromState);
    }
    else if (fxOrderSlot0Param != nullptr && fxOrderSlot1Param != nullptr && fxOrderSlot2Param != nullptr)
    {
        // Backward-compatible path: newer hosts/presets can restore from parameter slots.
        setFxProcessingOrder({ { fxOrderSlot0Param->get(), fxOrderSlot1Param->get(), fxOrderSlot2Param->get() } });
    }

    if (state.hasProperty("imagePath"))
    {
        const auto path = state["imagePath"].toString();
        {
            const std::scoped_lock<std::mutex> lock(imageStateMutex);
            lastLoadedImagePath = path;
        }

        if (path.isNotEmpty())
        {
            const juce::File file(path);
            if (file.existsAsFile())
            {
                requestImageLoadAsync(file);
            }
        }
    }

    if (state.hasProperty("audioPath"))
    {
        const auto path = state["audioPath"].toString();
        {
            const std::scoped_lock<std::mutex> lock(imageStateMutex);
            lastLoadedAudioPath = path;
        }

        if (path.isNotEmpty())
        {
            const juce::File file(path);
            if (file.existsAsFile())
            {
                requestAudioLoadAsync(file);
            }
        }
    }

    if (error != nullptr)
    {
        error->clear();
    }

    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SynthProjectAudioProcessor();
}
