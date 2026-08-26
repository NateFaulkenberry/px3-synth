#include "Mood.h"

#include <cmath>

float Mood::clamp01(float v)
{
    return juce::jlimit(0.0f, 1.0f, v);
}

float Mood::sanitizeAudioSample(float v)
{
    if (!std::isfinite(v))
    {
        return 0.0f;
    }
    return juce::jlimit(-3.5f, 3.5f, v);
}

float Mood::semitoneRatio(float semitones)
{
    return std::pow(2.0f, semitones / 12.0f);
}

void Mood::prepare(double sampleRate)
{
    sampleRateHz = juce::jmax(1.0, sampleRate);

    constexpr float maxHistorySeconds = 8.0f;
    constexpr float maxWetDelaySeconds = 6.0f;

    historySize = juce::jmax(2, static_cast<int>(std::round(sampleRateHz * maxHistorySeconds)));
    wetDelaySize = juce::jmax(2, static_cast<int>(std::round(sampleRateHz * maxWetDelaySeconds)));

    historyBuffer[0].assign(static_cast<std::size_t>(historySize), 0.0f);
    historyBuffer[1].assign(static_cast<std::size_t>(historySize), 0.0f);
    wetDelayBuffer[0].assign(static_cast<std::size_t>(wetDelaySize), 0.0f);
    wetDelayBuffer[1].assign(static_cast<std::size_t>(wetDelaySize), 0.0f);

    reset();
}

void Mood::reset()
{
    for (auto& line : historyBuffer)
    {
        std::fill(line.begin(), line.end(), 0.0f);
    }
    for (auto& line : wetDelayBuffer)
    {
        std::fill(line.begin(), line.end(), 0.0f);
    }

    historyWritePos = 0;
    wetDelayWritePos = 0;
    loopReadPos = 0.0f;
    loopHeldReadPos = 0.0f;
    envFollower = 0.0f;
    envSliceHoldSamples = 0;
    stretchSpawnCounter = 0;
    slipReadPos = 0.0f;
    slipCapturePos = 0.0f;
    clockHoldSamples = 1;
    clockSampleCounter = 0;
    clockHeldL = 0.0f;
    clockHeldR = 0.0f;

    for (auto& grain : grains)
    {
        grain.active = false;
    }
}

void Mood::updateForBlock(const MoodSettings& settings)
{
    const auto nextEnabled = settings.enabled;
    if (wasEnabled && !nextEnabled)
    {
        // Entering bypass: clear all loop/delay memory so re-enable starts clean.
        reset();
    }
    wasEnabled = nextEnabled;

    currentSettings = settings;

    currentSettings.mix = clamp01(settings.mix);
    currentSettings.clock = clamp01(settings.clock);
    currentSettings.routing = clamp01(settings.routing);
    currentSettings.wetTime = clamp01(settings.wetTime);
    currentSettings.wetModify = clamp01(settings.wetModify);
    currentSettings.loopLength = clamp01(settings.loopLength);
    currentSettings.loopModify = clamp01(settings.loopModify);
    currentSettings.feedback = clamp01(settings.feedback);
    currentSettings.spread = clamp01(settings.spread);
    currentSettings.degrade = clamp01(settings.degrade);

    currentSettings.wetModeIndex = juce::jlimit(0, 2, settings.wetModeIndex);
    currentSettings.loopModeIndex = juce::jlimit(0, 2, settings.loopModeIndex);

    const auto clockRatio = 0.25f + currentSettings.clock * 1.75f;
    const auto degradeWeight = std::pow(currentSettings.degrade, 1.35f);
    const auto effectiveRate = juce::jmax(0.05f, clockRatio * (1.0f - 0.82f * degradeWeight));
    clockHoldSamples = juce::jlimit(1,
                                    1024,
                                    static_cast<int>(std::round(static_cast<float>(sampleRateHz) / (sampleRateHz * effectiveRate))));
}

float Mood::readInterp(const std::vector<float>& line, float pos) const
{
    if (line.empty())
    {
        return 0.0f;
    }

    const auto size = static_cast<int>(line.size());
    auto p = pos;
    while (p < 0.0f)
    {
        p += static_cast<float>(size);
    }
    while (p >= static_cast<float>(size))
    {
        p -= static_cast<float>(size);
    }

    const auto i0 = static_cast<int>(p);
    const auto i1 = (i0 + 1) % size;
    const auto frac = p - static_cast<float>(i0);
    return line[static_cast<std::size_t>(i0)] + (line[static_cast<std::size_t>(i1)] - line[static_cast<std::size_t>(i0)]) * frac;
}

void Mood::writeHistory(float l, float r)
{
    historyBuffer[0][static_cast<std::size_t>(historyWritePos)] = sanitizeAudioSample(l);
    historyBuffer[1][static_cast<std::size_t>(historyWritePos)] = sanitizeAudioSample(r);

    ++historyWritePos;
    if (historyWritePos >= historySize)
    {
        historyWritePos = 0;
    }
}

void Mood::writeWetDelay(float l, float r)
{
    wetDelayBuffer[0][static_cast<std::size_t>(wetDelayWritePos)] = sanitizeAudioSample(l);
    wetDelayBuffer[1][static_cast<std::size_t>(wetDelayWritePos)] = sanitizeAudioSample(r);

    ++wetDelayWritePos;
    if (wetDelayWritePos >= wetDelaySize)
    {
        wetDelayWritePos = 0;
    }
}

void Mood::processClockReduction(float inL, float inR, float& outL, float& outR)
{
    if (clockSampleCounter <= 0)
    {
        clockHeldL = inL;
        clockHeldR = inR;
        clockSampleCounter = clockHoldSamples;
    }

    --clockSampleCounter;
    outL = clockHeldL;
    outR = clockHeldR;
}

void Mood::renderLoopTape(float& loopL, float& loopR)
{
    const auto loopSeconds = juce::jmap(currentSettings.loopLength, 0.05f, 2.2f);
    const auto loopSamples = juce::jlimit(64.0f,
                                          static_cast<float>(historySize - 1),
                                          loopSeconds * static_cast<float>(sampleRateHz));

    const auto semitone = juce::jmap(currentSettings.loopModify, -24.0f, 24.0f);
    auto rate = semitoneRatio(semitone);
    if (std::abs(currentSettings.loopModify - 0.5f) < 0.03f)
    {
        rate *= 0.0f;
    }

    if (currentSettings.loopModify < 0.22f)
    {
        rate = -std::abs(rate);
    }

    const auto loopStart = static_cast<float>(historyWritePos) - loopSamples;
    const auto readAbs = loopStart + loopReadPos;
    loopL = readInterp(historyBuffer[0], readAbs);
    loopR = readInterp(historyBuffer[1], readAbs);

    loopReadPos += rate;
    while (loopReadPos < 0.0f)
    {
        loopReadPos += loopSamples;
    }
    while (loopReadPos >= loopSamples)
    {
        loopReadPos -= loopSamples;
    }
}

void Mood::renderLoopEnv(float inL, float inR, float& loopL, float& loopR)
{
    const auto envIn = 0.5f * (std::abs(inL) + std::abs(inR));
    envFollower += (envIn - envFollower) * 0.06f;

    const auto loopSeconds = juce::jmap(currentSettings.loopLength, 0.03f, 0.40f);
    const auto sliceSamples = juce::jlimit(32,
                                           historySize - 1,
                                           static_cast<int>(std::round(loopSeconds * static_cast<float>(sampleRateHz))));

    const auto threshold = juce::jmap(currentSettings.loopModify, 0.10f, 0.45f);
    if (envFollower > threshold && envSliceHoldSamples <= 0)
    {
        envSliceHoldSamples = sliceSamples;
        loopHeldReadPos = loopReadPos;
    }

    if (envSliceHoldSamples > 0)
    {
        loopReadPos = loopHeldReadPos;
        --envSliceHoldSamples;
    }

    const auto loopStart = static_cast<float>(historyWritePos) - static_cast<float>(sliceSamples * 2);
    const auto readAbs = loopStart + loopReadPos;
    loopL = readInterp(historyBuffer[0], readAbs);
    loopR = readInterp(historyBuffer[1], readAbs);

    loopReadPos += 1.0f;
    if (loopReadPos >= static_cast<float>(sliceSamples))
    {
        loopReadPos = 0.0f;
    }
}

void Mood::maybeSpawnStretchGrain()
{
    for (auto& grain : grains)
    {
        if (grain.active)
        {
            continue;
        }

        const auto baseLenMs = juce::jmap(currentSettings.loopLength, 22.0f, 210.0f);
        grain.lengthSamples = juce::jmax(12, static_cast<int>(std::round((baseLenMs / 1000.0f) * sampleRateHz)));

        const auto speed = juce::jmap(currentSettings.loopModify, 0.33f, 1.66f);
        grain.increment = speed;

        const auto jitter = (juce::Random::getSystemRandom().nextFloat() - 0.5f) * 0.22f * static_cast<float>(historySize);
        grain.readPos = static_cast<float>(historyWritePos) + jitter;
        grain.pan = juce::jlimit(0.0f, 1.0f,
                                 0.5f + (juce::Random::getSystemRandom().nextFloat() - 0.5f) * currentSettings.spread);
        grain.gain = 0.12f + 0.22f * currentSettings.mix;
        grain.ageSamples = 0;
        grain.active = true;
        return;
    }
}

void Mood::renderLoopStretch(float& loopL, float& loopR)
{
    ++stretchSpawnCounter;
    const auto spawnEvery = juce::jlimit(16,
                                         2048,
                                         static_cast<int>(std::round((0.015f + currentSettings.loopLength * 0.09f) * sampleRateHz)));
    if (stretchSpawnCounter >= spawnEvery)
    {
        stretchSpawnCounter = 0;
        maybeSpawnStretchGrain();
    }

    loopL = 0.0f;
    loopR = 0.0f;

    for (auto& grain : grains)
    {
        if (!grain.active)
        {
            continue;
        }

        const auto phase = static_cast<float>(grain.ageSamples) / static_cast<float>(juce::jmax(1, grain.lengthSamples));
        if (phase >= 1.0f)
        {
            grain.active = false;
            continue;
        }

        const auto window = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * phase);
        const auto l = readInterp(historyBuffer[0], grain.readPos);
        const auto r = readInterp(historyBuffer[1], grain.readPos);
        const auto mono = 0.5f * (l + r);
        const auto panAngle = clamp01(grain.pan) * juce::MathConstants<float>::halfPi;
        const auto gainL = std::cos(panAngle);
        const auto gainR = std::sin(panAngle);

        loopL += mono * grain.gain * window * gainL;
        loopR += mono * grain.gain * window * gainR;

        grain.readPos += grain.increment;
        ++grain.ageSamples;
    }
}

void Mood::renderWetReverb(float inL, float inR, float& wetL, float& wetR)
{
    static constexpr std::array<float, 8> tapsSec { 0.017f, 0.023f, 0.031f, 0.043f, 0.059f, 0.071f, 0.089f, 0.113f };

    const auto smear = currentSettings.wetModify;
    const auto tScale = juce::jmap(currentSettings.wetTime, 0.4f, 3.0f);

    float sumL = 0.0f;
    float sumR = 0.0f;
    for (int i = 0; i < static_cast<int>(tapsSec.size()); ++i)
    {
        const auto tap = tapsSec[static_cast<std::size_t>(i)] * tScale * static_cast<float>(sampleRateHz);
        const auto modTap = tap * (1.0f + smear * 0.35f * std::sin(0.13f * static_cast<float>(wetDelayWritePos + i * 17)));
        const auto rp = static_cast<float>(wetDelayWritePos) - modTap;
        const auto l = readInterp(wetDelayBuffer[0], rp);
        const auto r = readInterp(wetDelayBuffer[1], rp);
        if ((i & 1) == 0)
        {
            sumL += l;
            sumR += r * (0.75f + 0.25f * smear);
        }
        else
        {
            sumR += r;
            sumL += l * (0.75f + 0.25f * smear);
        }
    }

    wetL = sumL / static_cast<float>(tapsSec.size());
    wetR = sumR / static_cast<float>(tapsSec.size());

    const auto fb = 0.18f + 0.72f * currentSettings.feedback;
    writeWetDelay(inL + wetL * fb, inR + wetR * fb);
}

void Mood::renderWetDelay(float inL, float inR, float& wetL, float& wetR)
{
    const auto baseSeconds = juce::jmap(currentSettings.wetTime, 0.03f, 1.6f);
    const auto semitone = juce::jmap(currentSettings.clock, -12.0f, 12.0f);
    const auto ratio = semitoneRatio(semitone);
    const auto delaySamples = juce::jlimit(8.0f,
                                           static_cast<float>(wetDelaySize - 2),
                                           baseSeconds * static_cast<float>(sampleRateHz) / juce::jmax(0.25f, ratio));

    const auto readPos = static_cast<float>(wetDelayWritePos) - delaySamples;
    wetL = readInterp(wetDelayBuffer[0], readPos);
    wetR = readInterp(wetDelayBuffer[1], readPos);

    const auto fb = 0.12f + 0.83f * currentSettings.feedback;
    const auto satL = std::tanh(inL + wetL * fb);
    const auto satR = std::tanh(inR + wetR * fb);
    writeWetDelay(satL, satR);
}

void Mood::renderWetSlip(float inL, float inR, float& wetL, float& wetR)
{
    juce::ignoreUnused(inL, inR);

    const auto windowSeconds = juce::jmap(currentSettings.wetTime, 0.05f, 0.55f);
    const auto windowSamples = juce::jlimit(64.0f,
                                            static_cast<float>(historySize - 2),
                                            windowSeconds * static_cast<float>(sampleRateHz));

    slipCapturePos = static_cast<float>(historyWritePos);

    const auto speed = juce::jmap(currentSettings.wetModify, -2.0f, 2.0f);
    const auto readAbs = slipCapturePos - windowSamples + slipReadPos;
    wetL = readInterp(historyBuffer[0], readAbs);
    wetR = readInterp(historyBuffer[1], readAbs);

    slipReadPos += speed;
    while (slipReadPos < 0.0f)
    {
        slipReadPos += windowSamples;
    }
    while (slipReadPos >= windowSamples)
    {
        slipReadPos -= windowSamples;
    }
}

void Mood::processSampleFrame(float inL, float inR, float& outL, float& outR)
{
    if (!currentSettings.enabled)
    {
        outL = inL;
        outR = inR;
        return;
    }

    // Always-listening behavior: circular history keeps running unless frozen.
    if (!currentSettings.freeze)
    {
        writeHistory(inL, inR);
    }

    float clockInL = inL;
    float clockInR = inR;
    processClockReduction(clockInL, clockInR, clockInL, clockInR);

    float loopL = 0.0f;
    float loopR = 0.0f;

    switch (currentSettings.loopModeIndex)
    {
        case 0: renderLoopEnv(clockInL, clockInR, loopL, loopR); break;
        case 1: renderLoopTape(loopL, loopR); break;
        case 2: renderLoopStretch(loopL, loopR); break;
        default: break;
    }

    float wetInL = clockInL;
    float wetInR = clockInR;

    if (currentSettings.routing > 0.66f)
    {
        wetInL += loopL;
        wetInR += loopR;
    }
    else if (currentSettings.routing > 0.33f)
    {
        wetInL = loopL;
        wetInR = loopR;
    }

    float wetL = 0.0f;
    float wetR = 0.0f;

    switch (currentSettings.wetModeIndex)
    {
        case 0: renderWetReverb(wetInL, wetInR, wetL, wetR); break;
        case 1: renderWetDelay(wetInL, wetInR, wetL, wetR); break;
        case 2: renderWetSlip(wetInL, wetInR, wetL, wetR); break;
        default: break;
    }

    if (!currentSettings.freeze)
    {
        // Cross routing: wet channel feeds back into the always-listening loop history.
        writeHistory(loopL * 0.35f + wetL * 0.20f,
                     loopR * 0.35f + wetR * 0.20f);
    }

    const auto spread = juce::jmap(currentSettings.spread, 0.0f, 1.0f, 0.0f, 0.35f);
    const auto loopXL = loopL * (1.0f - spread) + loopR * spread;
    const auto loopXR = loopR * (1.0f - spread) + loopL * spread;

    const auto wetBlendL = wetL + loopXL;
    const auto wetBlendR = wetR + loopXR;

    const auto wetMix = currentSettings.mix;
    const auto dryMix = 1.0f - wetMix;

    const auto yL = dryMix * inL + wetMix * wetBlendL;
    const auto yR = dryMix * inR + wetMix * wetBlendR;

    outL = sanitizeAudioSample(yL);
    outR = sanitizeAudioSample(yR);
}
