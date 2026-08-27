#include "SynthVoice.h"

#include "PX3Diagnostics.h"
#include "SynthSound.h"

#include <atomic>
#include <cmath>

namespace
{
std::atomic<uint32_t> gNoteStartSequence { 1u };

inline float softClip(float x)
{
    return std::tanh(x);
}

// Long enough to be inaudible, short enough that a pruned release tail stops
// consuming CPU within a single typical host block.
constexpr float kFastReleaseSeconds = 0.005f;

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
    const auto sequence = gNoteStartSequence.fetch_add(1u, std::memory_order_relaxed);
    auto hash = static_cast<uint32_t>(voiceIndex + 1) * 747796405u;
    hash ^= sequence * 2891336453u;
    hash ^= static_cast<uint32_t>(midiNoteNumber + 1) * 277803737u;
    hash ^= (hash >> 16);
    hash *= 2246822519u;
    const auto phaseSeed = static_cast<double>(hash & 0x00FFFFFFu) / static_cast<double>(0x01000000u);
    currentAngle = juce::MathConstants<double>::twoPi * phaseSeed;
    updateAngleDelta();
    const auto sampleRate = juce::jmax(1.0, getSampleRate());
    if (std::abs(sampleRate - ampEnvelopePreparedSampleRate) > 0.5)
    {
        ampEnvelope.prepare(sampleRate);
        ampEnvelopePreparedSampleRate = sampleRate;
    }
    if (std::abs(sampleRate - modEnvelopePreparedSampleRate) > 0.5)
    {
        for (auto& modEnvelope : modEnvelopeGenerators)
        {
            modEnvelope.prepare(sampleRate);
        }
        modEnvelopePreparedSampleRate = sampleRate;
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
    for (std::size_t envIndex = 0; envIndex < modEnvelopeGenerators.size(); ++envIndex)
    {
        modEnvelopeGenerators[envIndex].setSettings(modEnvelopeSettings[envIndex]);
        if (modEnvelopeEnabled[envIndex])
        {
            modEnvelopeGenerators[envIndex].noteOn();
        }
        else
        {
            modEnvelopeGenerators[envIndex].reset();
            modEnvelopeValues[envIndex] = 0.0f;
        }
    }
    noteAgeSamples = 0;
    fastReleaseTotalSamples = 0;
    fastReleaseSamplesRemaining = 0;
    for (auto& oscillatorUnit : oscillatorUnits)
    {
        oscillatorUnit.resetForNote(sampleRate, currentFrequencyHz);
    }
    for (auto& audible : oscillatorAudibleForCurrentNote)
    {
        audible = true;
    }
    for (int oscIndex = 0; oscIndex < kOscillatorSourceCount; ++oscIndex)
    {
        const auto spread = (juce::MathConstants<double>::twoPi / static_cast<double>(kOscillatorSourceCount))
                            * static_cast<double>(oscIndex);
        oscillatorAngles[static_cast<std::size_t>(oscIndex)] = std::fmod(currentAngle + spread,
                                                                          juce::MathConstants<double>::twoPi);
    }
    releaseSmoothingState.fill(0.0f);

#if PX3_DIAGNOSTICS
    {
        auto& diag = px3::diag::state();
        if (diag.capturing)
        {
            ++diag.noteStarts;
            diag.oscillatorResets += kOscillatorSourceCount;
        }
        diagMarkStart = true;
        diagHasPrevEnv = false;
        diagHasPrevVoiceGain = false;
        diagLastVoiceOut = 0.0f;
    }
#endif
}

void SynthVoice::stopNote(float, bool allowTailOff)
{
    if (!allowTailOff)
    {
#if PX3_DIAGNOSTICS
        {
            auto& diag = px3::diag::state();
            if (diag.capturing && isVoiceActive())
            {
                ++diag.hardStops;
                ++diag.clearCurrentNoteEvents;
                diag.maxHardStopEnv = juce::jmax(diag.maxHardStopEnv, currentAmpEnvelopeValue);
                diag.maxTruncationStep = juce::jmax(diag.maxTruncationStep, std::abs(diagLastVoiceOut));
                diag.markLifecycle(0);
            }
        }
#endif
        retireVoice();
        return;
    }

    ampEnvelope.noteOff();
    for (std::size_t envIndex = 0; envIndex < modEnvelopeGenerators.size(); ++envIndex)
    {
        if (modEnvelopeEnabled[envIndex])
        {
            modEnvelopeGenerators[envIndex].noteOff();
        }
    }
}

void SynthVoice::retireVoice()
{
    ampEnvelope.reset();
    for (auto& modEnvelope : modEnvelopeGenerators)
    {
        modEnvelope.reset();
    }
    modEnvelopeValues.fill(0.0f);
    fastReleaseTotalSamples = 0;
    fastReleaseSamplesRemaining = 0;
    clearCurrentNote();
    angleDelta = 0.0;
}

void SynthVoice::beginFastRelease()
{
    if (!isVoiceActive() || fastReleaseSamplesRemaining > 0)
    {
        return;
    }

    const auto sampleRate = juce::jmax(1.0, getSampleRate());
    fastReleaseTotalSamples = juce::jmax(1, static_cast<int>(kFastReleaseSeconds * static_cast<float>(sampleRate)));
    fastReleaseSamplesRemaining = fastReleaseTotalSamples;
}

bool SynthVoice::isFastReleasing() const
{
    return fastReleaseSamplesRemaining > 0;
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
        currentAmpEnvelopeValue = 0.0f;
        lastBlockPeak = 0.0f;
        lastBlockSourcePeaks.fill(0.0f);
        return;
    }

    if (!ampEnvelope.isActive())
    {
        currentAmpEnvelopeValue = 0.0f;
        lastBlockPeak = 0.0f;
        lastBlockSourcePeaks.fill(0.0f);
#if PX3_DIAGNOSTICS
        diagNoteEnvelopeInactiveClear(startSample);
#endif
        clearCurrentNote();
        angleDelta = 0.0;
        return;
    }

#if PX3_DIAGNOSTICS
    auto& diag = px3::diag::state();
    if (diagMarkStart)
    {
        diag.markLifecycle(startSample);
        diagMarkStart = false;
    }
#endif

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

    std::array<float, 3> modEnvelopePeakValues { { 0.0f, 0.0f, 0.0f } };
    auto blockPeak = 0.0f;
    std::array<float, kVoiceMixerSourceCount> blockSourcePeaks { { 0.0f, 0.0f, 0.0f, 0.0f } };

    auto activeSourceCount = 0;
    if (subOscillatorSettings.enabled)
    {
        ++activeSourceCount;
    }
    for (const auto& layer : oscillatorLayerSettings)
    {
        if (layer.enabled)
        {
            ++activeSourceCount;
        }
    }
    const auto perSourceNormalization = activeSourceCount > 0
                                            ? 1.0f / std::sqrt(static_cast<float>(activeSourceCount))
                                            : 1.0f;

    constexpr float kReleaseSilenceThreshold = 1.0e-4f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        for (std::size_t envIndex = 0; envIndex < modEnvelopeGenerators.size(); ++envIndex)
        {
            if (modEnvelopeEnabled[envIndex])
            {
                const auto envSample = modEnvelopeGenerators[envIndex].getNextSample();
                modEnvelopeValues[envIndex] = envSample;
                modEnvelopePeakValues[envIndex] = juce::jmax(modEnvelopePeakValues[envIndex], envSample);
            }
            else
            {
                modEnvelopeValues[envIndex] = 0.0f;
            }
        }

        // Pull AMP ENV once per sample and use it consistently across all
        // per-voice stages to avoid release-tail modulation grain.
        const auto env = ampEnvelope.getNextSample();
        currentAmpEnvelopeValue = env;

#if PX3_DIAGNOSTICS
        diag.setEnvSample(startSample + sample, env);
        if (diagHasPrevEnv)
        {
            diag.noteEnvDelta(env - diagPrevEnv);
        }
        diagPrevEnv = env;
        diagHasPrevEnv = true;
#endif

        if (!isKeyDown() && env <= kReleaseSilenceThreshold)
        {
#if PX3_DIAGNOSTICS
            if (diag.capturing)
            {
                ++diag.clearFromReleaseFloor;
                ++diag.clearCurrentNoteEvents;
                diag.maxTruncationStep = juce::jmax(diag.maxTruncationStep, std::abs(diagLastVoiceOut));
                diag.markLifecycle(startSample + sample);
            }
#endif
            retireVoice();
            break;
        }

        // Budget-driven retirement: fade out over a few milliseconds rather
        // than cutting the tail off at its current amplitude.
        auto fastReleaseGain = 1.0f;
        if (fastReleaseTotalSamples > 0)
        {
            if (fastReleaseSamplesRemaining <= 0)
            {
#if PX3_DIAGNOSTICS
                if (diag.capturing)
                {
                    ++diag.clearCurrentNoteEvents;
                    diag.maxTruncationStep = juce::jmax(diag.maxTruncationStep, std::abs(diagLastVoiceOut));
                    diag.markLifecycle(startSample + sample);
                }
#endif
                retireVoice();
                break;
            }

            const auto fadeProgress = 1.0f
                                      - static_cast<float>(fastReleaseSamplesRemaining)
                                            / static_cast<float>(fastReleaseTotalSamples);
            fastReleaseGain = 0.5f * (1.0f + std::cos(juce::MathConstants<float>::pi * fadeProgress));
            --fastReleaseSamplesRemaining;
        }

        const auto releaseTailShape = isKeyDown()
                                          ? 1.0f
                                          : juce::jlimit(0.0f, 1.0f, (env - 0.02f) / 0.30f);
        auto ecoReleaseVoice = !isKeyDown();
#if PX3_DIAGNOSTICS
        if (diag.freezeVibeReleaseSwitch)
        {
            ecoReleaseVoice = false;
        }
#endif

        const auto applyVibeSourceStage = [&](float inSample, float noiseScale)
        {
            if (!vibeActive)
            {
                return inSample;
            }

            // Release voices can dominate CPU under dense overlap. Keep held
            // voices fully detailed, but run release tails through a cleaner,
            // lighter path to prevent real-time overload artifacts.
            if (ecoReleaseVoice)
            {
                juce::ignoreUnused(noiseScale);
                return inSample;
            }

            const auto asym = (vibeTuning.waveformAsymmetry * (0.35f + 0.65f * vibeVariation.asymmetryBias)) * vibeDepth;
            const auto sat = (vibeTuning.saturation * (0.45f + 0.55f * vibeVariation.saturationBias))
                             * vibeDepth
                             * (0.30f + 0.70f * releaseTailShape);
            const auto chaos = vibeShared.chaos * vibeTuning.correlatedChaos * 0.18f * vibeDepth * releaseTailShape;

            const auto asymShaped = inSample
                                    + asym * inSample * inSample * (inSample >= 0.0f ? 0.9f : -0.7f)
                                    + chaos;
            auto stageSample = std::tanh(asymShaped * (1.0f + sat * 3.4f)) * (1.0f / (1.0f + sat * 0.90f));

            const auto noiseAmount = vibeTuning.noise * vibeDepth * (0.55f + 0.45f * std::abs(vibeShared.psu));
            const auto noiseTailScale = 0.18f + 0.82f * releaseTailShape;
            stageSample += oscillatorUnits[0].nextDeterministicNoise()
                           * (0.0035f + 0.0165f * noiseAmount)
                           * noiseTailScale
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
            auto& audibleForCurrentNote = oscillatorAudibleForCurrentNote[static_cast<std::size_t>(oscIndex)];
            const auto semitoneOffset = static_cast<double>(layer.pitchSemitones)
                                        + static_cast<double>(layer.coarseSemitones)
                                        + static_cast<double>(layer.fineCents) * 0.01;
            const auto sourcePitchRatio = std::pow(2.0, semitoneOffset / 12.0);
            const auto sourceFrequencyHz = currentFrequencyHz * sourcePitchRatio;
            const auto sourceAngleDelta = juce::MathConstants<double>::twoPi * sourceFrequencyHz / sampleRate;

            auto& sourceAngle = oscillatorAngles[static_cast<std::size_t>(oscIndex)];

            if (!layer.enabled)
            {
                if (audibleForCurrentNote)
                {
                    audibleForCurrentNote = false;

                    // If an oscillator is bypassed while a note is still held,
                    // clear its per-source filter memory so no residual ring
                    // leaks and no stale note resumes when re-enabled.
                    constexpr int sourceOffset = 1; // [0]=sub, [1..3]=osc1..3
                    const auto sourceIndex = oscIndex + sourceOffset;
                    for (int filterIndex = 0; filterIndex < kFilterInstanceCount; ++filterIndex)
                    {
                        auto& filter = sourceFilters[static_cast<std::size_t>(sourceIndex)][static_cast<std::size_t>(filterIndex)];
                        filter.reset();
                        filter.setCurrentSettingsImmediate(filterSettings[static_cast<std::size_t>(filterIndex)]);
                    }
                }

                sourceAngle += sourceAngleDelta;
                if (sourceAngle >= juce::MathConstants<double>::twoPi)
                {
                    sourceAngle -= juce::MathConstants<double>::twoPi;
                }
                continue;
            }

            if (!audibleForCurrentNote)
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
#if PX3_DIAGNOSTICS
        const auto ampEnvGainForAudio = diag.bypassAmpEnvGain ? 1.0f : env;
#else
        const auto ampEnvGainForAudio = env;
#endif
        auto voiceGain = level * ampEnvGainForAudio * subtractiveSettings.masterGain;

        // Fast attacks can still produce a tiny startup edge when many voices
        // overlap; apply a very short, attack-dependent onset guard only while
        // keys are held. Slow attacks are effectively unchanged.
#if PX3_DIAGNOSTICS
        if (isKeyDown() && !diag.disableOnsetGuard)
#else
        if (isKeyDown())
#endif
        {
            const auto attackSeconds = juce::jmax(0.001f, envelopeSettings.attackSeconds);
            if (attackSeconds < 0.02f)
            {
                const auto fastAttackNorm = juce::jlimit(0.0f, 1.0f, (0.02f - attackSeconds) / 0.019f);
                const auto onsetSamples = juce::jlimit(8,
                                                       96,
                                                       static_cast<int>(8.0f + 88.0f * fastAttackNorm));
                const auto onsetPos = juce::jlimit(0.0f,
                                                   1.0f,
                                                   static_cast<float>(noteAgeSamples)
                                                       / static_cast<float>(juce::jmax(1, onsetSamples)));
                const auto onsetGuard = onsetPos * onsetPos;
                voiceGain *= onsetGuard;
            }
        }

        if (vibeActive)
        {
            const auto gainVariation = (vibeVariation.gainOffset * vibeTuning.voiceVariation
                                        + vibeShared.psu * vibeTuning.psuMovement * 0.12f
                                        + vibeShared.temperature * vibeTuning.temperatureDrift * 0.10f) * vibeDepth;
            voiceGain *= (1.0f + gainVariation);
            voiceGain = juce::jlimit(0.0f, 2.0f, voiceGain);
        }

        voiceGain *= fastReleaseGain;

#if PX3_DIAGNOSTICS
        if (diagHasPrevVoiceGain)
        {
            diag.noteVoiceGainDelta(voiceGain - diagPrevVoiceGain);
        }
        diagPrevVoiceGain = voiceGain;
        diagHasPrevVoiceGain = true;
        float diagOscStageSum = 0.0f;
        float diagPostEnvStageSum = 0.0f;
#endif

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

            auto voicedSample = filteredSample * voiceGain * perSourceNormalization;
#if PX3_DIAGNOSTICS
            diagOscStageSum += filteredSample * perSourceNormalization;
            diagPostEnvStageSum += voicedSample;
#endif
            if (vibeActive && !ecoReleaseVoice)
            {
                const auto vcaAmount = (vibeTuning.vcaNonlinearity * vibeDepth
                                        + vibeShared.chaos * vibeTuning.correlatedChaos * 0.16f * vibeDepth)
                                       * (0.28f + 0.72f * releaseTailShape);
                voicedSample = std::tanh(voicedSample * (1.0f + vcaAmount * 3.2f))
                              * (1.0f / (1.0f + vcaAmount * 0.95f));
            }

#if PX3_DIAGNOSTICS
            if (!isKeyDown() && !diag.disableReleaseTailFilter)
#else
            if (!isKeyDown())
#endif
            {
                auto& tailState = releaseSmoothingState[static_cast<std::size_t>(sourceIndex)];
                const auto tailSmooth = 0.02f + 0.18f * releaseTailShape;
                tailState += (voicedSample - tailState) * tailSmooth;
                voicedSample = tailState;
            }
            else
            {
                releaseSmoothingState[static_cast<std::size_t>(sourceIndex)] = voicedSample;
            }

            voicedSample = sanitizeAudioSample(voicedSample);

            voicedSourceSamples[static_cast<std::size_t>(sourceIndex)] = voicedSample;
            blockSourcePeaks[static_cast<std::size_t>(sourceIndex)] = juce::jmax(blockSourcePeaks[static_cast<std::size_t>(sourceIndex)],
                                                                                  std::abs(voicedSample));
            summedSample += voicedSample;
        }

        summedSample = sanitizeAudioSample(summedSample);
        blockPeak = juce::jmax(blockPeak, std::abs(summedSample));

#if PX3_DIAGNOSTICS
        diagLastVoiceOut = summedSample;
        if (diag.capturing)
        {
            const auto diagIndex = startSample + sample;
            diag.addVoiceSample(px3::diag::stageOsc, diagIndex, diagOscStageSum);
            diag.addVoiceSample(px3::diag::stagePostEnv, diagIndex, diagPostEnvStageSum);
            diag.addVoiceSample(px3::diag::stageVoiceOut, diagIndex, summedSample);
        }
#endif

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

    lastBlockPeak = blockPeak;
    lastBlockSourcePeaks = blockSourcePeaks;

    // Source interface contract for processor-side modulation sampling is a
    // block representative value. Use peak-per-block so short envelopes are
    // still observable by downstream control-rate modulation reads.
    if (numSamples > 0)
    {
        for (std::size_t envIndex = 0; envIndex < modEnvelopeGenerators.size(); ++envIndex)
        {
            modEnvelopeValues[envIndex] = modEnvelopeEnabled[envIndex]
                                              ? juce::jlimit(0.0f, 1.0f, modEnvelopePeakValues[envIndex])
                                              : 0.0f;
        }
    }

    if (!ampEnvelope.isActive())
    {
        currentAmpEnvelopeValue = 0.0f;
#if PX3_DIAGNOSTICS
        diagNoteEnvelopeInactiveClear(startSample + numSamples - 1);
#endif
        clearCurrentNote();
        angleDelta = 0.0;
    }
}

#if PX3_DIAGNOSTICS
void SynthVoice::diagNoteEnvelopeInactiveClear(int sampleIndex)
{
    auto& diag = px3::diag::state();
    if (!diag.capturing || !isVoiceActive())
    {
        return;
    }

    ++diag.clearFromEnvInactive;
    ++diag.clearCurrentNoteEvents;
    diag.maxTruncationStep = juce::jmax(diag.maxTruncationStep, std::abs(diagLastVoiceOut));
    diag.markLifecycle(sampleIndex);
}
#endif

void SynthVoice::setAmpEnvelope(const EnvelopeSettings& settings)
{
    envelopeSettings = settings;
    if (ampEnvelopeEnabled)
    {
        ampEnvelope.setSettings(settings);
    }
}

void SynthVoice::setAmpEnvelopeEnabled(bool shouldEnable)
{
    ampEnvelopeEnabled = shouldEnable;
    if (ampEnvelopeEnabled)
    {
        ampEnvelope.setSettings(envelopeSettings);
    }
    else
    {
        ampEnvelope.setSettings(EnvelopeSettings {});
    }
}

void SynthVoice::setModEnvelopeSettings(const std::array<EnvelopeSettings, 3>& settings,
                                        const std::array<bool, 3>& enabled)
{
    modEnvelopeSettings = settings;
    modEnvelopeEnabled = enabled;

    for (std::size_t envIndex = 0; envIndex < modEnvelopeGenerators.size(); ++envIndex)
    {
        modEnvelopeGenerators[envIndex].setSettings(modEnvelopeSettings[envIndex]);
        if (!modEnvelopeEnabled[envIndex])
        {
            modEnvelopeGenerators[envIndex].reset();
            modEnvelopeValues[envIndex] = 0.0f;
        }
    }
}

float SynthVoice::getModEnvelopeValue(int envIndex) const
{
    if (envIndex < 0 || envIndex >= static_cast<int>(modEnvelopeValues.size()))
    {
        return 0.0f;
    }

    return modEnvelopeValues[static_cast<std::size_t>(envIndex)];
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

float SynthVoice::getCurrentAmpEnvelopeValue() const
{
    return currentAmpEnvelopeValue;
}

float SynthVoice::getLastBlockPeak() const
{
    return lastBlockPeak;
}

float SynthVoice::getLastBlockSourcePeak(int sourceIndex) const
{
    if (sourceIndex < 0 || sourceIndex >= kVoiceMixerSourceCount)
    {
        return 0.0f;
    }

    return lastBlockSourcePeaks[static_cast<std::size_t>(sourceIndex)];
}

int SynthVoice::getNoteAgeSamples() const
{
    return noteAgeSamples;
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
