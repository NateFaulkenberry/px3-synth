#include "Wavetable.h"

#include <cmath>

namespace px3
{
namespace
{
int orderFor(int length)
{
    int order = 0;
    while ((1 << order) < length)
    {
        ++order;
    }
    return order;
}
} // namespace

FrameSpectrum analyseFrame(const float* samples, int length)
{
    FrameSpectrum spectrum;
    if (samples == nullptr || length < 4 || (length & (length - 1)) != 0)
    {
        return spectrum;
    }

    juce::dsp::FFT fft(orderFor(length));
    std::vector<float> scratch(static_cast<std::size_t>(length) * 2, 0.0f);
    std::copy(samples, samples + length, scratch.begin());
    fft.performRealOnlyForwardTransform(scratch.data());

    const auto count = length / 2;
    spectrum.amplitude.assign(static_cast<std::size_t>(count), 0.0f);
    spectrum.phase.assign(static_cast<std::size_t>(count), 0.0f);

    for (int h = 1; h < count; ++h)
    {
        const auto re = static_cast<double>(scratch[static_cast<std::size_t>(h) * 2]);
        const auto im = static_cast<double>(scratch[static_cast<std::size_t>(h) * 2 + 1]);

        // The exact inverse of the convention build() writes: a bin of
        // (L/2) * A * exp(i*(phase - pi/2)) came from A*sin(2*pi*h*i/L + phase).
        spectrum.amplitude[static_cast<std::size_t>(h)] =
            static_cast<float>(2.0 * std::sqrt(re * re + im * im) / length);
        spectrum.phase[static_cast<std::size_t>(h)] =
            static_cast<float>(std::atan2(im, re) + juce::MathConstants<double>::halfPi);
    }

    return spectrum;
}

std::shared_ptr<const Wavetable> Wavetable::build(juce::String name,
                                                  juce::String category,
                                                  const std::vector<FrameSpectrum>& frames)
{
    if (frames.empty() || static_cast<int>(frames.size()) > kMaxFrameCount)
    {
        return nullptr;
    }

    auto table = std::make_shared<Wavetable>();
    table->name = std::move(name);
    table->category = std::move(category);
    table->frameCount = static_cast<int>(frames.size());

    for (int level = 0; level < kMaxLevels; ++level)
    {
        // Harmonics first, then the length that gives them room - the reverse
        // of the obvious order, and the reason this reads cleanly at every
        // pitch. See kLevelHeadroom.
        const auto harmonics = kMaxHarmonics >> level;
        const auto length = juce::jmax(kMinLevelLength, 2 * kLevelHeadroom * harmonics);

        if (harmonics < 1)
        {
            break;
        }

        Level built;
        built.length = length;
        built.harmonics = harmonics;
        built.data.assign(static_cast<std::size_t>(table->frameCount)
                              * static_cast<std::size_t>(length),
                          0.0f);

        juce::dsp::FFT fft(orderFor(length));
        std::vector<float> scratch(static_cast<std::size_t>(length) * 2, 0.0f);

        for (int f = 0; f < table->frameCount; ++f)
        {
            const auto& spectrum = frames[static_cast<std::size_t>(f)];
            std::fill(scratch.begin(), scratch.end(), 0.0f);

            const auto available = juce::jmin(harmonics, spectrum.harmonicCount());
            for (int h = 1; h <= available; ++h)
            {
                const auto amplitude = spectrum.amplitude[static_cast<std::size_t>(h)];
                if (amplitude == 0.0f)
                {
                    continue;
                }

                const auto phase = h < static_cast<int>(spectrum.phase.size())
                                     ? spectrum.phase[static_cast<std::size_t>(h)]
                                     : 0.0f;

                // x[i] = A sin(2*pi*h*i/L + phase), which as a bin is
                // (L/2) * A * exp(i*(phase - pi/2)). Deriving the bin from the
                // level's OWN length is what makes every level agree: the same
                // harmonic comes out at the same amplitude whether it was
                // written into a 2048-point transform or a 256-point one.
                const auto angle = static_cast<double>(phase) - juce::MathConstants<double>::halfPi;
                const auto scale = 0.5 * length * static_cast<double>(amplitude);
                const auto re = static_cast<float>(scale * std::cos(angle));
                const auto im = static_cast<float>(scale * std::sin(angle));

                scratch[static_cast<std::size_t>(h) * 2] = re;
                scratch[static_cast<std::size_t>(h) * 2 + 1] = im;

                // The conjugate half, written explicitly rather than relying on
                // the transform to infer it.
                const auto mirror = static_cast<std::size_t>(length - h);
                scratch[mirror * 2] = re;
                scratch[mirror * 2 + 1] = -im;
            }

            fft.performRealOnlyInverseTransform(scratch.data());

            auto* destination = built.data.data()
                                + static_cast<std::size_t>(f) * static_cast<std::size_t>(length);
            std::copy(scratch.begin(), scratch.begin() + length, destination);
        }

        table->sizeInBytes += built.data.size() * sizeof(float);
        table->levels.push_back(std::move(built));
    }

    if (table->levels.empty())
    {
        return nullptr;
    }

    return table;
}

} // namespace px3
