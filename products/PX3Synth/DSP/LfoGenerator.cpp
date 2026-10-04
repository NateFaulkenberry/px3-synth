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
    settings.clockMode = newSettings.clockMode;
    settings.clockDivision = juce::jlimit(0, static_cast<int>(lfoClockBeats.size()) - 1, newSettings.clockDivision);
    settings.tempoBpm = std::isfinite(newSettings.tempoBpm) ? juce::jlimit(10.0, 999.0, newSettings.tempoBpm) : 120.0;
    settings.transportPpq = std::isfinite(newSettings.transportPpq) ? newSettings.transportPpq : 0.0;
    settings.beatsPerBar = std::isfinite(newSettings.beatsPerBar) ? juce::jlimit(0.25, 32.0, newSettings.beatsPerBar) : 4.0;
    settings.clockAvailable = newSettings.clockAvailable;
    settings.transportPlaying = newSettings.transportPlaying;
    settings.clockRateScale = std::isfinite(newSettings.clockRateScale) ? juce::jlimit(0.001f, 100.0f, newSettings.clockRateScale) : 1.0f;
    settings.clockRampScale = std::isfinite(newSettings.clockRampScale) ? juce::jlimit(0.001f, 100.0f, newSettings.clockRampScale) : 1.0f;
    if (settings.clockMode != LfoClockMode::free && settings.clockAvailable)
    {
        auto beats = lfoClockBeats[static_cast<std::size_t>(settings.clockDivision)];
        if (settings.clockDivision <= 2) { beats *= settings.beatsPerBar / 4.0; }
        beats /= settings.clockRateScale;
        settings.frequencyHz = static_cast<float>(settings.tempoBpm / (60.0 * beats));
        settings.rampSeconds = static_cast<float>(60.0 * beats / settings.tempoBpm) * settings.clockRampScale;
    }
}

void LfoGenerator::retrigger()
{
    phaseRadians = 0.0f;
    cycleIndex = 0;
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
    if (settings.clockMode != LfoClockMode::free) { return getMidpointSignalAndAdvance(1); }
    const auto output = px3::isRampLfoWaveformIndex(settings.waveformIndex)
                            ? rampSampleAt(rampElapsedSeconds, settings.rampSeconds, settings.waveformIndex)
                            : sampleAtPhase(phaseRadians, settings.waveformIndex);
    const auto phaseDelta = juce::MathConstants<float>::twoPi * settings.frequencyHz
                            / static_cast<float>(juce::jmax(1.0, sampleRateHz));
    phaseRadians += phaseDelta;
    if (phaseRadians >= juce::MathConstants<float>::twoPi)
    {
        phaseRadians -= juce::MathConstants<float>::twoPi;
        ++cycleIndex;
    }

    rampElapsedSeconds = std::min(kRampElapsedCeilingSeconds,
                                  rampElapsedSeconds + 1.0 / juce::jmax(1.0, sampleRateHz));
    return output;
}

float LfoGenerator::getMidpointSignalAndAdvance(int numSamples)
{
    if (settings.clockMode != LfoClockMode::free && ! settings.clockAvailable) { return 0.0f; }
    if (settings.clockMode == LfoClockMode::transport)
    {
        auto beats = lfoClockBeats[static_cast<std::size_t>(settings.clockDivision)];
        if (settings.clockDivision <= 2) { beats *= settings.beatsPerBar / 4.0; }
        beats /= settings.clockRateScale;
        const auto cycles = settings.transportPpq / beats;
        const auto phase = cycles - std::floor(cycles);
        // The transport owns the cycle too, so a random shape repeats with the song.
        cycleIndex = static_cast<std::int64_t>(std::floor(cycles));
        resetPhase(static_cast<float>(phase * juce::MathConstants<double>::twoPi));
        rampElapsedSeconds = phase / settings.frequencyHz;
        if (! settings.transportPlaying)
        {
            return px3::isRampLfoWaveformIndex(settings.waveformIndex)
                       ? rampSampleAt(rampElapsedSeconds, settings.rampSeconds, settings.waveformIndex)
                       : sampleAtPhase(phaseRadians, settings.waveformIndex);
        }
    }
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
        auto elapsed = rampElapsedSeconds + 0.5 * blockSeconds;
        if (settings.clockMode != LfoClockMode::free) { elapsed = std::fmod(elapsed, 1.0 / settings.frequencyHz); }
        output = rampSampleAt(elapsed, settings.rampSeconds, settings.waveformIndex);
    }
    else
    {
        const auto midpointPhase = phaseRadians + phaseDeltaPerSample * static_cast<float>(clampedSamples) * 0.5f;
        output = sampleAtPhase(midpointPhase, settings.waveformIndex);
    }

    phaseRadians += phaseDeltaPerSample * static_cast<float>(clampedSamples);
    while (phaseRadians >= juce::MathConstants<float>::twoPi)
    {
        phaseRadians -= juce::MathConstants<float>::twoPi;
        ++cycleIndex;
    }

    rampElapsedSeconds = std::min(kRampElapsedCeilingSeconds, rampElapsedSeconds + blockSeconds);
    if (settings.clockMode == LfoClockMode::transport)
    {
        settings.transportPpq += blockSeconds * settings.tempoBpm / 60.0;
    }
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

float LfoGenerator::randomForCycle(std::int64_t cycle, std::uint64_t seed) noexcept
{
    // A hash, not a generator: the value for a cycle is the same however the
    // LFO got there, so transport sync and key sync stay deterministic.
    auto x = static_cast<std::uint64_t>(cycle) * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull
             + seed * 0xD1B54A32D192ED03ull;
    x ^= x >> 31; x *= 0xBF58476D1CE4E5B9ull; x ^= x >> 27; x *= 0x94D049BB133111EBull; x ^= x >> 33;
    return static_cast<float>(static_cast<double>(x >> 11) / static_cast<double>(1ull << 53) * 2.0 - 1.0);
}

float LfoGenerator::sampleAtPhase(float inPhaseRadians, int waveformIndex) const
{
    const auto index = px3::clampLfoWaveformIndex(waveformIndex);
    // The cycle the phase is actually in. The block path reads at the block's
    // midpoint, which can lie past the end of the current cycle before the
    // counter has moved; easing from the old cycle's value at t ~ 0 there was a
    // full-scale jump back for one block.
    const auto cycle = cycleIndex + static_cast<std::int64_t>(std::floor(inPhaseRadians / juce::MathConstants<float>::twoPi));
    if (index == px3::lfoWaveformToIndex(px3::LfoWaveform::sampleHold))
    {
        return randomForCycle(cycle, randomSeed);
    }
    if (index == px3::lfoWaveformToIndex(px3::LfoWaveform::smoothRandom))
    {
        // Cosine-eased from this cycle's value to the next: continuous, with
        // zero slope at every cycle boundary.
        auto t = std::fmod(inPhaseRadians, juce::MathConstants<float>::twoPi) / juce::MathConstants<float>::twoPi;
        if (t < 0.0f) t += 1.0f;
        const auto ease = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * t);
        const auto a = randomForCycle(cycle, randomSeed);
        const auto b = randomForCycle(cycle + 1, randomSeed);
        return a + (b - a) * ease;
    }
    return waveformSampleAtPhase(inPhaseRadians, index);
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
