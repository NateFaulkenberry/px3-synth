#include "LfoGenerator.h"

#include <algorithm>
#include <cmath>

namespace
{
// A finished ramp holds its end value, so counting past this gains nothing -
// and a double that is never reset would otherwise creep toward losing
// precision over a very long session.
constexpr double kRampElapsedCeilingSeconds = 1.0e6;
}

void LfoGenerator::prepare(double newSampleRateHz)
{
    sampleRateHz = juce::jmax(1.0, newSampleRateHz);
}

void LfoGenerator::setSettings(const LfoSettings& newSettings)
{
    settings.frequencyHz = juce::jlimit(0.01f, 20.0f, newSettings.frequencyHz);
    settings.waveformIndex = px3::clampLfoWaveformIndex(newSettings.waveformIndex);
    settings.rampSeconds = juce::jlimit(px3::lfoMinRampSeconds, px3::lfoMaxRampSeconds, newSettings.rampSeconds);
    settings.keySync = newSettings.keySync;
}

void LfoGenerator::retrigger()
{
    phaseRadians = 0.0f;
    rampElapsedSeconds = 0.0;
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
    const auto output = px3::isRampLfoWaveformIndex(settings.waveformIndex)
                            ? rampSampleAt(rampElapsedSeconds, settings.rampSeconds, settings.waveformIndex)
                            : waveformSampleAtPhase(phaseRadians, settings.waveformIndex);
    const auto phaseDelta = juce::MathConstants<float>::twoPi * settings.frequencyHz
                            / static_cast<float>(juce::jmax(1.0, sampleRateHz));
    phaseRadians += phaseDelta;
    if (phaseRadians >= juce::MathConstants<float>::twoPi)
    {
        phaseRadians -= juce::MathConstants<float>::twoPi;
    }

    rampElapsedSeconds = std::min(kRampElapsedCeilingSeconds,
                                  rampElapsedSeconds + 1.0 / juce::jmax(1.0, sampleRateHz));
    return output;
}

float LfoGenerator::getMidpointSignalAndAdvance(int numSamples)
{
    const auto clampedSamples = juce::jmax(1, numSamples);
    const auto phaseDeltaPerSample = juce::MathConstants<float>::twoPi * settings.frequencyHz
                                     / static_cast<float>(juce::jmax(1.0, sampleRateHz));
    const auto blockSeconds = static_cast<double>(clampedSamples) / juce::jmax(1.0, sampleRateHz);

    // The value at the MIDDLE of the block, for the cyclic shapes and the ramps
    // alike - a ramp read at the block's start would lag its own duration by
    // half a block at every block size.
    auto output = 0.0f;
    if (px3::isRampLfoWaveformIndex(settings.waveformIndex))
    {
        output = rampSampleAt(rampElapsedSeconds + 0.5 * blockSeconds, settings.rampSeconds, settings.waveformIndex);
    }
    else
    {
        const auto midpointPhase = phaseRadians + phaseDeltaPerSample * static_cast<float>(clampedSamples) * 0.5f;
        output = waveformSampleAtPhase(midpointPhase, settings.waveformIndex);
    }

    phaseRadians += phaseDeltaPerSample * static_cast<float>(clampedSamples);
    while (phaseRadians >= juce::MathConstants<float>::twoPi)
    {
        phaseRadians -= juce::MathConstants<float>::twoPi;
    }

    rampElapsedSeconds = std::min(kRampElapsedCeilingSeconds, rampElapsedSeconds + blockSeconds);
    return output;
}

float LfoGenerator::getPhaseRadians() const
{
    return phaseRadians;
}

double LfoGenerator::getRampElapsedSeconds() const
{
    return rampElapsedSeconds;
}

float LfoGenerator::rampSampleAt(double elapsedSeconds, float rampSeconds, int waveformIndex)
{
    // Bipolar, -1 to +1, like every other shape. A ramp therefore reaches its
    // destination under exactly the same modulation rule, and switching shape
    // does not also change how far the amount knob goes.
    const auto duration = juce::jmax(1.0e-4, static_cast<double>(rampSeconds));
    const auto progress = static_cast<float>(juce::jlimit(0.0, 1.0, elapsedSeconds / duration));
    const auto rising = -1.0f + 2.0f * progress;
    return waveformIndex == px3::lfoWaveformToIndex(px3::LfoWaveform::rampDown) ? -rising : rising;
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
