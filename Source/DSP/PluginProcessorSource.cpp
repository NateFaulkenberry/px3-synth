#include "PluginProcessor.h"
#include "PluginProcessorInternals.h"

#include <memory>

// File role: image/audio source engines, async load jobs, and source-derived
// control/wavetable updates. Keep load/completion/reset flows here and avoid
// unrelated global processor orchestration logic.

using namespace px3::processor_internal;

namespace
{
class ImageLoadJob final : public juce::ThreadPoolJob
{
public:
    ImageLoadJob(PX3SynthAudioProcessor& ownerIn, juce::File fileIn, int serialIn)
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
    PX3SynthAudioProcessor& owner;
    juce::File file;
    int serial { 0 };
};

class AudioLoadJob final : public juce::ThreadPoolJob
{
public:
    AudioLoadJob(PX3SynthAudioProcessor& ownerIn, juce::File fileIn, int serialIn)
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
    PX3SynthAudioProcessor& owner;
    juce::File file;
    int serial { 0 };
};
}

//==============================================================================
// Source Engine Management
//==============================================================================
void PX3SynthAudioProcessor::requestImageLoadAsync(const juce::File& imageFile)
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

void PX3SynthAudioProcessor::disableImageEngine()
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

void PX3SynthAudioProcessor::resetImageEngine()
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

std::shared_ptr<ImageWavetable> PX3SynthAudioProcessor::buildImageWavetableFromImage(const juce::Image& sourceImage) const
{
    return createImageWavetableFromImage(sourceImage);
}

int PX3SynthAudioProcessor::getImageLoadRequestSerial() const
{
    return imageLoadRequestSerial.load(std::memory_order_relaxed);
}

void PX3SynthAudioProcessor::notifyImageLoadError()
{
    imageLoadErrorFlag.store(true, std::memory_order_relaxed);
}

void PX3SynthAudioProcessor::completeImageLoad(int serial,
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

bool PX3SynthAudioProcessor::copyImagePreview(juce::Image& imageOut) const
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

bool PX3SynthAudioProcessor::hasLoadedImage() const
{
    return imageLoadedFromDisk.load(std::memory_order_relaxed);
}

bool PX3SynthAudioProcessor::consumeImageLoadErrorFlag()
{
    return imageLoadErrorFlag.exchange(false, std::memory_order_relaxed);
}

float PX3SynthAudioProcessor::copyCurrentImagePosition() const
{
    return currentImagePositionNorm.load(std::memory_order_relaxed);
}

std::vector<float> PX3SynthAudioProcessor::copyCurrentImageWaveformPreview(int sampleCount) const
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

void PX3SynthAudioProcessor::requestAudioLoadAsync(const juce::File& audioFile)
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

void PX3SynthAudioProcessor::disableAudioEngine()
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

void PX3SynthAudioProcessor::resetAudioEngine()
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

void PX3SynthAudioProcessor::notifyAudioLoadError()
{
    audioLoadErrorFlag.store(true, std::memory_order_relaxed);
}

void PX3SynthAudioProcessor::completeAudioLoad(int serial,
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

bool PX3SynthAudioProcessor::consumeAudioLoadErrorFlag()
{
    return audioLoadErrorFlag.exchange(false, std::memory_order_relaxed);
}

bool PX3SynthAudioProcessor::copyCurrentAudioWaveformPreview(std::vector<float>& waveformOut) const
{
    const std::scoped_lock<std::mutex> lock(imageStateMutex);
    waveformOut = audioWaveformPreview;
    return !waveformOut.empty();
}

float PX3SynthAudioProcessor::copyCurrentAudioPosition() const
{
    return currentAudioPositionNorm.load(std::memory_order_relaxed);
}

bool PX3SynthAudioProcessor::hasLoadedAudio() const
{
    return audioLoadedFromDisk.load(std::memory_order_relaxed);
}

SubtractiveSettings PX3SynthAudioProcessor::currentSubtractiveSettings() const
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

OscillatorSettings PX3SynthAudioProcessor::currentOscillatorSettings() const
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

EnvelopeSettings PX3SynthAudioProcessor::currentEnvelopeSettings() const
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
float PX3SynthAudioProcessor::imageSyncBeatsForIndex(int index) const
{
    static constexpr std::array<float, 6> beatDivisions { 0.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f };
    const auto clamped = juce::jlimit(0, static_cast<int>(beatDivisions.size()) - 1, index);
    return beatDivisions[static_cast<std::size_t>(clamped)];
}

float PX3SynthAudioProcessor::audioSyncBeatsForIndex(int index) const
{
    static constexpr std::array<float, 6> beatDivisions { 0.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f };
    const auto clamped = juce::jlimit(0, static_cast<int>(beatDivisions.size()) - 1, index);
    return beatDivisions[static_cast<std::size_t>(clamped)];
}

float PX3SynthAudioProcessor::updateImageAnimationPosition(int samplesThisBlock)
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

float PX3SynthAudioProcessor::updateAudioAnimationPosition(int samplesThisBlock)
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

float PX3SynthAudioProcessor::computeImageTargetControlSignal(float imagePositionNorm, int samplesThisBlock)
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

std::shared_ptr<ImageWavetable> PX3SynthAudioProcessor::createDefaultImageWavetable() const
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

std::shared_ptr<ImageWavetable> PX3SynthAudioProcessor::createImageWavetableFromImage(const juce::Image& sourceImage) const
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

void PX3SynthAudioProcessor::installImageWavetable(std::shared_ptr<ImageWavetable> newTable, const juce::Image& sourcePreview)
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

void PX3SynthAudioProcessor::updateTransportState()
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
