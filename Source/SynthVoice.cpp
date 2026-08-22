#include "SynthVoice.h"

#include "SynthSound.h"

#include <cmath>

bool SynthVoice::canPlaySound(juce::SynthesiserSound* sound)
{
    return dynamic_cast<SynthSound*>(sound) != nullptr;
}

void SynthVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int)
{
    currentFrequencyHz = juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
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

void SynthVoice::pitchWheelMoved(int)
{
}

void SynthVoice::controllerMoved(int, int)
{
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

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Basic subtractive source: sine + saw + square from one phase accumulator.
        const auto sine = static_cast<float>(std::sin(currentAngle));
        const auto saw = static_cast<float>((currentAngle / juce::MathConstants<double>::pi) - 1.0);
        const auto square = currentAngle < juce::MathConstants<double>::pi ? 1.0f : -1.0f;

        const auto mixTotal = subtractiveSettings.sineMix + subtractiveSettings.sawMix + subtractiveSettings.squareMix;
        const auto normalizer = mixTotal > 0.0001f ? (1.0f / mixTotal) : 0.0f;

        const auto sourceSample = (sine * subtractiveSettings.sineMix
                                   + saw * subtractiveSettings.sawMix
                                   + square * subtractiveSettings.squareMix)
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
