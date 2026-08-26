#include "SynthVoice.h"

#include "SynthSound.h"

#include <cmath>

namespace
{
inline float softClip(float x)
{
    return std::tanh(x);
}

inline float sanitizeAudioSample(float x)
{
    if (!std::isfinite(x))
    {
        return 0.0f;
    }

    // Flush tiny denormal magnitudes that can create zipper-like artifacts.
    if (std::abs(x) < 1.0e-20f)
    {
        return 0.0f;
    }

    return x;
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
    const auto sampleRate = juce::jmax(1.0, getSampleRate());
    if (std::abs(sampleRate - ampEnvelopePreparedSampleRate) > 0.5)
    {
        ampEnvelope.prepare(sampleRate);
        ampEnvelopePreparedSampleRate = sampleRate;
    }
    for (int sourceIndex = 0; sourceIndex < kVoiceMixerSourceCount; ++sourceIndex)
    {
        for (int filterIndex = 0; filterIndex < kFilterInstanceCount; ++filterIndex)
        {
            auto& filter = sourceFilters[static_cast<std::size_t>(sourceIndex)][static_cast<std::size_t>(filterIndex)];
            filter.prepare(sampleRate);
            filter.setCurrentSettingsImmediate(filterSettings[static_cast<std::size_t>(filterIndex)]);
        }
    }
    subOscillator.prepare(sampleRate);
    subOscillator.setSettings(subOscillatorSettings);
    subOscillator.resetForNote();

    ampEnvelope.setSettings(envelopeSettings);
    ampEnvelope.noteOn();
    noteAgeSamples = 0;
    for (auto& oscillatorUnit : oscillatorUnits)
    {
        oscillatorUnit.resetForNote(sampleRate, currentFrequencyHz);
    }
    for (auto& oscillatorAngle : oscillatorAngles)
    {
        oscillatorAngle = currentAngle;
    }
}

void SynthVoice::stopNote(float, bool allowTailOff)
{
    if (!allowTailOff)
    {
        ampEnvelope.reset();
        clearCurrentNote();
        angleDelta = 0.0;
        return;
    }

    ampEnvelope.noteOff();
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

    if (!ampEnvelope.isActive())
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

    for (int sourceIndex = 0; sourceIndex < kVoiceMixerSourceCount; ++sourceIndex)
    {
        for (int filterIndex = 0; filterIndex < kFilterInstanceCount; ++filterIndex)
        {
            auto runtimeFilter = filterSettings[static_cast<std::size_t>(filterIndex)];

            auto targetCutoffHz = runtimeFilter.cutoffHz;
            auto targetResonanceQ = runtimeFilter.resonanceQ;

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

            runtimeFilter.cutoffHz = juce::jlimit(20.0f, 20000.0f, targetCutoffHz);
            runtimeFilter.resonanceQ = juce::jlimit(0.20f, 10.0f, targetResonanceQ);
            sourceFilters[static_cast<std::size_t>(sourceIndex)][static_cast<std::size_t>(filterIndex)].setTargetSettings(runtimeFilter);
        }
    }

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto applyVibeSourceStage = [&](float inSample, float noiseScale)
        {
            if (!vibeActive)
            {
                return inSample;
            }

            const auto asym = (vibeTuning.waveformAsymmetry * (0.35f + 0.65f * vibeVariation.asymmetryBias)) * vibeDepth;
            const auto sat = (vibeTuning.saturation * (0.45f + 0.55f * vibeVariation.saturationBias)) * vibeDepth;
            const auto chaos = vibeShared.chaos * vibeTuning.correlatedChaos * 0.18f * vibeDepth;

            const auto asymShaped = inSample
                                    + asym * inSample * inSample * (inSample >= 0.0f ? 0.9f : -0.7f)
                                    + chaos;
            auto stageSample = std::tanh(asymShaped * (1.0f + sat * 3.4f)) * (1.0f / (1.0f + sat * 0.90f));

            const auto noiseAmount = vibeTuning.noise * vibeDepth * (0.55f + 0.45f * std::abs(vibeShared.psu));
            stageSample += oscillatorUnits[0].nextDeterministicNoise()
                           * (0.0035f + 0.0165f * noiseAmount)
                           * juce::jlimit(0.0f, 1.0f, noiseScale);
            return stageSample;
        };

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

        const auto baseOscillatorPitchRatio = static_cast<float>(pitchRatio);
        const auto subSample = subOscillator.renderSample(currentFrequencyHz);

        std::array<float, kVoiceMixerSourceCount> sourceSamples { { 0.0f, 0.0f, 0.0f, 0.0f } };

        for (int oscIndex = 0; oscIndex < kOscillatorSourceCount; ++oscIndex)
        {
            const auto& layer = oscillatorLayerSettings[static_cast<std::size_t>(oscIndex)];
            const auto semitoneOffset = static_cast<double>(layer.pitchSemitones)
                                        + static_cast<double>(layer.coarseSemitones)
                                        + static_cast<double>(layer.fineCents) * 0.01;
            const auto sourcePitchRatio = std::pow(2.0, semitoneOffset / 12.0);
            const auto sourceFrequencyHz = currentFrequencyHz * sourcePitchRatio;
            const auto sourceAngleDelta = juce::MathConstants<double>::twoPi * sourceFrequencyHz / sampleRate;

            auto& sourceAngle = oscillatorAngles[static_cast<std::size_t>(oscIndex)];

            if (!layer.enabled)
            {
                sourceAngle += sourceAngleDelta;
                if (sourceAngle >= juce::MathConstants<double>::twoPi)
                {
                    sourceAngle -= juce::MathConstants<double>::twoPi;
                }
                continue;
            }

            OscillatorUnit::RenderContext sourceContext = oscillatorContext;
            sourceContext.pitchRatio = baseOscillatorPitchRatio * static_cast<float>(sourcePitchRatio);
            sourceContext.currentFrequencyHz = sourceFrequencyHz;
            sourceContext.currentAngle = sourceAngle;

            const auto sourceSample = oscillatorUnits[static_cast<std::size_t>(oscIndex)].renderSample(sampleRate, sourceContext)
                                      * juce::jlimit(0.0f, 1.0f, layer.level);
            auto sourceStageSample = softClip(sanitizeAudioSample(sourceSample) * 0.92f);
            sourceStageSample = applyVibeSourceStage(sourceStageSample, 1.0f);
            sourceStageSample = sanitizeAudioSample(sourceStageSample);
            sourceSamples[static_cast<std::size_t>(oscIndex + 1)] = sourceStageSample;

            sourceAngle += sourceAngleDelta;
            if (sourceAngle >= juce::MathConstants<double>::twoPi)
            {
                sourceAngle -= juce::MathConstants<double>::twoPi;
            }
        }

        auto subStageSample = 0.0f;
        const auto subBypassed = !subOscillatorSettings.enabled;
        const auto subGain = juce::jlimit(0.0f, 1.0f, subOscillatorSettings.level);
        if (!subBypassed)
        {
            subStageSample = softClip(subSample * subGain * 0.92f);
            subStageSample = applyVibeSourceStage(subStageSample, 0.6f);
            subStageSample = sanitizeAudioSample(subStageSample);
        }
        sourceSamples[0] = subStageSample;

        // AMP STAGE: envelope and voice gain are downstream of filter.
        const auto env = ampEnvelope.getNextSample();
        auto voiceGain = level * env * subtractiveSettings.masterGain;

        if (vibeActive)
        {
            const auto gainVariation = (vibeVariation.gainOffset * vibeTuning.voiceVariation
                                        + vibeShared.psu * vibeTuning.psuMovement * 0.12f
                                        + vibeShared.temperature * vibeTuning.temperatureDrift * 0.10f) * vibeDepth;
            voiceGain *= (1.0f + gainVariation);
            voiceGain = juce::jlimit(0.0f, 2.0f, voiceGain);
        }

        std::array<float, kVoiceMixerSourceCount> voicedSourceSamples { { 0.0f, 0.0f, 0.0f, 0.0f } };
        float summedSample = 0.0f;
        for (int sourceIndex = 0; sourceIndex < kVoiceMixerSourceCount; ++sourceIndex)
        {
            auto filteredSample = sourceSamples[static_cast<std::size_t>(sourceIndex)];
            for (int filterIndex = 0; filterIndex < kFilterInstanceCount; ++filterIndex)
            {
                const auto& settings = filterSettings[static_cast<std::size_t>(filterIndex)];
                if (!settings.enabled)
                {
                    continue;
                }
                filteredSample = sourceFilters[static_cast<std::size_t>(sourceIndex)][static_cast<std::size_t>(filterIndex)].processSample(filteredSample);
                filteredSample = sanitizeAudioSample(filteredSample);
            }

            auto voicedSample = filteredSample * voiceGain;
            if (vibeActive)
            {
                const auto vcaAmount = (vibeTuning.vcaNonlinearity * vibeDepth
                                        + vibeShared.chaos * vibeTuning.correlatedChaos * 0.16f * vibeDepth);
                voicedSample = std::tanh(voicedSample * (1.0f + vcaAmount * 3.2f))
                              * (1.0f / (1.0f + vcaAmount * 0.95f));
            }

            voicedSample = sanitizeAudioSample(voicedSample);

            voicedSourceSamples[static_cast<std::size_t>(sourceIndex)] = voicedSample;
            summedSample += voicedSample;
        }

        summedSample = sanitizeAudioSample(summedSample);

        if (outputBuffer.getNumChannels() >= kVoiceMixerSourceCount)
        {
            for (int sourceIndex = 0; sourceIndex < kVoiceMixerSourceCount; ++sourceIndex)
            {
                outputBuffer.addSample(sourceIndex,
                                       startSample + sample,
                                       voicedSourceSamples[static_cast<std::size_t>(sourceIndex)]);
            }
        }
        else
        {
            for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
            {
                outputBuffer.addSample(channel, startSample + sample, summedSample);
            }
        }

        currentAngle += angleDelta;

        if (currentAngle >= juce::MathConstants<double>::twoPi)
        {
            currentAngle -= juce::MathConstants<double>::twoPi;
        }

        ++noteAgeSamples;
    }

    if (!ampEnvelope.isActive())
    {
        clearCurrentNote();
        angleDelta = 0.0;
    }
}

void SynthVoice::setEnvelope(const EnvelopeSettings& settings)
{
    envelopeSettings = settings;
    ampEnvelope.setSettings(settings);
}

void SynthVoice::setFilterSettings(const std::array<FilterSettings, kFilterInstanceCount>& settings)
{
    filterSettings = settings;
    for (int sourceIndex = 0; sourceIndex < kVoiceMixerSourceCount; ++sourceIndex)
    {
        for (int filterIndex = 0; filterIndex < kFilterInstanceCount; ++filterIndex)
        {
            sourceFilters[static_cast<std::size_t>(sourceIndex)][static_cast<std::size_t>(filterIndex)].setTargetSettings(
                filterSettings[static_cast<std::size_t>(filterIndex)]);
        }
    }

    if (!ampEnvelope.isActive())
    {
        for (int sourceIndex = 0; sourceIndex < kVoiceMixerSourceCount; ++sourceIndex)
        {
            for (int filterIndex = 0; filterIndex < kFilterInstanceCount; ++filterIndex)
            {
                sourceFilters[static_cast<std::size_t>(sourceIndex)][static_cast<std::size_t>(filterIndex)].setCurrentSettingsImmediate(
                    filterSettings[static_cast<std::size_t>(filterIndex)]);
            }
        }
    }
}

void SynthVoice::setSubtractiveSettings(const SubtractiveSettings& settings)
{
    subtractiveSettings = settings;
}

void SynthVoice::setSubOscillatorSettings(const SubOscSettings& settings)
{
    subOscillatorSettings = settings;
    subOscillator.setSettings(subOscillatorSettings);
}

void SynthVoice::setOscillatorLayerSettings(const std::array<OscillatorLayerSettings, kOscillatorSourceCount>& settings)
{
    oscillatorLayerSettings = settings;
    for (int oscIndex = 0; oscIndex < kOscillatorSourceCount; ++oscIndex)
    {
        oscillatorUnits[static_cast<std::size_t>(oscIndex)].setSettings(
            oscillatorLayerSettings[static_cast<std::size_t>(oscIndex)].oscillator);
    }
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
