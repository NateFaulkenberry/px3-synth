#include "SubOscillator.h"

#include <cmath>

namespace
{
constexpr float kTwoPi = juce::MathConstants<float>::twoPi;
}

void SubOscillator::prepare(double newSampleRateHz)
{
    sampleRateHz = juce::jmax(1.0, newSampleRateHz);
}

void SubOscillator::setSettings(const SubOscSettings& newSettings)
{
    settings.enabled = newSettings.enabled;
    settings.level = juce::jlimit(0.0f, 1.0f, newSettings.level);
    settings.octaveIndex = px3::clampSubOscOctaveIndex(newSettings.octaveIndex);
    settings.waveformIndex = px3::clampSubOscWaveformIndex(newSettings.waveformIndex);
}

void SubOscillator::resetForNote(float newPhaseRadians)
{
    phaseNorm = wrapPhase01(newPhaseRadians / kTwoPi);
}

float SubOscillator::renderSample(double baseFrequencyHz)
{
    if (!settings.enabled || settings.level <= 0.0001f)
    {
        return 0.0f;
    }

    const auto semitones = static_cast<double>(px3::subOscSemitoneOffsetForOctaveIndex(settings.octaveIndex));
    const auto ratio = std::pow(2.0, semitones / 12.0);
    const auto subFrequencyHz = juce::jmax(1.0, baseFrequencyHz * ratio);
    const auto sampleRate = static_cast<float>(juce::jmax(1.0, sampleRateHz));
    const auto phaseDelta = juce::jlimit(0.0f,
                                         0.5f,
                                         static_cast<float>(subFrequencyHz) / sampleRate);

    const auto output = waveformSampleAtPhase(phaseNorm, phaseDelta, settings.waveformIndex)
                        * settings.level;

    phaseNorm = wrapPhase01(phaseNorm + phaseDelta);

    return output;
}

float SubOscillator::waveformSampleAtPhase(float inPhaseNorm, float phaseDelta, int waveformIndex)
{
    const auto phaseNorm = wrapPhase01(inPhaseNorm);

    switch (px3::clampSubOscWaveformIndex(waveformIndex))
    {
        case 0:
            return std::sin(phaseNorm * kTwoPi);
        case 1:
        {
            // Bandlimited 50% pulse (square): two transitions per cycle.
            auto square = phaseNorm < 0.5f ? 1.0f : -1.0f;
            square += polyBlep(phaseNorm, phaseDelta);
            square -= polyBlep(wrapPhase01(phaseNorm + 0.5f), phaseDelta);
            return juce::jlimit(-1.2f, 1.2f, square);
        }
        default:
            break;
    }

    return std::sin(phaseNorm * kTwoPi);
}

float SubOscillator::polyBlep(float t, float dt)
{
    if (dt <= 0.0f || dt >= 1.0f)
    {
        return 0.0f;
    }

    if (t < dt)
    {
        const auto x = t / dt;
        return x + x - x * x - 1.0f;
    }

    if (t > 1.0f - dt)
    {
        const auto x = (t - 1.0f) / dt;
        return x * x + x + x + 1.0f;
    }

    return 0.0f;
}

float SubOscillator::wrapPhase01(float inPhaseNorm)
{
    auto wrapped = std::fmod(inPhaseNorm, 1.0f);
    if (wrapped < 0.0f)
    {
        wrapped += 1.0f;
    }
    return wrapped;
}
