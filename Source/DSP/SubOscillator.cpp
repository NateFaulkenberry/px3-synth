#include "SubOscillator.h"

#include <cmath>

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
    phaseRadians = std::fmod(newPhaseRadians, juce::MathConstants<float>::twoPi);
    if (phaseRadians < 0.0f)
    {
        phaseRadians += juce::MathConstants<float>::twoPi;
    }
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

    const auto output = waveformSampleAtPhase(phaseRadians, settings.waveformIndex) * settings.level;

    const auto phaseDelta = juce::MathConstants<float>::twoPi * static_cast<float>(subFrequencyHz)
                            / static_cast<float>(juce::jmax(1.0, sampleRateHz));
    phaseRadians += phaseDelta;
    if (phaseRadians >= juce::MathConstants<float>::twoPi)
    {
        phaseRadians -= juce::MathConstants<float>::twoPi;
    }

    return output;
}

float SubOscillator::waveformSampleAtPhase(float inPhaseRadians, int waveformIndex)
{
    auto wrapped = std::fmod(inPhaseRadians, juce::MathConstants<float>::twoPi);
    if (wrapped < 0.0f)
    {
        wrapped += juce::MathConstants<float>::twoPi;
    }

    const auto phaseNorm = wrapped / juce::MathConstants<float>::twoPi;

    switch (px3::clampSubOscWaveformIndex(waveformIndex))
    {
        case 0:
            return std::sin(wrapped);
        case 1:
            return phaseNorm < 0.5f ? 1.0f : -1.0f;
        default:
            break;
    }

    return std::sin(wrapped);
}
