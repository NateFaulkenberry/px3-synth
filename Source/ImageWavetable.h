#pragma once

#include <JuceHeader.h>

#include <cstddef>
#include <vector>

struct ImageWavetable
{
    int frames { 128 };
    int samplesPerFrame { 2048 };
    int mipLevels { 6 };

    // Layout: [mip][frame][sample]
    std::vector<float> data;

    float getSample(int mip, int frame, int sample) const
    {
        if (frames <= 0 || samplesPerFrame <= 0 || mipLevels <= 0)
        {
            return 0.0f;
        }

        const auto m = juce::jlimit(0, mipLevels - 1, mip);
        const auto f = juce::jlimit(0, frames - 1, frame);

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
            return 0.0f;
        }

        return data[base];
    }
};
