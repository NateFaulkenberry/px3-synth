#include "SynthVoice.h"

#include "OscillatorMode.h"
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

    for (auto& grain : audioGrains)
    {
        grain = AudioGrain {};
    }
    audioSpawnCounter = 0;
    noteAgeSamples = 0;

    for (std::size_t i = 0; i < superSawAngles.size(); ++i)
    {
        const auto r = juce::Random::getSystemRandom().nextDouble();
        superSawAngles[i] = r * juce::MathConstants<double>::twoPi;
        superSawDrift[i] = juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f;
    }

    fmModAngle = 0.0;
    syncMasterAngle = 0.0;
    syncSlaveAngle = 0.0;
    digitalHoldCounter = 0;
    digitalHeldSample = 0.0f;

    for (auto& s : pinkState)
    {
        s = 0.0f;
    }
    noiseColorState = 0.0f;
    pinkColorState = 0.0f;

    const auto sampleRate = juce::jmax(1.0, getSampleRate());
    const auto frequency = juce::jmax(20.0, currentFrequencyHz);
    karplusDelaySamples = juce::jlimit(8,
                                       karplusBufferSize - 2,
                                       static_cast<int>(std::round(sampleRate / frequency)));
    karplusWriteIndex = 0;
    karplusLastSample = 0.0f;

    for (int i = 0; i < karplusDelaySamples; ++i)
    {
        const auto n = juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f;
        karplusBuffer[static_cast<std::size_t>(i)] = n * 0.5f;
    }

    for (std::size_t i = 0; i < physicalState.size(); ++i)
    {
        physicalState[i] = juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f;
        physicalPhase[i] = juce::Random::getSystemRandom().nextDouble() * juce::MathConstants<double>::twoPi;
    }
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

        auto sourceSample = renderOscillatorSample(sampleRate,
                                                   static_cast<float>(pitchRatio),
                                                   currentModWheelNorm,
                                                   imageSample,
                                                   granularSample);

        sourceSample += externalSample * subtractiveSettings.imageMix * 0.42f;
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
            sourceSample += nextDeterministicNoise() * (0.0035f + 0.0165f * noiseAmount);
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
    oscillatorSettings = settings;
    oscillatorSettings.modeIndex = px3::clampOscillatorModeIndex(oscillatorSettings.modeIndex);
    oscillatorSettings.macroA = clamp01(oscillatorSettings.macroA);
    oscillatorSettings.macroB = clamp01(oscillatorSettings.macroB);
    oscillatorSettings.macroC = clamp01(oscillatorSettings.macroC);
    oscillatorSettings.vowelIndex = juce::jlimit(0, 4, oscillatorSettings.vowelIndex);
    for (auto& h : oscillatorSettings.harmonics)
    {
        h = clamp01(h);
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

void SynthVoice::setImageWavetable(std::shared_ptr<const ImageWavetable> table, float wavetablePosition)
{
    imageWavetable = std::move(table);
    targetImagePosition = juce::jlimit(0.0f, 1.0f, wavetablePosition);
}

void SynthVoice::setAudioGranularSource(std::shared_ptr<const AudioSourceData> data,
                                        const AudioGranularSettings& settings)
{
    // Shared_ptr handoff keeps ownership explicit: loader/processor publish a
    // stable immutable buffer; voices only hold read references.
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

float SynthVoice::nextDeterministicNoise()
{
    noiseSeed = noiseSeed * 1664525u + 1013904223u;
    const auto bits = static_cast<int32_t>((noiseSeed >> 9) & 0x007FFFFFu);
    return (static_cast<float>(bits) / 4194303.5f) * 2.0f - 1.0f;
}

float SynthVoice::renderPinkNoise(float white)
{
    pinkState[0] = 0.99886f * pinkState[0] + white * 0.0555179f;
    pinkState[1] = 0.99332f * pinkState[1] + white * 0.0750759f;
    pinkState[2] = 0.96900f * pinkState[2] + white * 0.1538520f;
    pinkState[3] = 0.86650f * pinkState[3] + white * 0.3104856f;
    pinkState[4] = 0.55000f * pinkState[4] + white * 0.5329522f;
    pinkState[5] = -0.7616f * pinkState[5] - white * 0.0168980f;
    const auto pink = pinkState[0] + pinkState[1] + pinkState[2] + pinkState[3] + pinkState[4] + pinkState[5] + pinkState[6] + white * 0.5362f;
    pinkState[6] = white * 0.115926f;
    return pink * 0.11f;
}

float SynthVoice::renderSuperSaw(double sampleRate)
{
    const auto detune = std::pow(oscillatorSettings.macroA, 1.65f);
    const auto width = std::pow(oscillatorSettings.macroB, 1.2f);
    float sum = 0.0f;

    for (std::size_t i = 0; i < superSawAngles.size(); ++i)
    {
        const auto driftHz = superSawDrift[i] * (0.03 + 0.95 * width);
        const auto detuneSemitones = superSawDetunes[i] * (0.04f + 16.0f * detune);
        const auto ratio = std::pow(2.0, static_cast<double>(detuneSemitones) / 12.0);
        const auto freq = juce::jmax(8.0, currentFrequencyHz * ratio + driftHz);
        const auto delta = juce::MathConstants<double>::twoPi * freq / sampleRate;

        superSawAngles[i] += delta;
        if (superSawAngles[i] >= juce::MathConstants<double>::twoPi)
        {
            superSawAngles[i] -= juce::MathConstants<double>::twoPi;
        }

        const auto phase = static_cast<float>(superSawAngles[i] / juce::MathConstants<double>::twoPi);
        const auto saw = phase * 2.0f - 1.0f;
        const auto edgeSoft = 0.58f + 0.42f * (1.0f - width);
        sum += softClip(saw * edgeSoft * 1.35f);
    }

    return sum * (1.0f / 7.0f) * (0.84f + 0.10f * width);
}

float SynthVoice::renderPwm()
{
    const auto widthCurve = std::pow(oscillatorSettings.macroA, 1.15f);
    const auto width = juce::jlimit(0.08f,
                                    0.92f,
                                    0.1f + widthCurve * 0.8f + (targetModWheelNorm - 0.5f) * 0.14f);
    const auto phase = static_cast<float>(currentAngle / juce::MathConstants<double>::twoPi);
    return phase < width ? 1.0f : -1.0f;
}

float SynthVoice::readHarmonicSumFromSettings(float rolloffBias, float oddEvenBias, float inharmonicity)
{
    float sum = 0.0f;
    float norm = 0.0f;

    for (int i = 0; i < 8; ++i)
    {
        const auto h = static_cast<float>(i + 1);
        auto amp = oscillatorSettings.harmonics[static_cast<std::size_t>(i)];
        amp *= std::pow(1.0f / h, rolloffBias);

        const auto isOdd = (i % 2) == 0;
        const auto oddEven = isOdd ? (1.0f + oddEvenBias) : (1.0f - oddEvenBias * 0.82f);
        amp *= juce::jmax(0.0f, oddEven);

        const auto ratio = h * (1.0f + inharmonicity * 0.03f * h);
        const auto v = std::sin(currentAngle * static_cast<double>(ratio));
        sum += amp * static_cast<float>(v);
        norm += std::abs(amp);
    }

    return norm > 0.0001f ? sum / norm : 0.0f;
}

float SynthVoice::renderAdditive(bool dynamic)
{
    const auto rolloff = juce::jmap(std::pow(oscillatorSettings.macroA, 1.15f), 0.45f, 2.55f);
    const auto oddEven = juce::jmap(std::pow(oscillatorSettings.macroB, 1.1f), -0.65f, 0.65f);
    const auto inh = dynamic ? juce::jmap(std::pow(oscillatorSettings.macroC, 1.2f), 0.0f, 1.1f) : 0.0f;
    const auto base = readHarmonicSumFromSettings(rolloff, oddEven, inh);

    if (!dynamic)
    {
        return base;
    }

    const auto shimmer = std::sin(currentAngle * 0.5 + static_cast<double>(noteAgeSamples) * 0.0007) * 0.15f;
    return softClip(static_cast<float>(base + shimmer * (0.18f + oscillatorSettings.macroC * 0.42f)));
}

float SynthVoice::renderFm(double sampleRate)
{
    const auto ratio = std::pow(2.0f, juce::jmap(std::pow(oscillatorSettings.macroA, 1.1f), -1.6f, 2.2f));
    const auto index = juce::jmap(std::pow(oscillatorSettings.macroB, 1.35f), 0.0f, 10.0f);

    fmModAngle += juce::MathConstants<double>::twoPi * (currentFrequencyHz * static_cast<double>(ratio)) / sampleRate;
    if (fmModAngle >= juce::MathConstants<double>::twoPi)
    {
        fmModAngle -= juce::MathConstants<double>::twoPi;
    }

    const auto mod = std::sin(fmModAngle) * index;
    const auto sample = std::sin(currentAngle + mod);
    return static_cast<float>(sample) * (0.66f + 0.05f * (1.0f - oscillatorSettings.macroB));
}

float SynthVoice::renderHardSync(double sampleRate)
{
    const auto ratio = juce::jmap(std::pow(oscillatorSettings.macroA, 1.3f), 1.0f, 11.0f);
    syncMasterAngle += juce::MathConstants<double>::twoPi * currentFrequencyHz / sampleRate;

    if (syncMasterAngle >= juce::MathConstants<double>::twoPi)
    {
        syncMasterAngle -= juce::MathConstants<double>::twoPi;
        syncSlaveAngle = 0.0;
    }

    syncSlaveAngle += juce::MathConstants<double>::twoPi * currentFrequencyHz * ratio / sampleRate;
    if (syncSlaveAngle >= juce::MathConstants<double>::twoPi)
    {
        syncSlaveAngle -= juce::MathConstants<double>::twoPi;
    }

    const auto slavePhase = static_cast<float>(syncSlaveAngle / juce::MathConstants<double>::twoPi);
    const auto synced = slavePhase * 2.0f - 1.0f;
    const auto drive = 1.0f + std::pow(oscillatorSettings.macroB, 1.15f) * 2.3f;
    return softClip(synced * drive) * 0.82f;
}

float SynthVoice::renderKarplus()
{
    const auto decayCurve = std::pow(oscillatorSettings.macroA, 1.85f);
    const auto decay = juce::jmap(decayCurve, 0.90f, 0.99945f);
    const auto brightness = juce::jmap(std::pow(oscillatorSettings.macroB, 1.2f), 0.03f, 0.94f);

    const auto readIndex = (karplusWriteIndex - karplusDelaySamples + karplusBufferSize) % karplusBufferSize;
    const auto delayed = karplusBuffer[static_cast<std::size_t>(readIndex)];
    const auto filtered = brightness * delayed + (1.0f - brightness) * karplusLastSample;
    karplusLastSample = filtered;

    const auto excite = noteAgeSamples < 8 ? nextDeterministicNoise() * 0.18f : 0.0f;
    const auto writeSample = (filtered + excite) * decay;
    karplusBuffer[static_cast<std::size_t>(karplusWriteIndex)] = writeSample;
    karplusWriteIndex = (karplusWriteIndex + 1) % karplusBufferSize;

    return delayed * (1.28f + 0.08f * brightness);
}

float SynthVoice::renderOrgan()
{
    static constexpr std::array<float, 8> drawbarPreset { 0.92f, 0.72f, 0.46f, 0.36f, 0.27f, 0.18f, 0.13f, 0.10f };
    float sum = 0.0f;
    float norm = 0.0f;

    const auto tone = std::pow(oscillatorSettings.macroA, 1.05f);
    const auto click = std::pow(oscillatorSettings.macroB, 1.2f);

    for (int i = 0; i < 8; ++i)
    {
        const auto harmonic = static_cast<double>(i + 1);
        auto amp = drawbarPreset[static_cast<std::size_t>(i)] * (0.6f + oscillatorSettings.harmonics[static_cast<std::size_t>(i)] * 0.8f);
        amp *= std::pow(1.0f / static_cast<float>(i + 1), juce::jmap(tone, 0.5f, 1.8f));
        sum += amp * static_cast<float>(std::sin(currentAngle * harmonic));
        norm += std::abs(amp);
    }

    const auto clickEnv = std::exp(-static_cast<float>(noteAgeSamples) * (0.0006f + 0.003f * click));
    const auto keyClick = (nextDeterministicNoise() * 0.08f + std::sin(currentAngle * 9.0) * 0.05f) * clickEnv * click;
    const auto organ = norm > 0.0001f ? sum / norm : 0.0f;
    return softClip(static_cast<float>((organ + keyClick) * (1.05f + 0.07f * (1.0f - click))));
}

float SynthVoice::renderDigital(double sampleRate)
{
    const auto bitsCurve = std::pow(oscillatorSettings.macroA, 1.25f);
    const auto rateCurve = std::pow(oscillatorSettings.macroB, 1.15f);
    const auto bitDepth = juce::jlimit(2, 16, static_cast<int>(std::round(juce::jmap(bitsCurve, 2.0f, 16.0f))));
    digitalHoldSamples = juce::jlimit(1, 64, static_cast<int>(std::round(juce::jmap(rateCurve, 1.0f, 52.0f))));

    if (++digitalHoldCounter >= digitalHoldSamples)
    {
        digitalHoldCounter = 0;
        const auto phase = static_cast<float>(currentAngle / juce::MathConstants<double>::twoPi);
        const auto steps = static_cast<float>(1 << juce::jlimit(1, 20, bitDepth));
        const auto quantizedPhase = std::floor(phase * steps) / juce::jmax(2.0f, steps - 1.0f);
        const auto aliasFold = 1.0 + static_cast<double>(juce::jmap(rateCurve, 0.4f, 6.8f));
        const auto aliased = std::sin(static_cast<double>(quantizedPhase) * juce::MathConstants<double>::twoPi * aliasFold);
        const auto crushSteps = static_cast<float>(1 << juce::jlimit(1, 14, bitDepth - 1));
        digitalHeldSample = std::floor(static_cast<float>(aliased) * crushSteps) / juce::jmax(2.0f, crushSteps);
    }

    juce::ignoreUnused(sampleRate);
    return softClip(digitalHeldSample * 1.08f);
}

float SynthVoice::renderPhysical(double sampleRate)
{
    const auto decayCurve = std::pow(oscillatorSettings.macroA, 1.6f);
    const auto damping = juce::jmap(decayCurve, 0.9995f, 0.9957f);
    const auto material = juce::jmap(std::pow(oscillatorSettings.macroB, 1.2f), 0.85f, 2.7f);

    const std::array<double, 4> ratios { 1.0, 2.32, 3.91, 5.48 };
    float sum = 0.0f;

    for (std::size_t i = 0; i < ratios.size(); ++i)
    {
        const auto freq = currentFrequencyHz * ratios[i] * material;
        physicalPhase[i] += juce::MathConstants<double>::twoPi * freq / sampleRate;
        if (physicalPhase[i] >= juce::MathConstants<double>::twoPi)
        {
            physicalPhase[i] -= juce::MathConstants<double>::twoPi;
        }

        const auto excite = noteAgeSamples < 10 ? nextDeterministicNoise() * 0.22f : 0.0f;
        physicalState[i] = physicalState[i] * damping + static_cast<float>(std::sin(physicalPhase[i])) * 0.012f + excite * (0.02f / static_cast<float>(i + 1));
        sum += physicalState[i] * (0.72f / static_cast<float>(i + 1));
    }

    return softClip(sum * 1.95f);
}

float SynthVoice::renderRobOsc(double sampleRate)
{
    const auto transCurve = std::pow(oscillatorSettings.macroA, 0.55f);
    const auto bodyCurve = std::pow(oscillatorSettings.macroB, 0.72f);
    const auto chaosCurve = std::pow(oscillatorSettings.macroC, 0.80f);
    const auto transientDecay = juce::jmap(transCurve, 0.085f, 0.012f);
    const auto transient = std::exp(-static_cast<float>(noteAgeSamples) * transientDecay);
    const auto bodyPhase = currentAngle * (1.0 + chaosCurve * 1.05 + bodyCurve * 0.45)
                           + std::sin(currentAngle * (3.5 + chaosCurve * 10.0)) * (0.02 + chaosCurve * 0.38);

    const auto bodyFund = std::sin(bodyPhase);
    const auto bodySub = std::sin(bodyPhase * 0.5) * (0.12f + bodyCurve * 0.42f);
    const auto bodySecond = std::sin(bodyPhase * (1.34 + bodyCurve * 1.10)) * (0.08f + bodyCurve * 0.34f);
    const auto bodyThird = std::sin(bodyPhase * (2.00 + bodyCurve * 2.05)) * (0.03f + bodyCurve * 0.22f);
    auto body = bodyFund * (0.42f + bodyCurve * 0.52f) + bodySub + bodySecond + bodyThird;
    body = std::tanh(body * (1.12f + bodyCurve * 2.40f));

    // TRANS now drives a dedicated transient exciter so the knob has obvious sonic impact.
    const auto clickTone = std::sin(bodyPhase * (9.0f + transCurve * 46.0f));
    const auto clickNoise = nextDeterministicNoise();
    const auto clickMix = juce::jmap(transCurve, 0.25f, 0.80f);
    const auto clickCore = clickTone * (1.0f - clickMix) + clickNoise * clickMix;
    const auto transientGain = juce::jmap(transCurve, 0.04f, 2.30f);
    const auto smack = clickCore * transient * transientGain;

    // Add a very short attack impulse whose duration and intensity are TRANS-dependent.
    const auto attackSamples = juce::jlimit(10,
                                            96,
                                            static_cast<int>(10 + transCurve * 86.0f));
    float onsetEnv = 0.0f;
    if (noteAgeSamples < attackSamples)
    {
        onsetEnv = 1.0f - static_cast<float>(noteAgeSamples) / static_cast<float>(attackSamples);
        onsetEnv = onsetEnv * onsetEnv;
    }
    const auto onset = nextDeterministicNoise() * onsetEnv * juce::jmap(transCurve, 0.0f, 1.25f);

    // Keep TRANS audible after attack: add continuous edge/saturation movement.
    const auto edgeShaper = std::tanh(body * (1.0f + transCurve * 3.8f));
    const auto edgeCarrier = std::sin(bodyPhase * (5.0f + transCurve * 22.0f + chaosCurve * 24.0f));
    const auto edge = (edgeShaper - body) * (0.08f + transCurve * 0.60f)
                      + edgeCarrier * (0.01f + transCurve * 0.22f);

    const auto chaosRate = 6.0 + chaosCurve * 32.0;
    const auto chaosWarp = std::sin(bodyPhase * (3.0 + chaosCurve * 9.0) + std::sin(currentAngle * (11.0 + chaosCurve * 27.0)));
    const auto chaosNoise = nextDeterministicNoise() * (0.02f + chaosCurve * 0.22f);
    const auto chaos = std::sin(bodyPhase * chaosRate + chaosWarp * (0.6f + chaosCurve * 2.4f)) * (0.05f + chaosCurve * 0.34f)
                       + chaosNoise;

    juce::ignoreUnused(sampleRate);
    return softClip(static_cast<float>((body + smack + onset + edge + chaos) * 0.86f));
}

float SynthVoice::renderPx3(double sampleRate, float externalSample)
{
    const auto morph = std::pow(oscillatorSettings.macroA, 1.1f);
    const auto character = std::pow(oscillatorSettings.macroB, 1.2f);
    const auto movement = std::pow(oscillatorSettings.macroC, 1.1f);

    const auto fmPart = renderFm(sampleRate);
    const auto additivePart = renderAdditive(true);
    const auto foldedSaw = softClip(static_cast<float>((currentAngle / juce::MathConstants<double>::pi) - 1.0) * (1.0f + 4.8f * character));
    const auto ext = externalSample * (0.22f + movement * 0.38f);

    const auto blendA = fmPart * (1.0f - morph) + additivePart * morph;
    const auto blendB = foldedSaw * (0.45f + 0.45f * character) + ext;
    const auto movingPhase = std::sin(static_cast<double>(noteAgeSamples) * (0.0008 + movement * 0.0022));
    const auto px3 = softClip((blendA * 0.74f + blendB * 0.66f) + static_cast<float>(movingPhase) * 0.25f * movement);
    return px3 * 0.9f;
}

float SynthVoice::renderOscillatorSample(double sampleRate,
                                         float pitchRatio,
                                         float modWheelNorm,
                                         float imageSample,
                                         float granularSample)
{
    const auto mode = static_cast<px3::OscillatorMode>(px3::clampOscillatorModeIndex(oscillatorSettings.modeIndex));
    const auto modeIndex = px3::clampOscillatorModeIndex(oscillatorSettings.modeIndex);
    const auto phase = static_cast<float>(currentAngle / juce::MathConstants<double>::twoPi);
    const auto sine = static_cast<float>(std::sin(currentAngle));
    const auto saw = phase * 2.0f - 1.0f;
    const auto square = phase < 0.5f ? 1.0f : -1.0f;
    const auto triangle = 1.0f - 4.0f * std::abs(phase - 0.5f);
    const auto external = externalSourceMode == ExternalSourceMode::audio ? granularSample : imageSample;

    float sample = 0.0f;

    switch (mode)
    {
        case px3::OscillatorMode::sine:
            sample = sine;
            break;
        case px3::OscillatorMode::saw:
            sample = saw;
            break;
        case px3::OscillatorMode::square:
            sample = square;
            break;
        case px3::OscillatorMode::triangle:
            sample = triangle;
            break;
        case px3::OscillatorMode::noise:
        {
            const auto white = nextDeterministicNoise();
            const auto color = oscillatorSettings.macroA; // 0=darker, 1=brighter
            const auto lpCoeff = juce::jmap(color, 0.02f, 0.48f);
            noiseColorState += (white - noiseColorState) * lpCoeff;
            sample = (noiseColorState * (1.0f - color) + white * color) * 0.78f;
            break;
        }
        case px3::OscillatorMode::pinkNoise:
        {
            const auto white = nextDeterministicNoise();
            auto pink = renderPinkNoise(white);
            const auto color = oscillatorSettings.macroA; // 0=darker, 1=brighter
            const auto lpCoeff = juce::jmap(color, 0.01f, 0.30f);
            pinkColorState += (pink - pinkColorState) * lpCoeff;
            pink = pinkColorState * (1.0f - color) + pink * color;
            sample = pink * 1.45f;
            break;
        }
        case px3::OscillatorMode::superSaw:
            sample = renderSuperSaw(sampleRate);
            break;
        case px3::OscillatorMode::pwm:
            sample = renderPwm();
            break;
        case px3::OscillatorMode::wavetable:
        {
            const auto pos = std::pow(oscillatorSettings.macroA, 1.1f); // POSITION macro
            const auto warp = std::sin(currentAngle * (1.0 + pos * 6.0));
            sample = external * (0.30f + pos * 1.15f) + static_cast<float>(warp) * (0.35f - pos * 0.20f);
            break;
        }
        case px3::OscillatorMode::additive:
            sample = renderAdditive(false);
            break;
        case px3::OscillatorMode::formant:
        {
            static constexpr std::array<std::array<float, 8>, 5> vowelProfiles { {
                { 1.0f, 0.62f, 0.42f, 0.24f, 0.15f, 0.09f, 0.06f, 0.03f },
                { 0.88f, 0.92f, 0.36f, 0.26f, 0.21f, 0.12f, 0.07f, 0.05f },
                { 0.72f, 0.98f, 0.63f, 0.22f, 0.14f, 0.11f, 0.09f, 0.07f },
                { 1.0f, 0.54f, 0.31f, 0.47f, 0.21f, 0.16f, 0.10f, 0.07f },
                { 0.86f, 0.38f, 0.69f, 0.34f, 0.22f, 0.17f, 0.11f, 0.08f }
            } };

            const auto vowelA = juce::jlimit(0, 4, oscillatorSettings.vowelIndex);
            const auto morph = oscillatorSettings.macroA * 4.0f;
            const auto vowelB = juce::jlimit(0, 4, vowelA + 1);
            const auto frac = morph - std::floor(morph);

            auto oldHarm = oscillatorSettings.harmonics;
            for (int i = 0; i < 8; ++i)
            {
                oscillatorSettings.harmonics[static_cast<std::size_t>(i)] = vowelProfiles[static_cast<std::size_t>(vowelA)][static_cast<std::size_t>(i)]
                                                                            + (vowelProfiles[static_cast<std::size_t>(vowelB)][static_cast<std::size_t>(i)]
                                                                               - vowelProfiles[static_cast<std::size_t>(vowelA)][static_cast<std::size_t>(i)])
                                                                                  * frac;
            }

            sample = readHarmonicSumFromSettings(juce::jmap(oscillatorSettings.macroB, 0.7f, 2.2f),
                                                 0.0f,
                                                 0.0f);
            sample = softClip(sample * (0.95f + oscillatorSettings.macroB * 0.25f));
            oscillatorSettings.harmonics = oldHarm;
            break;
        }
        case px3::OscillatorMode::fm:
            sample = renderFm(sampleRate);
            break;
        case px3::OscillatorMode::hardSync:
            sample = renderHardSync(sampleRate);
            break;
        case px3::OscillatorMode::karplus:
            sample = renderKarplus();
            break;
        case px3::OscillatorMode::organ:
            sample = renderOrgan();
            break;
        case px3::OscillatorMode::digital:
            sample = renderDigital(sampleRate);
            break;
        case px3::OscillatorMode::physical:
            sample = renderPhysical(sampleRate);
            break;
        case px3::OscillatorMode::rob:
            sample = renderRobOsc(sampleRate);
            break;
        case px3::OscillatorMode::isaac:
            sample = renderAdditive(true);
            break;
        case px3::OscillatorMode::px3:
            sample = renderPx3(sampleRate, external);
            break;
        default:
            break;
    }

    // Loudness normalization target around noon controls, with gentle dynamic compensation
    // for the modes that can naturally spike in level at extreme settings.
    static constexpr std::array<float, 20> kModeTrim {
        0.82f, // SINE
        0.74f, // SAW
        0.72f, // SQUARE
        0.78f, // TRIANGLE
        0.64f, // NOISE
        0.67f, // PINK NOISE
        0.62f, // SUPER SAW
        0.70f, // PWM
        0.76f, // WAVETABLE
        0.80f, // ADDITIVE
        0.73f, // FORMANT
        0.64f, // FM
        0.60f, // HARD SYNC
        0.78f, // KARPLUS
        0.76f, // ORGAN
        0.66f, // DIGITAL
        0.70f, // PHYSICAL
        0.62f, // ROB
        0.74f, // ISAAC
        0.60f  // PX3
    };

    auto modeGain = kModeTrim[static_cast<std::size_t>(modeIndex)];

    const auto macroEnergy = [this, mode]()
    {
        const auto a = oscillatorSettings.macroA;
        const auto b = oscillatorSettings.macroB;
        const auto c = oscillatorSettings.macroC;

        switch (mode)
        {
            case px3::OscillatorMode::sine:
            case px3::OscillatorMode::saw:
            case px3::OscillatorMode::square:
            case px3::OscillatorMode::triangle:
                return 0.5f;
            case px3::OscillatorMode::noise:
            case px3::OscillatorMode::pinkNoise:
            case px3::OscillatorMode::pwm:
            case px3::OscillatorMode::wavetable:
                return a;
            case px3::OscillatorMode::superSaw:
            case px3::OscillatorMode::karplus:
            case px3::OscillatorMode::organ:
            case px3::OscillatorMode::digital:
            case px3::OscillatorMode::physical:
            case px3::OscillatorMode::fm:
            case px3::OscillatorMode::hardSync:
            case px3::OscillatorMode::formant:
                return 0.5f * (a + b);
            case px3::OscillatorMode::additive:
            case px3::OscillatorMode::isaac:
            case px3::OscillatorMode::rob:
            case px3::OscillatorMode::px3:
                return (a + b + c) * (1.0f / 3.0f);
            default:
                return 0.5f;
        }
    }();

    static constexpr std::array<float, 20> kModeTravelSlope {
        0.00f, // SINE
        0.08f, // SAW
        0.10f, // SQUARE
        0.06f, // TRIANGLE
        0.16f, // NOISE
        0.14f, // PINK NOISE
        0.42f, // SUPER SAW
        0.18f, // PWM
        0.20f, // WAVETABLE
        0.14f, // ADDITIVE
        0.20f, // FORMANT
        0.38f, // FM
        0.46f, // HARD SYNC
        0.16f, // KARPLUS
        0.14f, // ORGAN
        0.34f, // DIGITAL
        0.24f, // PHYSICAL
        0.34f, // ROB
        0.20f, // ISAAC
        0.40f  // PX3
    };

    const auto slope = kModeTravelSlope[static_cast<std::size_t>(modeIndex)];
    const auto centered = macroEnergy - 0.5f;
    modeGain *= 1.0f - slope * centered;

    if (mode == px3::OscillatorMode::superSaw)
    {
        modeGain *= juce::jmap(std::pow(oscillatorSettings.macroA, 1.35f), 1.0f, 0.84f);
    }
    else if (mode == px3::OscillatorMode::fm)
    {
        modeGain *= juce::jmap(std::pow(oscillatorSettings.macroB, 1.2f), 1.0f, 0.82f);
    }
    else if (mode == px3::OscillatorMode::hardSync)
    {
        modeGain *= juce::jmap(std::pow(oscillatorSettings.macroB, 1.18f), 1.0f, 0.78f);
    }
    else if (mode == px3::OscillatorMode::digital)
    {
        modeGain *= juce::jmap(std::pow(oscillatorSettings.macroB, 1.1f), 1.0f, 0.86f);
    }
    else if (mode == px3::OscillatorMode::rob || mode == px3::OscillatorMode::px3)
    {
        modeGain *= juce::jmap(std::pow(oscillatorSettings.macroC, 1.12f), 1.0f, 0.84f);
    }

    modeGain = juce::jlimit(0.45f, 1.08f, modeGain);

    sample *= modeGain;

    const auto wheelBlend = 0.15f + modWheelNorm * 0.22f;
    sample = sample * (1.0f - wheelBlend) + external * wheelBlend;
    juce::ignoreUnused(pitchRatio);
    return softClip(sample);
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
