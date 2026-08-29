#include "StftEngine.h"

namespace px3
{

void StftEngine::prepare(int fftOrder, int channels, int hopDivisor)
{
    fft = std::make_unique<juce::dsp::FFT>(fftOrder);
    fftSize = 1 << fftOrder;
    hop = juce::jmax(1, fftSize / juce::jmax(1, hopDivisor));
    channelCount = juce::jmax(1, channels);

    window.assign(static_cast<std::size_t>(fftSize), 0.0f);
    for (int i = 0; i < fftSize; ++i)
    {
        // Periodic Hann: the periodic form is the one that satisfies overlap-add
        // exactly. The symmetric form leaves a ripple at the frame rate.
        window[static_cast<std::size_t>(i)] =
            0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi
                                   * static_cast<float>(i) / static_cast<float>(fftSize));
    }

    inputRing.assign(static_cast<std::size_t>(channelCount),
                     std::vector<float>(static_cast<std::size_t>(fftSize), 0.0f));
    outputRing.assign(static_cast<std::size_t>(channelCount),
                      std::vector<float>(static_cast<std::size_t>(fftSize), 0.0f));
    ringPos.assign(static_cast<std::size_t>(channelCount), 0);
    hopCounter.assign(static_cast<std::size_t>(channelCount), 0);

    scratch.assign(static_cast<std::size_t>(fftSize) * 2u, 0.0f);
    real.assign(static_cast<std::size_t>(fftSize / 2 + 1), 0.0f);
    imag.assign(static_cast<std::size_t>(fftSize / 2 + 1), 0.0f);
}

void StftEngine::reset()
{
    for (auto& ring : inputRing)
    {
        std::fill(ring.begin(), ring.end(), 0.0f);
    }
    for (auto& ring : outputRing)
    {
        std::fill(ring.begin(), ring.end(), 0.0f);
    }
    std::fill(ringPos.begin(), ringPos.end(), 0);
    std::fill(hopCounter.begin(), hopCounter.end(), 0);
}

void StftEngine::transformFrame(int channel, const FrameFn& onFrame)
{
    const auto ch = static_cast<std::size_t>(channel);
    const auto& in = inputRing[ch];
    const auto pos = ringPos[ch];

    // Unwrap the ring into the scratch buffer, oldest sample first, windowing on
    // the way through.
    for (int i = 0; i < fftSize; ++i)
    {
        const auto index = (pos + i) % fftSize;
        scratch[static_cast<std::size_t>(i) * 2u] =
            in[static_cast<std::size_t>(index)] * window[static_cast<std::size_t>(i)];
        scratch[static_cast<std::size_t>(i) * 2u + 1u] = 0.0f;
    }

    fft->perform(reinterpret_cast<juce::dsp::Complex<float>*>(scratch.data()),
                 reinterpret_cast<juce::dsp::Complex<float>*>(scratch.data()),
                 false);

    const auto bins = fftSize / 2 + 1;
    for (int k = 0; k < bins; ++k)
    {
        real[static_cast<std::size_t>(k)] = scratch[static_cast<std::size_t>(k) * 2u];
        imag[static_cast<std::size_t>(k)] = scratch[static_cast<std::size_t>(k) * 2u + 1u];
    }

    if (onFrame)
    {
        onFrame(channel, real.data(), imag.data(), bins);
    }

    // Rebuild the full spectrum from the half the callback edited. A real signal
    // needs the conjugate mirror; without it the inverse transform returns a
    // complex result and the imaginary half is silently discarded, which halves
    // the output and smears the phase.
    for (int k = 0; k < bins; ++k)
    {
        scratch[static_cast<std::size_t>(k) * 2u] = real[static_cast<std::size_t>(k)];
        scratch[static_cast<std::size_t>(k) * 2u + 1u] = imag[static_cast<std::size_t>(k)];
    }
    for (int k = bins; k < fftSize; ++k)
    {
        const auto mirror = static_cast<std::size_t>(fftSize - k);
        scratch[static_cast<std::size_t>(k) * 2u] = real[mirror];
        scratch[static_cast<std::size_t>(k) * 2u + 1u] = -imag[mirror];
    }

    fft->perform(reinterpret_cast<juce::dsp::Complex<float>*>(scratch.data()),
                 reinterpret_cast<juce::dsp::Complex<float>*>(scratch.data()),
                 true);

    // Overlap-add, windowing again on synthesis.
    auto& out = outputRing[ch];
    for (int i = 0; i < fftSize; ++i)
    {
        const auto index = (pos + i) % fftSize;
        out[static_cast<std::size_t>(index)] +=
            scratch[static_cast<std::size_t>(i) * 2u] * window[static_cast<std::size_t>(i)];
    }
}

float StftEngine::processSample(int channel, float input, const FrameFn& onFrame)
{
    if (fft == nullptr || channel < 0 || channel >= channelCount)
    {
        return input;
    }

    const auto ch = static_cast<std::size_t>(channel);
    const auto pos = ringPos[ch];

    // Read before write: this slot holds the oldest completed output, and is
    // about to be reused for the newest input.
    const auto output = outputRing[ch][static_cast<std::size_t>(pos)];
    outputRing[ch][static_cast<std::size_t>(pos)] = 0.0f;
    inputRing[ch][static_cast<std::size_t>(pos)] = input;

    ringPos[ch] = (pos + 1) % fftSize;

    if (++hopCounter[ch] >= hop)
    {
        hopCounter[ch] = 0;
        transformFrame(channel, onFrame);
    }

    return output;
}

} // namespace px3
