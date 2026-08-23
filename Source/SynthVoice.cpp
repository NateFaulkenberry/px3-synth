#include "SynthVoice.h"

#include "SynthSound.h"

#include <cmath>

namespace
{
inline float clamp01(float v)
{
    return juce::jlimit(0.0f, 1.0f, v);
}

enum class FilterMode
{
    lp12 = 0,
    lp24,
    hp12,
    hp24,
    bp,
    notch,
    allPass
};
}

bool SynthVoice::canPlaySound(juce::SynthesiserSound* sound)
{
    return dynamic_cast<SynthSound*>(sound) != nullptr;
}

void SynthVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int)
{
    currentMidiNote = midiNoteNumber;
    baseFrequencyHz = juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
    currentFrequencyHz = baseFrequencyHz;
    level = velocity;
    currentAngle = 0.0;
    updateAngleDelta();
    updateFilter();
    lowPassFilter.reset();
    lowPassFilterStage2.reset();

    adsr.noteOn();

    for (auto& grain : audioGrains)
    {
        grain = AudioGrain {};
    }
    audioSpawnCounter = 0;
}

void SynthVoice::stopNote(float, bool allowTailOff)
{
    if (allowTailOff)
    {
        adsr.noteOff();
    }
    else
    {
        adsr.reset();
        clearCurrentNote();
        angleDelta = 0.0;
    }
}

void SynthVoice::pitchWheelMoved(int newPitchWheelValue)
{
    // MIDI pitch bend uses 14-bit values with 8192 as center.
    const auto normalized = (static_cast<float>(newPitchWheelValue) - 8192.0f) / 8192.0f;
    targetPitchBendNorm = juce::jlimit(-1.0f, 1.0f, normalized);
}

void SynthVoice::controllerMoved(int controllerNumber, int newControllerValue)
{
    if (controllerNumber == 1)
    {
        targetModWheelNorm = juce::jlimit(0.0f, 1.0f, static_cast<float>(newControllerValue) / 127.0f);
    }
}

void SynthVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (angleDelta == 0.0)
    {
        return;
    }

    if (!adsr.isActive())
    {
        clearCurrentNote();
        angleDelta = 0.0;
        return;
    }

    const auto sampleRate = juce::jmax(1.0, getSampleRate());
    const auto vibratoPhaseInc = juce::MathConstants<double>::twoPi * static_cast<double>(vibratoRateHz) / sampleRate;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Light smoothing avoids pitch stepping while keeping wheel response immediate.
        currentPitchBendNorm += (targetPitchBendNorm - currentPitchBendNorm) * 0.06f;
        currentModWheelNorm += (targetModWheelNorm - currentModWheelNorm) * 0.045f;

        const auto bendSemitones = static_cast<double>(currentPitchBendNorm * pitchBendRangeSemitones);
        const auto lfo = std::sin(static_cast<double>(sharedVibratoPhaseRadians) + vibratoPhaseInc * static_cast<double>(sample));
        const auto vibratoSemitones = static_cast<double>(currentModWheelNorm * vibratoMaxDepthSemitones) * lfo;
        const auto pitchRatio = std::pow(2.0, (bendSemitones + vibratoSemitones) / 12.0);

        currentFrequencyHz = baseFrequencyHz * pitchRatio;
        angleDelta = juce::MathConstants<double>::twoPi * currentFrequencyHz / sampleRate;

        const auto sine = static_cast<float>(std::sin(currentAngle));
        const auto saw = static_cast<float>((currentAngle / juce::MathConstants<double>::pi) - 1.0);
        const auto square = currentAngle < juce::MathConstants<double>::pi ? 1.0f : -1.0f;

        currentImagePosition += (targetImagePosition - currentImagePosition) * 0.025f;
        float imageSample = 0.0f;

        if (imageWavetable != nullptr && imageWavetable->frames > 1 && imageWavetable->samplesPerFrame > 8)
        {
            const auto phaseNorm = static_cast<float>(currentAngle / juce::MathConstants<double>::twoPi);
            const auto wrappedPhase = phaseNorm - std::floor(phaseNorm);
            const auto samplePos = wrappedPhase * static_cast<float>(imageWavetable->samplesPerFrame);
            const auto i0 = static_cast<int>(samplePos);
            const auto fracX = samplePos - static_cast<float>(i0);

            const auto framePos = juce::jlimit(0.0f,
                                               1.0f,
                                               currentImagePosition) * static_cast<float>(imageWavetable->frames - 1);
            const auto f0 = static_cast<int>(framePos);
            const auto fracF = framePos - static_cast<float>(f0);

            const auto nyquist = static_cast<float>(sampleRate * 0.5);
            const auto normFreq = static_cast<float>(currentFrequencyHz) / juce::jmax(1.0f, nyquist);
            const auto mipEstimate = std::log2(1.0f + normFreq * 28.0f);
            const auto mip = juce::jlimit(0, imageWavetable->mipLevels - 1, static_cast<int>(mipEstimate));

            const auto readFrame = [this, mip, i0, fracX](int frame)
            {
                const auto s0 = imageWavetable->getSample(mip, frame, i0);
                const auto s1 = imageWavetable->getSample(mip, frame, i0 + 1);
                return s0 + (s1 - s0) * fracX;
            };

            const auto a = readFrame(f0);
            const auto b = readFrame((f0 + 1) % imageWavetable->frames);
            imageSample = a + (b - a) * fracF;
        }

        float granularSample = 0.0f;
        if (externalSourceMode == ExternalSourceMode::audio && audioGranularSettings.enabled)
        {
            const auto rootHz = juce::MidiMessage::getMidiNoteInHertz(audioGranularSettings.rootMidiNote);
            const auto granularPitchRatio = static_cast<float>(currentFrequencyHz / juce::jmax(1.0, static_cast<double>(rootHz)));
            granularSample = renderAudioGranularSample(granularPitchRatio,
                                                       audioGranularSettings.texture,
                                                       audioGranularSettings.grainSize);
        }

        const auto externalSample = externalSourceMode == ExternalSourceMode::audio ? granularSample : imageSample;

        const auto mixTotal = subtractiveSettings.sineMix + subtractiveSettings.sawMix + subtractiveSettings.squareMix + subtractiveSettings.imageMix;
        const auto normalizer = mixTotal > 0.0001f ? (1.0f / mixTotal) : 0.0f;

        const auto sourceSample = (sine * subtractiveSettings.sineMix
                                   + saw * subtractiveSettings.sawMix
                                   + square * subtractiveSettings.squareMix
                                   + externalSample * subtractiveSettings.imageMix)
                                  * normalizer;

        const auto env = adsr.getNextSample();
        const auto voicedSample = sourceSample * level * env * subtractiveSettings.masterGain;
        auto currentSample = lowPassFilter.processSample(voicedSample);

        const auto mode = static_cast<FilterMode>(juce::jlimit(0, 6, subtractiveSettings.filterTypeIndex));
        if (mode == FilterMode::lp24 || mode == FilterMode::hp24)
        {
            currentSample = lowPassFilterStage2.processSample(currentSample);
        }
        else if (mode == FilterMode::notch)
        {
            // Notch response via subtracting a resonant band-pass component from the dry sample.
            currentSample = voicedSample - currentSample * 0.92f;
        }

        for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
        {
            outputBuffer.addSample(channel, startSample + sample, currentSample);
        }

        currentAngle += angleDelta;

        if (currentAngle >= juce::MathConstants<double>::twoPi)
        {
            currentAngle -= juce::MathConstants<double>::twoPi;
        }
    }

    if (!adsr.isActive())
    {
        clearCurrentNote();
        angleDelta = 0.0;
    }
}

void SynthVoice::setEnvelope(const EnvelopeSettings& settings)
{
    envelopeSettings = settings;

    adsrParameters.attack = envelopeSettings.attackSeconds;
    adsrParameters.decay = envelopeSettings.decaySeconds;
    adsrParameters.sustain = envelopeSettings.sustainLevel;
    adsrParameters.release = envelopeSettings.releaseSeconds;

    adsr.setParameters(adsrParameters);
}

void SynthVoice::setSubtractiveSettings(const SubtractiveSettings& settings)
{
    subtractiveSettings = settings;
    updateFilter();
}

void SynthVoice::setPerformanceModulation(float pitchBendNormalized,
                                          float modWheelNormalized,
                                          float newPitchBendRangeSemitones,
                                          float vibratoPhaseRadians,
                                          float newVibratoRateHz,
                                          float newVibratoMaxDepthSemitones)
{
    targetPitchBendNorm = juce::jlimit(-1.0f, 1.0f, pitchBendNormalized);
    targetModWheelNorm = juce::jlimit(0.0f, 1.0f, modWheelNormalized);
    pitchBendRangeSemitones = juce::jlimit(1.0f, 24.0f, newPitchBendRangeSemitones);
    sharedVibratoPhaseRadians = vibratoPhaseRadians;
    vibratoRateHz = juce::jlimit(0.1f, 20.0f, newVibratoRateHz);
    vibratoMaxDepthSemitones = juce::jlimit(0.0f, 12.0f, newVibratoMaxDepthSemitones);
}

void SynthVoice::setImageWavetable(std::shared_ptr<const ImageWavetable> table, float wavetablePosition)
{
    imageWavetable = std::move(table);
    targetImagePosition = juce::jlimit(0.0f, 1.0f, wavetablePosition);
}

void SynthVoice::setAudioGranularSource(std::shared_ptr<const AudioSourceData> data,
                                        const AudioGranularSettings& settings)
{
    audioSourceData = std::move(data);
    audioGranularSettings = settings;
    audioGranularSettings.position = clamp01(audioGranularSettings.position);
    audioGranularSettings.grainSize = clamp01(audioGranularSettings.grainSize);
    audioGranularSettings.texture = clamp01(audioGranularSettings.texture);
    audioGranularSettings.rootMidiNote = juce::jlimit(0, 127, audioGranularSettings.rootMidiNote);
}

void SynthVoice::setExternalSourceMode(ExternalSourceMode mode)
{
    if (externalSourceMode == mode)
    {
        return;
    }

    externalSourceMode = mode;
    for (auto& grain : audioGrains)
    {
        grain = AudioGrain {};
    }
    audioSpawnCounter = 0;
}

void SynthVoice::spawnAudioGrain(float pitchRatio, float textureNorm, float grainNorm)
{
    if (audioSourceData == nullptr || audioSourceData->numSamples < 8)
    {
        return;
    }

    const auto sampleRate = static_cast<float>(juce::jmax(1.0, getSampleRate()));
    const auto grainMs = juce::jmap(grainNorm, 18.0f, 240.0f);
    const auto duration = juce::jmax(18, static_cast<int>(std::round(grainMs * 0.001f * sampleRate)));

    const auto sourceLen = static_cast<float>(audioSourceData->numSamples);
    const auto center = audioGranularSettings.position * sourceLen;
    const auto regionWidth = juce::jlimit(16.0f,
                                          sourceLen,
                                          sourceLen * juce::jmap(textureNorm, 0.015f, 0.22f));
    const auto jitter = (juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f) * 0.5f * regionWidth;
    auto startPos = center + jitter;

    while (startPos < 0.0f)
    {
        startPos += sourceLen;
    }
    while (startPos >= sourceLen)
    {
        startPos -= sourceLen;
    }

    const std::array<int, 7> intervals { -12, -7, -5, 0, 5, 7, 12 };
    const auto variationChance = juce::jmap(textureNorm, 0.03f, 0.42f);
    int interval = 0;
    if (juce::Random::getSystemRandom().nextFloat() < variationChance)
    {
        const auto idx = juce::Random::getSystemRandom().nextInt(static_cast<int>(intervals.size()));
        interval = intervals[static_cast<std::size_t>(idx)];
    }

    const auto intervalRatio = std::pow(2.0f, static_cast<float>(interval) / 12.0f);
    const auto fileRate = static_cast<float>(juce::jmax(2000.0, audioSourceData->sampleRate));
    const auto rateToHost = fileRate / sampleRate;

    audioPanPhase += juce::jmap(textureNorm, 0.10f, 0.34f);
    if (audioPanPhase > juce::MathConstants<float>::twoPi)
    {
        audioPanPhase -= juce::MathConstants<float>::twoPi;
    }

    const auto panSpread = juce::jmap(textureNorm, 0.06f, 0.94f);
    const auto pan = 0.5f + std::sin(audioPanPhase) * 0.5f * panSpread;

    for (auto& grain : audioGrains)
    {
        if (grain.active)
        {
            continue;
        }

        grain.active = true;
        grain.sourcePos = startPos;
        grain.increment = juce::jlimit(0.03f, 9.0f, pitchRatio * intervalRatio * rateToHost);
        grain.ageSamples = 0;
        grain.durationSamples = duration;
        grain.gain = juce::jmap(textureNorm, 0.18f, 0.42f);
        grain.pan = pan;
        return;
    }
}

float SynthVoice::renderAudioGranularSample(float pitchRatio, float textureNorm, float grainNorm)
{
    if (audioSourceData == nullptr || audioSourceData->numSamples < 8)
    {
        return 0.0f;
    }

    const auto sampleRate = static_cast<float>(juce::jmax(1.0, getSampleRate()));
    const auto grainMs = juce::jmap(grainNorm, 18.0f, 240.0f);
    const auto grainSamples = juce::jmax(18, static_cast<int>(std::round(grainMs * 0.001f * sampleRate)));
    const auto overlap = juce::jmap(textureNorm, 1.4f, 6.4f);
    const auto spawnInterval = juce::jmax(1, static_cast<int>(std::round(static_cast<float>(grainSamples) / overlap)));

    if (++audioSpawnCounter >= spawnInterval)
    {
        audioSpawnCounter = 0;
        spawnAudioGrain(pitchRatio, textureNorm, grainNorm);
    }

    float sumL = 0.0f;
    float sumR = 0.0f;
    int activeCount = 0;

    for (auto& grain : audioGrains)
    {
        if (!grain.active)
        {
            continue;
        }

        const auto age = static_cast<float>(grain.ageSamples);
        const auto len = static_cast<float>(juce::jmax(1, grain.durationSamples));
        const auto phase = age / len;

        if (phase >= 1.0f)
        {
            grain.active = false;
            continue;
        }

        const auto window = 0.5f - 0.5f * std::cos(phase * juce::MathConstants<float>::twoPi);
        const auto g = grain.gain * window;

        const auto left = readAudioSample(0, grain.sourcePos);
        const auto right = audioSourceData->numChannels > 1 ? readAudioSample(1, grain.sourcePos) : left;

        const auto pan = clamp01(grain.pan);
        const auto panAngle = pan * juce::MathConstants<float>::halfPi;
        const auto panL = std::cos(panAngle);
        const auto panR = std::sin(panAngle);

        sumL += left * g * panL;
        sumR += right * g * panR;

        grain.sourcePos += grain.increment;
        const auto srcLen = static_cast<float>(audioSourceData->numSamples);
        while (grain.sourcePos < 0.0f)
        {
            grain.sourcePos += srcLen;
        }
        while (grain.sourcePos >= srcLen)
        {
            grain.sourcePos -= srcLen;
        }

        ++grain.ageSamples;
        ++activeCount;
    }

    if (activeCount <= 0)
    {
        return 0.0f;
    }

    const auto norm = 1.0f / std::sqrt(static_cast<float>(activeCount));
    return 0.5f * (sumL + sumR) * norm;
}

float SynthVoice::readAudioSample(int channel, float position) const
{
    if (audioSourceData == nullptr || audioSourceData->numSamples <= 1)
    {
        return 0.0f;
    }

    const auto srcLen = static_cast<float>(audioSourceData->numSamples);
    auto p = position;
    while (p < 0.0f)
    {
        p += srcLen;
    }
    while (p >= srcLen)
    {
        p -= srcLen;
    }

    const auto i0 = static_cast<int>(p);
    const auto i1 = (i0 + 1) % audioSourceData->numSamples;
    const auto frac = p - static_cast<float>(i0);

    const auto ch = juce::jlimit(0, juce::jmax(0, audioSourceData->numChannels - 1), channel);
    const auto s0 = audioSourceData->samples.getSample(ch, i0);
    const auto s1 = audioSourceData->samples.getSample(ch, i1);
    return s0 + (s1 - s0) * frac;
}

void SynthVoice::updateAngleDelta()
{
    const auto sampleRate = getSampleRate();

    if (sampleRate > 0.0)
    {
        angleDelta = juce::MathConstants<double>::twoPi * currentFrequencyHz / sampleRate;
    }
    else
    {
        angleDelta = 0.0;
    }
}

void SynthVoice::updateFilter()
{
    const auto sampleRate = getSampleRate();

    if (sampleRate <= 0.0)
    {
        return;
    }

    const auto mode = static_cast<FilterMode>(juce::jlimit(0, 6, subtractiveSettings.filterTypeIndex));
    const auto cutoff = juce::jlimit(20.0f, 20000.0f, subtractiveSettings.filterCutoffHz);
    const auto q = juce::jlimit(0.20f, 10.0f, subtractiveSettings.filterResonanceQ);

    switch (mode)
    {
        case FilterMode::lp12:
        case FilterMode::lp24:
            lowPassFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(static_cast<double>(sampleRate),
                                                                                           cutoff,
                                                                                           q);
            lowPassFilterStage2.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(static_cast<double>(sampleRate),
                                                                                                 cutoff,
                                                                                                 q);
            break;
        case FilterMode::hp12:
        case FilterMode::hp24:
            lowPassFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass(static_cast<double>(sampleRate),
                                                                                            cutoff,
                                                                                            q);
            lowPassFilterStage2.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass(static_cast<double>(sampleRate),
                                                                                                  cutoff,
                                                                                                  q);
            break;
        case FilterMode::bp:
            lowPassFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass(static_cast<double>(sampleRate),
                                                                                            cutoff,
                                                                                            q);
            break;
        case FilterMode::notch:
            lowPassFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass(static_cast<double>(sampleRate),
                                                                                            cutoff,
                                                                                            q);
            break;
        case FilterMode::allPass:
            lowPassFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeAllPass(static_cast<double>(sampleRate),
                                                                                           cutoff,
                                                                                           q);
            break;
        default:
            break;
    }
}
