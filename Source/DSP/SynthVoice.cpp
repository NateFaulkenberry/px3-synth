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

// Sine saturation, after the approach used in Airwindows' Console family
// (Chris Johnson, MIT licence - see THIRD_PARTY_NOTICES.md). Below the quarter
// cycle sin(x) is very nearly x, so it is transparent at low level and folds
// smoothly as it approaches the peak; past that point it is held rather than
// allowed to fold back, which would sound like ring modulation.
inline float sineSaturate(float x)
{
    constexpr float quarterCycle = 1.57079633f;
    if (x > quarterCycle) return 1.0f;
    if (x < -quarterCycle) return -1.0f;
    return std::sin(x);
}

// Output gain that makes a sine saturator unity at a nominal operating level,
// so driving it harder trades peaks for density instead of simply turning the
// signal down. Normalising at zero level instead makes the stage quieter the
// harder it is driven; normalising by an unrelated constant - which is what
// this code used to do - makes it a level-dependent gain, loud on quiet signals
// and quiet on loud ones.
//
// gain = nominal / sin(nominal * drive), evaluated through the series expansion
// of 1/sinc so the hot path does not need a second sine per sample.
inline float saturationMakeupGain(float drive)
{
    constexpr float nominal = 0.30f;
    const auto y = nominal * drive;
    const auto y2 = y * y;
    const auto inverseSinc = 1.0f + y2 * (1.0f / 6.0f) + y2 * y2 * (7.0f / 360.0f);
    return inverseSinc / drive;
}

// Long enough to be inaudible, short enough that a pruned release tail stops
// consuming CPU within a single typical host block.
constexpr float kFastReleaseSeconds = 0.005f;

// The release lowpass is faded in over this long instead of being switched on
// at note-off. Switching it on multiplies the waveform's per-sample increment
// by the filter coefficient in a single sample, which measured as a 52-86%
// instantaneous slope drop - an audible click at the instant of key release.
constexpr float kReleaseFilterBlendSeconds = 0.010f;

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

#if PX3_DIAGNOSTICS
namespace px3::diag
{
void resetNoteStartSequence()
{
    gNoteStartSequence.store(1u, std::memory_order_relaxed);
    juce::Random::getSystemRandom().setSeed(20260827);
}
}
#endif

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
    startSequence = sequence;
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
    // Filters are PREPARED in setCurrentPlaybackSampleRate, not here.
    //
    // VoiceFilter::prepare reaches CombResonator::prepare, which sizes a delay
    // line with std::vector::assign - a 3856 byte heap allocation, taken on the
    // audio thread at the exact moment a note starts. Captured at the
    // allocation:
    //
    //     SynthVoice::startNote
    //       -> VoiceFilter::prepare
    //         -> px3::CombResonator::prepare
    //           -> std::vector<float>::assign
    //             -> operator new
    //
    // malloc can block for as long as the allocator's own lock is held, and a
    // missed deadline is a dropout, heard as a click on every note.
    //
    // The guard below is a safety net for a rate that changed without
    // setCurrentPlaybackSampleRate being called; in normal operation it never
    // fires. What genuinely belongs per note is the state clearing and the
    // settings, and both stay - reset() fills the same delay line with
    // std::fill and allocates nothing, which is what prepare() called it for.
    if (std::abs(sampleRate - filtersPreparedSampleRate) > 0.5)
    {
        for (auto& sourceRow : sourceFilters)
        {
            for (auto& filter : sourceRow)
            {
                filter.prepare(sampleRate);
            }
        }
        filtersPreparedSampleRate = sampleRate;
    }

    for (int sourceIndex = 0; sourceIndex < kVoiceMixerSourceCount; ++sourceIndex)
    {
        for (int filterIndex = 0; filterIndex < kFilterInstanceCount; ++filterIndex)
        {
            auto& filter = sourceFilters[static_cast<std::size_t>(sourceIndex)][static_cast<std::size_t>(filterIndex)];
            filter.reset();
            filter.setCurrentSettingsImmediate(filterSettings[static_cast<std::size_t>(filterIndex)]);
        }
    }
    // ~12 Hz coupling capacitor: blocks DC without touching the bass.
    vibeCouplingCoeff = std::exp(-2.0f * juce::MathConstants<float>::pi * 12.0f
                                 / static_cast<float>(juce::jmax(1.0, sampleRate)));
    subOscillator.prepare(sampleRate);
    subOscillator.setSettings(subOscillatorSettings);
    subOscillator.resetForNote();

    if (std::abs(sampleRate - masterGainPreparedSampleRate) > 0.5)
    {
        masterGainSmoother.prepare(sampleRate, 0.015);
        vibeGainSmoother.prepare(sampleRate, 0.010);
        sourceNormalisationSmoother.prepare(sampleRate, 0.015);
        masterGainPreparedSampleRate = sampleRate;
    }
    vibeGainPrimed = false;
    sourceNormalisationPrimed = false;
    // Start at the current value: a new note must not fade in from wherever the
    // previous note left the smoother.
    masterGainSmoother.setCurrent(subtractiveSettings.masterGain);

    // The SHAPE when there is one, the parameters otherwise.
    //
    // This rebuilt from envelopeSettings unconditionally, which threw away the
    // shape the processor had just handed the voice. Once a curve is edited
    // past what four numbers can describe, the parameters are no longer
    // written back - so they keep whatever they last held, and the note began
    // on that instead.
    //
    // Captured from a real host: the voice held attackSeconds 0.0120 while the
    // drawn envelope had a four second attack, and only for its first block -
    // the next block's push reinstated the shape. A 12 ms attack is ~90% done
    // by the end of a 512 sample block, which is the 0.771 the capture read,
    // and then the level collapsed to where the real attack had got to. That
    // is the click.
    if (hasShapedAmpEnvelope)
    {
        ampEnvelope.setEnvelope(shapedAmpEnvelope);
    }
    else
    {
        ampEnvelope.setSettings(envelopeSettings);
    }
    ampEnvelope.noteOn();
    for (std::size_t envIndex = 0; envIndex < modEnvelopeGenerators.size(); ++envIndex)
    {
        // The shape when there is one, for the same reason as the amp envelope.
        if (hasShapedModEnvelopes)
        {
            modEnvelopeGenerators[envIndex].setEnvelope(shapedModEnvelopes[envIndex]);
        }
        else
        {
            modEnvelopeGenerators[envIndex].setSettings(modEnvelopeSettings[envIndex]);
        }
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
    releaseAgeSamples = 0;
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
    subAudibleForCurrentNote = true;
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

        // A note-off marked for the PREVIOUS note but never consumed does not
        // belong to this one. The mark is deferred - stopNote does not know the
        // sample index within the block, so it is placed at the next render -
        // and a voice that is already silent when the key is released may be
        // retired without rendering again. The flag then survived into whatever
        // note next reused this voice, and the note-off metric scored that
        // note's ATTACK as a release transient: measured at 8.7 against a
        // threshold of 6, with the stale mark landing one sample after the new
        // note's own start mark.
        //
        // Dropping it loses nothing. A voice with no audio left has no note-off
        // transient to measure.
        diagMarkNoteOff = false;

        diagHasPrevEnv = false;
        diagHasPrevVoiceGain = false;
        diagVoiceGainHistory = 0;
        diagHasPrevMasterGain = false;
        diagHasPrevSourceNorm = false;
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

#if PX3_DIAGNOSTICS
    diagMarkNoteOff = true;
#endif

    ampEnvelope.noteOff();
    for (std::size_t envIndex = 0; envIndex < modEnvelopeGenerators.size(); ++envIndex)
    {
        if (modEnvelopeEnabled[envIndex])
        {
            modEnvelopeGenerators[envIndex].noteOff();
        }
    }
}

void SynthVoice::setCurrentPlaybackSampleRate(double newRate)
{
    juce::SynthesiserVoice::setCurrentPlaybackSampleRate(newRate);

    for (auto& oscillatorUnit : oscillatorUnits)
    {
        oscillatorUnit.prepare(newRate);
    }

    // The filters belong here for the same reason the oscillators do, and did
    // not: VoiceFilter::prepare reaches CombResonator::prepare, which sizes a
    // delay line with std::vector::assign. Preparing them from startNote put
    // that allocation on the audio thread, at note-on.
    for (auto& sourceRow : sourceFilters)
    {
        for (auto& filter : sourceRow)
        {
            filter.prepare(newRate);
        }
    }
    filtersPreparedSampleRate = newRate;
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
    if (diagMarkNoteOff)
    {
        diag.markNoteOff(startSample);
        diagMarkNoteOff = false;
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

    // Each layer's detune is fixed for the block: pitch, coarse and fine come
    // from oscillatorLayerSettings, which the processor refreshes once per
    // block. Computed per sample this was three exp2 calls per sample per
    // voice - 8% of CPU in a 64-voice profile - for three values that cannot
    // change between samples.
    std::array<double, kOscillatorSourceCount> sourcePitchRatios { { 1.0, 1.0, 1.0 } };
    for (int oscIndex = 0; oscIndex < kOscillatorSourceCount; ++oscIndex)
    {
        const auto& layer = oscillatorLayerSettings[static_cast<std::size_t>(oscIndex)];
        const auto semitoneOffset = static_cast<double>(layer.pitchSemitones)
                                    + static_cast<double>(layer.coarseSemitones)
                                    + static_cast<double>(layer.fineCents) * 0.01;
        sourcePitchRatios[static_cast<std::size_t>(oscIndex)] = std::pow(2.0, semitoneOffset / 12.0);
    }

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

        // Scheduled off release progress, not off the envelope value. Keying it
        // to the value meant the filter's timing moved when the AMP ENV release
        // curve changed shape: an exponential release reaches 0.02 less than
        // halfway through the tail, which pinned this filter at its most
        // aggressive setting for the rest of every release.
#if PX3_DIAGNOSTICS
        const auto releaseTailShape =
            isKeyDown()
                ? 1.0f
                : (diag.legacyTailShapeFromEnv
                       ? juce::jlimit(0.0f, 1.0f, (env - 0.02f) / 0.30f)
                       : juce::jlimit(0.0f, 1.0f, (0.98f - ampEnvelope.getReleaseProgress()) / 0.30f));
#else
        const auto releaseTailShape =
            isKeyDown()
                ? 1.0f
                : juce::jlimit(0.0f, 1.0f, (0.98f - ampEnvelope.getReleaseProgress()) / 0.30f);
#endif
#if PX3_DIAGNOSTICS
        if (diag.capturing && !isKeyDown())
        {
            ++diag.releaseSamplesTotal;

            // Only where there is still something to filter.
            //
            // The fault this counts was the tail filter pinned at its most
            // aggressive setting while the tail was still LOUD - scheduled off
            // the envelope VALUE, which an exponential release crosses less
            // than halfway through. Counting every heavily-filtered sample
            // instead measured the last sliver of every tail, which is a
            // fixed slice of release PROGRESS and therefore a large fraction
            // of a short release and a negligible one of a long release. A
            // clean 10 ms release scored 19.5% against a 10% threshold with
            // its loudest heavily-filtered sample at 0.0017 - about -55 dBFS,
            // which is the voice on its way out rather than an artifact.
            //
            // Gated at -60 dBFS the same case scores 3.9%, and the reproduced
            // fault (PX3Diag regress-tailbug) scores 41% at -26 dBFS. The two
            // are no longer told apart by how long the release happens to be.
            constexpr auto kAudibleTailFloor = 0.001f;
            if (releaseTailShape < 0.1f && env > kAudibleTailFloor)
            {
                ++diag.releaseSamplesHeavilyFiltered;
            }
        }
#endif

        // Fade the release lowpass in rather than switching it on. At blend 0
        // the output is bit-for-bit the unfiltered signal, so note-off is
        // continuous in both value and slope.
        auto releaseFilterBlend = 0.0f;
        if (isKeyDown())
        {
            releaseAgeSamples = 0;
        }
        else
        {
            const auto blendSamples = juce::jmax(1, static_cast<int>(kReleaseFilterBlendSeconds
                                                                     * static_cast<float>(sampleRate)));
            const auto t = juce::jlimit(0.0f,
                                        1.0f,
                                        static_cast<float>(releaseAgeSamples) / static_cast<float>(blendSamples));
            releaseFilterBlend = t * t * (3.0f - 2.0f * t);
            ++releaseAgeSamples;
        }
#if PX3_DIAGNOSTICS
        if (diag.legacyInstantReleaseFilter)
        {
            releaseFilterBlend = isKeyDown() ? 0.0f : 1.0f;
        }
#endif

        auto ecoReleaseVoice = !isKeyDown();
#if PX3_DIAGNOSTICS
        if (diag.freezeVibeReleaseSwitch)
        {
            ecoReleaseVoice = false;
        }
#endif

        const auto applyVibeSourceStage = [&](float inSample, float noiseScale, int sourceSlot)
        {
            if (!vibeActive)
            {
                return inSample;
            }

            // Release voices can dominate CPU under dense overlap. Keep held
            // voices fully detailed, but run release tails through a cleaner,
            // lighter path to prevent real-time overload artifacts.
            //
            // Crossfade into that lighter path rather than switching to it, for
            // the same reason as the release lowpass: an instantaneous change of
            // waveshaping at note-off is a discontinuity at the exact moment the
            // key is released.
            const auto detailMix = 1.0f - releaseFilterBlend;
            if (ecoReleaseVoice && detailMix <= 0.0f)
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

            // Sine saturation rather than tanh, and normalised by its own drive
            // so the small-signal gain is unity. The previous form divided by a
            // separate constant, which made the stage a level-dependent gain:
            // up to +6.6 dB on a quiet signal and -1.5 dB on a loud one.
            const auto drive = 1.0f + sat * 3.4f;
            auto stageSample = sineSaturate(asymShaped * drive) * saturationMakeupGain(drive);

            // Pink-weighted hiss. Real analog noise falls with frequency; flat
            // white noise reads as digital.
            //
            // The gain is strictly PROPORTIONAL to the amount. It used to be
            // "0.0035 + 0.0165 * amount", and that fixed floor did not scale
            // with the amount knob or with the NOISE tuning: hiss went from
            // -161 dBFS with vibe off to -75 dBFS the instant the stage
            // engaged, an 86 dB step that landed within 14 dB of the hiss at
            // FULL amount. At the lowest usable setting the floor was 99% of
            // the noise gain, so neither the knob nor the profile could be
            // heard - Clean (noise 0.03) and LoFi (noise 0.84) measured the
            // same. The coefficient is set so full amount is unchanged.
            const auto white = oscillatorUnits[0].nextDeterministicNoise();
            vibePinkState[0] = 0.99765f * vibePinkState[0] + white * 0.0990460f;
            vibePinkState[1] = 0.96300f * vibePinkState[1] + white * 0.2965164f;
            vibePinkState[2] = 0.57000f * vibePinkState[2] + white * 1.0526913f;
            const auto pink = (vibePinkState[0] + vibePinkState[1] + vibePinkState[2] + white * 0.1848f) * 0.22f;

            const auto noiseAmount = vibeTuning.noise * vibeDepth * (0.55f + 0.45f * std::abs(vibeShared.psu));
            const auto noiseTailScale = 0.18f + 0.82f * releaseTailShape;
            stageSample += pink
                           * (0.0218f * noiseAmount)
                           * noiseTailScale
                           * juce::jlimit(0.0f, 1.0f, noiseScale);

            // Coupling capacitor. The asymmetry term is a squared quantity and
            // therefore carries DC; a real circuit blocks it here rather than
            // letting it consume headroom all the way to the output.
            const auto slot = static_cast<std::size_t>(juce::jlimit(0, kVoiceMixerSourceCount - 1, sourceSlot));
            const auto coupled = stageSample - vibeCouplingX1[slot] + vibeCouplingCoeff * vibeCouplingY1[slot];
            vibeCouplingX1[slot] = stageSample;
            vibeCouplingY1[slot] = coupled;

            return inSample + (coupled - inSample) * juce::jlimit(0.0f, 1.0f, detailMix);
        };

        currentPitchBendNorm += (targetPitchBendNorm - currentPitchBendNorm) * 0.06f;
        currentModWheelNorm += (targetModWheelNorm - currentModWheelNorm) * 0.045f;

        auto bendSemitones = static_cast<double>(currentPitchBendNorm * pitchBendRangeSemitones);

        // The vibrato LFO is only ever heard through the mod wheel depth. With
        // the wheel at rest the product is zero whatever the sine returns, so
        // the sine is skipped rather than computed and multiplied away - it was
        // a libm call per sample per voice on every patch, played or not.
        auto vibratoSemitones = 0.0;
        const auto vibratoDepthSemitones = currentModWheelNorm * vibratoMaxDepthSemitones;
        if (vibratoDepthSemitones != 0.0f)
        {
            const auto lfo = std::sin(static_cast<double>(sharedVibratoPhaseRadians)
                                      + vibratoPhaseInc * static_cast<double>(sample));
            vibratoSemitones = static_cast<double>(vibratoDepthSemitones) * lfo;
        }

        if (vibeActive)
        {
            const auto driftCents = (vibeShared.oscillatorDrift * vibeTuning.oscillatorDrift * 32.0f
                                     + vibeShared.psu * vibeTuning.psuMovement * 13.0f
                                     + vibeShared.temperature * vibeTuning.temperatureDrift * 18.0f
                                     + vibeShared.chaos * vibeTuning.correlatedChaos * 12.0f
                                     + vibeVariation.pitchCents * vibeTuning.voiceVariation) * vibeDepth;
            bendSemitones += static_cast<double>(juce::jlimit(-60.0f, 60.0f, driftCents) * 0.01f);
        }

        // Bend, vibrato depth and vibe drift are all settled for most of a
        // block, which makes this exponent identical from sample to sample.
        // Keyed on the exponent, so any change still recomputes: this returns
        // the same value the unconditional call would have, never a stale one.
        const auto pitchExponent = (bendSemitones + vibratoSemitones) / 12.0;
        if (pitchExponent != lastPitchExponent)
        {
            lastPitchExponent = pitchExponent;
            lastPitchRatio = std::pow(2.0, pitchExponent);
        }
        const auto pitchRatio = lastPitchRatio;

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
            const auto sourcePitchRatio = sourcePitchRatios[static_cast<std::size_t>(oscIndex)];
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

            const auto sourceSample = oscillatorUnits[static_cast<std::size_t>(oscIndex)].renderSample(sampleRate, sourceContext);
            auto sourceStageSample = softClip(sanitizeAudioSample(sourceSample) * 0.92f);
            sourceStageSample = applyVibeSourceStage(sourceStageSample, 1.0f, oscIndex + 1);
            // Source level is a trim on the oscillator's OUTPUT, applied after
            // its own soft clipper. Applied before the clipper it would also
            // change how hard the oscillator saturates, so moving the headroom
            // here would have altered the tone as well as the level.
            sourceStageSample = sanitizeAudioSample(sourceStageSample) * juce::jlimit(0.0f, 1.0f, layer.level);
            sourceSamples[static_cast<std::size_t>(oscIndex + 1)] = sourceStageSample;

            sourceAngle += sourceAngleDelta;
            if (sourceAngle >= juce::MathConstants<double>::twoPi)
            {
                sourceAngle -= juce::MathConstants<double>::twoPi;
            }
        }

        auto subStageSample = 0.0f;
        const auto subGain = juce::jlimit(0.0f, 1.0f, subOscillatorSettings.level);
        constexpr int kSubSourceIndex = 0;

        if (!subOscillatorSettings.enabled)
        {
            if (subAudibleForCurrentNote)
            {
                subAudibleForCurrentNote = false;

                // Bypassing the sub while a note is still sounding clears its
                // per-source filter memory and release-tail state, so the tail
                // is cut rather than left ringing, and switching the sub back on
                // mid-note cannot resurrect it. This matches the oscillator
                // layers exactly; the sub previously had no such handling.
                for (int filterIndex = 0; filterIndex < kFilterInstanceCount; ++filterIndex)
                {
                    auto& filter = sourceFilters[kSubSourceIndex][static_cast<std::size_t>(filterIndex)];
                    filter.reset();
                    filter.setCurrentSettingsImmediate(filterSettings[static_cast<std::size_t>(filterIndex)]);
                }
                releaseSmoothingState[kSubSourceIndex] = 0.0f;
                subOscillator.resetForNote();
            }
        }
        else if (subAudibleForCurrentNote)
        {
            subStageSample = softClip(subSample * 0.92f);
            subStageSample = applyVibeSourceStage(subStageSample, 0.6f, kSubSourceIndex);
            // Trim after the clipper, for the same reason as the oscillators.
            subStageSample = sanitizeAudioSample(subStageSample) * subGain;
        }

        sourceSamples[kSubSourceIndex] = subStageSample;

        // AMP STAGE: envelope and voice gain are downstream of filter.
#if PX3_DIAGNOSTICS
        const auto ampEnvGainForAudio = diag.bypassAmpEnvGain ? 1.0f : env;
#else
        const auto ampEnvGainForAudio = env;
#endif
        auto smoothedMasterGain = masterGainSmoother.next(subtractiveSettings.masterGain);
#if PX3_DIAGNOSTICS
        if (diag.legacyUnsmoothedMixer)
        {
            smoothedMasterGain = subtractiveSettings.masterGain;
        }
#endif
#if PX3_DIAGNOSTICS
        if (diag.capturing)
        {
            if (diagHasPrevMasterGain)
            {
                diag.maxMasterGainStep = juce::jmax(diag.maxMasterGainStep,
                                                    std::abs(smoothedMasterGain - diagPrevMasterGain));
            }
            diagPrevMasterGain = smoothedMasterGain;
            diagHasPrevMasterGain = true;
        }
#endif
        auto voiceGain = level * ampEnvGainForAudio * smoothedMasterGain;

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
                // Smoothstep, not onsetPos^2. A squared ramp arrives at full
                // gain with a slope of 2/onsetSamples still on it and then
                // clamps, leaving a corner in the amplitude envelope exactly
                // onsetSamples after note-on. That corner is inaudible on
                // harmonically rich waveforms but is a distinct tick on a sine,
                // which has nothing of its own to mask it. Smoothstep reaches
                // 1.0 with zero slope, so the guard lands flat.
                const auto smoothstep = onsetPos * onsetPos * (3.0f - 2.0f * onsetPos);
#if PX3_DIAGNOSTICS
                const auto curve = px3::diag::state().onsetGuardCurve;
                const auto onsetGuard = curve == 1 ? onsetPos * onsetPos
                                      : curve == 2 ? smoothstep
                                                   : smoothstep * smoothstep;
#else
                const auto onsetGuard = smoothstep * smoothstep;
#endif
                voiceGain *= onsetGuard;
            }
        }

        if (vibeActive)
        {
            const auto gainVariation = (vibeVariation.gainOffset * vibeTuning.voiceVariation
                                        + vibeShared.psu * vibeTuning.psuMovement * 0.12f
                                        + vibeShared.temperature * vibeTuning.temperatureDrift * 0.10f) * vibeDepth;
            const auto vibeGainTarget = 1.0f + gainVariation;
            if (!vibeGainPrimed)
            {
                // Start at the current value so a note does not swell in.
                vibeGainSmoother.setCurrent(vibeGainTarget);
                vibeGainPrimed = true;
            }
            voiceGain *= vibeGainSmoother.next(vibeGainTarget);
            voiceGain = juce::jlimit(0.0f, 2.0f, voiceGain);
        }

        voiceGain *= fastReleaseGain;

        auto smoothedNormalisation = perSourceNormalization;
        if (!sourceNormalisationPrimed)
        {
            sourceNormalisationSmoother.setCurrent(perSourceNormalization);
            sourceNormalisationPrimed = true;
        }
        else
        {
            smoothedNormalisation = sourceNormalisationSmoother.next(perSourceNormalization);
        }

#if PX3_DIAGNOSTICS
        if (diag.capturing)
        {
            if (diagHasPrevSourceNorm)
            {
                diag.maxSourceNormalisationStep = juce::jmax(diag.maxSourceNormalisationStep,
                                                             std::abs(smoothedNormalisation - diagPrevSourceNorm));
            }
            diagPrevSourceNorm = smoothedNormalisation;
            diagHasPrevSourceNorm = true;
        }
        if (diagHasPrevVoiceGain)
        {
            diag.noteVoiceGainDelta(voiceGain - diagPrevVoiceGain);
            if (diagVoiceGainHistory >= 2)
            {
                const auto curvature = std::abs(voiceGain - 2.0f * diagPrevVoiceGain + diagPrevVoiceGain2);
                if (curvature > diag.maxVoiceGainCurvature)
                {
                    diag.maxVoiceGainCurvature = curvature;
                    diag.worstCurvatureSample = diag.globalSampleBase + startSample + sample;
                    diag.worstCurvatureNoteAge = noteAgeSamples;
                    diag.worstCurvatureKeyDown = isKeyDown();
                    diag.worstCurvatureGains[0] = diagPrevVoiceGain2;
                    diag.worstCurvatureGains[1] = diagPrevVoiceGain;
                    diag.worstCurvatureGains[2] = voiceGain;
                    diag.worstCurvatureEnv = env;
                }
            }
        }
        diagPrevVoiceGain2 = diagPrevVoiceGain;
        diagPrevVoiceGain = voiceGain;
        diagHasPrevVoiceGain = true;
        ++diagVoiceGainHistory;
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
                // Bypass is the filter's own business, so every sample goes
                // through every instance and enabling/disabling crossfades
                // instead of switching.
                filteredSample = sourceFilters[static_cast<std::size_t>(sourceIndex)][static_cast<std::size_t>(filterIndex)].processSample(filteredSample);
                filteredSample = sanitizeAudioSample(filteredSample);
            }

            auto voicedSample = filteredSample * voiceGain * smoothedNormalisation;
#if PX3_DIAGNOSTICS
            diagOscStageSum += filteredSample * smoothedNormalisation;
            diagPostEnvStageSum += voicedSample;
#endif
            const auto vcaDetailMix = juce::jlimit(0.0f, 1.0f, 1.0f - releaseFilterBlend);
            if (vibeActive && vcaDetailMix > 0.0f)
            {
                const auto vcaAmount = (vibeTuning.vcaNonlinearity * vibeDepth
                                        + vibeShared.chaos * vibeTuning.correlatedChaos * 0.16f * vibeDepth)
                                       * (0.28f + 0.72f * releaseTailShape);
                // Normalised by its own drive so a quiet voice passes at unity.
                // Dividing by an unrelated constant made this a level-dependent
                // gain stage, which is why vibe grew louder as sources got
                // quieter instead of simply adding character.
                const auto vcaDrive = 1.0f + vcaAmount * 3.2f;
                const auto shapedSample = sineSaturate(voicedSample * vcaDrive)
                                          * saturationMakeupGain(vcaDrive);
                voicedSample += (shapedSample - voicedSample) * vcaDetailMix;
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
                voicedSample += (tailState - voicedSample) * releaseFilterBlend;
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

    // The processor pushes the ADSR settings every block and follows them with
    // the full shape only when there is one, so this is where "there is no
    // shape any more" is learned.
    hasShapedAmpEnvelope = false;

    if (ampEnvelopeEnabled)
    {
        ampEnvelope.setSettings(settings);
    }
}

void SynthVoice::setAmpEnvelopeShape(const px3::BreakpointEnvelope& envelope)
{
    shapedAmpEnvelope = envelope;
    hasShapedAmpEnvelope = true;

    if (ampEnvelopeEnabled)
    {
        ampEnvelope.setEnvelope(envelope);
    }
}

void SynthVoice::setModEnvelopeShapes(const std::array<px3::BreakpointEnvelope, 3>& envelopes)
{
    shapedModEnvelopes = envelopes;
    hasShapedModEnvelopes = true;

    for (std::size_t i = 0; i < modEnvelopeGenerators.size(); ++i)
    {
        if (modEnvelopeEnabled[i])
        {
            modEnvelopeGenerators[i].setEnvelope(envelopes[i]);
        }
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
    hasShapedModEnvelopes = false;

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
