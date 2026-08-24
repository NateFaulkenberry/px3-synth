#include "SynthVoice.h"

#include "SynthSound.h"

#include <cmath>

namespace
{
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

inline float softClip(float x)
{
    return std::tanh(x);
}
}

bool SynthVoice::canPlaySound(juce::SynthesiserSound* sound)
{
    return dynamic_cast<SynthSound*>(sound) != nullptr;
}

void SynthVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int)
{
    // Voice start initializes all phase/noise/filter state deterministically so
    // repeated notes begin from musically stable conditions.
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
    noteAgeSamples = 0;

    const auto sampleRate = juce::jmax(1.0, getSampleRate());
    oscillatorUnit.resetForNote(sampleRate, currentFrequencyHz);
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
    // Keep fast exits cheap; this runs on the real-time audio thread.
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

    const auto vibeBase = vibeBypass ? 0.0f : std::pow(juce::jlimit(0.0f, 1.0f, vibeGlobalAmount), 1.35f);
    // Global amount is a macro depth control. Keep low values subtle, but let
    // the top of the range drive aggressively audible analog character.
    const auto vibeDepth = juce::jlimit(0.0f, 3.50f, vibeBase * (0.30f + 3.10f * vibeBase));
    const auto vibeActive = vibeDepth > 0.0001f;

    auto targetCutoffHz = subtractiveSettings.filterCutoffHz;
    auto targetResonanceQ = subtractiveSettings.filterResonanceQ;

    if (vibeActive)
    {
        const auto temperatureCutoff = vibeShared.temperature * vibeTuning.temperatureDrift * 0.34f;
        const auto psuCutoff = vibeShared.psu * vibeTuning.psuMovement * 0.22f;
        const auto voiceCutoff = vibeVariation.cutoffOffset * vibeTuning.voiceVariation;
        const auto chaosCutoff = vibeShared.chaos * vibeTuning.correlatedChaos * 0.28f;
        const auto cutoffMul = 1.0f + (temperatureCutoff + psuCutoff + voiceCutoff + chaosCutoff) * vibeDepth;

        const auto resoDelta = (vibeVariation.resonanceOffset * vibeTuning.voiceVariation
                    + vibeShared.chaos * vibeTuning.filterVariation * 0.16f) * vibeDepth;

        targetCutoffHz *= cutoffMul;
        targetResonanceQ *= (1.0f + resoDelta);
    }

    targetFilterCutoffHz = juce::jlimit(20.0f, 20000.0f, targetCutoffHz);
    targetFilterResonanceQ = juce::jlimit(0.20f, 10.0f, targetResonanceQ);

    if (activeFilterTypeIndex != subtractiveSettings.filterTypeIndex)
    {
        activeFilterTypeIndex = subtractiveSettings.filterTypeIndex;
        currentFilterCutoffHz = targetFilterCutoffHz;
        currentFilterResonanceQ = targetFilterResonanceQ;
        setFilterResponse(currentFilterCutoffHz,
                          currentFilterResonanceQ,
                          activeFilterTypeIndex);
        filterUpdateCounter = 0;
    }

    constexpr float filterTauSec = 0.005f;
    const auto filterCoeff = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate * filterTauSec));

    for (int sample = 0; sample < numSamples; ++sample)
    {
        currentFilterCutoffHz += (targetFilterCutoffHz - currentFilterCutoffHz) * filterCoeff;
        currentFilterResonanceQ += (targetFilterResonanceQ - currentFilterResonanceQ) * filterCoeff;
        if ((filterUpdateCounter++ & 0x07) == 0)
        {
            setFilterResponse(currentFilterCutoffHz,
                              currentFilterResonanceQ,
                              activeFilterTypeIndex);
        }

        currentPitchBendNorm += (targetPitchBendNorm - currentPitchBendNorm) * 0.06f;
        currentModWheelNorm += (targetModWheelNorm - currentModWheelNorm) * 0.045f;

        auto bendSemitones = static_cast<double>(currentPitchBendNorm * pitchBendRangeSemitones);
        const auto lfo = std::sin(static_cast<double>(sharedVibratoPhaseRadians) + vibratoPhaseInc * static_cast<double>(sample));
        const auto vibratoSemitones = static_cast<double>(currentModWheelNorm * vibratoMaxDepthSemitones) * lfo;

        if (vibeActive)
        {
            const auto driftCents = (vibeShared.oscillatorDrift * vibeTuning.oscillatorDrift * 32.0f
                                     + vibeShared.psu * vibeTuning.psuMovement * 13.0f
                                     + vibeShared.temperature * vibeTuning.temperatureDrift * 18.0f
                                     + vibeShared.chaos * vibeTuning.correlatedChaos * 12.0f
                                     + vibeVariation.pitchCents * vibeTuning.voiceVariation) * vibeDepth;
            bendSemitones += static_cast<double>(juce::jlimit(-60.0f, 60.0f, driftCents) * 0.01f);
        }

        const auto pitchRatio = std::pow(2.0, (bendSemitones + vibratoSemitones) / 12.0);

        currentFrequencyHz = baseFrequencyHz * pitchRatio;
        angleDelta = juce::MathConstants<double>::twoPi * currentFrequencyHz / sampleRate;

        OscillatorUnit::RenderContext oscillatorContext;
        oscillatorContext.currentAngle = currentAngle;
        oscillatorContext.currentFrequencyHz = currentFrequencyHz;
        oscillatorContext.noteAgeSamples = noteAgeSamples;
        oscillatorContext.pitchRatio = static_cast<float>(pitchRatio);
        oscillatorContext.modWheelNorm = currentModWheelNorm;
        oscillatorContext.pwmModWheelNorm = targetModWheelNorm;

        auto sourceSample = oscillatorUnit.renderSample(sampleRate, oscillatorContext);
        sourceSample = softClip(sourceSample * 0.92f);

        if (vibeActive)
        {
            const auto asym = (vibeTuning.waveformAsymmetry * (0.35f + 0.65f * vibeVariation.asymmetryBias)) * vibeDepth;
            const auto sat = (vibeTuning.saturation * (0.45f + 0.55f * vibeVariation.saturationBias)) * vibeDepth;
            const auto chaos = vibeShared.chaos * vibeTuning.correlatedChaos * 0.18f * vibeDepth;

            const auto asymShaped = sourceSample
                                    + asym * sourceSample * sourceSample * (sourceSample >= 0.0f ? 0.9f : -0.7f)
                                    + chaos;
            sourceSample = std::tanh(asymShaped * (1.0f + sat * 3.4f)) * (1.0f / (1.0f + sat * 0.90f));

            const auto noiseAmount = vibeTuning.noise * vibeDepth * (0.55f + 0.45f * std::abs(vibeShared.psu));
            sourceSample += oscillatorUnit.nextDeterministicNoise() * (0.0035f + 0.0165f * noiseAmount);
        }

        const auto env = adsr.getNextSample();
        auto voiceGain = level * env * subtractiveSettings.masterGain;

        if (vibeActive)
        {
            const auto gainVariation = (vibeVariation.gainOffset * vibeTuning.voiceVariation
                                        + vibeShared.psu * vibeTuning.psuMovement * 0.12f
                                        + vibeShared.temperature * vibeTuning.temperatureDrift * 0.10f) * vibeDepth;
            voiceGain *= (1.0f + gainVariation);
            voiceGain = juce::jlimit(0.0f, 2.0f, voiceGain);
        }

        auto voicedSample = sourceSample * voiceGain;
        if (vibeActive)
        {
            const auto vcaAmount = (vibeTuning.vcaNonlinearity * vibeDepth
                                    + vibeShared.chaos * vibeTuning.correlatedChaos * 0.16f * vibeDepth);
            voicedSample = std::tanh(voicedSample * (1.0f + vcaAmount * 3.2f))
                          * (1.0f / (1.0f + vcaAmount * 0.95f));
        }

        auto currentSample = lowPassFilter.processSample(voicedSample);

        const auto mode = static_cast<FilterMode>(juce::jlimit(0, 6, subtractiveSettings.filterTypeIndex));
        if (mode == FilterMode::lp24 || mode == FilterMode::hp24)
        {
            currentSample = lowPassFilterStage2.processSample(currentSample);
        }
        else if (mode == FilterMode::notch)
        {
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

        ++noteAgeSamples;
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

    targetFilterCutoffHz = juce::jlimit(20.0f, 20000.0f, subtractiveSettings.filterCutoffHz);
    targetFilterResonanceQ = juce::jlimit(0.20f, 10.0f, subtractiveSettings.filterResonanceQ);

    if (!adsr.isActive())
    {
        currentFilterCutoffHz = targetFilterCutoffHz;
        currentFilterResonanceQ = targetFilterResonanceQ;
        activeFilterTypeIndex = subtractiveSettings.filterTypeIndex;
        updateFilter();
    }
}

void SynthVoice::setOscillatorSettings(const OscillatorSettings& settings)
{
    oscillatorUnit.setSettings(settings);
}

void SynthVoice::setPerformanceModulation(float pitchBendNormalized,
                                          float modWheelNormalized,
                                          float newPitchBendRangeSemitones,
                                          float vibratoPhaseRadians,
                                          float newVibratoRateHz,
                                          float newVibratoMaxDepthSemitones)
{
    // Inputs arrive from processor-level shared performance state. Values are
    // clamped here so render path can assume valid ranges.
    targetPitchBendNorm = juce::jlimit(-1.0f, 1.0f, pitchBendNormalized);
    targetModWheelNorm = juce::jlimit(0.0f, 1.0f, modWheelNormalized);
    pitchBendRangeSemitones = juce::jlimit(1.0f, 24.0f, newPitchBendRangeSemitones);
    sharedVibratoPhaseRadians = vibratoPhaseRadians;
    vibratoRateHz = juce::jlimit(0.1f, 20.0f, newVibratoRateHz);
    vibratoMaxDepthSemitones = juce::jlimit(0.0f, 12.0f, newVibratoMaxDepthSemitones);
}

void SynthVoice::setVoiceIndex(int index)
{
    voiceIndex = juce::jmax(0, index);
}

void SynthVoice::setVibeState(float globalAmount,
                              bool bypass,
                              const VibeSharedState& sharedState,
                              const VibeVoiceVariation& variation,
                              const VibeTuning& tuningState)
{
    vibeGlobalAmount = juce::jlimit(0.0f, 1.0f, globalAmount);
    vibeBypass = bypass;
    vibeShared = sharedState;
    vibeVariation = variation;
    vibeTuning = tuningState;
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
    currentFilterCutoffHz = juce::jlimit(20.0f, 20000.0f, targetFilterCutoffHz);
    currentFilterResonanceQ = juce::jlimit(0.20f, 10.0f, targetFilterResonanceQ);
    activeFilterTypeIndex = subtractiveSettings.filterTypeIndex;
    setFilterResponse(currentFilterCutoffHz,
                      currentFilterResonanceQ,
                      activeFilterTypeIndex);
}

void SynthVoice::setFilterResponse(float cutoffHz, float resonanceQ, int filterTypeIndex)
{
    const auto sampleRate = getSampleRate();

    if (sampleRate <= 0.0)
    {
        return;
    }

    const auto mode = static_cast<FilterMode>(juce::jlimit(0, 6, filterTypeIndex));
    const auto cutoff = juce::jlimit(20.0f, 20000.0f, cutoffHz);
    const auto q = juce::jlimit(0.20f, 10.0f, resonanceQ);

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
