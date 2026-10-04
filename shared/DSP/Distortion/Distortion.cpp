#include "Distortion.h"

#include <cmath>

namespace px3
{
namespace
{
constexpr float kAsymBias = 0.35f;

float logCosh(float x) noexcept
{
    const auto a = std::abs(x);
    return a + std::log1p(std::exp(-2.0f * a)) - 0.69314718f;
}

float onePole(float hz, double sampleRate) noexcept
{
    return static_cast<float>(1.0 - std::exp(-juce::MathConstants<double>::twoPi * static_cast<double>(hz) / sampleRate));
}
} // namespace

float Distortion::shape(int type, float x) noexcept
{
    switch (type)
    {
        case 1: return juce::jlimit(-1.0f, 1.0f, x);
        case 2: return std::tanh(x + kAsymBias) - std::tanh(kAsymBias);
        default: return std::tanh(x);
    }
}

float Distortion::antiderivative(int type, float x) noexcept
{
    switch (type)
    {
        case 1: return std::abs(x) <= 1.0f ? 0.5f * x * x : std::abs(x) - 0.5f;
        case 2: return logCosh(x + kAsymBias) - x * std::tanh(kAsymBias);
        default: return logCosh(x);
    }
}

void Distortion::prepare(double sampleRate)
{
    sampleRateHz = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (auto* s : { &mixSmoothed, &driveSmoothed, &levelSmoothed }) { s->reset(sampleRateHz, 0.02); }
    followCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (0.15 * sampleRateHz)));
    dcPole = static_cast<float>(std::pow(0.9995, 48000.0 / sampleRateHz));
    reset();
}

void Distortion::reset()
{
    tightState = toneState = previousInput = dcX1 = dcY1 = { { 0.0f, 0.0f } };
    inPower = outPower = 0.0f;
}

void Distortion::updateForBlock(const DistortionSettings& settings)
{
    current = settings;
    current.type = juce::jlimit(0, 2, settings.type);
    mixSmoothed.setTargetValue(current.enabled ? juce::jlimit(0.0f, 1.0f, current.mix) : 0.0f);
    driveSmoothed.setTargetValue(juce::Decibels::decibelsToGain(40.0f * juce::jlimit(0.0f, 1.0f, current.drive)));
    levelSmoothed.setTargetValue(juce::Decibels::decibelsToGain(juce::jmap(juce::jlimit(0.0f, 1.0f, current.level), -12.0f, 12.0f)));
    // TIGHT: the low cut ahead of the clipper, 60 Hz loose to 900 Hz tight.
    tightCoeff = onePole(60.0f * std::pow(15.0f, juce::jlimit(0.0f, 1.0f, current.tight)), sampleRateHz);
    // TONE: post low-pass, 1.2 kHz dark to 14 kHz open.
    toneCoeff = onePole(1200.0f * std::pow(11.7f, juce::jlimit(0.0f, 1.0f, current.tone)), sampleRateHz);
}

float Distortion::clip(int channel, float x) noexcept
{
    // First-order ADAA: the average of the curve between this sample and the
    // last, from its antiderivative; the plain curve where they nearly agree.
    const auto c = static_cast<std::size_t>(channel);
    const auto previous = previousInput[c];
    previousInput[c] = x;
    const auto difference = x - previous;
    if (std::abs(difference) < 1.0e-4f) { return shape(current.type, 0.5f * (x + previous)); }
    return (antiderivative(current.type, x) - antiderivative(current.type, previous)) / difference;
}

void Distortion::processSampleFrame(float inL, float inR, float& outL, float& outR)
{
    const auto mix = mixSmoothed.getNextValue();
    const auto drive = driveSmoothed.getNextValue();
    const auto level = levelSmoothed.getNextValue();

    // Inaudible and settled: hand the input back and clear state once, so it
    // costs nothing and re-engaging does not replay stale filter memory.
    if (mix <= 1.0e-5f && ! mixSmoothed.isSmoothing())
    {
        if (! idle) { reset(); idle = true; }
        outL = inL;
        outR = inR;
        return;
    }
    idle = false;

    std::array<float, 2> in { { inL, inR } };
    std::array<float, 2> shaped {};
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto c = static_cast<std::size_t>(ch);
        // Mid emphasis: the lows are taken down before the clipper so the low
        // end stays tight and the grit sits in the mids.
        tightState[c] += (in[c] - tightState[c]) * tightCoeff;
        const auto emphasised = in[c] - 0.8f * tightState[c];
        auto y = clip(ch, emphasised * drive);
        // Unmatched diodes leave DC; it never reaches the output.
        const auto dc = y - dcX1[c] + dcPole * dcY1[c];
        dcX1[c] = y;
        dcY1[c] = dc;
        toneState[c] += (dc - toneState[c]) * toneCoeff;
        shaped[c] = toneState[c];
    }

    // Level match on loudness, linked across channels, over about 150 ms.
    inPower += (0.5f * (inL * inL + inR * inR) - inPower) * followCoeff;
    outPower += (0.5f * (shaped[0] * shaped[0] + shaped[1] * shaped[1]) - outPower) * followCoeff;
    const auto match = juce::jlimit(0.05f, 4.0f, std::sqrt((inPower + 1.0e-9f) / (outPower + 1.0e-9f)));

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto c = static_cast<std::size_t>(ch);
        auto wet = shaped[c] * match * level;
        if (! std::isfinite(wet)) { wet = 0.0f; }
        const auto out = in[c] + (wet - in[c]) * mix;
        (ch == 0 ? outL : outR) = out;
    }
}
} // namespace px3
