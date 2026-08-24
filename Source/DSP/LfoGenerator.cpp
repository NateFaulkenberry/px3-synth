#include "LfoGenerator.h"

#include <cmath>

void LfoGenerator::prepare(double newSampleRateHz)
{
    sampleRateHz = juce::jmax(1.0, newSampleRateHz);
}

void LfoGenerator::setSettings(const LfoSettings& newSettings)
{
    settings.frequencyHz = juce::jlimit(0.01f, 20.0f, newSettings.frequencyHz);
    settings.waveformIndex = px3::clampLfoWaveformIndex(newSettings.waveformIndex);
}

void LfoGenerator::resetPhase(float newPhaseRadians)
{
    phaseRadians = std::fmod(newPhaseRadians, juce::MathConstants<float>::twoPi);
    if (phaseRadians < 0.0f)
    {
        phaseRadians += juce::MathConstants<float>::twoPi;
    }
}

float LfoGenerator::getNextSample()
{
    const auto output = waveformSampleAtPhase(phaseRadians, settings.waveformIndex);
    const auto phaseDelta = juce::MathConstants<float>::twoPi * settings.frequencyHz
                            / static_cast<float>(juce::jmax(1.0, sampleRateHz));
    phaseRadians += phaseDelta;
    if (phaseRadians >= juce::MathConstants<float>::twoPi)
    {
        phaseRadians -= juce::MathConstants<float>::twoPi;
    }

    return output;
}

float LfoGenerator::getMidpointSignalAndAdvance(int numSamples)
{
    const auto clampedSamples = juce::jmax(1, numSamples);
    const auto phaseDeltaPerSample = juce::MathConstants<float>::twoPi * settings.frequencyHz
                                     / static_cast<float>(juce::jmax(1.0, sampleRateHz));
    const auto midpointPhase = phaseRadians + phaseDeltaPerSample * static_cast<float>(clampedSamples) * 0.5f;
    const auto output = waveformSampleAtPhase(midpointPhase, settings.waveformIndex);

    phaseRadians += phaseDeltaPerSample * static_cast<float>(clampedSamples);
    while (phaseRadians >= juce::MathConstants<float>::twoPi)
    {
        phaseRadians -= juce::MathConstants<float>::twoPi;
    }

    return output;
}

float LfoGenerator::getPhaseRadians() const
{
    return phaseRadians;
}

float LfoGenerator::waveformSampleAtPhase(float inPhaseRadians, int waveformIndex)
{
    auto wrapped = std::fmod(inPhaseRadians, juce::MathConstants<float>::twoPi);
    if (wrapped < 0.0f)
    {
        wrapped += juce::MathConstants<float>::twoPi;
    }

    const auto phaseNorm = wrapped / juce::MathConstants<float>::twoPi;

    switch (px3::clampLfoWaveformIndex(waveformIndex))
    {
        case 0: // SINE
            return std::sin(wrapped);

        case 1: // TRIANGLE
            return 1.0f - 4.0f * std::abs(phaseNorm - 0.5f);

        case 2: // SAW
            return phaseNorm * 2.0f - 1.0f;

        case 3: // SQUARE
            return phaseNorm < 0.5f ? 1.0f : -1.0f;

        default:
            break;
    }

    return std::sin(wrapped);
}
