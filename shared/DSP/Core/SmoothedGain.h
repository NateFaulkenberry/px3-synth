#pragma once

#include <JuceHeader.h>

#include <cmath>

// Per-sample one-pole gain smoother.
//
// Parameter values are read once per block, so applying them straight to the
// audio makes every fader move a staircase at the block rate (~94 Hz at 512
// samples / 48 kHz), which zippers. A linear ramp restarted each block would fix
// the staircase but leave a corner at every block boundary; a one-pole
// approaches asymptotically and never corners.
struct SmoothedGain
{
    void prepare(double sampleRate, double smoothingSeconds)
    {
        const auto safeRate = juce::jmax(1.0, sampleRate);
        const auto safeSeconds = juce::jmax(1.0e-6, smoothingSeconds);
        coefficient = 1.0f - std::exp(-1.0f / static_cast<float>(safeSeconds * safeRate));
    }

    // Jump straight to a value, for initialisation and note starts where a ramp
    // from the previous value would be wrong.
    void setCurrent(float value) noexcept { current = value; }

    float getCurrent() const noexcept { return current; }

    float next(float target) noexcept
    {
        current += (target - current) * coefficient;
        return current;
    }

private:
    float current { 0.0f };
    float coefficient { 1.0f };
};

// Mute/solo gate.
//
// A gate has to reach exactly 0 and exactly 1 - a mute must be silent, not
// asymptotically quiet - so it advances a linear phase and shapes it with
// smoothstep. That lands at both ends with zero slope, unlike a linear ramp,
// which stops dead on arrival and leaves a corner in the gain that ticks on
// material with no harmonics of its own to mask it.
struct SmoothedGate
{
    void prepare(double sampleRate, double fadeSeconds)
    {
        const auto samples = juce::jmax(1.0, fadeSeconds * juce::jmax(1.0, sampleRate));
        phaseStep = 1.0f / static_cast<float>(samples);
    }

    void setCurrent(bool open) noexcept
    {
        targetOpen = open;
        phase = open ? 1.0f : 0.0f;
    }

    void setTarget(bool open) noexcept { targetOpen = open; }

    float next() noexcept
    {
        phase = juce::jlimit(0.0f, 1.0f, phase + (targetOpen ? phaseStep : -phaseStep));
        return phase * phase * (3.0f - 2.0f * phase);
    }

private:
    float phase { 1.0f };
    float phaseStep { 1.0f };
    bool targetOpen { true };
};
