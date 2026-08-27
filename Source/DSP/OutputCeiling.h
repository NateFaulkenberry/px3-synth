#pragma once

#include <JuceHeader.h>

#include <cmath>

// Fixed output ceiling for the whole instrument.
//
// Below the knee this is exactly identity, so ordinary material passes through
// unchanged. Above it the response is C1 at the knee and asymptotic to full
// scale, so the output cannot hard clip no matter how the faders are set. It is
// memoryless: no attack/release to pump, and no latency.
namespace px3
{
inline constexpr float kOutputCeilingKnee = 0.90f;

inline float applyOutputCeiling(float value) noexcept
{
    const auto magnitude = std::abs(value);
    if (magnitude <= kOutputCeilingKnee)
    {
        return value;
    }

    constexpr auto headroom = 1.0f - kOutputCeilingKnee;
    const auto shaped = kOutputCeilingKnee
                        + headroom * std::tanh((magnitude - kOutputCeilingKnee) / headroom);
    return value < 0.0f ? -shaped : shaped;
}
}
