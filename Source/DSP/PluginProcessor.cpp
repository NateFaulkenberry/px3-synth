#include "PluginProcessor.h"

#include "PluginEditor.h"
#include <cmath>
#include <cstring>
#include <memory>

namespace
{
constexpr double kMaxIsaacDelaySeconds = 4.0;
constexpr double kMaxMoonDelaySeconds = 0.35;
constexpr int kCurrentStateVersion = 4;

const juce::Identifier kStateTypeId("PX3_STATE");
const juce::Identifier kStateVersionId("stateVersion");
const juce::Identifier kModuleOrderId("MODULE_ORDER");
const juce::Identifier kModuleEntryId("MODULE");
const juce::Identifier kModuleIdProperty("id");
const juce::Identifier kModuleOrderRevisionId("moduleOrderRevision");
const juce::Identifier kLfoStateId("LFO");
const juce::Identifier kLfoFrequencyId("frequency");
const juce::Identifier kLfoAssignmentId("assignment");
const juce::Identifier kVibeStateId("VIBE");
const juce::Identifier kVibeBypassId("bypass");
const juce::Identifier kVibeSeedId("seed");
std::atomic<uint32_t> kInstanceCounter { 0u };

const juce::StringArray kVibeTypeChoices {
    "Warm",
    "Hot",
    "Cool",
    "Vintage",
    "Clean",
    "LoFi"
};

juce::String moduleIdForStage(int stage);

juce::String nowTimestamp()
{
    const auto now = juce::Time::getCurrentTime();
    const auto ms = static_cast<int>(juce::Time::getMillisecondCounter() % 1000u);
    return now.formatted("%H:%M:%S") + "." + juce::String(ms).paddedLeft('0', 3);
}

juce::String formatOrderString(const std::array<int, 3>& order)
{
    juce::StringArray items;
    for (const auto stage : order)
    {
        items.add(moduleIdForStage(stage));
    }
    return items.joinIntoString(",");
}

const std::array<juce::String, 3> kFxModuleIds { juce::String("harmonicDrive"),
                                                  juce::String("delay"),
                                                  juce::String("reverb") };

juce::String moduleIdForStage(int stage)
{
    const auto clamped = juce::jlimit(0, 2, stage);
    return kFxModuleIds[static_cast<std::size_t>(clamped)];
}

int stageForModuleId(const juce::String& moduleId)
{
    for (int stage = 0; stage < 3; ++stage)
    {
        if (moduleId.equalsIgnoreCase(kFxModuleIds[static_cast<std::size_t>(stage)]))
        {
            return stage;
        }
    }

    return -1;
}

int decodeLegacyStageValue(const juce::var& value)
{
    if (value.isDouble() || value.isInt() || value.isInt64() || value.isBool())
    {
        const auto asDouble = static_cast<double>(value);
        if (asDouble >= 0.0 && asDouble <= 1.0)
        {
            return juce::jlimit(0, 2, static_cast<int>(std::lround(asDouble * 2.0)));
        }

        return juce::jlimit(0, 2, static_cast<int>(std::lround(asDouble)));
    }

    return juce::jlimit(0, 2, static_cast<int>(std::lround(value.toString().getDoubleValue())));
}

uint32_t packFxOrder(const std::array<int, 3>& order)
{
    return (static_cast<uint32_t>(order[0] & 0x3)
            | (static_cast<uint32_t>(order[1] & 0x3) << 2)
            | (static_cast<uint32_t>(order[2] & 0x3) << 4));
}

std::array<int, 3> unpackFxOrder(uint32_t packed)
{
    return {
        { static_cast<int>(packed & 0x3u),
          static_cast<int>((packed >> 2) & 0x3u),
          static_cast<int>((packed >> 4) & 0x3u) }
    };
}

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

SynthProjectAudioProcessor::~SynthProjectAudioProcessor()
{
    debugLogEvent("LIFECYCLE", "PROCESSOR_DESTROYED",
                  "id=" + debugInstanceId + " order=" + debugDescribeOrder(getFxProcessingOrder()));
}

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
    const auto sr = static_cast<float>(juce::jmax(1.0, sampleRate));
    constexpr float delayControlTauSec = 0.008f;
    delayControlSmoothingCoeff = 1.0f - std::exp(-1.0f / (sr * delayControlTauSec));
    delayAmountSmoothed = clamp01(delayAmountParam->get());
    delayTimeControlSmoothed = clamp01(delayTimeParam->get());
    delayFeedbackControlSmoothed = clamp01(delayFeedbackParam->get());
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

float SynthProjectAudioProcessor::currentLfoSignalForBlock(int numSamples)
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

int SynthProjectAudioProcessor::sanitizeVibeTypeIndex(int typeIndex) const
{
    return juce::jlimit(0, juce::jmax(0, vibeTypeParam->choices.size() - 1), typeIndex);
}

void SynthProjectAudioProcessor::applyVibeTypeProfile(int typeIndex)
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

void SynthProjectAudioProcessor::updateVibeStateForBlock(int numSamples, float lfoSignal)
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

float SynthProjectAudioProcessor::debugGetVibeGlobalAmount() const
{
    return juce::jlimit(0.0f, 1.0f, vibeAmountParam->get());
}

float SynthProjectAudioProcessor::debugGetVibeEffectiveAmount() const
{
    return juce::jlimit(0.0f, 1.0f, vibeEngine.getEffectiveAmount());
}

bool SynthProjectAudioProcessor::debugGetVibeBypass() const
{
    return !vibeEnabledParam->get();
}

uint32_t SynthProjectAudioProcessor::debugGetVibeSeed() const
{
    return vibeSeed.load(std::memory_order_relaxed);
}

VibeTuning SynthProjectAudioProcessor::debugGetVibeTuning() const
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

void SynthProjectAudioProcessor::debugSetVibeBypass(bool shouldBypass)
{
    const auto enabledValue = shouldBypass ? 0.0f : 1.0f;
    vibeEnabledParam->setValueNotifyingHost(enabledValue);
}

void SynthProjectAudioProcessor::debugSetVibeSeed(uint32_t seed)
{
    vibeSeed.store(seed == 0u ? 1u : seed, std::memory_order_relaxed);
}

void SynthProjectAudioProcessor::debugSetVibeTuningValue(const juce::String& key, float value)
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

float SynthProjectAudioProcessor::debugGetVibeTuningValue(const juce::String& key) const
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

float SynthProjectAudioProcessor::applyLfoToNormalizedValue(juce::RangedAudioParameter* parameter,
                                                             float baseNormalized,
                                                             float lfoSignal,
                                                             float* outBaseNormalized,
                                                             float* outEffectiveNormalized) const
{
    // `base` is the host-visible parameter value (automation/presets/state).
    // `effective` is the transient DSP value after modulation. We never write
    // `effective` back into parameters so DAW automation lanes stay deterministic.
    const auto base = clamp01(baseNormalized);
    auto effective = base;

    if (parameter == nullptr)
    {
        if (outBaseNormalized != nullptr)
        {
            *outBaseNormalized = base;
        }
        if (outEffectiveNormalized != nullptr)
        {
            *outEffectiveNormalized = effective;
        }
        return effective;
    }

    const auto assignment = juce::jlimit(0,
                                         juce::jmax(0, static_cast<int>(lfoAssignableTargets.size()) - 1),
                                         lfoAssignmentIndex.load(std::memory_order_relaxed));

    if (assignment > 0 && assignment < static_cast<int>(lfoAssignableTargets.size()))
    {
        const auto& target = lfoAssignableTargets[static_cast<std::size_t>(assignment)];
        const auto sameId = target.parameterId.equalsIgnoreCase(parameter->getParameterID());
        const auto samePointer = (target.parameter == parameter);
        if (sameId || samePointer)
        {
            effective = clamp01(base + target.normalizedDepth * lfoSignal);
        }
    }

    if (outBaseNormalized != nullptr)
    {
        *outBaseNormalized = base;
    }
    if (outEffectiveNormalized != nullptr)
    {
        *outEffectiveNormalized = effective;
    }

    return effective;
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
juce::AudioParameterFloat& SynthProjectAudioProcessor::getVibeAmountParam() const { return *vibeAmountParam; }
juce::AudioParameterBool& SynthProjectAudioProcessor::getVibeEnabledParam() const { return *vibeEnabledParam; }
juce::AudioParameterChoice& SynthProjectAudioProcessor::getVibeTypeParam() const { return *vibeTypeParam; }
juce::AudioParameterFloat& SynthProjectAudioProcessor::getDelayAmountParam() const { return *delayAmountParam; }
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
juce::AudioParameterFloat& SynthProjectAudioProcessor::getLfoFrequencyParam() const { return *lfoFrequencyParam; }

const juce::StringArray& SynthProjectAudioProcessor::getLfoAssignmentDisplayNames() const
{
    return lfoAssignmentDisplayNames;
}

int SynthProjectAudioProcessor::getLfoAssignmentIndex() const
{
    return juce::jlimit(0,
                        juce::jmax(0, static_cast<int>(lfoAssignableTargets.size()) - 1),
                        lfoAssignmentIndex.load(std::memory_order_relaxed));
}

juce::String SynthProjectAudioProcessor::getLfoAssignmentParameterId() const
{
    const auto index = getLfoAssignmentIndex();
    if (index <= 0 || index >= static_cast<int>(lfoAssignableTargets.size()))
    {
        return "none";
    }

    return lfoAssignableTargets[static_cast<std::size_t>(index)].parameterId;
}

bool SynthProjectAudioProcessor::setLfoAssignmentIndex(int index, bool notifyHost)
{
    if (lfoAssignableTargets.empty())
    {
        lfoAssignmentIndex.store(0, std::memory_order_relaxed);
        return false;
    }

    const auto clamped = juce::jlimit(0,
                                      static_cast<int>(lfoAssignableTargets.size()) - 1,
                                      index);
    lfoAssignmentIndex.store(clamped, std::memory_order_relaxed);

    if (notifyHost)
    {
        updateHostDisplay();
        updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
    }

    debugLogEvent("LFO",
                  "ASSIGNMENT_CHANGED",
                  "index=" + juce::String(clamped)
                      + " id=" + getLfoAssignmentParameterId());
    return true;
}

bool SynthProjectAudioProcessor::setLfoAssignmentByParameterId(const juce::String& parameterId, bool notifyHost)
{
    if (parameterId.isEmpty() || parameterId.equalsIgnoreCase("none"))
    {
        return setLfoAssignmentIndex(0, notifyHost);
    }

    for (int i = 0; i < static_cast<int>(lfoAssignableTargets.size()); ++i)
    {
        if (lfoAssignableTargets[static_cast<std::size_t>(i)].parameterId.equalsIgnoreCase(parameterId))
        {
            return setLfoAssignmentIndex(i, notifyHost);
        }
    }

    return false;
}

void SynthProjectAudioProcessor::buildLfoAssignableTargets()
{
    // This list is the authoritative mapping between UI assignment index and
    // processor parameter targets. It is built from existing float parameters
    // so new automatable controls become assignable without custom plumbing.
    lfoAssignableTargets.clear();
    lfoAssignmentDisplayNames.clear();

    lfoAssignableTargets.push_back({ "none", "None", nullptr, 0.0f });
    lfoAssignmentDisplayNames.add("None");

    const auto shouldExclude = [](const juce::String& id)
    {
        // Exclude controls that define modulation behavior itself, rather than
        // being destinations of modulation.
        return id.equalsIgnoreCase("lfoFrequency")
               || id.equalsIgnoreCase("pitchBendRange");
    };

    for (auto* parameter : getParameters())
    {
        auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(parameter);
        if (floatParam == nullptr)
        {
            continue;
        }

        const auto id = floatParam->getParameterID();
        if (shouldExclude(id))
        {
            continue;
        }

        lfoAssignableTargets.push_back({ id,
                                         floatParam->getName(64),
                                         floatParam,
                                         lfoDepthForParameterId(id) });

        lfoAssignmentDisplayNames.add(floatParam->getName(64));
    }

    lfoAssignmentIndex.store(0, std::memory_order_relaxed);
}

float SynthProjectAudioProcessor::lfoDepthForParameterId(const juce::String& parameterId) const
{
    // Depths are intentionally conservative by default to avoid abrupt jumps on
    // sensitive controls. Specific musical targets get tuned overrides.
    if (parameterId.equalsIgnoreCase("masterGain"))
    {
        return 0.30f;
    }

    if (parameterId.equalsIgnoreCase("filterResonance")
        || parameterId.equalsIgnoreCase("delayFeedback")
        || parameterId.equalsIgnoreCase("reverbAmount"))
    {
        return 0.12f;
    }

    if (parameterId.equalsIgnoreCase("filterCutoff")
        || parameterId.equalsIgnoreCase("delayTime")
        || parameterId.equalsIgnoreCase("reverbDecay"))
    {
        return 0.22f;
    }

    return 0.10f;
}

std::array<int, 3> SynthProjectAudioProcessor::getFxProcessingOrder() const
{
    // Stored order is packed atomically; sanitize on read so malformed legacy or
    // duplicate values always recover to a valid permutation.
    const auto packed = fxProcessingOrderPacked.load(std::memory_order_relaxed);
    const auto raw = unpackFxOrder(packed);

    std::array<int, 3> sanitized { { 0, 1, 2 } };
    std::array<bool, 3> seen { { false, false, false } };

    int write = 0;
    for (int i = 0; i < 3; ++i)
    {
        const auto stage = juce::jlimit(0, 2, raw[static_cast<std::size_t>(i)]);
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
    setFxProcessingOrderWithReason(order, "UNKNOWN", "UNSPECIFIED", -1, -1);
}

void SynthProjectAudioProcessor::setFxProcessingOrderWithReason(const std::array<int, 3>& order,
                                                                const juce::String& source,
                                                                const juce::String& reason,
                                                                int fromIndex,
                                                                int toIndex)
{
    // Authoritative module order lives in the processor (not UI). UI drag-drop
    // requests are sanitized and committed here so DSP, state save, and debug
    // diagnostics all observe the same canonical order.
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

    const auto packed = packFxOrder(sanitized);
    const auto previous = fxProcessingOrderPacked.load(std::memory_order_relaxed);
    if (packed == previous)
    {
        return;
    }

    const auto beforeOrder = getFxProcessingOrder();
    const auto oldRevision = fxOrderRevision.load(std::memory_order_relaxed);
    fxProcessingOrderPacked.store(packed, std::memory_order_relaxed);
    const auto newRevision = fxOrderRevision.fetch_add(1u, std::memory_order_relaxed) + 1u;
    const auto afterOrder = getFxProcessingOrder();

    debugLogEvent(source,
                  "MODULE_ORDER_CHANGED",
                  "reason=" + reason
                      + " fromIndex=" + juce::String(fromIndex)
                      + " toIndex=" + juce::String(toIndex)
                      + " oldOrder=" + debugDescribeOrder(beforeOrder)
                      + " newOrder=" + debugDescribeOrder(afterOrder)
                      + " oldHash=" + juce::String(static_cast<int64_t>(previous))
                      + " newHash=" + juce::String(static_cast<int64_t>(packed))
                      + " gen=" + juce::String(static_cast<int64_t>(oldRevision))
                      + "->" + juce::String(static_cast<int64_t>(newRevision)));

    // Module order is part of plugin state but not an automatable parameter.
    // Explicit host notifications ensure project dirty-state and save prompts
    // stay accurate after UI drag reorder operations.
    updateHostDisplay();
    updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
    updateHostDisplay(juce::AudioProcessor::ChangeDetails().withProgramChanged(true));
}

juce::String SynthProjectAudioProcessor::debugGetInstanceId() const
{
    return debugInstanceId;
}

juce::String SynthProjectAudioProcessor::debugGetProcessorCreatedTime() const
{
    return debugProcessorCreatedTime;
}

juce::String SynthProjectAudioProcessor::debugNowTimestamp() const
{
    return nowTimestamp();
}

void SynthProjectAudioProcessor::debugNotifyEditorCreated(void* editorPtr)
{
    debugLogEvent("LIFECYCLE",
                  "EDITOR_CREATED",
                  "editor=" + juce::String::toHexString(reinterpret_cast<juce::int64>(editorPtr))
                      + " order=" + debugDescribeOrder(getFxProcessingOrder()));
}

void SynthProjectAudioProcessor::debugNotifyEditorDestroyed(void* editorPtr)
{
    debugLogEvent("LIFECYCLE",
                  "EDITOR_DESTROYED",
                  "editor=" + juce::String::toHexString(reinterpret_cast<juce::int64>(editorPtr))
                      + " order=" + debugDescribeOrder(getFxProcessingOrder()));
}

void SynthProjectAudioProcessor::debugLogEvent(const juce::String& source,
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

void SynthProjectAudioProcessor::debugClearEventLog()
{
    const std::scoped_lock<std::mutex> lock(debugStateMutex);
    debugEventLogLines.clear();
}

juce::String SynthProjectAudioProcessor::debugGetEventLogText() const
{
    const std::scoped_lock<std::mutex> lock(debugStateMutex);
    return debugEventLogLines.joinIntoString("\n");
}

int SynthProjectAudioProcessor::debugGetLastSerializedStateSize() const
{
    const std::scoped_lock<std::mutex> lock(debugStateMutex);
    return static_cast<int>(debugLastSerializedState.getSize());
}

juce::String SynthProjectAudioProcessor::debugGetLastSerializedStateXml() const
{
    const std::scoped_lock<std::mutex> lock(debugStateMutex);
    return debugLastSerializedStateXml;
}

juce::MemoryBlock SynthProjectAudioProcessor::debugGetLastSerializedStateCopy() const
{
    const std::scoped_lock<std::mutex> lock(debugStateMutex);
    return debugLastSerializedState;
}

bool SynthProjectAudioProcessor::debugRestoreLastSerializedState(juce::String& report)
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

bool SynthProjectAudioProcessor::debugRoundTripCurrentState(juce::String& report)
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

uint32_t SynthProjectAudioProcessor::debugGetModuleOrderGeneration() const
{
    return fxOrderRevision.load(std::memory_order_relaxed);
}

uint32_t SynthProjectAudioProcessor::debugGetModuleOrderHash() const
{
    return fxProcessingOrderPacked.load(std::memory_order_relaxed);
}

juce::String SynthProjectAudioProcessor::debugDescribeOrder(const std::array<int, 3>& order) const
{
    return formatOrderString(order);
}

float SynthProjectAudioProcessor::debugGetLfoPhase() const
{
    const auto wrapped = std::fmod(lfoPhaseForDebug.load(std::memory_order_relaxed), juce::MathConstants<float>::twoPi);
    return wrapped < 0.0f ? wrapped + juce::MathConstants<float>::twoPi : wrapped;
}

float SynthProjectAudioProcessor::debugGetLfoCurrentValue() const
{
    return lfoCurrentValue.load(std::memory_order_relaxed);
}

float SynthProjectAudioProcessor::debugGetLfoBaseNormalized() const
{
    return lfoDebugBaseNormalized.load(std::memory_order_relaxed);
}

float SynthProjectAudioProcessor::debugGetLfoEffectiveNormalized() const
{
    return lfoDebugEffectiveNormalized.load(std::memory_order_relaxed);
}

juce::String SynthProjectAudioProcessor::debugGetLfoAssignmentName() const
{
    const auto index = getLfoAssignmentIndex();
    if (index <= 0 || index >= static_cast<int>(lfoAssignableTargets.size()))
    {
        return "None";
    }

    return lfoAssignableTargets[static_cast<std::size_t>(index)].displayName
        + " [" + lfoAssignableTargets[static_cast<std::size_t>(index)].parameterId + "]";
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
    const auto lfoSignal = lfoCurrentValue.load(std::memory_order_relaxed);
    SubtractiveSettings settings;
    settings.sineMix = oscSineParam->convertFrom0to1(applyLfoToNormalizedValue(oscSineParam,
                                                                                static_cast<juce::RangedAudioParameter*>(oscSineParam)->getValue(),
                                                                                lfoSignal));
    settings.sawMix = oscSawParam->convertFrom0to1(applyLfoToNormalizedValue(oscSawParam,
                                                                              static_cast<juce::RangedAudioParameter*>(oscSawParam)->getValue(),
                                                                              lfoSignal));
    settings.squareMix = oscSquareParam->convertFrom0to1(applyLfoToNormalizedValue(oscSquareParam,
                                                                                    static_cast<juce::RangedAudioParameter*>(oscSquareParam)->getValue(),
                                                                                    lfoSignal));
    settings.imageMix = 0.35f;
    settings.filterCutoffHz = filterCutoffParam->convertFrom0to1(applyLfoToNormalizedValue(filterCutoffParam,
                                                                                            static_cast<juce::RangedAudioParameter*>(filterCutoffParam)->getValue(),
                                                                                            lfoSignal));
    settings.filterResonanceQ = filterResonanceParam->convertFrom0to1(applyLfoToNormalizedValue(filterResonanceParam,
                                                                                                 static_cast<juce::RangedAudioParameter*>(filterResonanceParam)->getValue(),
                                                                                                 lfoSignal));
    settings.filterTypeIndex = filterTypeParam->getIndex();
    settings.masterGain = masterGainParam->convertFrom0to1(applyLfoToNormalizedValue(masterGainParam,
                                                                                      static_cast<juce::RangedAudioParameter*>(masterGainParam)->getValue(),
                                                                                      lfoSignal));
    return settings;
}

OscillatorSettings SynthProjectAudioProcessor::currentOscillatorSettings() const
{
    const auto lfoSignal = lfoCurrentValue.load(std::memory_order_relaxed);
    OscillatorSettings settings;
    settings.modeIndex = oscModeParam->getIndex();
    settings.macroA = clamp01(oscMacroAParam->convertFrom0to1(applyLfoToNormalizedValue(oscMacroAParam,
                                                                                          static_cast<juce::RangedAudioParameter*>(oscMacroAParam)->getValue(),
                                                                                          lfoSignal)));
    settings.macroB = clamp01(oscMacroBParam->convertFrom0to1(applyLfoToNormalizedValue(oscMacroBParam,
                                                                                          static_cast<juce::RangedAudioParameter*>(oscMacroBParam)->getValue(),
                                                                                          lfoSignal)));
    settings.macroC = clamp01(oscMacroCParam->convertFrom0to1(applyLfoToNormalizedValue(oscMacroCParam,
                                                                                          static_cast<juce::RangedAudioParameter*>(oscMacroCParam)->getValue(),
                                                                                          lfoSignal)));
    settings.vowelIndex = oscVowelParam->getIndex();
    for (std::size_t i = 0; i < settings.harmonics.size(); ++i)
    {
        if (oscHarmonicParams[i] != nullptr)
        {
            settings.harmonics[i] = clamp01(oscHarmonicParams[i]->convertFrom0to1(
                applyLfoToNormalizedValue(oscHarmonicParams[i],
                                          static_cast<juce::RangedAudioParameter*>(oscHarmonicParams[i])->getValue(),
                                          lfoSignal)));
        }
    }
    return settings;
}

EnvelopeSettings SynthProjectAudioProcessor::currentEnvelopeSettings() const
{
    const auto lfoSignal = lfoCurrentValue.load(std::memory_order_relaxed);
    EnvelopeSettings settings;
    settings.attackSeconds = attackParam->convertFrom0to1(applyLfoToNormalizedValue(attackParam,
                                                                                      static_cast<juce::RangedAudioParameter*>(attackParam)->getValue(),
                                                                                      lfoSignal));
    settings.decaySeconds = decayParam->convertFrom0to1(applyLfoToNormalizedValue(decayParam,
                                                                                    static_cast<juce::RangedAudioParameter*>(decayParam)->getValue(),
                                                                                    lfoSignal));
    settings.sustainLevel = sustainParam->convertFrom0to1(applyLfoToNormalizedValue(sustainParam,
                                                                                      static_cast<juce::RangedAudioParameter*>(sustainParam)->getValue(),
                                                                                      lfoSignal));
    settings.releaseSeconds = releaseParam->convertFrom0to1(applyLfoToNormalizedValue(releaseParam,
                                                                                        static_cast<juce::RangedAudioParameter*>(releaseParam)->getValue(),
                                                                                        lfoSignal));
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
    const auto lfoSignal = lfoCurrentValue.load(std::memory_order_relaxed);
    const auto basePos = applyLfoToNormalizedValue(imagePositionParam,
                                                   static_cast<juce::RangedAudioParameter*>(imagePositionParam)->getValue(),
                                                   lfoSignal);
    const auto animAmount = applyLfoToNormalizedValue(imageAnimateParam,
                                                      static_cast<juce::RangedAudioParameter*>(imageAnimateParam)->getValue(),
                                                      lfoSignal);

    if (animAmount <= 0.0001f)
    {
        currentImagePositionNorm.store(basePos, std::memory_order_relaxed);
        return basePos;
    }

    const auto sec = static_cast<float>(samplesThisBlock) / static_cast<float>(juce::jmax(1.0, currentSampleRateHz));
    auto rateHz = imageRateParam->convertFrom0to1(applyLfoToNormalizedValue(imageRateParam,
                                                                            static_cast<juce::RangedAudioParameter*>(imageRateParam)->getValue(),
                                                                            lfoSignal));
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
    const auto lfoSignal = lfoCurrentValue.load(std::memory_order_relaxed);
    const auto basePos = applyLfoToNormalizedValue(audioPositionParam,
                                                   static_cast<juce::RangedAudioParameter*>(audioPositionParam)->getValue(),
                                                   lfoSignal);
    const auto animAmount = applyLfoToNormalizedValue(audioAnimateParam,
                                                      static_cast<juce::RangedAudioParameter*>(audioAnimateParam)->getValue(),
                                                      lfoSignal);

    if (animAmount <= 0.0001f)
    {
        currentAudioPositionNorm.store(basePos, std::memory_order_relaxed);
        return basePos;
    }

    const auto sec = static_cast<float>(samplesThisBlock) / static_cast<float>(juce::jmax(1.0, currentSampleRateHz));
    auto rateHz = audioRateParam->convertFrom0to1(applyLfoToNormalizedValue(audioRateParam,
                                                                            static_cast<juce::RangedAudioParameter*>(audioRateParam)->getValue(),
                                                                            lfoSignal));
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
    const auto lfoSignal = lfoCurrentValue.load(std::memory_order_relaxed);
    const auto imageRateNorm = applyLfoToNormalizedValue(imageRateParam,
                                                         static_cast<juce::RangedAudioParameter*>(imageRateParam)->getValue(),
                                                         lfoSignal);
    const auto scanRateHz = lerp(0.35f, 6.0f, juce::jlimit(0.0f, 1.0f, imageRateNorm * 0.25f));
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
    // Granular delay summary:
    // - Input is written into a circular delay buffer.
    // - Short grains are spawned from delayed positions (mode-dependent).
    // - Grain output is diffused/filtered/saturated, then mixed with dry signal.
    // This all runs sample-by-sample in the audio thread, so no allocations.
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
        // Rhythmic mode uses a step-trigger pattern with optional swing to
        // produce tempo-locked, repeatable phrase-like textures.
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
        // Non-rhythmic modes use density-based spawning intervals; sync mode
        // still quantizes intervals to beat subdivisions when requested.
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

    // Diffusion broadens grain clouds and decorrelates channels, helping avoid
    // narrow comb-like artifacts at higher feedback values.
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
    const auto coeff = juce::jlimit(0.0001f, 1.0f, delayControlSmoothingCoeff);
    delayAmountSmoothed += coeff * (clamp01(amount) - delayAmountSmoothed);
    delayTimeControlSmoothed += coeff * (clamp01(timeControl) - delayTimeControlSmoothed);
    delayFeedbackControlSmoothed += coeff * (clamp01(feedbackControl) - delayFeedbackControlSmoothed);

    const auto amountSmooth = clamp01(delayAmountSmoothed);
    const auto timeControlSmooth = clamp01(delayTimeControlSmoothed);
    const auto feedbackControlSmooth = clamp01(delayFeedbackControlSmoothed);

    // All delay algorithms share one circular memory line for coherence across
    // mode changes; each algorithm then colors/modulates reads differently.
    const auto algo = juce::jlimit(0, 6, algorithmIndex);

    if (algo == 0)
    {
        processIsaacGranularSample(inL,
                                   inR,
                                   amountSmooth,
                                   timeControlSmooth,
                                   feedbackControlSmooth,
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

    const auto a = smoothstep(amountSmooth);
    const auto secPerBeat = static_cast<float>(60.0 / currentBpm);
    float baseDelaySec = lerp(0.04f, 1.25f, timeControlSmooth);
    const auto syncBeats = divisionBeatsForIndex(syncDivisionIndex);
    if (syncDivisionIndex > 0 && syncBeats > 0.0f)
    {
        baseDelaySec = secPerBeat * syncBeats;
    }

    auto feedback = juce::jlimit(0.0f, 0.95f, lerp(0.05f, 0.92f, feedbackControlSmooth));
    if (algo == 2) // BBD can get unstable quickly; keep a stricter cap
    {
        feedback = juce::jmin(feedback, 0.82f);
    }

    delayModPhase += juce::MathConstants<float>::twoPi * (0.18f + 0.65f * timeControlSmooth)
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
                                     lerp(0.02f, 0.55f, timeControlSmooth) * static_cast<float>(currentSampleRateHz));
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
    // Reverb modes intentionally mix classic algorithm families:
    // room (JUCE reverb), plate-style tank, and hall-style multi-line network.
    // Parameters are base values with optional LFO modulation applied at read time.
    if (amount <= 0.0001f)
    {
        outL = inL;
        outR = inR;
        return;
    }

    const auto lfoSignal = lfoCurrentValue.load(std::memory_order_relaxed);
    const auto mode = juce::jlimit(0, 3, algorithmIndex);
    const auto mix = smoothstep(amount);
    const auto size = reverbSizeParam != nullptr
                          ? applyLfoToNormalizedValue(reverbSizeParam,
                                                      static_cast<juce::RangedAudioParameter*>(reverbSizeParam)->getValue(),
                                                      lfoSignal)
                          : 0.52f;
    const auto decayControl = reverbDecayParam != nullptr
                                  ? applyLfoToNormalizedValue(reverbDecayParam,
                                                              static_cast<juce::RangedAudioParameter*>(reverbDecayParam)->getValue(),
                                                              lfoSignal)
                                  : 0.48f;
    const auto damping = reverbDampingParam != nullptr
                             ? applyLfoToNormalizedValue(reverbDampingParam,
                                                         static_cast<juce::RangedAudioParameter*>(reverbDampingParam)->getValue(),
                                                         lfoSignal)
                             : 0.46f;
    const auto preDelay = reverbPreDelayParam != nullptr
                              ? applyLfoToNormalizedValue(reverbPreDelayParam,
                                                          static_cast<juce::RangedAudioParameter*>(reverbPreDelayParam)->getValue(),
                                                          lfoSignal)
                              : 0.08f;
    const auto modDepth = reverbModDepthParam != nullptr
                              ? applyLfoToNormalizedValue(reverbModDepthParam,
                                                          static_cast<juce::RangedAudioParameter*>(reverbModDepthParam)->getValue(),
                                                          lfoSignal)
                              : 0.24f;
    const auto modRate = reverbModRateParam != nullptr
                             ? applyLfoToNormalizedValue(reverbModRateParam,
                                                         static_cast<juce::RangedAudioParameter*>(reverbModRateParam)->getValue(),
                                                         lfoSignal)
                             : 0.18f;
    const auto width = reverbWidthParam != nullptr
                           ? applyLfoToNormalizedValue(reverbWidthParam,
                                                       static_cast<juce::RangedAudioParameter*>(reverbWidthParam)->getValue(),
                                                       lfoSignal)
                           : 0.86f;
    const auto cloudFeedback = reverbCloudFeedbackParam != nullptr
                                   ? applyLfoToNormalizedValue(reverbCloudFeedbackParam,
                                                               static_cast<juce::RangedAudioParameter*>(reverbCloudFeedbackParam)->getValue(),
                                                               lfoSignal)
                                   : 0.62f;
    const auto cloudDiffusion = reverbCloudDiffusionParam != nullptr
                                    ? applyLfoToNormalizedValue(reverbCloudDiffusionParam,
                                                                static_cast<juce::RangedAudioParameter*>(reverbCloudDiffusionParam)->getValue(),
                                                                lfoSignal)
                                    : 0.54f;

    // Predelay separates dry transient from wet onset, improving clarity.
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
    debugLogEvent("LIFECYCLE", "CREATE_EDITOR", "order=" + debugDescribeOrder(getFxProcessingOrder()));
    return new SynthProjectAudioProcessorEditor(*this);
}

void SynthProjectAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
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

void SynthProjectAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
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

juce::ValueTree SynthProjectAudioProcessor::createParameterStateTree() const
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

    {
        const std::scoped_lock<std::mutex> lock(imageStateMutex);
        state.setProperty("imagePath", lastLoadedImagePath, nullptr);
        state.setProperty("audioPath", lastLoadedAudioPath, nullptr);
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

    // Keep LFO settings in a dedicated node for backward-compatible evolution.
    juce::ValueTree lfoState(kLfoStateId);
    lfoState.setProperty(kLfoFrequencyId, lfoFrequencyParam->get(), nullptr);
    lfoState.setProperty(kLfoAssignmentId, getLfoAssignmentParameterId(), nullptr);
    state.addChild(lfoState, -1, nullptr);

    juce::ValueTree vibeState(kVibeStateId);
    vibeState.setProperty(kVibeBypassId, debugGetVibeBypass(), nullptr);
    vibeState.setProperty(kVibeSeedId, static_cast<int64_t>(debugGetVibeSeed()), nullptr);
    state.addChild(vibeState, -1, nullptr);

    return state;
}

bool SynthProjectAudioProcessor::applyParameterStateTree(const juce::ValueTree& state, juce::String* error)
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

    std::array<int, 3> fxOrderFromState { { 0, 1, 2 } };
    auto hasModuleOrder = false;
    auto moduleOrderSource = juce::String("none");

    if (const auto moduleOrder = state.getChildWithName(kModuleOrderId); moduleOrder.isValid())
    {
        std::array<bool, 3> seen { { false, false, false } };
        int write = 0;

        for (int i = 0; i < moduleOrder.getNumChildren() && write < 3; ++i)
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
            for (int stage = 0; stage < 3 && write < 3; ++stage)
            {
                if (!seen[static_cast<std::size_t>(stage)])
                {
                    fxOrderFromState[static_cast<std::size_t>(write++)] = stage;
                }
            }
        }
    }

    if (!hasModuleOrder)
    {
        // Backward compatibility: old states serialized order as flat integer properties.
        std::array<int, 3> legacyOrder { { 0, 1, 2 } };
        auto hasLegacyOrder = false;
        for (int i = 0; i < 3; ++i)
        {
            const auto propertyName = juce::String("fxOrder") + juce::String(i);
            if (state.hasProperty(propertyName))
            {
                hasLegacyOrder = true;
                legacyOrder[static_cast<std::size_t>(i)] = decodeLegacyStageValue(state[propertyName]);
            }
        }

        // Older builds also exposed hidden slot parameters; parse if present.
        if (!hasLegacyOrder && state.hasProperty("fxOrderSlot0") && state.hasProperty("fxOrderSlot1") && state.hasProperty("fxOrderSlot2"))
        {
            hasLegacyOrder = true;
            legacyOrder = {
                { decodeLegacyStageValue(state["fxOrderSlot0"]),
                  decodeLegacyStageValue(state["fxOrderSlot1"]),
                  decodeLegacyStageValue(state["fxOrderSlot2"]) }
            };
        }

        if (hasLegacyOrder)
        {
            hasModuleOrder = true;
            fxOrderFromState = legacyOrder;
            moduleOrderSource = "legacy";
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
        setFxProcessingOrderWithReason({ { 0, 1, 2 } }, "HOST", "STATE_RESTORE_DEFAULT", -1, -1);
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

    if (const auto lfoState = state.getChildWithName(kLfoStateId); lfoState.isValid())
    {
        if (lfoState.hasProperty(kLfoFrequencyId))
        {
            const auto frequency = juce::jlimit(0.01f, 20.0f, static_cast<float>(lfoState[kLfoFrequencyId]));
            lfoFrequencyParam->setValueNotifyingHost(lfoFrequencyParam->convertTo0to1(frequency));
        }

        if (lfoState.hasProperty(kLfoAssignmentId))
        {
            setLfoAssignmentByParameterId(lfoState[kLfoAssignmentId].toString(), false);
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

    applyVibeTypeProfile(vibeTypeParam->getIndex());

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
