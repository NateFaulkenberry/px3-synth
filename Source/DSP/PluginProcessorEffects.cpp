#include "PluginProcessor.h"
#include "PluginProcessorInternals.h"

// File role: delay/granular/reverb DSP helper implementation details.
// Keep per-sample/per-frame effect algorithms here; route ordering and block
// orchestration remain in the core processor implementation.

using namespace px3::processor_internal;

//==============================================================================
// Delay, Granular, And Reverb DSP
//==============================================================================
void PX3SynthAudioProcessor::prepareReverbEngine(double sampleRate)
{
    juce::ignoreUnused(sampleRate);

    reverb.reset();
    reverbOutputCompGain = 1.0f;
    reverbInputDcX1 = { { 0.0f, 0.0f } };
    reverbInputDcY1 = { { 0.0f, 0.0f } };
    reverbWetDcX1 = { { 0.0f, 0.0f } };
    reverbWetDcY1 = { { 0.0f, 0.0f } };
    reverbWetSlewState = { { 0.0f, 0.0f } };

    const auto maxPreDelaySamples = juce::jmax(8, static_cast<int>(std::round(currentSampleRateHz * 0.30)));
    for (auto& line : reverbPreDelayLines)
    {
        resizeReverbLine(line, maxPreDelaySamples);
    }

    static constexpr std::array<float, 6> plateDelaySeconds { { 0.0113f, 0.0089f, 0.0721f, 0.0887f, 0.0317f, 0.0439f } };
    for (std::size_t i = 0; i < plateLines.size(); ++i)
    {
        resizeReverbLine(plateLines[i], juce::jmax(16, static_cast<int>(std::round(currentSampleRateHz * plateDelaySeconds[i] * 1.8f))));
        plateLines[i].modPhase = static_cast<float>(i) * 0.67f;
        plateLines[i].lpState = 0.0f;
    }
    plateTankState = { { 0.0f, 0.0f } };

    static constexpr std::array<float, 8> hallDelaySeconds { { 0.030f, 0.037f, 0.041f, 0.047f, 0.053f, 0.059f, 0.067f, 0.079f } };
    for (std::size_t i = 0; i < hallLines.size(); ++i)
    {
        resizeReverbLine(hallLines[i], juce::jmax(32, static_cast<int>(std::round(currentSampleRateHz * hallDelaySeconds[i] * 2.2f))));
        hallLines[i].modPhase = static_cast<float>(i) * 0.53f;
        hallLines[i].lpState = 0.0f;
    }
    hallReadCache.fill(0.0f);

    static constexpr std::array<float, 8> cloudDelaySeconds { { 0.049f, 0.061f, 0.073f, 0.089f, 0.104f, 0.127f, 0.013f, 0.017f } };
    for (std::size_t i = 0; i < cloudLines.size(); ++i)
    {
        resizeReverbLine(cloudLines[i], juce::jmax(32, static_cast<int>(std::round(currentSampleRateHz * cloudDelaySeconds[i] * 2.6f))));
        cloudLines[i].modPhase = static_cast<float>(i) * 0.91f;
        cloudLines[i].lpState = 0.0f;
    }
    cloudReadCache.fill(0.0f);
}

float PX3SynthAudioProcessor::readDelaySample(int channel, float readPos) const
{
    const auto& buffer = isaacDelayBuffer[static_cast<std::size_t>(channel)];
    auto rp = readPos;
    while (rp < 0.0f)
    {
        rp += static_cast<float>(isaacBufferSize);
    }
    while (rp >= static_cast<float>(isaacBufferSize))
    {
        rp -= static_cast<float>(isaacBufferSize);
    }

    const auto i0 = static_cast<int>(rp) % isaacBufferSize;
    const auto i1 = (i0 + 1) % isaacBufferSize;
    const auto frac = rp - static_cast<float>(i0);
    return buffer[static_cast<std::size_t>(i0)] + (buffer[static_cast<std::size_t>(i1)] - buffer[static_cast<std::size_t>(i0)]) * frac;
}

float PX3SynthAudioProcessor::sanitizeAudioSample(float x) const
{
    if (!std::isfinite(x))
    {
        return 0.0f;
    }

    return juce::jlimit(-4.0f, 4.0f, x);
}

void PX3SynthAudioProcessor::clearGranularDiffusionState()
{
    for (auto& line : isaacDiffusionLineA)
    {
        std::fill(line.begin(), line.end(), 0.0f);
    }
    for (auto& line : isaacDiffusionLineB)
    {
        std::fill(line.begin(), line.end(), 0.0f);
    }
    isaacDiffusionIndexA = { { 0, 0 } };
    isaacDiffusionIndexB = { { 0, 0 } };
}

float PX3SynthAudioProcessor::processAllpassSample(float x,
                                                       std::vector<float>& line,
                                                       int& index,
                                                       float feedback) const
{
    if (line.empty())
    {
        return x;
    }

    auto& d = line[static_cast<std::size_t>(index)];
    const auto g = juce::jlimit(0.0f, 0.82f, feedback);
    const auto y = -g * x + d;
    d = x + g * y;

    ++index;
    if (index >= static_cast<int>(line.size()))
    {
        index = 0;
    }

    return y;
}

void PX3SynthAudioProcessor::processGranularDiffusion(float& wetL,
                                                          float& wetR,
                                                          float diffusionAmount,
                                                          float stereoAmount)
{
    const auto d = juce::jlimit(0.0f, 1.0f, diffusionAmount);
    if (d <= 0.0001f)
    {
        return;
    }

    const auto gA = lerp(0.38f, 0.74f, d);
    const auto gB = lerp(0.32f, 0.68f, d);

    auto dl = processAllpassSample(wetL, isaacDiffusionLineA[0], isaacDiffusionIndexA[0], gA);
    dl = processAllpassSample(dl, isaacDiffusionLineB[0], isaacDiffusionIndexB[0], gB);

    auto dr = processAllpassSample(wetR, isaacDiffusionLineA[1], isaacDiffusionIndexA[1], gA);
    dr = processAllpassSample(dr, isaacDiffusionLineB[1], isaacDiffusionIndexB[1], gB);

    const auto width = juce::jlimit(0.0f, 1.0f, stereoAmount);
    const auto cross = 0.08f + 0.28f * d;
    const auto widenedL = dl * (1.0f - cross) + dr * cross;
    const auto widenedR = dr * (1.0f - cross) + dl * cross;
    wetL = lerp(wetL, widenedL, d * (0.50f + 0.40f * width));
    wetR = lerp(wetR, widenedR, d * (0.50f + 0.40f * width));
}

void PX3SynthAudioProcessor::renderActiveGranularGrains(float& wetL, float& wetR)
{
    wetL = 0.0f;
    wetR = 0.0f;

    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    for (auto& grain : isaacGrains)
    {
        if (!grain.active)
        {
            continue;
        }

        const auto age = static_cast<float>(grain.ageSamples);
        const auto len = static_cast<float>(juce::jmax(1, grain.lengthSamples));
        const auto phase = age / len;

        if (phase >= 1.0f)
        {
            grain.active = false;
            continue;
        }

        const auto window = 0.5f - 0.5f * std::cos(phase * twoPi);
        const auto g = grain.gain * window;

        const auto left = readDelaySample(0, grain.readPos);
        const auto right = readDelaySample(1, grain.readPos);
        const auto mono = 0.5f * (left + right);

        const auto pan = clamp01(grain.pan);
        const auto panAngle = pan * juce::MathConstants<float>::halfPi;
        const auto gainL = std::cos(panAngle);
        const auto gainR = std::sin(panAngle);

        wetL += mono * g * gainL;
        wetR += mono * g * gainR;

        grain.readPos += grain.reverse ? -std::abs(grain.increment) : std::abs(grain.increment);
        while (grain.readPos < 0.0f)
        {
            grain.readPos += static_cast<float>(isaacBufferSize);
        }
        while (grain.readPos >= static_cast<float>(isaacBufferSize))
        {
            grain.readPos -= static_cast<float>(isaacBufferSize);
        }

        ++grain.ageSamples;
    }
}

void PX3SynthAudioProcessor::spawnIsaacGrain(float amount,
                                                 float timeControl,
                                                 float feedbackControl,
                                                 int syncDivisionIndex,
                                                 GranularMode mode,
                                                 int rhythmicStep)
{
    for (auto& grain : isaacGrains)
    {
        if (grain.active)
        {
            continue;
        }

        auto& random = juce::Random::getSystemRandom();
        grain.active = true;
        grain.reverse = false;

        const auto a = smoothstep(amount);
        const auto macro = std::pow(a, 0.62f);
        const auto sizeCtrl = juce::jlimit(0.0f, 1.0f, timeControl);
        const auto feedbackCtrl = juce::jlimit(0.0f, 1.0f, feedbackControl);

        float grainMs = lerp(35.0f, 170.0f, a);
        float semitone = 0.0f;
        float pitchMicro = 0.0f;
        float reverseChance = 0.0f;
        float panSpread = lerp(0.14f, 0.88f, macro);
        float baseDelayBeats = lerp(0.125f, 0.75f, macro);
        float jitterWidth = 0.06f + 0.22f * macro;
        float gain = lerp(0.11f, 0.30f, macro);

        if (mode == GranularMode::classic)
        {
            const std::array<int, 7> intervals { -12, -7, -5, 0, 5, 7, 12 };
            const auto chooseWide = random.nextFloat() < (0.22f + 0.70f * macro);
            const auto idx = chooseWide ? random.nextInt(static_cast<int>(intervals.size())) : 3;
            semitone = static_cast<float>(intervals[static_cast<std::size_t>(idx)]);
            pitchMicro = (random.nextFloat() - 0.5f) * (0.16f + 0.26f * macro);
            reverseChance = 0.0f;
        }
        else if (mode == GranularMode::cloud)
        {
            const std::array<int, 9> intervals { 0, 0, 0, 7, -7, 12, -12, 5, -5 };
            semitone = static_cast<float>(intervals[static_cast<std::size_t>(random.nextInt(static_cast<int>(intervals.size())))]);
            pitchMicro = (random.nextFloat() - 0.5f) * (0.10f + 0.20f * macro);
            grainMs = lerp(45.0f, 260.0f, sizeCtrl);
            panSpread = lerp(0.35f, 0.98f, a);
            reverseChance = 0.08f + 0.52f * feedbackCtrl;
            baseDelayBeats = lerp(0.18f, 0.95f, sizeCtrl);
            jitterWidth = 0.03f + 0.14f * a;
            gain = lerp(0.08f, 0.22f, macro);
        }
        else if (mode == GranularMode::shimmer)
        {
            const std::array<int, 5> shimmerIntervals { 12, 7, 5, 19, 24 };
            const auto idx = juce::jlimit(0,
                                          static_cast<int>(shimmerIntervals.size()) - 1,
                                          static_cast<int>(std::round(sizeCtrl * static_cast<float>(shimmerIntervals.size() - 1))));
            semitone = static_cast<float>(shimmerIntervals[static_cast<std::size_t>(idx)]);
            pitchMicro = (random.nextFloat() - 0.5f) * 0.10f;
            grainMs = lerp(70.0f, 230.0f, sizeCtrl);
            panSpread = lerp(0.24f, 0.80f, a);
            reverseChance = 0.04f + 0.20f * feedbackCtrl;
            baseDelayBeats = lerp(0.25f, 1.0f, sizeCtrl);
            jitterWidth = 0.01f + 0.06f * a;
            gain = lerp(0.10f, 0.24f, macro);
        }
        else
        {
            static constexpr std::array<std::array<int, 8>, 3> pitchPatterns { {
                { 0, 7, 12, 7, 0, 7, 12, 7 },
                { 0, 12, 0, 7, 0, 12, 0, 7 },
                { 0, -12, 12, 0, 0, -12, 12, 0 }
            } };
            const auto patternIndex = juce::jlimit(0, 2, static_cast<int>(std::floor(sizeCtrl * 2.99f)));
            semitone = static_cast<float>(pitchPatterns[static_cast<std::size_t>(patternIndex)][static_cast<std::size_t>(rhythmicStep & 7)]);
            pitchMicro = 0.0f;
            grainMs = lerp(22.0f, 110.0f, sizeCtrl);
            panSpread = lerp(0.30f, 0.92f, a);
            reverseChance = juce::jlimit(0.0f, 0.9f, feedbackCtrl);
            baseDelayBeats = juce::jmax(0.0625f, divisionBeatsForIndex(syncDivisionIndex));
            jitterWidth = 0.0f;
            gain = lerp(0.10f, 0.26f, macro);
        }

        grain.lengthSamples = juce::jmax(24,
                                         static_cast<int>(std::round(grainMs * 0.001f * static_cast<float>(currentSampleRateHz))));
        grain.ageSamples = 0;
        grain.reverse = random.nextFloat() < reverseChance;

        const auto ratio = std::pow(2.0f, (semitone + pitchMicro) / 12.0f);
        grain.increment = juce::jlimit(0.25f, 4.0f, std::abs(ratio));

        const auto secPerBeat = static_cast<float>(60.0 / juce::jmax(20.0, currentBpm));
        const auto syncBeats = divisionBeatsForIndex(syncDivisionIndex);
        const auto syncEnabled = syncDivisionIndex > 0 && syncBeats > 0.0f;
        if (syncEnabled)
        {
            baseDelayBeats = syncBeats;
            if (mode == GranularMode::rhythmic)
            {
                jitterWidth = 0.0f;
            }
            else
            {
                jitterWidth = juce::jmin(jitterWidth, 0.07f);
            }
        }

        const auto baseDelaySamples = baseDelayBeats * secPerBeat * static_cast<float>(currentSampleRateHz);
        const auto jitter = (random.nextFloat() - 0.5f) * jitterWidth * baseDelaySamples;
        auto readPos = static_cast<float>(isaacWritePos) - (baseDelaySamples + jitter);
        while (readPos < 0.0f)
        {
            readPos += static_cast<float>(isaacBufferSize);
        }
        while (readPos >= static_cast<float>(isaacBufferSize))
        {
            readPos -= static_cast<float>(isaacBufferSize);
        }

        if (grain.reverse)
        {
            readPos += static_cast<float>(grain.lengthSamples) * grain.increment;
            while (readPos >= static_cast<float>(isaacBufferSize))
            {
                readPos -= static_cast<float>(isaacBufferSize);
            }
        }
        grain.readPos = readPos;

        isaacPanPhase += lerp(0.13f, 0.36f, macro);
        if (isaacPanPhase > juce::MathConstants<float>::twoPi)
        {
            isaacPanPhase -= juce::MathConstants<float>::twoPi;
        }

        if (mode == GranularMode::rhythmic)
        {
            const auto alt = ((rhythmicStep & 1) == 0) ? -1.0f : 1.0f;
            grain.pan = 0.5f + alt * 0.5f * panSpread * 0.82f;
        }
        else
        {
            grain.pan = 0.5f + std::sin(isaacPanPhase) * 0.5f * panSpread;
        }
        grain.gain = gain;
        return;
    }
}

void PX3SynthAudioProcessor::processIsaacGranularSample(float inL,
                                                            float inR,
                                                            float amount,
                                                            float timeControl,
                                                            float feedbackControl,
                                                            int syncDivisionIndex,
                                                            float& outL,
                                                            float& outR)
{
    // Granular delay summary:
    // - Input is written into a circular delay buffer.
    // - Short grains are spawned from delayed positions (mode-dependent).
    // - Grain output is diffused/filtered/saturated, then mixed with dry signal.
    // This all runs sample-by-sample in the audio thread, so no allocations.
    if (isaacBufferSize <= 1)
    {
        outL = inL;
        outR = inR;
        return;
    }

    const auto dryL = sanitizeAudioSample(inL);
    const auto dryR = sanitizeAudioSample(inR);

    if (amount <= 0.0001f)
    {
        isaacDelayBuffer[0][static_cast<std::size_t>(isaacWritePos)] = dryL;
        isaacDelayBuffer[1][static_cast<std::size_t>(isaacWritePos)] = dryR;
        isaacWritePos = (isaacWritePos + 1) % isaacBufferSize;
        outL = dryL;
        outR = dryR;
        return;
    }

    const auto mode = static_cast<GranularMode>(juce::jlimit(0, 3, granularModeParam->getIndex()));
    const auto a = smoothstep(amount);
    const auto macro = std::pow(a, 0.62f);
    const auto secPerBeat = static_cast<float>(60.0 / juce::jmax(20.0, currentBpm));
    const auto syncBeats = divisionBeatsForIndex(syncDivisionIndex);

    if (mode == GranularMode::rhythmic)
    {
        // Rhythmic mode uses a step-trigger pattern with optional swing to
        // produce tempo-locked, repeatable phrase-like textures.
        if (isaacRhythmicSamplesUntilNext <= 0)
        {
            const std::array<std::array<int, 16>, 3> triggerPatterns { {
                { 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 0 },
                { 1, 0, 1, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 1, 0, 0 },
                { 1, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1 }
            } };

            const auto patternIndex = juce::jlimit(0, 2, static_cast<int>(std::floor(timeControl * 2.99f)));
            const auto step = isaacRhythmicStepIndex & 15;
            const auto shouldTrigger = triggerPatterns[static_cast<std::size_t>(patternIndex)][static_cast<std::size_t>(step)] != 0;
            const auto densityChance = juce::jlimit(0.0f, 1.0f, lerp(0.15f, 0.98f, a));

            if (shouldTrigger || juce::Random::getSystemRandom().nextFloat() < densityChance * 0.22f)
            {
                const auto layers = 1 + (juce::Random::getSystemRandom().nextFloat() < densityChance * 0.26f ? 1 : 0);
                for (int i = 0; i < layers; ++i)
                {
                    spawnIsaacGrain(amount,
                                    timeControl,
                                    feedbackControl,
                                    syncDivisionIndex,
                                    mode,
                                    step + i);
                }
            }

            const auto baseBeats = syncDivisionIndex > 0 && syncBeats > 0.0f
                                       ? syncBeats
                                       : lerp(1.0f, 0.125f, juce::jlimit(0.0f, 1.0f, timeControl));
            const auto swingAmount = juce::jlimit(0.0f, 0.48f, feedbackControl * 0.42f);
            auto stepBeats = baseBeats;
            if (isaacRhythmicSwingToggle)
            {
                stepBeats *= (1.0f + swingAmount);
            }
            else
            {
                stepBeats *= (1.0f - swingAmount);
            }
            isaacRhythmicSwingToggle = !isaacRhythmicSwingToggle;
            isaacRhythmicStepIndex = (isaacRhythmicStepIndex + 1) & 15;
            isaacRhythmicSamplesUntilNext = juce::jmax(8,
                                                       static_cast<int>(std::round(stepBeats * secPerBeat
                                                                                   * static_cast<float>(currentSampleRateHz))));
        }

        --isaacRhythmicSamplesUntilNext;
    }
    else
    {
        // Non-rhythmic modes use density-based spawning intervals; sync mode
        // still quantizes intervals to beat subdivisions when requested.
        auto spawnEverySec = lerp(0.085f, 0.028f, macro) * secPerBeat * 2.0f;
        if (mode == GranularMode::cloud)
        {
            spawnEverySec = lerp(0.040f, 0.009f, a) * lerp(1.08f, 0.72f, juce::jlimit(0.0f, 1.0f, timeControl));
        }
        else if (mode == GranularMode::shimmer)
        {
            spawnEverySec = lerp(0.072f, 0.016f, a);
        }

        if (syncDivisionIndex > 0 && syncBeats > 0.0f)
        {
            auto syncMul = lerp(0.38f, 0.25f, macro);
            if (mode == GranularMode::cloud)
            {
                syncMul = lerp(0.22f, 0.14f, a);
            }
            else if (mode == GranularMode::shimmer)
            {
                syncMul = lerp(0.44f, 0.28f, a);
            }
            spawnEverySec = juce::jmax(0.008f, secPerBeat * syncBeats * syncMul);
        }

        const auto spawnEverySamples = juce::jmax(8,
                                                   static_cast<int>(std::round(spawnEverySec * static_cast<float>(currentSampleRateHz))));
        if (++isaacSpawnCounter >= spawnEverySamples)
        {
            isaacSpawnCounter = 0;
            const auto layerCount = mode == GranularMode::cloud
                                        ? 1 + (juce::Random::getSystemRandom().nextFloat() < a * 0.62f ? 1 : 0)
                                        : 1;
            for (int i = 0; i < layerCount; ++i)
            {
                spawnIsaacGrain(amount,
                                timeControl,
                                feedbackControl,
                                syncDivisionIndex,
                                mode,
                                0);
            }
        }
    }

    float wetL = 0.0f;
    float wetR = 0.0f;
    renderActiveGranularGrains(wetL, wetR);

    float feedback = lerp(0.16f, 0.74f, macro);
    float diffusion = 0.0f;
    float stereo = 0.5f;
    float dampCoeff = lerp(0.19f, 0.06f, macro);

    if (mode == GranularMode::cloud)
    {
        feedback = juce::jlimit(0.0f, 0.86f, 0.20f + 0.66f * feedbackControl);
        diffusion = juce::jlimit(0.0f, 1.0f, 0.22f + 0.72f * feedbackControl);
        stereo = juce::jlimit(0.0f, 1.0f, 0.35f + 0.60f * timeControl);
        dampCoeff = lerp(0.14f, 0.04f, a);
    }
    else if (mode == GranularMode::shimmer)
    {
        feedback = juce::jlimit(0.0f, 0.88f, 0.34f + 0.50f * feedbackControl);
        diffusion = juce::jlimit(0.0f, 1.0f, 0.28f + 0.58f * a);
        stereo = juce::jlimit(0.0f, 1.0f, 0.24f + 0.56f * a);
        dampCoeff = lerp(0.11f, 0.03f, a);
    }
    else if (mode == GranularMode::rhythmic)
    {
        feedback = juce::jlimit(0.0f, 0.80f, 0.10f + 0.70f * feedbackControl);
        diffusion = juce::jlimit(0.0f, 0.40f, 0.05f + 0.35f * a);
        stereo = juce::jlimit(0.0f, 1.0f, 0.35f + 0.54f * a);
        dampCoeff = lerp(0.18f, 0.07f, a);
    }

    // Diffusion broadens grain clouds and decorrelates channels, helping avoid
    // narrow comb-like artifacts at higher feedback values.
    processGranularDiffusion(wetL, wetR, diffusion, stereo);

    isaacFeedbackFilter[0] += dampCoeff * (wetL - isaacFeedbackFilter[0]);
    isaacFeedbackFilter[1] += dampCoeff * (wetR - isaacFeedbackFilter[1]);

    if (mode == GranularMode::shimmer)
    {
        const auto shimmerTone = 1.0f + 0.32f * a;
        wetL = std::tanh((wetL * 0.76f + isaacFeedbackFilter[0] * 0.24f) * shimmerTone);
        wetR = std::tanh((wetR * 0.76f + isaacFeedbackFilter[1] * 0.24f) * shimmerTone);

        // Shimmer can jump in level as new pitched grains spawn; smooth those
        // transients to reduce crackle/pops while keeping the bright character.
        const auto shimmerSmoothCoeff = lerp(0.26f, 0.12f, a);
        isaacShimmerSmooth[0] += shimmerSmoothCoeff * (wetL - isaacShimmerSmooth[0]);
        isaacShimmerSmooth[1] += shimmerSmoothCoeff * (wetR - isaacShimmerSmooth[1]);
        wetL = isaacShimmerSmooth[0];
        wetR = isaacShimmerSmooth[1];
    }
    else
    {
        wetL = std::tanh((wetL * 0.82f + isaacFeedbackFilter[0] * 0.18f) * (1.0f + 0.25f * macro));
        wetR = std::tanh((wetR * 0.82f + isaacFeedbackFilter[1] * 0.18f) * (1.0f + 0.25f * macro));
    }

    const auto writeL = sanitizeAudioSample(dryL + wetL * feedback);
    const auto writeR = sanitizeAudioSample(dryR + wetR * feedback);

    isaacDelayBuffer[0][static_cast<std::size_t>(isaacWritePos)] = writeL;
    isaacDelayBuffer[1][static_cast<std::size_t>(isaacWritePos)] = writeR;
    isaacWritePos = (isaacWritePos + 1) % isaacBufferSize;

    auto wetMix = lerp(0.08f, 0.92f, std::pow(macro, 1.02f));
    auto dryMix = lerp(0.95f, 0.12f, macro);

    if (mode == GranularMode::cloud)
    {
        wetMix = lerp(0.14f, 0.97f, a);
        dryMix = lerp(0.90f, 0.08f, a);
    }
    else if (mode == GranularMode::shimmer)
    {
        wetMix = lerp(0.16f, 0.94f, a);
        dryMix = lerp(0.92f, 0.10f, a);
    }
    else if (mode == GranularMode::rhythmic)
    {
        wetMix = lerp(0.10f, 0.90f, a);
        dryMix = lerp(0.96f, 0.22f, a);
    }

    auto outLeft = sanitizeAudioSample(dryL * dryMix + wetL * wetMix);
    auto outRight = sanitizeAudioSample(dryR * dryMix + wetR * wetMix);

    if (mode == GranularMode::shimmer)
    {
        outLeft = std::tanh(outLeft * 0.94f);
        outRight = std::tanh(outRight * 0.94f);
    }

    outL = outLeft;
    outR = outRight;
}

void PX3SynthAudioProcessor::processDelayAlgorithmSample(float inL,
                                                             float inR,
                                                             float amount,
                                                             int algorithmIndex,
                                                             float timeControl,
                                                             float feedbackControl,
                                                             int syncDivisionIndex,
                                                             float& outL,
                                                             float& outR)
{
    const auto coeff = juce::jlimit(0.0001f, 1.0f, delayControlSmoothingCoeff);
    delayAmountSmoothed += coeff * (clamp01(amount) - delayAmountSmoothed);
    delayTimeControlSmoothed += coeff * (clamp01(timeControl) - delayTimeControlSmoothed);
    delayFeedbackControlSmoothed += coeff * (clamp01(feedbackControl) - delayFeedbackControlSmoothed);

    const auto amountSmooth = clamp01(delayAmountSmoothed);
    const auto timeControlSmooth = clamp01(delayTimeControlSmoothed);
    const auto feedbackControlSmooth = clamp01(delayFeedbackControlSmoothed);

    // All delay algorithms share one circular memory line for coherence across
    // mode changes; each algorithm then colors/modulates reads differently.
    const auto algo = juce::jlimit(0, 6, algorithmIndex);

    if (algo == 0)
    {
        processIsaacGranularSample(inL,
                                   inR,
                                   amountSmooth,
                                   timeControlSmooth,
                                   feedbackControlSmooth,
                                   syncDivisionIndex,
                                   outL,
                                   outR);
        return;
    }

    if (isaacBufferSize <= 1)
    {
        outL = inL;
        outR = inR;
        return;
    }

    const auto a = smoothstep(amountSmooth);
    const auto secPerBeat = static_cast<float>(60.0 / currentBpm);
    float baseDelaySec = lerp(0.04f, 1.25f, timeControlSmooth);
    const auto syncBeats = divisionBeatsForIndex(syncDivisionIndex);
    if (syncDivisionIndex > 0 && syncBeats > 0.0f)
    {
        baseDelaySec = secPerBeat * syncBeats;
    }

    auto feedback = juce::jlimit(0.0f, 0.95f, lerp(0.05f, 0.92f, feedbackControlSmooth));
    if (algo == 2) // BBD can get unstable quickly; keep a stricter cap
    {
        feedback = juce::jmin(feedback, 0.82f);
    }

    delayModPhase += juce::MathConstants<float>::twoPi * (0.18f + 0.65f * timeControlSmooth)
                     / static_cast<float>(juce::jmax(1.0, currentSampleRateHz));
    while (delayModPhase >= juce::MathConstants<float>::twoPi)
    {
        delayModPhase -= juce::MathConstants<float>::twoPi;
    }

    auto delaySamplesL = baseDelaySec * static_cast<float>(currentSampleRateHz);
    auto delaySamplesR = delaySamplesL;

    if (algo == 2) // Analog/BBD
    {
        delaySamplesL = juce::jlimit(20.0f,
                                     static_cast<float>(isaacBufferSize - 2),
                                     lerp(0.02f, 0.55f, timeControlSmooth) * static_cast<float>(currentSampleRateHz));
        delaySamplesR = delaySamplesL;
    }
    else if (algo == 4) // Stereo delay
    {
        delaySamplesL *= 0.82f;
        delaySamplesR *= 1.28f;
    }
    else if (algo == 5) // Modulated delay
    {
        const auto modDepthSamples = (6.0f + 26.0f * a);
        const auto modA = std::sin(delayModPhase);
        const auto modB = std::sin(delayModPhase * 1.31f + 1.2f);
        delaySamplesL += modDepthSamples * modA;
        delaySamplesR += modDepthSamples * modB;
    }

    delaySamplesL = juce::jlimit(10.0f, static_cast<float>(isaacBufferSize - 2), delaySamplesL);
    delaySamplesR = juce::jlimit(10.0f, static_cast<float>(isaacBufferSize - 2), delaySamplesR);

    const auto readPosL = static_cast<float>(isaacWritePos) - delaySamplesL;
    const auto readPosR = static_cast<float>(isaacWritePos) - delaySamplesR;

    auto wetL = readDelaySample(0, readPosL);
    auto wetR = readDelaySample(1, readPosR);

    if (algo == 1) // Tape
    {
        const auto wow = std::sin(delayModPhase * 0.7f) * (0.003f + 0.007f * a);
        wetL = std::tanh((wetL + wow) * (0.95f + 0.35f * a));
        wetR = std::tanh((wetR - wow) * (0.95f + 0.35f * a));
        isaacFeedbackFilter[0] += 0.05f * (wetL - isaacFeedbackFilter[0]);
        isaacFeedbackFilter[1] += 0.05f * (wetR - isaacFeedbackFilter[1]);
        wetL = wetL * 0.62f + isaacFeedbackFilter[0] * 0.38f;
        wetR = wetR * 0.62f + isaacFeedbackFilter[1] * 0.38f;
    }
    else if (algo == 2) // Analog/BBD
    {
        isaacFeedbackFilter[0] += 0.03f * (wetL - isaacFeedbackFilter[0]);
        isaacFeedbackFilter[1] += 0.03f * (wetR - isaacFeedbackFilter[1]);
        wetL = wetL * 0.52f + isaacFeedbackFilter[0] * 0.48f;
        wetR = wetR * 0.52f + isaacFeedbackFilter[1] * 0.48f;
    }
    else if (algo == 3) // Ping-Pong
    {
        wetL = readDelaySample(1, readPosL);
        wetR = readDelaySample(0, readPosR);
    }
    else if (algo == 6) // Diffusion
    {
        const auto tapA = readDelaySample(0, readPosL - delaySamplesL * 0.23f);
        const auto tapB = readDelaySample(1, readPosR - delaySamplesR * 0.17f);
        const auto tapC = readDelaySample(0, readPosL - delaySamplesL * 0.37f);
        const auto tapD = readDelaySample(1, readPosR - delaySamplesR * 0.31f);
        wetL = 0.50f * wetL + 0.24f * tapA + 0.18f * tapB + 0.08f * tapD;
        wetR = 0.50f * wetR + 0.24f * tapB + 0.18f * tapC + 0.08f * tapA;
    }

    float writeL = inL;
    float writeR = inR;

    if (algo == 3) // Ping-Pong cross feedback
    {
        writeL += wetR * feedback;
        writeR += wetL * feedback;
    }
    else
    {
        writeL += wetL * feedback;
        writeR += wetR * feedback;
    }

    if (algo == 1 || algo == 2)
    {
        const auto sat = algo == 1 ? 0.85f : 1.05f;
        writeL = std::tanh(writeL * sat);
        writeR = std::tanh(writeR * sat);
    }

    isaacDelayBuffer[0][static_cast<std::size_t>(isaacWritePos)] = writeL;
    isaacDelayBuffer[1][static_cast<std::size_t>(isaacWritePos)] = writeR;
    isaacWritePos = (isaacWritePos + 1) % isaacBufferSize;

    const auto wetMix = lerp(0.06f, 0.86f, std::pow(a, 0.95f));
    const auto dryMix = 1.0f - wetMix * 0.88f;

    outL = inL * dryMix + wetL * wetMix;
    outR = inR * dryMix + wetR * wetMix;
}

void PX3SynthAudioProcessor::resizeReverbLine(ReverbDelayLine& line, int size)
{
    line.buffer.assign(static_cast<std::size_t>(juce::jmax(2, size)), 0.0f);
    line.writePos = 0;
    line.lpState = 0.0f;
}

float PX3SynthAudioProcessor::readReverbLine(const ReverbDelayLine& line, float delaySamples) const
{
    if (line.buffer.empty())
    {
        return 0.0f;
    }

    const auto size = static_cast<int>(line.buffer.size());
    auto readPos = static_cast<float>(line.writePos) - delaySamples;
    while (readPos < 0.0f)
    {
        readPos += static_cast<float>(size);
    }
    while (readPos >= static_cast<float>(size))
    {
        readPos -= static_cast<float>(size);
    }

    const auto i0 = static_cast<int>(readPos);
    const auto i1 = (i0 + 1) % size;
    const auto frac = readPos - static_cast<float>(i0);
    return line.buffer[static_cast<std::size_t>(i0)]
           + (line.buffer[static_cast<std::size_t>(i1)] - line.buffer[static_cast<std::size_t>(i0)]) * frac;
}

void PX3SynthAudioProcessor::writeReverbLine(ReverbDelayLine& line, float sample)
{
    if (line.buffer.empty())
    {
        return;
    }

    line.buffer[static_cast<std::size_t>(line.writePos)] = sample;
    line.writePos = (line.writePos + 1) % static_cast<int>(line.buffer.size());
}

float PX3SynthAudioProcessor::processReverbAllpass(ReverbDelayLine& line, float in, float delaySamples, float gain)
{
    const auto delayed = readReverbLine(line, delaySamples);
    const auto v = in - gain * delayed;
    writeReverbLine(line, v);
    return delayed + gain * v;
}

float PX3SynthAudioProcessor::processReverbDelay(ReverbDelayLine& line, float in, float delaySamples)
{
    const auto delayed = readReverbLine(line, delaySamples);
    writeReverbLine(line, in);
    return delayed;
}

void PX3SynthAudioProcessor::processReverbSampleFrame(float inL,
                                                          float inR,
                                                          float amount,
                                                          int algorithmIndex,
                                                          float& outL,
                                                          float& outR)
{
    // Reverb modes intentionally mix classic algorithm families:
    // room (JUCE reverb), plate-style tank, and hall-style multi-line network.
    // Parameters are base values with optional LFO modulation applied at read time.
    if (amount <= 0.0001f)
    {
        reverbWetSlewState[0] += 0.04f * (0.0f - reverbWetSlewState[0]);
        reverbWetSlewState[1] += 0.04f * (0.0f - reverbWetSlewState[1]);
        outL = inL;
        outR = inR;
        return;
    }

    const auto lfoSignal = lfoCurrentValue.load(std::memory_order_relaxed);
    const auto mode = juce::jlimit(0, 3, algorithmIndex);
    const auto mix = smoothstep(amount);
    const auto size = reverbSizeParam != nullptr
                          ? applyLfoToNormalizedValue(reverbSizeParam,
                                                      static_cast<juce::RangedAudioParameter*>(reverbSizeParam)->getValue(),
                                                      lfoSignal)
                          : 0.52f;
    const auto decayControl = reverbDecayParam != nullptr
                                  ? applyLfoToNormalizedValue(reverbDecayParam,
                                                              static_cast<juce::RangedAudioParameter*>(reverbDecayParam)->getValue(),
                                                              lfoSignal)
                                  : 0.48f;
    const auto damping = reverbDampingParam != nullptr
                             ? applyLfoToNormalizedValue(reverbDampingParam,
                                                         static_cast<juce::RangedAudioParameter*>(reverbDampingParam)->getValue(),
                                                         lfoSignal)
                             : 0.46f;
    const auto preDelay = reverbPreDelayParam != nullptr
                              ? applyLfoToNormalizedValue(reverbPreDelayParam,
                                                          static_cast<juce::RangedAudioParameter*>(reverbPreDelayParam)->getValue(),
                                                          lfoSignal)
                              : 0.08f;
    const auto modDepth = reverbModDepthParam != nullptr
                              ? applyLfoToNormalizedValue(reverbModDepthParam,
                                                          static_cast<juce::RangedAudioParameter*>(reverbModDepthParam)->getValue(),
                                                          lfoSignal)
                              : 0.24f;
    const auto modRate = reverbModRateParam != nullptr
                             ? applyLfoToNormalizedValue(reverbModRateParam,
                                                         static_cast<juce::RangedAudioParameter*>(reverbModRateParam)->getValue(),
                                                         lfoSignal)
                             : 0.18f;
    const auto width = reverbWidthParam != nullptr
                           ? applyLfoToNormalizedValue(reverbWidthParam,
                                                       static_cast<juce::RangedAudioParameter*>(reverbWidthParam)->getValue(),
                                                       lfoSignal)
                           : 0.86f;
    const auto cloudFeedback = reverbCloudFeedbackParam != nullptr
                                   ? applyLfoToNormalizedValue(reverbCloudFeedbackParam,
                                                               static_cast<juce::RangedAudioParameter*>(reverbCloudFeedbackParam)->getValue(),
                                                               lfoSignal)
                                   : 0.62f;
    const auto cloudDiffusion = reverbCloudDiffusionParam != nullptr
                                    ? applyLfoToNormalizedValue(reverbCloudDiffusionParam,
                                                                static_cast<juce::RangedAudioParameter*>(reverbCloudDiffusionParam)->getValue(),
                                                                lfoSignal)
                                    : 0.54f;

    // Remove ultra-low/DC bias before feeding the reverb network.
    const auto hpCoeff = std::exp(-2.0f * juce::MathConstants<float>::pi * 28.0f
                                  / static_cast<float>(juce::jmax(1.0, currentSampleRateHz)));
    const auto inputHpL = inL - reverbInputDcX1[0] + hpCoeff * reverbInputDcY1[0];
    const auto inputHpR = inR - reverbInputDcX1[1] + hpCoeff * reverbInputDcY1[1];
    reverbInputDcX1[0] = inL;
    reverbInputDcX1[1] = inR;
    reverbInputDcY1[0] = inputHpL;
    reverbInputDcY1[1] = inputHpR;

    // Predelay separates dry transient from wet onset, improving clarity.
    const auto predelaySamples = 1.0f + preDelay * static_cast<float>(currentSampleRateHz) * 0.30f;
    const auto inPredelayedL = processReverbDelay(reverbPreDelayLines[0], inputHpL, predelaySamples);
    const auto inPredelayedR = processReverbDelay(reverbPreDelayLines[1], inputHpR, predelaySamples);

    float wetL = 0.0f;
    float wetR = 0.0f;

    if (mode == 0)
    {
        juce::Reverb::Parameters p;
        p.freezeMode = 0.0f;
        p.roomSize = lerp(0.30f, 0.68f, size);
        p.damping = lerp(0.75f, 0.30f, damping);
        p.width = lerp(0.35f, 1.0f, width);
        p.wetLevel = 1.0f;
        p.dryLevel = 0.0f;
        reverb.setParameters(p);

        wetL = inPredelayedL;
        wetR = inPredelayedR;
        reverb.processStereo(&wetL, &wetR, 1);
    }
    else if (mode == 1)
    {
        const auto monoIn = 0.5f * (inPredelayedL + inPredelayedR);
        auto x = processReverbAllpass(plateLines[0], monoIn, lerp(160.0f, 520.0f, size), 0.72f);
        x = processReverbAllpass(plateLines[1], x, lerp(130.0f, 420.0f, size), 0.68f);

        const auto decay = juce::jlimit(0.0f, 0.95f, lerp(0.40f, 0.92f, decayControl));
        const auto dampCoeff = lerp(0.35f, 0.02f, damping);
        const auto modHz = lerp(0.08f, 1.4f, modRate);
        const auto modSamples = 2.0f + 14.0f * modDepth;

        const auto step = juce::MathConstants<float>::twoPi * modHz / static_cast<float>(juce::jmax(1.0, currentSampleRateHz));
        plateLines[4].modPhase += step;
        plateLines[5].modPhase += step * 1.13f;
        if (plateLines[4].modPhase > juce::MathConstants<float>::twoPi) plateLines[4].modPhase -= juce::MathConstants<float>::twoPi;
        if (plateLines[5].modPhase > juce::MathConstants<float>::twoPi) plateLines[5].modPhase -= juce::MathConstants<float>::twoPi;

        const auto tankInA = x + plateTankState[1] * decay;
        const auto tankInB = x + plateTankState[0] * decay;

        auto a = processReverbAllpass(plateLines[4], tankInA, lerp(380.0f, 980.0f, size) + std::sin(plateLines[4].modPhase) * modSamples, 0.62f);
        a = processReverbDelay(plateLines[2], a, lerp(1800.0f, 3900.0f, size));
        plateLines[2].lpState += dampCoeff * (a - plateLines[2].lpState);
        plateTankState[0] = plateLines[2].lpState;

        auto b = processReverbAllpass(plateLines[5], tankInB, lerp(420.0f, 1120.0f, size) + std::sin(plateLines[5].modPhase) * modSamples, 0.60f);
        b = processReverbDelay(plateLines[3], b, lerp(2100.0f, 4300.0f, size));
        plateLines[3].lpState += dampCoeff * (b - plateLines[3].lpState);
        plateTankState[1] = plateLines[3].lpState;

        wetL = 0.66f * plateTankState[0] + 0.34f * readReverbLine(plateLines[3], lerp(970.0f, 1740.0f, size));
        wetR = 0.66f * plateTankState[1] + 0.34f * readReverbLine(plateLines[2], lerp(1030.0f, 1830.0f, size));
    }
    else if (mode == 2)
    {
        const auto monoIn = 0.5f * (inPredelayedL + inPredelayedR);
        const auto decay = juce::jlimit(0.0f, 0.96f, lerp(0.45f, 0.95f, decayControl));
        const auto dampCoeff = lerp(0.28f, 0.015f, damping);
        const auto modHz = lerp(0.05f, 0.80f, modRate);
        const auto modSamples = 1.0f + 8.0f * modDepth;
        const auto modStep = juce::MathConstants<float>::twoPi * modHz / static_cast<float>(juce::jmax(1.0, currentSampleRateHz));

        float sum = 0.0f;
        for (auto& line : hallLines)
        {
            line.modPhase += modStep;
            if (line.modPhase > juce::MathConstants<float>::twoPi)
            {
                line.modPhase -= juce::MathConstants<float>::twoPi;
            }
        }

        for (int i = 0; i < 8; ++i)
        {
            const auto baseDelay = lerp(1200.0f + static_cast<float>(i) * 260.0f,
                                        5200.0f + static_cast<float>(i) * 540.0f,
                                        size);
            const auto mod = std::sin(hallLines[static_cast<std::size_t>(i)].modPhase + static_cast<float>(i) * 0.47f) * modSamples;
            const auto read = readReverbLine(hallLines[static_cast<std::size_t>(i)], baseDelay + mod);
            hallLines[static_cast<std::size_t>(i)].lpState += dampCoeff * (read - hallLines[static_cast<std::size_t>(i)].lpState);
            hallReadCache[static_cast<std::size_t>(i)] = hallLines[static_cast<std::size_t>(i)].lpState;
            sum += hallReadCache[static_cast<std::size_t>(i)];
        }

        const auto householder = 2.0f / 8.0f;
        for (int i = 0; i < 8; ++i)
        {
            const auto fb = (-hallReadCache[static_cast<std::size_t>(i)] + householder * sum) * decay;
            const auto inputInject = monoIn * (0.24f + 0.06f * static_cast<float>((i & 1) != 0));
            writeReverbLine(hallLines[static_cast<std::size_t>(i)], sanitizeAudioSample(inputInject + fb));
        }

        wetL = hallReadCache[0] * 0.27f + hallReadCache[2] * 0.23f + hallReadCache[5] * 0.20f + hallReadCache[7] * 0.18f;
        wetR = hallReadCache[1] * 0.27f + hallReadCache[3] * 0.23f + hallReadCache[4] * 0.20f + hallReadCache[6] * 0.18f;
    }
    else
    {
        const auto monoIn = 0.5f * (inPredelayedL + inPredelayedR);
        const auto decay = juce::jlimit(0.0f, 0.985f, lerp(0.55f, 0.985f, decayControl));
        const auto fb = juce::jlimit(0.0f, 0.985f, lerp(0.45f, 0.985f, cloudFeedback));
        const auto dampCoeff = lerp(0.22f, 0.010f, damping);
        const auto modHz = lerp(0.03f, 0.65f, modRate);
        const auto modSamples = 2.0f + 18.0f * modDepth;
        const auto modStep = juce::MathConstants<float>::twoPi * modHz / static_cast<float>(juce::jmax(1.0, currentSampleRateHz));

        for (int i = 0; i < 6; ++i)
        {
            auto& line = cloudLines[static_cast<std::size_t>(i)];
            line.modPhase += modStep * (1.0f + 0.11f * static_cast<float>(i));
            if (line.modPhase > juce::MathConstants<float>::twoPi)
            {
                line.modPhase -= juce::MathConstants<float>::twoPi;
            }

            const auto baseDelay = lerp(1800.0f + static_cast<float>(i) * 410.0f,
                                        9200.0f + static_cast<float>(i) * 960.0f,
                                        size);
            const auto mod = std::sin(line.modPhase + static_cast<float>(i) * 0.73f) * modSamples;
            const auto delayed = readReverbLine(line, baseDelay + mod);
            line.lpState += dampCoeff * (delayed - line.lpState);
            cloudReadCache[static_cast<std::size_t>(i)] = line.lpState;
        }

        for (int i = 0; i < 6; ++i)
        {
            const auto a = cloudReadCache[static_cast<std::size_t>((i + 1) % 6)];
            const auto b = cloudReadCache[static_cast<std::size_t>((i + 3) % 6)];
            const auto write = monoIn * 0.20f + (a * 0.58f + b * 0.42f) * fb * decay;
            writeReverbLine(cloudLines[static_cast<std::size_t>(i)], sanitizeAudioSample(write));
        }

        auto sumL = cloudReadCache[0] * 0.35f + cloudReadCache[2] * 0.28f + cloudReadCache[4] * 0.24f;
        auto sumR = cloudReadCache[1] * 0.35f + cloudReadCache[3] * 0.28f + cloudReadCache[5] * 0.24f;

        const auto diffAmt = juce::jlimit(0.0f, 1.0f, cloudDiffusion);
        sumL = processReverbAllpass(cloudLines[6], sumL, lerp(120.0f, 520.0f, diffAmt), 0.70f);
        sumR = processReverbAllpass(cloudLines[7], sumR, lerp(150.0f, 610.0f, diffAmt), 0.68f);

        wetL = sumL;
        wetR = sumR;
    }

    // Width/decorrelation stage shared by all modes.
    const auto mid = 0.5f * (wetL + wetR);
    const auto side = 0.5f * (wetL - wetR);
    const auto widthAmt = lerp(0.35f, 1.35f, width);
    wetL = mid + side * widthAmt;
    wetR = mid - side * widthAmt;

    // Remove accumulated DC/ultra-low bias from wet output.
    const auto wetHpL = wetL - reverbWetDcX1[0] + hpCoeff * reverbWetDcY1[0];
    const auto wetHpR = wetR - reverbWetDcX1[1] + hpCoeff * reverbWetDcY1[1];
    reverbWetDcX1[0] = wetL;
    reverbWetDcX1[1] = wetR;
    reverbWetDcY1[0] = wetHpL;
    reverbWetDcY1[1] = wetHpR;
    wetL = wetHpL;
    wetR = wetHpR;

    // High reverb mixes can create sharp composite peaks; apply gentle
    // peak control and soft saturation to reduce crackle/pop artifacts.
    if (mix > 0.30f)
    {
        const auto wetPeak = juce::jmax(std::abs(wetL), std::abs(wetR));
        if (wetPeak > 1.35f)
        {
            const auto scale = 1.35f / wetPeak;
            wetL *= scale;
            wetR *= scale;
        }

        const auto satAmount = juce::jlimit(0.0f, 0.42f, (mix - 0.30f) * 0.70f);
        const auto satDrive = 1.0f + satAmount;
        wetL = std::tanh(wetL * satDrive);
        wetR = std::tanh(wetR * satDrive);
    }

    // Slew-limit discontinuities to suppress residual clicks.
    const auto maxWetDelta = lerp(0.45f, 0.16f, mix);
    const auto deltaL = juce::jlimit(-maxWetDelta, maxWetDelta, wetL - reverbWetSlewState[0]);
    const auto deltaR = juce::jlimit(-maxWetDelta, maxWetDelta, wetR - reverbWetSlewState[1]);
    reverbWetSlewState[0] += deltaL;
    reverbWetSlewState[1] += deltaR;
    wetL = reverbWetSlewState[0];
    wetR = reverbWetSlewState[1];

    const auto dryGain = 1.0f - mix * 0.88f;
    outL = sanitizeAudioSample(inL * dryGain + wetL * mix);
    outR = sanitizeAudioSample(inR * dryGain + wetR * mix);
}

