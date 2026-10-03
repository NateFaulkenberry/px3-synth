#include "UniVibe.h"

#include <cmath>

namespace px3
{
namespace
{
// Per-stage sweep ranges (Hz). Staggered like the pedal's unequal capacitors,
// so the four notches move together but sit far apart.
constexpr std::array<float, 4> kMinHz { { 70.0f, 190.0f, 520.0f, 1100.0f } };
constexpr std::array<float, 4> kMaxHz { { 1000.0f, 2300.0f, 5200.0f, 9500.0f } };
} // namespace

void UniVibe::prepare(double sampleRate)
{
    sampleRateHz = sampleRate > 0.0 ? sampleRate : 48000.0;
    intensitySmoothed.reset(sampleRateHz, 0.03);
    rateSmoothed.reset(sampleRateHz, 0.05);
    wetMixSmoothed.reset(sampleRateHz, 0.03);
    // The lamp heats in a few milliseconds and cools over tens of them.
    lampAttack = static_cast<float>(1.0 - std::exp(-1.0 / (0.004 * sampleRateHz)));
    lampRelease = static_cast<float>(1.0 - std::exp(-1.0 / (0.045 * sampleRateHz)));
    reset();
}

void UniVibe::reset()
{
    for (auto& c : channels) { c = Channel {}; }
    channels[1].lfoPhase = 0.25f;
}

void UniVibe::updateForBlock(const UniVibeSettings& settings)
{
    current = settings;
    intensitySmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.intensity));
    rateSmoothed.setTargetValue(0.5f * std::pow(20.0f, juce::jlimit(0.0f, 1.0f, settings.speed)));
    // CHORUS: equal parts dry and phased. VIBRATO: phased only.
    wetMixSmoothed.setTargetValue(settings.mode == 1 ? 1.0f : 0.5f);
}

float UniVibe::processChannel(Channel& c, float input, float rateHz, float intensity) noexcept
{
    c.lfoPhase += rateHz / static_cast<float>(sampleRateHz);
    c.lfoPhase -= std::floor(c.lfoPhase);
    const auto drive = 0.5f + 0.5f * std::sin(juce::MathConstants<float>::twoPi * c.lfoPhase);
    c.lamp += (drive - c.lamp) * (drive > c.lamp ? lampAttack : lampRelease);
    // LDR: resistance falls steeply with light, so the sweep spends most of the
    // cycle near one end and snaps through the other.
    const auto light = std::pow(juce::jlimit(0.0f, 1.0f, c.lamp), 1.8f);
    const auto position = 0.5f + (light - 0.5f) * intensity * 2.0f;

    auto x = input;
    for (std::size_t s = 0; s < 4; ++s)
    {
        const auto hz = kMinHz[s] * std::pow(kMaxHz[s] / kMinHz[s], juce::jlimit(0.0f, 1.0f, position));
        const auto t = std::tan(juce::MathConstants<float>::pi
                                * std::min(hz, static_cast<float>(sampleRateHz) * 0.45f) / static_cast<float>(sampleRateHz));
        const auto a = (t - 1.0f) / (t + 1.0f);
        const auto y = a * x + c.x1[s] - a * c.y1[s];
        c.x1[s] = x;
        c.y1[s] = y;
        x = y;
    }
    // Lamp bleed: the pedal's slight amplitude throb in step with the sweep.
    return x * (1.0f - 0.12f * intensity * light);
}

void UniVibe::processSampleFrame(float inL, float inR, float& outL, float& outR)
{
    const auto intensity = intensitySmoothed.getNextValue();
    const auto rate = rateSmoothed.getNextValue();
    const auto wetMix = wetMixSmoothed.getNextValue();
    if (intensity <= 1.0e-5f && ! intensitySmoothed.isSmoothing())
    {
        if (! idle) { reset(); idle = true; }
        outL = inL;
        outR = inR;
        return;
    }
    idle = false;
    // Fades in with INTENSITY so engaging it from zero does not click.
    const auto engage = juce::jmin(1.0f, intensity * 8.0f);
    const auto mix = wetMix * engage;
    const auto wetL = processChannel(channels[0], inL, rate, intensity);
    const auto wetR = processChannel(channels[1], inR, rate, intensity);
    outL = inL + (wetL - inL) * mix;
    outR = inR + (wetR - inR) * mix;
    if (! std::isfinite(outL) || ! std::isfinite(outR)) { reset(); outL = inL; outR = inR; }
}
} // namespace px3
