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
