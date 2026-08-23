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
                                                           "Reverb Algorithm",
                                                           juce::StringArray { "Hall", "Plate", "Room", "Cavern", "Moon" },
                                                           0);
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
                                                       juce::StringArray { "Harmonic Drive", "Granular Delay", "Reverb" },
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
    pitchBendRangeParam = new juce::AudioParameterInt("pitchBendRange",
                                                       "Pitch Bend Range",
                                                       1,
                                                       24,
                                                       2);

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
    addParameter(delayAlgorithmParam);
    addParameter(delayEnabledParam);
    addParameter(delayTimeParam);
    addParameter(delayFeedbackParam);
    addParameter(reverbAmountParam);
    addParameter(reverbEnabledParam);
    addParameter(reverbAlgorithmParam);
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
    addParameter(pitchBendRangeParam);

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
    const auto delayAlgorithmIndex = delayAlgorithmParam->getIndex();
    const auto delayEnabled = delayEnabledParam->get();
    const auto delayTimeControl = clamp01(delayTimeParam->get());
    const auto delayFeedbackControl = clamp01(delayFeedbackParam->get());
    const auto reverbAmountBase = clamp01(reverbAmountParam->get());
    const auto reverbEnabled = reverbEnabledParam->get();

    if (delayAlgorithmIndex != lastDelayAlgorithmIndex)
    {
        lastDelayAlgorithmIndex = delayAlgorithmIndex;
        isaacSpawnCounter = 0;
        isaacFeedbackFilter = { { 0.0f, 0.0f } };
        for (auto& grain : isaacGrains)
        {
            grain.active = false;
        }
    }
    const auto reverbAlgorithmIndex = reverbAlgorithmParam->getIndex();
    const auto imageTargetIndex = imageTargetParam->getIndex();

    const auto imageControlRaw = computeImageTargetControlSignal(currentImagePosition, buffer.getNumSamples());
    const auto controlSmoothingSec = 0.06f;
    const auto controlBlend = 1.0f - std::exp(-static_cast<float>(buffer.getNumSamples())
                                              / (static_cast<float>(juce::jmax(1.0, currentSampleRateHz)) * controlSmoothingSec));
    imageTargetControlSmoothed += (imageControlRaw - imageTargetControlSmoothed) * controlBlend;

    const auto imageScale = lerp(0.45f, 1.25f, imageTargetControlSmoothed);

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
                granularScaleTarget = imageScale;
                break;
            case 2:
                reverbScaleTarget = imageScale;
                break;
            default:
                break;
        }
    }

    const auto routeSmoothingSec = 0.09f;
    const auto routeBlend = 1.0f - std::exp(-static_cast<float>(buffer.getNumSamples())
                                            / (static_cast<float>(juce::jmax(1.0, currentSampleRateHz)) * routeSmoothingSec));
    imageDriveScaleSmoothed += (driveScaleTarget - imageDriveScaleSmoothed) * routeBlend;
    imageGranularScaleSmoothed += (granularScaleTarget - imageGranularScaleSmoothed) * routeBlend;
    imageReverbScaleSmoothed += (reverbScaleTarget - imageReverbScaleSmoothed) * routeBlend;

    const auto robAmount = clamp01(robAmountBase * imageDriveScaleSmoothed);
    const auto isaacAmount = clamp01(isaacAmountBase * imageGranularScaleSmoothed);
    const auto reverbAmount = clamp01(reverbAmountBase * imageReverbScaleSmoothed);

    if (reverbEnabled && reverbAmount > 0.0001f)
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

    double reverbPreEnergy = 0.0;
    double reverbPostEnergy = 0.0;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        auto inL = buffer.getSample(0, sample);
        auto inR = (buffer.getNumChannels() > 1) ? buffer.getSample(1, sample) : inL;

        if (robEnabled)
        {
            inL = processRobSample(inL, 0, robAmount, robModeIndex);
            inR = processRobSample(inR, 1, robAmount, robModeIndex);
        }

        float outL = inL;
        float outR = inR;
        if (delayEnabled)
        {
            processDelayAlgorithmSample(inL,
                            inR,
                            isaacAmount,
                            delayAlgorithmIndex,
                            delayTimeControl,
                            delayFeedbackControl,
                            syncDivisionIndex,
                            outL,
                            outR);
        }

        const auto reverbInL = outL;
        const auto reverbInR = outR;
        if (reverbEnabled)
        {
            processReverbSampleFrame(outL, outR, reverbAmount, reverbAlgorithmIndex, outL, outR);
        }

        reverbPreEnergy += 0.5 * (static_cast<double>(reverbInL) * static_cast<double>(reverbInL)
                      + static_cast<double>(reverbInR) * static_cast<double>(reverbInR));
        reverbPostEnergy += 0.5 * (static_cast<double>(outL) * static_cast<double>(outL)
                       + static_cast<double>(outR) * static_cast<double>(outR));

        buffer.setSample(0, sample, outL);
        if (buffer.getNumChannels() > 1)
        {
            buffer.setSample(1, sample, outR);
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

    isaacWritePos = 0;
    isaacSpawnCounter = 0;
    isaacPanPhase = 0.0f;
    delayModPhase = 0.0f;
    lastDelayAlgorithmIndex = -1;
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
        processIsaacGranularSample(inL, inR, amount, syncDivisionIndex, outL, outR);
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

    {
        const std::scoped_lock<std::mutex> lock(imageStateMutex);
        state.setProperty("imagePath", lastLoadedImagePath, nullptr);
        state.setProperty("audioPath", lastLoadedAudioPath, nullptr);
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
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SynthProjectAudioProcessor();
}
