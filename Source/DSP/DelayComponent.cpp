#include "DelayComponent.h"

#include <cmath>

float DelayComponent::clamp01(float v)
{
    return juce::jlimit(0.0f, 1.0f, v);
}

float DelayComponent::lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

float DelayComponent::smoothstep(float x)
{
    const auto t = clamp01(x);
    return t * t * (3.0f - 2.0f * t);
}

float DelayComponent::sanitizeAudioSample(float x)
{
    if (!std::isfinite(x))
    {
        return 0.0f;
    }

    return juce::jlimit(-4.0f, 4.0f, x);
}

float DelayComponent::divisionBeatsForIndex(int index)
{
    static constexpr std::array<float, 8> beatDivisions { 0.0f, 4.0f, 2.0f, 1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f };
    const auto clamped = juce::jlimit(0, static_cast<int>(beatDivisions.size()) - 1, index);
    return beatDivisions[static_cast<std::size_t>(clamped)];
}

void DelayComponent::prepare(double sampleRate)
{
    currentSampleRateHz = juce::jmax(1.0, sampleRate);
    const auto sr = static_cast<float>(currentSampleRateHz);
    constexpr float delayControlTauSec = 0.008f;
    delayControlSmoothingCoeff = 1.0f - std::exp(-1.0f / (sr * delayControlTauSec));

    constexpr float maxDelaySeconds = 16.0f;
    isaacBufferSize = juce::jmax(2, static_cast<int>(std::round(currentSampleRateHz * maxDelaySeconds)));
    isaacDelayBuffer[0].assign(static_cast<std::size_t>(isaacBufferSize), 0.0f);
    isaacDelayBuffer[1].assign(static_cast<std::size_t>(isaacBufferSize), 0.0f);

    constexpr int diffusionA = 127;
    constexpr int diffusionB = 211;
    isaacDiffusionLineA[0].assign(diffusionA, 0.0f);
    isaacDiffusionLineA[1].assign(diffusionA, 0.0f);
    isaacDiffusionLineB[0].assign(diffusionB, 0.0f);
    isaacDiffusionLineB[1].assign(diffusionB, 0.0f);

    reset();
}

void DelayComponent::reset()
{
    for (auto& line : isaacDelayBuffer)
    {
        std::fill(line.begin(), line.end(), 0.0f);
    }

    isaacFeedbackFilter = { { 0.0f, 0.0f } };
    isaacShimmerSmooth = { { 0.0f, 0.0f } };
    clearGranularDiffusionState();

    isaacWritePos = 0;
    isaacSpawnCounter = 0;
    isaacRhythmicStepIndex = 0;
    isaacRhythmicSamplesUntilNext = 0;
    isaacRhythmicSwingToggle = false;
    isaacPanPhase = 0.0f;

    delayAmountSmoothed = 0.0f;
    delayTimeControlSmoothed = 0.5f;
    delayFeedbackControlSmoothed = 0.35f;
    delayModPhase = 0.0f;
    lastDelayAlgorithmIndex = -1;
    lastGranularModeIndex = -1;
    smoothingPrimed = false;

    for (auto& grain : isaacGrains)
    {
        grain.active = false;
    }
}

void DelayComponent::updateForBlock(const DelaySettings& settings)
{
    currentSettings = settings;
    currentSettings.amount = clamp01(settings.amount);
    currentSettings.timeControl = clamp01(settings.timeControl);
    currentSettings.feedbackControl = clamp01(settings.feedbackControl);
    currentSettings.syncDivisionIndex = juce::jlimit(0, 7, settings.syncDivisionIndex);
    currentSettings.algorithmIndex = juce::jlimit(0, 6, settings.algorithmIndex);
    currentSettings.granularModeIndex = juce::jlimit(0, 3, settings.granularModeIndex);
    currentSettings.enabled = settings.enabled;
    currentBpm = juce::jmax(20.0, settings.bpm);

    if (!smoothingPrimed)
    {
        delayAmountSmoothed = currentSettings.amount;
        delayTimeControlSmoothed = currentSettings.timeControl;
        delayFeedbackControlSmoothed = currentSettings.feedbackControl;
        smoothingPrimed = true;
    }

    if (currentSettings.algorithmIndex != lastDelayAlgorithmIndex
        || currentSettings.granularModeIndex != lastGranularModeIndex)
    {
        lastDelayAlgorithmIndex = currentSettings.algorithmIndex;
        lastGranularModeIndex = currentSettings.granularModeIndex;
        isaacSpawnCounter = 0;
        isaacRhythmicStepIndex = 0;
        isaacRhythmicSamplesUntilNext = 0;
        isaacRhythmicSwingToggle = false;
        isaacFeedbackFilter = { { 0.0f, 0.0f } };
        isaacShimmerSmooth = { { 0.0f, 0.0f } };
        clearGranularDiffusionState();
        for (auto& grain : isaacGrains)
        {
            grain.active = false;
        }
    }
}

void DelayComponent::processSampleFrame(float inL, float inR, float& outL, float& outR)
{
    if (!currentSettings.enabled)
    {
        outL = inL;
        outR = inR;
        return;
    }

    processDelayAlgorithmSample(inL,
                                inR,
                                currentSettings.amount,
                                currentSettings.algorithmIndex,
                                currentSettings.timeControl,
                                currentSettings.feedbackControl,
                                currentSettings.syncDivisionIndex,
                                outL,
                                outR);
}

float DelayComponent::readDelaySample(int channel, float readPos) const
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

void DelayComponent::clearGranularDiffusionState()
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

float DelayComponent::processAllpassSample(float x,
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

void DelayComponent::processGranularDiffusion(float& wetL,
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

void DelayComponent::renderActiveGranularGrains(float& wetL, float& wetR)
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

void DelayComponent::spawnIsaacGrain(float amount,
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

void DelayComponent::processIsaacGranularSample(float inL,
                                                float inR,
                                                float amount,
                                                float timeControl,
                                                float feedbackControl,
                                                int syncDivisionIndex,
                                                float& outL,
                                                float& outR)
{
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

    const auto mode = static_cast<GranularMode>(juce::jlimit(0, 3, currentSettings.granularModeIndex));
    const auto a = smoothstep(amount);
    const auto macro = std::pow(a, 0.62f);
    const auto secPerBeat = static_cast<float>(60.0 / juce::jmax(20.0, currentBpm));
    const auto syncBeats = divisionBeatsForIndex(syncDivisionIndex);

    if (mode == GranularMode::rhythmic)
    {
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

    processGranularDiffusion(wetL, wetR, diffusion, stereo);

    isaacFeedbackFilter[0] += dampCoeff * (wetL - isaacFeedbackFilter[0]);
    isaacFeedbackFilter[1] += dampCoeff * (wetR - isaacFeedbackFilter[1]);

    if (mode == GranularMode::shimmer)
    {
        const auto shimmerTone = 1.0f + 0.32f * a;
        wetL = std::tanh((wetL * 0.76f + isaacFeedbackFilter[0] * 0.24f) * shimmerTone);
        wetR = std::tanh((wetR * 0.76f + isaacFeedbackFilter[1] * 0.24f) * shimmerTone);

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

void DelayComponent::processDelayAlgorithmSample(float inL,
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
    if (algo == 2)
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

    if (algo == 2)
    {
        delaySamplesL = juce::jlimit(20.0f,
                                     static_cast<float>(isaacBufferSize - 2),
                                     lerp(0.02f, 0.55f, timeControlSmooth) * static_cast<float>(currentSampleRateHz));
        delaySamplesR = delaySamplesL;
    }
    else if (algo == 4)
    {
        delaySamplesL *= 0.82f;
        delaySamplesR *= 1.28f;
    }
    else if (algo == 5)
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

    if (algo == 1)
    {
        const auto wow = std::sin(delayModPhase * 0.7f) * (0.003f + 0.007f * a);
        wetL = std::tanh((wetL + wow) * (0.95f + 0.35f * a));
        wetR = std::tanh((wetR - wow) * (0.95f + 0.35f * a));
        isaacFeedbackFilter[0] += 0.05f * (wetL - isaacFeedbackFilter[0]);
        isaacFeedbackFilter[1] += 0.05f * (wetR - isaacFeedbackFilter[1]);
        wetL = wetL * 0.62f + isaacFeedbackFilter[0] * 0.38f;
        wetR = wetR * 0.62f + isaacFeedbackFilter[1] * 0.38f;
    }
    else if (algo == 2)
    {
        isaacFeedbackFilter[0] += 0.03f * (wetL - isaacFeedbackFilter[0]);
        isaacFeedbackFilter[1] += 0.03f * (wetR - isaacFeedbackFilter[1]);
        wetL = wetL * 0.52f + isaacFeedbackFilter[0] * 0.48f;
        wetR = wetR * 0.52f + isaacFeedbackFilter[1] * 0.48f;
    }
    else if (algo == 3)
    {
        wetL = readDelaySample(1, readPosL);
        wetR = readDelaySample(0, readPosR);
    }
    else if (algo == 6)
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

    if (algo == 3)
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
