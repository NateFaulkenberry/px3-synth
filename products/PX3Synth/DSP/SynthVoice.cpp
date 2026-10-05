#include "SynthVoice.h"
#include "OscillatorTuning.h"
#include "OscillatorDsp.h"

#include "PX3Diagnostics.h"
#include "SynthSound.h"

#include <atomic>
#include <cmath>

namespace
{
std::atomic<uint32_t> gNoteStartSequence { 1u };

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
    // The start phase is hashed from this voice's own note count, not the
    // global sequence, which carries on across processor instances: the same
    // MIDI into a fresh instance has to render the same audio.
    const auto noteCount = ++notesStarted;
    // Every note starts its slop from a different place, seeded from this
    // voice and its own note count (not the cross-instance sequence), so two
    // voices never wander together and a fresh instance renders the same.
    slopRandom = (static_cast<std::uint32_t>(voiceIndex + 1) * 2654435761u) ^ (noteCount * 40503u)
                 ^ static_cast<std::uint32_t>(midiNoteNumber + 1) * 97u;
    slopValue = { { 0.0f, 0.0f, 0.0f } };
    slopHoldBlocks = { { 0, 0, 0 } };
    auto hash = static_cast<uint32_t>(voiceIndex + 1) * 747796405u;
    hash ^= noteCount * 2891336453u;
    hash ^= static_cast<uint32_t>(midiNoteNumber + 1) * 277803737u;
    hash ^= (hash >> 16);
    hash *= 2246822519u;
    const auto phaseSeed = static_cast<double>(hash & 0x00FFFFFFu) / static_cast<double>(0x01000000u);
    currentAngle = juce::MathConstants<double>::twoPi * phaseSeed;
    updateAngleDelta();
    const auto sampleRate = juce::jmax(1.0, getSampleRate());
    updateRateDependentCoefficients(sampleRate);
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
            // Key tracking applied here too, not only as the render's target:
            // started at the untracked cutoff, every tracked note swept up to
            // its own over its first ~20 ms.
            auto start = filterSettings[static_cast<std::size_t>(filterIndex)];
            if (start.keyTrack != 0.0f && currentMidiNote >= 0)
            {
                start.cutoffHz = juce::jlimit(20.0f, 20000.0f,
                                              start.cutoffHz * std::exp2((static_cast<float>(currentMidiNote) - start.keyTrackReference)
                                                                         / 12.0f * start.keyTrack));
            }
            filter.setCurrentSettingsImmediate(start);
        }
    }
    analogStage.prepareCoupling(sampleRate);
    subOscillator.prepare(sampleRate);
    subOscillator.setSettings(subOscillatorSettings);
    // Every source starts at the same phase. The oscillators used to start 120
    // degrees apart and the sub always at zero, so no two were aligned.
    subOscillator.resetForNote(phaseSeed);

    if (std::abs(sampleRate - masterGainPreparedSampleRate) > 0.5)
    {
        masterGainSmoother.prepare(sampleRate, 0.015);
        analogStage.gainSmoother.prepare(sampleRate, 0.010);
        sourceNormalisationSmoother.prepare(sampleRate, 0.015);
        masterGainPreparedSampleRate = sampleRate;
    }
    analogStage.gainPrimed = false;
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
            modEnvelopeGenerators[envIndex].setTimeScale(std::exp2(
                (static_cast<double>(currentMidiNote) - 60.0) / 12.0
                * static_cast<double>(modEnvelopeSettings[envIndex].keyTrack)));
            modEnvelopeGenerators[envIndex].noteOn();
        }
        else
        {
            modEnvelopeGenerators[envIndex].reset();
            modEnvelopeValues[envIndex] = 0.0f;
            modEnvelopeBlockPeaks[envIndex] = 0.0f;
        }
    }
    noteAgeSamples = 0;
    releaseAgeSamples = 0;
    fastReleaseTotalSamples = 0;
    fastReleaseSamplesRemaining = 0;
    // Each oscillator gets its own reproducible random stream, derived from the
    // voice, the note and the oscillator - never the shared system Random, and
    // never one seed shared by every voice.
    for (int oscIndex = 0; oscIndex < kOscillatorSourceCount; ++oscIndex)
    {
        oscillatorUnits[static_cast<std::size_t>(oscIndex)].resetForNote(
            phaseSeed,
            px3::dsp::streamSeed(static_cast<std::uint32_t>(voiceIndex), noteCount, static_cast<std::uint32_t>(oscIndex)));
    }
    analogNoise.seed(px3::dsp::streamSeed(static_cast<std::uint32_t>(voiceIndex), noteCount, 7u));
    for (auto& clip : sourceClips)
    {
        clip.reset();
    }
    sourceRatiosPrimed = false;
    for (auto& audible : oscillatorAudibleForCurrentNote)
    {
        audible = true;
    }
    subAudibleForCurrentNote = true;
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
    subOscillator.prepare(newRate);
    updateRateDependentCoefficients(newRate);

    // The filters belong here for the same reason the oscillators do, and did
    // not: VoiceFilter::prepare reaches CombResonator::prepare, which sizes a
    // delay line with std::vector::assign. Preparing them from startNote put
    // that allocation on the audio thread, at note-on. (In the synth the comb
    // lines now come from the processor's pool - useExternalCombStorage - so
    // this no longer allocates at all; it still must not run at note-on, since
    // it detaches the lines until the next block re-attaches them.)
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
    modEnvelopeBlockPeaks.fill(0.0f);
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

    // ANALOG's global amount is a macro depth control: low values subtle, the
    // top of the range aggressively audible.
    const auto analogDepth = px3::analogdrift::depthForAmount(analogControl.globalAmount, analogControl.bypass);
    const auto analogActive = analogDepth > 0.0001f;

    pushFilterTargets(analogActive, analogDepth);

    std::array<float, kModEnvelopeCount> modEnvelopePeakValues {};
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

    // Tuning and Pitch Mod arrive once per control block. They ramp from the
    // last block's ratio to this one's across the block, so a modulated Pitch
    // Mod glides instead of stepping at the block rate. The ratio itself is
    // still worked out once per block, not once per sample.
    {
        std::array<double, kOscillatorSourceCount> ratios { { 1.0, 1.0, 1.0 } };
        auto changed = false;
        const auto blockSeconds = static_cast<float>(controlBlockLength) / static_cast<float>(juce::jmax(1.0, getSampleRate()));
        // About a 0.35 s glide toward a new random point every 0.6-1.6 s.
        const auto slopGlide = 1.0f - std::exp(-blockSeconds / 0.35f);
        for (int oscIndex = 0; oscIndex < kOscillatorSourceCount; ++oscIndex)
        {
            const auto& layer = oscillatorLayerSettings[static_cast<std::size_t>(oscIndex)];
            auto slopCents = 0.0f;
            if (layer.slop > 0.0f)
            {
                const auto i = static_cast<std::size_t>(oscIndex);
                if (--slopHoldBlocks[i] <= 0)
                {
                    slopRandom = slopRandom * 1664525u + 1013904223u;
                    slopTarget[i] = static_cast<float>(slopRandom >> 8) / static_cast<float>(1u << 24) * 2.0f - 1.0f;
                    slopRandom = slopRandom * 1664525u + 1013904223u;
                    const auto holdSeconds = 0.6f + static_cast<float>(slopRandom >> 8) / static_cast<float>(1u << 24);
                    slopHoldBlocks[i] = juce::jmax(1, static_cast<int>(holdSeconds / juce::jmax(1.0e-4f, blockSeconds)));
                }
                slopValue[i] += (slopTarget[i] - slopValue[i]) * slopGlide;
                slopCents = layer.slop * 12.0f * slopValue[i];
            }
            blockSlopCents[static_cast<std::size_t>(oscIndex)] = slopCents;
            ratios[static_cast<std::size_t>(oscIndex)] =
                px3::tuning::pitchRatio(layer.coarseOctaves, layer.fineCents + slopCents, layer.pitchModSemitones, layer.semitones);
            changed = changed || ratios[static_cast<std::size_t>(oscIndex)] != sourceRatioTarget[static_cast<std::size_t>(oscIndex)];
        }
        if (!sourceRatiosPrimed)
        {
            sourceRatioStart = sourceRatioTarget = sourceRatioCurrent = ratios;
            sourceRatioRampPosition = sourceRatioRampLength = 1;
            sourceRatiosPrimed = true;
        }
        else if (changed)
        {
            sourceRatioStart = sourceRatioCurrent;
            sourceRatioTarget = ratios;
            // With in-voice envelope routes the target is refreshed every
            // control interval, so it ramps across one interval, not the host
            // block - or the glide's length would follow the host's buffer.
            sourceRatioRampLength = activePlan != nullptr ? juce::jmin(controlBlockLength, kEnvelopeControlSamples)
                                                          : controlBlockLength;
            sourceRatioRampPosition = 0;
        }
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

        // Envelope routes into the filters and the oscillators' pitch follow
        // the envelope every kEnvelopeControlSamples, whatever the host's
        // buffer: once per host block, a pluck or a pitch drop was a staircase
        // whose step was the buffer size (21 ms at 1024 samples).
        if (activePlan != nullptr && sample > 0 && sample % kEnvelopeControlSamples == 0)
        {
            refreshEnvelopeTargets(analogActive, analogDepth, juce::jmin(kEnvelopeControlSamples, numSamples - sample));
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
        if (diag.freezeAnalogReleaseSwitch)
        {
            ecoReleaseVoice = false;
        }
#endif

        const auto applyAnalogSourceStage = [&](float inSample, float noiseScale, int sourceSlot)
        {
            if (!analogActive)
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

            return analogStage.processSource(analogControl, analogDepth, releaseTailShape, detailMix,
                                             inSample, noiseScale, sourceSlot, analogNoise);
        };

        currentPitchBendNorm += (targetPitchBendNorm - currentPitchBendNorm) * bendSmoothing;
        currentModWheelNorm += (targetModWheelNorm - currentModWheelNorm) * wheelSmoothing;

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

        if (analogActive)
        {
            bendSemitones += static_cast<double>(px3::analogdrift::pitchDriftCents(analogControl, analogDepth) * 0.01f);
        }

        // Bend, vibrato depth and ANALOG drift are all settled for most of a
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

        if (sourceRatioRampPosition < sourceRatioRampLength)
        {
            ++sourceRatioRampPosition;
            const auto t = static_cast<double>(sourceRatioRampPosition) / static_cast<double>(sourceRatioRampLength);
            for (std::size_t i = 0; i < sourceRatioCurrent.size(); ++i)
            {
                sourceRatioCurrent[i] = sourceRatioStart[i] + (sourceRatioTarget[i] - sourceRatioStart[i]) * t;
            }
        }

        const auto subSample = static_cast<float>(subOscillator.renderSample(currentFrequencyHz));

        std::array<float, kVoiceMixerSourceCount> sourceSamples { { 0.0f, 0.0f, 0.0f, 0.0f } };

        for (int oscIndex = 0; oscIndex < kOscillatorSourceCount; ++oscIndex)
        {
            const auto& layer = oscillatorLayerSettings[static_cast<std::size_t>(oscIndex)];
            auto& audibleForCurrentNote = oscillatorAudibleForCurrentNote[static_cast<std::size_t>(oscIndex)];
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

                continue;
            }

            if (!audibleForCurrentNote)
            {
                continue;
            }

            OscillatorUnit::RenderContext sourceContext;
            sourceContext.frequencyHz = currentFrequencyHz * sourceRatioCurrent[static_cast<std::size_t>(oscIndex)];
            sourceContext.noteAgeSamples = noteAgeSamples;
            sourceContext.modWheelNorm = currentModWheelNorm;

            const auto sourceSample = static_cast<float>(oscillatorUnits[static_cast<std::size_t>(oscIndex)].renderSample(sourceContext));
            // The oscillator's soft clip and the voice's tanh(0.92 x), as the one
            // curve they are in series, anti-aliased (docs/OSCILLATOR_DSP_DESIGN.md).
            auto sourceStageSample = static_cast<float>(sourceClips[static_cast<std::size_t>(oscIndex + 1)].process(
                px3::dsp::kSourceClipAdaa, static_cast<double>(sanitizeAudioSample(sourceSample))));
            sourceStageSample = applyAnalogSourceStage(sourceStageSample, 1.0f, oscIndex + 1);
            // Source level is a trim on the oscillator's OUTPUT, applied after
            // its own soft clipper. Applied before the clipper it would also
            // change how hard the oscillator saturates, so moving the headroom
            // here would have altered the tone as well as the level.
            sourceStageSample = sanitizeAudioSample(sourceStageSample) * juce::jlimit(0.0f, 1.0f, layer.level);
            sourceSamples[static_cast<std::size_t>(oscIndex + 1)] = sourceStageSample;

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
            subStageSample = static_cast<float>(sourceClips[kSubSourceIndex].process(
                px3::dsp::kSourceClipAdaa, static_cast<double>(sanitizeAudioSample(subSample))));
            subStageSample = applyAnalogSourceStage(subStageSample, 0.6f, kSubSourceIndex);
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
                // 8 to 96 samples at 48 kHz, as the same time at any rate.
                const auto onsetSamples = px3::dsp::sampleCount(8.0 + 88.0 * static_cast<double>(fastAttackNorm), sampleRate);
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

        if (analogActive)
        {
            const auto analogGainTarget = px3::analogdrift::gainTarget(analogControl, analogDepth);
            if (!analogStage.gainPrimed)
            {
                // Start at the current value so a note does not swell in.
                analogStage.gainSmoother.setCurrent(analogGainTarget);
                analogStage.gainPrimed = true;
            }
            voiceGain *= analogStage.gainSmoother.next(analogGainTarget);
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

        // One-pole, so the blend approaches its target without the corner a
        // linear ramp lands with.
        filterParallelCurrent += (filterParallelTarget - filterParallelCurrent) * filterRoutingCoeff;
        filterBalanceCurrent += (filterBalanceTarget - filterBalanceCurrent) * filterRoutingCoeff;
        const auto parallelMix = filterParallelCurrent;
        const auto parallelBalance = filterBalanceCurrent;

        std::array<float, kVoiceMixerSourceCount> voicedSourceSamples { { 0.0f, 0.0f, 0.0f, 0.0f } };
        float summedSample = 0.0f;
        for (int sourceIndex = 0; sourceIndex < kVoiceMixerSourceCount; ++sourceIndex)
        {
            // Bypass is each filter's own business, so every sample goes through
            // both instances and enabling or disabling crossfades inside them.
            //
            // One expression covers both routings:
            //   filter 2 hears   y1 + (x - y1) * p     series at p=0, the dry input at p=1
            //   the output is    y2 + (mix - y2) * p   filter 2 at p=0, the balance at p=1
            // so SERIES (p=0) is exactly the old chain and PARALLEL (p=1) is exactly
            // two filters on one input, with every value in between continuous.
            //
            // The parallel mix is LINEAR, not constant-power. Both filters are fed
            // the same signal, so their outputs are correlated: with the two set
            // alike a constant-power law would sum to +3 dB at the centre, where a
            // linear one returns unity.
            static_assert(kFilterInstanceCount == 2, "filter routing is written for two filters");
            const auto dryInput = sourceSamples[static_cast<std::size_t>(sourceIndex)];
            auto& firstFilter = sourceFilters[static_cast<std::size_t>(sourceIndex)][0];
            auto& secondFilter = sourceFilters[static_cast<std::size_t>(sourceIndex)][1];

            const auto firstOut = sanitizeAudioSample(firstFilter.processSample(dryInput));
            const auto secondIn = firstOut + (dryInput - firstOut) * parallelMix;
            const auto secondOut = sanitizeAudioSample(secondFilter.processSample(secondIn));
            const auto parallelOut = firstOut + (secondOut - firstOut) * parallelBalance;
            auto filteredSample = sanitizeAudioSample(secondOut + (parallelOut - secondOut) * parallelMix);

            auto voicedSample = filteredSample * voiceGain * smoothedNormalisation;
#if PX3_DIAGNOSTICS
            diagOscStageSum += filteredSample * smoothedNormalisation;
            diagPostEnvStageSum += voicedSample;
#endif
            const auto vcaDetailMix = juce::jlimit(0.0f, 1.0f, 1.0f - releaseFilterBlend);
            if (analogActive && vcaDetailMix > 0.0f)
            {
                // ANALOG's VCA nonlinearity, normalised by its own drive so a
                // quiet voice passes at unity.
                voicedSample = px3::analogdrift::applyVca(analogControl, analogDepth, releaseTailShape,
                                                          vcaDetailMix, voicedSample);
            }

#if PX3_DIAGNOSTICS
            if (!isKeyDown() && !diag.disableReleaseTailFilter)
#else
            if (!isKeyDown())
#endif
            {
                auto& tailState = releaseSmoothingState[static_cast<std::size_t>(sourceIndex)];
                const auto tailSmooth = tailSmoothingLow + (tailSmoothingHigh - tailSmoothingLow) * releaseTailShape;
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
            modEnvelopeBlockPeaks[envIndex] = modEnvelopeEnabled[envIndex]
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

void SynthVoice::setModEnvelopeShapes(const std::array<px3::BreakpointEnvelope, kModEnvelopeCount>& envelopes)
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

void SynthVoice::setModEnvelopeSettings(const std::array<EnvelopeSettings, kModEnvelopeCount>& settings,
                                        const std::array<bool, kModEnvelopeCount>& enabled)
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
            modEnvelopeBlockPeaks[envIndex] = 0.0f;
        }
    }
}

float SynthVoice::getModEnvelopeValue(int envIndex) const
{
    if (envIndex < 0 || envIndex >= static_cast<int>(modEnvelopeValues.size()))
    {
        return 0.0f;
    }

    return modEnvelopeBlockPeaks[static_cast<std::size_t>(envIndex)];
}

void SynthVoice::useExternalCombStorage() noexcept
{
    for (auto& sourceRow : sourceFilters)
    {
        for (auto& filter : sourceRow)
        {
            filter.useExternalCombStorage();
        }
    }
}

void SynthVoice::attachCombLines(int filterIndex, float* voiceBase, int lineCapacity) noexcept
{
    const auto slot = static_cast<std::size_t>(juce::jlimit(0, kFilterInstanceCount - 1, filterIndex));
    for (int source = 0; source < kVoiceMixerSourceCount; ++source)
    {
        sourceFilters[static_cast<std::size_t>(source)][slot].attachCombLine(
            voiceBase != nullptr ? voiceBase + static_cast<std::ptrdiff_t>(source) * lineCapacity : nullptr,
            lineCapacity);
    }
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

void SynthVoice::setFilterRouting(bool parallel, float balance)
{
    filterParallelTarget = parallel ? 1.0f : 0.0f;
    filterBalanceTarget = juce::jlimit(0.0f, 1.0f, balance);

    const auto sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    filterRoutingCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (kFilterRoutingSmoothingSeconds * sampleRate)));

    // A voice that is not sounding has nothing to click, so it takes the new
    // routing at once rather than ramping into its next note from a stale one.
    if (! ampEnvelope.isActive())
    {
        filterParallelCurrent = filterParallelTarget;
        filterBalanceCurrent = filterBalanceTarget;
    }
}

void SynthVoice::setSubtractiveSettings(const SubtractiveSettings& settings)
{
    subtractiveSettings = settings;
}

void SynthVoice::setSubOscillatorSettings(const SubOscSettings& settings)
{
    subOscillatorSettings = settings;
    subOscillator.setSettings(subOscillatorSettings, controlBlockLength);
}

void SynthVoice::setOscillatorLayerSettings(const std::array<OscillatorLayerSettings, kOscillatorSourceCount>& settings)
{
    oscillatorLayerSettings = settings;
    for (int oscIndex = 0; oscIndex < kOscillatorSourceCount; ++oscIndex)
    {
        // An oscillator the envelope plan modulates is pushed once, by
        // setVoiceModulationPlan, with the modulated values. Pushing the
        // unmodulated settings here as well restarted its macro ramp from the
        // base value every block, so a ramped control (DIGITAL RATE and FOLD,
        // and every other mode's ramped macros) swung from base to target and
        // back each block instead of sitting at the modulated value.
        if (! oscillatorModulatedByPlan[static_cast<std::size_t>(oscIndex)])
        {
            oscillatorUnits[static_cast<std::size_t>(oscIndex)].setSettings(
                oscillatorLayerSettings[static_cast<std::size_t>(oscIndex)].oscillator, controlBlockLength);
        }
    }
}

void SynthVoice::pushFilterTargets(bool analogActive, float analogDepth)
{
    for (int sourceIndex = 0; sourceIndex < kVoiceMixerSourceCount; ++sourceIndex)
    {
        for (int filterIndex = 0; filterIndex < kFilterInstanceCount; ++filterIndex)
        {
            auto runtimeFilter = filterSettings[static_cast<std::size_t>(filterIndex)];

            auto targetCutoffHz = runtimeFilter.cutoffHz;
            auto targetResonanceQ = runtimeFilter.resonanceQ;

            if (runtimeFilter.keyTrack != 0.0f && currentMidiNote >= 0)
            {
                targetCutoffHz *= std::exp2((static_cast<float>(currentMidiNote) - runtimeFilter.keyTrackReference)
                                            / 12.0f * runtimeFilter.keyTrack);
            }

            if (analogActive)
            {
                const auto cutoffMul = px3::analogdrift::cutoffMultiplier(analogControl, analogDepth);
                const auto resoDelta = px3::analogdrift::resonanceDelta(analogControl, analogDepth);

                targetCutoffHz *= cutoffMul;
                targetResonanceQ *= (1.0f + resoDelta);
            }

            runtimeFilter.cutoffHz = juce::jlimit(20.0f, 20000.0f, targetCutoffHz);
            runtimeFilter.resonanceQ = juce::jlimit(0.20f, px3::analogfilter::kMaxUserQ, targetResonanceQ);
            sourceFilters[static_cast<std::size_t>(sourceIndex)][static_cast<std::size_t>(filterIndex)].setTargetSettings(runtimeFilter);
        }
    }

}

float SynthVoice::envelopePlanValue(const px3::synth::VoiceModDestination& d) const noexcept
{
    auto delta = 0.0f;
    for (int r = 0; r < d.routeCount; ++r)
    {
        const auto& route = d.routes[static_cast<std::size_t>(r)];
        const auto envelope = static_cast<std::size_t>(juce::jlimit(0, kModEnvelopeCount - 1, route.envelope));
        delta += px3::synth::routeContribution(route.sourceBipolar, modEnvelopeValues[envelope],
                                               route.polarity, route.curve, route.depth, d.base);
    }
    return px3::synth::rangeFrom0to1(d, px3::synth::foldUnit(d.unfolded + delta));
}

void SynthVoice::refreshEnvelopeTargets(bool analogActive, float analogDepth, int rampSamples)
{
    using T = px3::synth::VoiceModTarget;
    const auto& plan = *activePlan;
    const auto at = [&plan](T t) -> const px3::synth::VoiceModDestination& { return plan.destinations[static_cast<std::size_t>(t)]; };

    auto filtersMoved = false;
    for (const auto& [target, filterIndex, cutoff] : { std::tuple<T, int, bool> { T::filter1Cutoff, 0, true },
                                                       { T::filter2Cutoff, 1, true },
                                                       { T::filter1Resonance, 0, false },
                                                       { T::filter2Resonance, 1, false } })
    {
        const auto& d = at(target);
        if (d.routeCount == 0) { continue; }
        auto& settings = filterSettings[static_cast<std::size_t>(filterIndex)];
        (cutoff ? settings.cutoffHz : settings.resonanceQ) = envelopePlanValue(d);
        filtersMoved = true;
    }
    if (filtersMoved) { pushFilterTargets(analogActive, analogDepth); }

    auto pitchMoved = false;
    for (int osc = 0; osc < kOscillatorSourceCount; ++osc)
    {
        auto& layer = oscillatorLayerSettings[static_cast<std::size_t>(osc)];
        const auto& fine = at(static_cast<T>(static_cast<int>(T::osc1Fine) + osc));
        const auto& pitch = at(static_cast<T>(static_cast<int>(T::osc1PitchMod) + osc));
        if (fine.routeCount > 0) { layer.fineCents = envelopePlanValue(fine); pitchMoved = true; }
        if (pitch.routeCount > 0) { layer.pitchModSemitones = envelopePlanValue(pitch); pitchMoved = true; }
    }
    if (pitchMoved && sourceRatiosPrimed)
    {
        // From wherever the ratio is now to the new one, across the next
        // control interval: a glide, never a step.
        for (int osc = 0; osc < kOscillatorSourceCount; ++osc)
        {
            const auto& layer = oscillatorLayerSettings[static_cast<std::size_t>(osc)];
            sourceRatioTarget[static_cast<std::size_t>(osc)] =
                px3::tuning::pitchRatio(layer.coarseOctaves, layer.fineCents + blockSlopCents[static_cast<std::size_t>(osc)],
                                        layer.pitchModSemitones, layer.semitones);
        }
        sourceRatioStart = sourceRatioCurrent;
        sourceRatioRampLength = juce::jmax(1, rampSamples);
        sourceRatioRampPosition = 0;
    }
}

void SynthVoice::setVoiceModulationPlan(const px3::synth::VoiceModulationPlan& plan)
{
    // Kept for the render, which refreshes the filter and pitch targets from
    // the live envelopes inside the block. The plan is the processor's, and
    // outlives the block it is set for.
    activePlan = plan.active ? &plan : nullptr;
    using T = px3::synth::VoiceModTarget;
    std::array<bool, kOscillatorSourceCount> oscillatorChanged { { false, false, false } };
    for (int target = 0; plan.active && target < px3::synth::kVoiceModTargetCount; ++target)
    {
        const auto& d = plan.destinations[static_cast<std::size_t>(target)];
        if (d.routeCount == 0) { continue; }

        auto delta = 0.0f;
        for (int r = 0; r < d.routeCount; ++r)
        {
            const auto& route = d.routes[static_cast<std::size_t>(r)];
            const auto envelope = static_cast<std::size_t>(juce::jlimit(0, kModEnvelopeCount - 1, route.envelope));
            delta += px3::synth::routeContribution(route.sourceBipolar, modEnvelopeValues[envelope],
                                                   route.polarity, route.curve, route.depth, d.base);
        }
        const auto value = px3::synth::rangeFrom0to1(d, px3::synth::foldUnit(d.unfolded + delta));

        const auto t = static_cast<T>(target);
        const auto osc = [&](T first) { return static_cast<std::size_t>(target - static_cast<int>(first)); };
        switch (t)
        {
            case T::filter1Cutoff: filterSettings[0].cutoffHz = value; break;
            case T::filter2Cutoff: filterSettings[1].cutoffHz = value; break;
            case T::filter1Resonance: filterSettings[0].resonanceQ = value; break;
            case T::filter2Resonance: filterSettings[1].resonanceQ = value; break;
            case T::osc1Fine: case T::osc2Fine: case T::osc3Fine:
                oscillatorLayerSettings[osc(T::osc1Fine)].fineCents = value; break;
            case T::osc1PitchMod: case T::osc2PitchMod: case T::osc3PitchMod:
                oscillatorLayerSettings[osc(T::osc1PitchMod)].pitchModSemitones = value; break;
            case T::osc1MacroA: case T::osc2MacroA: case T::osc3MacroA:
                oscillatorLayerSettings[osc(T::osc1MacroA)].oscillator.macroA = juce::jlimit(0.0f, 1.0f, value);
                oscillatorChanged[osc(T::osc1MacroA)] = true; break;
            case T::osc1MacroB: case T::osc2MacroB: case T::osc3MacroB:
                oscillatorLayerSettings[osc(T::osc1MacroB)].oscillator.macroB = juce::jlimit(0.0f, 1.0f, value);
                oscillatorChanged[osc(T::osc1MacroB)] = true; break;
            case T::osc1MacroC: case T::osc2MacroC: case T::osc3MacroC:
                oscillatorLayerSettings[osc(T::osc1MacroC)].oscillator.macroC = juce::jlimit(0.0f, 1.0f, value);
                oscillatorChanged[osc(T::osc1MacroC)] = true; break;
            case T::osc1WtPosition: case T::osc2WtPosition: case T::osc3WtPosition:
                oscillatorLayerSettings[osc(T::osc1WtPosition)].oscillator.wtPosition = juce::jlimit(0.0f, 1.0f, value);
                oscillatorChanged[osc(T::osc1WtPosition)] = true; break;
            case T::count: break;
        }
    }

    for (int oscIndex = 0; oscIndex < kOscillatorSourceCount; ++oscIndex)
    {
        const auto idx = static_cast<std::size_t>(oscIndex);
        // Pushed when the plan modulates it, and once more on the block the
        // plan stops doing so, since setOscillatorLayerSettings skipped it.
        if (oscillatorChanged[idx] || oscillatorModulatedByPlan[idx])
        {
            oscillatorUnits[idx].setSettings(oscillatorLayerSettings[idx].oscillator, controlBlockLength);
        }
        oscillatorModulatedByPlan[idx] = oscillatorChanged[idx];
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

void SynthVoice::setAnalogDriftState(float globalAmount,
                                     bool bypass,
                                     const AnalogDriftSharedState& sharedState,
                                     const AnalogDriftVoiceVariation& variation,
                                     const AnalogDriftTuning& tuningState)
{
    analogControl.globalAmount = juce::jlimit(0.0f, 1.0f, globalAmount);
    analogControl.bypass = bypass;
    analogControl.shared = sharedState;
    analogControl.variation = variation;
    analogControl.tuning = tuningState;
}

void SynthVoice::updateRateDependentCoefficients(double sampleRate)
{
    if (std::abs(sampleRate - coefficientsSampleRate) < 0.5)
    {
        return;
    }
    coefficientsSampleRate = sampleRate;

    using px3::dsp::onePoleCoefficient;
    bendSmoothing = static_cast<float>(onePoleCoefficient(0.06, sampleRate));
    wheelSmoothing = static_cast<float>(onePoleCoefficient(0.045, sampleRate));
    tailSmoothingLow = static_cast<float>(onePoleCoefficient(0.02, sampleRate));
    tailSmoothingHigh = static_cast<float>(onePoleCoefficient(0.20, sampleRate));

    analogStage.preparePink(sampleRate, px3::dsp::kReferenceSampleRate);
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
