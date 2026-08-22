#include "SynthVoice.h"

#include "SynthSound.h"

#include <cmath>

bool SynthVoice::canPlaySound(juce::SynthesiserSound* sound)
{
    return dynamic_cast<SynthSound*>(sound) != nullptr;
}

void SynthVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int)
{
    baseFrequencyHz = juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
    currentFrequencyHz = baseFrequencyHz;
    level = velocity;
    currentAngle = 0.0;
    updateAngleDelta();
    updateFilter();
    lowPassFilter.reset();

    adsr.noteOn();
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

        const auto mixTotal = subtractiveSettings.sineMix + subtractiveSettings.sawMix + subtractiveSettings.squareMix + subtractiveSettings.imageMix;
        const auto normalizer = mixTotal > 0.0001f ? (1.0f / mixTotal) : 0.0f;

        const auto sourceSample = (sine * subtractiveSettings.sineMix
                                   + saw * subtractiveSettings.sawMix
                                   + square * subtractiveSettings.squareMix
                                   + imageSample * subtractiveSettings.imageMix)
                                  * normalizer;

        const auto env = adsr.getNextSample();
        const auto voicedSample = sourceSample * level * env * subtractiveSettings.masterGain;
        const auto currentSample = lowPassFilter.processSample(voicedSample);

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

    lowPassFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(static_cast<double>(sampleRate),
                                                                                   subtractiveSettings.filterCutoffHz,
                                                                                   subtractiveSettings.filterResonanceQ);
}
