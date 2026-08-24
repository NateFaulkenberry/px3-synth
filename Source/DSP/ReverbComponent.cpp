#include "ReverbComponent.h"

#include <cmath>

float ReverbComponent::clamp01(float v)
{
    return juce::jlimit(0.0f, 1.0f, v);
}

float ReverbComponent::lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

float ReverbComponent::smoothstep(float x)
{
    const auto t = clamp01(x);
    return t * t * (3.0f - 2.0f * t);
}

float ReverbComponent::sanitizeAudioSample(float x)
{
    if (!std::isfinite(x))
    {
        return 0.0f;
    }

    return juce::jlimit(-4.0f, 4.0f, x);
}

void ReverbComponent::resizeLine(DelayLine& line, int size)
{
    line.buffer.assign(static_cast<std::size_t>(juce::jmax(2, size)), 0.0f);
    line.writePos = 0;
    line.lpState = 0.0f;
}

void ReverbComponent::writeLine(DelayLine& line, float sample)
{
    if (line.buffer.empty())
    {
        return;
    }

    line.buffer[static_cast<std::size_t>(line.writePos)] = sample;
    line.writePos = (line.writePos + 1) % static_cast<int>(line.buffer.size());
}

float ReverbComponent::readLine(const DelayLine& line, float delaySamples)
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

float ReverbComponent::processAllpass(DelayLine& line, float in, float delaySamples, float gain)
{
    const auto delayed = readLine(line, delaySamples);
    const auto v = in - gain * delayed;
    writeLine(line, v);
    return delayed + gain * v;
}

float ReverbComponent::processDelay(DelayLine& line, float in, float delaySamples)
{
    const auto delayed = readLine(line, delaySamples);
    writeLine(line, in);
    return delayed;
}

void ReverbComponent::prepare(double sampleRate)
{
    sampleRateHz = juce::jmax(1.0, sampleRate);

    constexpr float reverbAmountTauSec = 0.020f;
    const auto sr = static_cast<float>(sampleRateHz);
    amountSmoothingCoeff = 1.0f - std::exp(-1.0f / (sr * reverbAmountTauSec));

    reset();
}

void ReverbComponent::reset()
{
    reverb.reset();
    outputCompGain = 1.0f;
    blockPreEnergy = 0.0;
    blockPostEnergy = 0.0;
    blockSampleCount = 0;

    inputDcX1 = { { 0.0f, 0.0f } };
    inputDcY1 = { { 0.0f, 0.0f } };
    wetDcX1 = { { 0.0f, 0.0f } };
    wetDcY1 = { { 0.0f, 0.0f } };
    wetSlewState = { { 0.0f, 0.0f } };

    const auto maxPreDelaySamples = juce::jmax(8, static_cast<int>(std::round(sampleRateHz * 0.30)));
    for (auto& line : preDelayLines)
    {
        resizeLine(line, maxPreDelaySamples);
    }

    static constexpr std::array<float, 6> plateDelaySeconds { { 0.0113f, 0.0089f, 0.0721f, 0.0887f, 0.0317f, 0.0439f } };
    for (std::size_t i = 0; i < plateLines.size(); ++i)
    {
        resizeLine(plateLines[i], juce::jmax(16, static_cast<int>(std::round(sampleRateHz * plateDelaySeconds[i] * 1.8f))));
        plateLines[i].modPhase = static_cast<float>(i) * 0.67f;
        plateLines[i].lpState = 0.0f;
    }
    plateTankState = { { 0.0f, 0.0f } };

    static constexpr std::array<float, 8> hallDelaySeconds { { 0.030f, 0.037f, 0.041f, 0.047f, 0.053f, 0.059f, 0.067f, 0.079f } };
    for (std::size_t i = 0; i < hallLines.size(); ++i)
    {
        resizeLine(hallLines[i], juce::jmax(32, static_cast<int>(std::round(sampleRateHz * hallDelaySeconds[i] * 2.2f))));
        hallLines[i].modPhase = static_cast<float>(i) * 0.53f;
        hallLines[i].lpState = 0.0f;
    }
    hallReadCache.fill(0.0f);

    static constexpr std::array<float, 8> cloudDelaySeconds { { 0.049f, 0.061f, 0.073f, 0.089f, 0.104f, 0.127f, 0.013f, 0.017f } };
    for (std::size_t i = 0; i < cloudLines.size(); ++i)
    {
        resizeLine(cloudLines[i], juce::jmax(32, static_cast<int>(std::round(sampleRateHz * cloudDelaySeconds[i] * 2.6f))));
        cloudLines[i].modPhase = static_cast<float>(i) * 0.91f;
        cloudLines[i].lpState = 0.0f;
    }
    cloudReadCache.fill(0.0f);
}

void ReverbComponent::updateForBlock(const ReverbSettings& settings, int numSamples)
{
    currentSettings.amount = clamp01(settings.amount);
    currentSettings.enabled = settings.enabled;
    currentSettings.algorithmIndex = juce::jlimit(0, 3, settings.algorithmIndex);
    currentSettings.size = clamp01(settings.size);
    currentSettings.decay = clamp01(settings.decay);
    currentSettings.damping = clamp01(settings.damping);
    currentSettings.preDelay = clamp01(settings.preDelay);
    currentSettings.modDepth = clamp01(settings.modDepth);
    currentSettings.modRate = clamp01(settings.modRate);
    currentSettings.width = clamp01(settings.width);
    currentSettings.cloudFeedback = clamp01(settings.cloudFeedback);
    currentSettings.cloudDiffusion = clamp01(settings.cloudDiffusion);

    blockSampleCount = juce::jmax(0, numSamples);
    blockPreEnergy = 0.0;
    blockPostEnergy = 0.0;
}

void ReverbComponent::processSampleFrame(float inL, float inR, float& outL, float& outR)
{
    const auto amountTarget = currentSettings.enabled ? currentSettings.amount : 0.0f;
    const auto smoothCoeff = juce::jlimit(0.0001f, 1.0f, amountSmoothingCoeff);
    amountSmoothed += smoothCoeff * (amountTarget - amountSmoothed);
    const auto amountForSample = clamp01(amountSmoothed);

    if (amountForSample > 0.0001f)
    {
        const auto beforeL = inL;
        const auto beforeR = inR;
        processCore(inL, inR, amountForSample, currentSettings.algorithmIndex, outL, outR);
        blockPreEnergy += 0.5 * (static_cast<double>(beforeL) * static_cast<double>(beforeL)
                                 + static_cast<double>(beforeR) * static_cast<double>(beforeR));
        blockPostEnergy += 0.5 * (static_cast<double>(outL) * static_cast<double>(outL)
                                  + static_cast<double>(outR) * static_cast<double>(outR));
        return;
    }

    wetSlewState[0] += 0.04f * (0.0f - wetSlewState[0]);
    wetSlewState[1] += 0.04f * (0.0f - wetSlewState[1]);
    outL = inL;
    outR = inR;
}

void ReverbComponent::applyPostBlockCompensation(juce::AudioBuffer<float>& buffer)
{
    if (amountSmoothed > 0.0001f && blockSampleCount > 0)
    {
        const auto invN = 1.0 / static_cast<double>(blockSampleCount);
        const auto preRms = static_cast<float>(std::sqrt(juce::jmax(1.0e-12, blockPreEnergy * invN)));
        const auto postRms = static_cast<float>(std::sqrt(juce::jmax(1.0e-12, blockPostEnergy * invN)));
        const auto rawComp = juce::jlimit(0.72f, 1.45f, preRms / juce::jmax(1.0e-5f, postRms));

        const auto compBlend = smoothstep(clamp01(amountSmoothed));
        const auto targetComp = 1.0f + (rawComp - 1.0f) * compBlend;
        outputCompGain += 0.03f * (targetComp - outputCompGain);
    }
    else
    {
        outputCompGain += 0.02f * (1.0f - outputCompGain);
    }

    if (std::abs(outputCompGain - 1.0f) > 0.001f)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            buffer.applyGain(channel, 0, buffer.getNumSamples(), outputCompGain);
        }
    }
}

void ReverbComponent::processCore(float inL,
                                  float inR,
                                  float amount,
                                  int algorithmIndex,
                                  float& outL,
                                  float& outR)
{
    const auto mode = juce::jlimit(0, 3, algorithmIndex);
    const auto mix = smoothstep(amount);

    const auto hpCoeff = std::exp(-2.0f * juce::MathConstants<float>::pi * 28.0f
                                  / static_cast<float>(juce::jmax(1.0, sampleRateHz)));
    const auto inputHpL = inL - inputDcX1[0] + hpCoeff * inputDcY1[0];
    const auto inputHpR = inR - inputDcX1[1] + hpCoeff * inputDcY1[1];
    inputDcX1[0] = inL;
    inputDcX1[1] = inR;
    inputDcY1[0] = inputHpL;
    inputDcY1[1] = inputHpR;

    const auto predelaySamples = 1.0f + currentSettings.preDelay * static_cast<float>(sampleRateHz) * 0.30f;
    const auto inPredelayedL = processDelay(preDelayLines[0], inputHpL, predelaySamples);
    const auto inPredelayedR = processDelay(preDelayLines[1], inputHpR, predelaySamples);

    float wetL = 0.0f;
    float wetR = 0.0f;

    if (mode == 0)
    {
        juce::Reverb::Parameters p;
        p.freezeMode = 0.0f;
        p.roomSize = lerp(0.30f, 0.68f, currentSettings.size);
        p.damping = lerp(0.75f, 0.30f, currentSettings.damping);
        p.width = lerp(0.35f, 1.0f, currentSettings.width);
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
        auto x = processAllpass(plateLines[0], monoIn, lerp(160.0f, 520.0f, currentSettings.size), 0.72f);
        x = processAllpass(plateLines[1], x, lerp(130.0f, 420.0f, currentSettings.size), 0.68f);

        const auto decay = juce::jlimit(0.0f, 0.95f, lerp(0.40f, 0.92f, currentSettings.decay));
        const auto dampCoeff = lerp(0.35f, 0.02f, currentSettings.damping);
        const auto modHz = lerp(0.08f, 1.4f, currentSettings.modRate);
        const auto modSamples = 2.0f + 14.0f * currentSettings.modDepth;

        const auto step = juce::MathConstants<float>::twoPi * modHz / static_cast<float>(juce::jmax(1.0, sampleRateHz));
        plateLines[4].modPhase += step;
        plateLines[5].modPhase += step * 1.13f;
        if (plateLines[4].modPhase > juce::MathConstants<float>::twoPi) plateLines[4].modPhase -= juce::MathConstants<float>::twoPi;
        if (plateLines[5].modPhase > juce::MathConstants<float>::twoPi) plateLines[5].modPhase -= juce::MathConstants<float>::twoPi;

        const auto tankInA = x + plateTankState[1] * decay;
        const auto tankInB = x + plateTankState[0] * decay;

        auto a = processAllpass(plateLines[4], tankInA, lerp(380.0f, 980.0f, currentSettings.size) + std::sin(plateLines[4].modPhase) * modSamples, 0.62f);
        a = processDelay(plateLines[2], a, lerp(1800.0f, 3900.0f, currentSettings.size));
        plateLines[2].lpState += dampCoeff * (a - plateLines[2].lpState);
        plateTankState[0] = plateLines[2].lpState;

        auto b = processAllpass(plateLines[5], tankInB, lerp(420.0f, 1120.0f, currentSettings.size) + std::sin(plateLines[5].modPhase) * modSamples, 0.60f);
        b = processDelay(plateLines[3], b, lerp(2100.0f, 4300.0f, currentSettings.size));
        plateLines[3].lpState += dampCoeff * (b - plateLines[3].lpState);
        plateTankState[1] = plateLines[3].lpState;

        wetL = 0.66f * plateTankState[0] + 0.34f * readLine(plateLines[3], lerp(970.0f, 1740.0f, currentSettings.size));
        wetR = 0.66f * plateTankState[1] + 0.34f * readLine(plateLines[2], lerp(1030.0f, 1830.0f, currentSettings.size));
    }
    else if (mode == 2)
    {
        const auto monoIn = 0.5f * (inPredelayedL + inPredelayedR);
        const auto decay = juce::jlimit(0.0f, 0.96f, lerp(0.45f, 0.95f, currentSettings.decay));
        const auto dampCoeff = lerp(0.28f, 0.015f, currentSettings.damping);
        const auto modHz = lerp(0.05f, 0.80f, currentSettings.modRate);
        const auto modSamples = 1.0f + 8.0f * currentSettings.modDepth;
        const auto modStep = juce::MathConstants<float>::twoPi * modHz / static_cast<float>(juce::jmax(1.0, sampleRateHz));

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
                                        currentSettings.size);
            const auto mod = std::sin(hallLines[static_cast<std::size_t>(i)].modPhase + static_cast<float>(i) * 0.47f) * modSamples;
            const auto read = readLine(hallLines[static_cast<std::size_t>(i)], baseDelay + mod);
            hallLines[static_cast<std::size_t>(i)].lpState += dampCoeff * (read - hallLines[static_cast<std::size_t>(i)].lpState);
            hallReadCache[static_cast<std::size_t>(i)] = hallLines[static_cast<std::size_t>(i)].lpState;
            sum += hallReadCache[static_cast<std::size_t>(i)];
        }

        const auto householder = 2.0f / 8.0f;
        for (int i = 0; i < 8; ++i)
        {
            const auto fb = (-hallReadCache[static_cast<std::size_t>(i)] + householder * sum) * decay;
            const auto inputInject = monoIn * (0.24f + 0.06f * static_cast<float>((i & 1) != 0));
            writeLine(hallLines[static_cast<std::size_t>(i)], sanitizeAudioSample(inputInject + fb));
        }

        wetL = hallReadCache[0] * 0.27f + hallReadCache[2] * 0.23f + hallReadCache[5] * 0.20f + hallReadCache[7] * 0.18f;
        wetR = hallReadCache[1] * 0.27f + hallReadCache[3] * 0.23f + hallReadCache[4] * 0.20f + hallReadCache[6] * 0.18f;
    }
    else
    {
        const auto monoIn = 0.5f * (inPredelayedL + inPredelayedR);
        const auto decay = juce::jlimit(0.0f, 0.985f, lerp(0.55f, 0.985f, currentSettings.decay));
        const auto fb = juce::jlimit(0.0f, 0.985f, lerp(0.45f, 0.985f, currentSettings.cloudFeedback));
        const auto dampCoeff = lerp(0.22f, 0.010f, currentSettings.damping);
        const auto modHz = lerp(0.03f, 0.65f, currentSettings.modRate);
        const auto modSamples = 2.0f + 18.0f * currentSettings.modDepth;
        const auto modStep = juce::MathConstants<float>::twoPi * modHz / static_cast<float>(juce::jmax(1.0, sampleRateHz));

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
                                        currentSettings.size);
            const auto mod = std::sin(line.modPhase + static_cast<float>(i) * 0.73f) * modSamples;
            const auto delayed = readLine(line, baseDelay + mod);
            line.lpState += dampCoeff * (delayed - line.lpState);
            cloudReadCache[static_cast<std::size_t>(i)] = line.lpState;
        }

        for (int i = 0; i < 6; ++i)
        {
            const auto a = cloudReadCache[static_cast<std::size_t>((i + 1) % 6)];
            const auto b = cloudReadCache[static_cast<std::size_t>((i + 3) % 6)];
            const auto write = monoIn * 0.20f + (a * 0.58f + b * 0.42f) * fb * decay;
            writeLine(cloudLines[static_cast<std::size_t>(i)], sanitizeAudioSample(write));
        }

        auto sumL = cloudReadCache[0] * 0.35f + cloudReadCache[2] * 0.28f + cloudReadCache[4] * 0.24f;
        auto sumR = cloudReadCache[1] * 0.35f + cloudReadCache[3] * 0.28f + cloudReadCache[5] * 0.24f;

        const auto diffAmt = juce::jlimit(0.0f, 1.0f, currentSettings.cloudDiffusion);
        sumL = processAllpass(cloudLines[6], sumL, lerp(120.0f, 520.0f, diffAmt), 0.70f);
        sumR = processAllpass(cloudLines[7], sumR, lerp(150.0f, 610.0f, diffAmt), 0.68f);

        wetL = sumL;
        wetR = sumR;
    }

    const auto mid = 0.5f * (wetL + wetR);
    const auto side = 0.5f * (wetL - wetR);
    const auto widthAmt = lerp(0.35f, 1.35f, currentSettings.width);
    wetL = mid + side * widthAmt;
    wetR = mid - side * widthAmt;

    const auto wetHpL = wetL - wetDcX1[0] + hpCoeff * wetDcY1[0];
    const auto wetHpR = wetR - wetDcX1[1] + hpCoeff * wetDcY1[1];
    wetDcX1[0] = wetL;
    wetDcX1[1] = wetR;
    wetDcY1[0] = wetHpL;
    wetDcY1[1] = wetHpR;
    wetL = wetHpL;
    wetR = wetHpR;

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

    const auto maxWetDelta = lerp(0.45f, 0.16f, mix);
    const auto deltaL = juce::jlimit(-maxWetDelta, maxWetDelta, wetL - wetSlewState[0]);
    const auto deltaR = juce::jlimit(-maxWetDelta, maxWetDelta, wetR - wetSlewState[1]);
    wetSlewState[0] += deltaL;
    wetSlewState[1] += deltaR;
    wetL = wetSlewState[0];
    wetR = wetSlewState[1];

    const auto dryGain = 1.0f - mix * 0.88f;
    outL = sanitizeAudioSample(inL * dryGain + wetL * mix);
    outR = sanitizeAudioSample(inR * dryGain + wetR * mix);
}
