#include "Distortion.h"

#include <cmath>

namespace px3
{
namespace
{
constexpr float kAsymBias = 0.35f;

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
    return static_cast<float>(antiderivativeDouble(type, static_cast<double>(x)));
}

// In double: the ADAA difference quotient subtracts two values near |x| (up to
// ~100 at full drive) and divides by a small step, which float cannot carry.
double Distortion::antiderivativeDouble(int type, double x) noexcept
{
    const auto logCoshD = [](double v)
    {
        const auto a = std::abs(v);
        return a + std::log1p(std::exp(-2.0 * a)) - 0.6931471805599453;
    };
    constexpr double bias = static_cast<double>(kAsymBias);
    switch (type)
    {
        case 1: return std::abs(x) <= 1.0 ? 0.5 * x * x : std::abs(x) - 0.5;
        case 2: return logCoshD(x + bias) - x * std::tanh(bias);
        default: return logCoshD(x);
    }
}

void Distortion::prepare(double sampleRate)
{
    sampleRateHz = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (auto* s : { &mixSmoothed, &driveSmoothed, &levelSmoothed, &engageSmoothed }) { s->reset(sampleRateHz, 0.02); }
    const auto stages = px3::dsp::PolyphaseOversampler::stagesForSampleRate(sampleRateHz);
    wetOversampler.prepare(stages, 2);
    dryOversampler.prepare(stages, 2);
    followCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (0.15 * sampleRateHz)));
    dcPole = static_cast<float>(std::pow(0.9995, 48000.0 / sampleRateHz));
    reset();
}

void Distortion::reset()
{
    tightState = toneState = dcX1 = dcY1 = { { 0.0f, 0.0f } };
    previousInput = { { 0.0, 0.0 } };
    cachedType = -1;
    wetOversampler.reset();
    dryOversampler.reset();
    inPower = outPower = 0.0f;
}

void Distortion::updateForBlock(const DistortionSettings& settings)
{
    current = settings;
    current.type = juce::jlimit(0, 2, settings.type);
    mixSmoothed.setTargetValue(current.enabled ? juce::jlimit(0.0f, 1.0f, current.mix) : 0.0f);
    engageSmoothed.setTargetValue(current.enabled && current.mix > 0.0f ? 1.0f : 0.0f);
    driveSmoothed.setTargetValue(juce::Decibels::decibelsToGain(40.0f * juce::jlimit(0.0f, 1.0f, current.drive)));
    levelSmoothed.setTargetValue(juce::Decibels::decibelsToGain(juce::jmap(juce::jlimit(0.0f, 1.0f, current.level), -12.0f, 12.0f)));
    // TIGHT: the low cut ahead of the clipper, 60 Hz loose to 900 Hz tight.
    tightCoeff = onePole(60.0f * std::pow(15.0f, juce::jlimit(0.0f, 1.0f, current.tight)), sampleRateHz);
    // TONE: post low-pass, 1.2 kHz dark to 14 kHz open.
    toneCoeff = onePole(1200.0f * std::pow(11.7f, juce::jlimit(0.0f, 1.0f, current.tone)), sampleRateHz);
}

double Distortion::clip(int channel, double x) noexcept
{
    // First-order ADAA: the average of the curve between this sample and the
    // last, from its antiderivative; the plain curve where they nearly agree.
    //
    // F(previous) is carried over from the last step rather than evaluated
    // again - the same value, half the transcendentals. A TYPE change
    // invalidates it, so it is rebuilt from the stored input.
    if (cachedType != current.type)
    {
        for (std::size_t ch = 0; ch < previousInput.size(); ++ch)
        {
            previousAntiderivative[ch] = antiderivativeDouble(current.type, previousInput[ch]);
        }
        cachedType = current.type;
    }
    const auto c = static_cast<std::size_t>(channel);
    const auto previous = previousInput[c];
    const auto previousF = previousAntiderivative[c];
    const auto f = antiderivativeDouble(current.type, x);
    previousInput[c] = x;
    previousAntiderivative[c] = f;
    const auto difference = x - previous;
    if (std::abs(difference) < 1.0e-6) { return static_cast<double>(shape(current.type, static_cast<float>(0.5 * (x + previous)))); }
    return (f - previousF) / difference;
}

void Distortion::processSampleFrame(float inL, float inR, float& outL, float& outR)
{
    const auto mix = mixSmoothed.getNextValue();
    const auto drive = driveSmoothed.getNextValue();
    const auto level = levelSmoothed.getNextValue();
    const auto engage = engageSmoothed.getNextValue();

    // Inaudible and settled: hand the input back and clear state once, so it
    // costs nothing and re-engaging does not replay stale filter memory.
    if (engage <= 1.0e-5f && ! engageSmoothed.isSmoothing())
    {
        if (! idle) { reset(); idle = true; }
        outL = inL;
        outR = inR;
        return;
    }
    idle = false;

    std::array<float, 2> in { { inL, inR } };
    std::array<float, 2> shaped {};
    std::array<float, 2> dryAligned {};
    std::array<float, 1 << px3::dsp::PolyphaseOversampler::kMaxStages> frame {};
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto c = static_cast<std::size_t>(ch);
        // Mid emphasis: the lows are taken down before the clipper so the low
        // end stays tight and the grit sits in the mids.
        tightState[c] += (in[c] - tightState[c]) * tightCoeff;
        const auto emphasised = in[c] - 0.8f * tightState[c];

        wetOversampler.upsample(ch, emphasised * drive, frame.data());
        for (int k = 0; k < wetOversampler.factor(); ++k)
        {
            frame[static_cast<std::size_t>(k)] = static_cast<float>(clip(ch, static_cast<double>(frame[static_cast<std::size_t>(k)])));
        }
        auto y = wetOversampler.downsample(ch, frame.data());

        dryOversampler.upsample(ch, in[c], frame.data());
        dryAligned[c] = dryOversampler.downsample(ch, frame.data());

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
        const auto stage = dryAligned[c] + (wet - dryAligned[c]) * mix;
        (ch == 0 ? outL : outR) = in[c] + (stage - in[c]) * engage;
    }
}
} // namespace px3
