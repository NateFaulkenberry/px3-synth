#pragma once

#include <JuceHeader.h>

#include <cstddef>
#include <vector>

/**
 * CPU-friendly image-derived wavetable storage.
 *
 * The image engine preprocesses visual content into a bank of waveform frames,
 * then adds mip levels so high pitches can read smoother, band-limited data.
 *
 * Data is immutable after construction and shared across voices.
 */
struct ImageWavetable
{
    // Number of horizontal frames available for position/morph traversal.
    int frames { 128 };

    // Number of samples per single-cycle frame.
    int samplesPerFrame { 2048 };

    // Number of mip levels. Higher mip index == lower effective bandwidth.
    int mipLevels { 6 };

    // Flattened layout: [mip][frame][sample]
    std::vector<float> data;

    float getSample(int mip, int frame, int sample) const
    {
        // Guard invalid tables defensively; callers can continue rendering.
        if (frames <= 0 || samplesPerFrame <= 0 || mipLevels <= 0)
        {
            return 0.0f;
        }

        const auto m = juce::jlimit(0, mipLevels - 1, mip);
        const auto f = juce::jlimit(0, frames - 1, frame);

        // Wrap phase/sample index manually so negative lookups (interpolation
        // around boundaries) remain valid.
        auto s = sample;
        while (s < 0)
        {
            s += samplesPerFrame;
        }
        while (s >= samplesPerFrame)
        {
            s -= samplesPerFrame;
        }

        const auto base = static_cast<std::size_t>(((m * frames) + f) * samplesPerFrame + s);
        if (base >= data.size())
        {
            // Out-of-range reads should fail silent to avoid destabilizing audio.
            return 0.0f;
        }

        return data[base];
    }
};
