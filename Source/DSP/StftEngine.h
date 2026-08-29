#pragma once

#include <JuceHeader.h>

#include <functional>
#include <vector>

namespace px3
{

// Short-time Fourier analysis/synthesis with overlap-add, for effects that
// genuinely work on a spectrum rather than on a delay line.
//
// The caller supplies a callback that rewrites one frame's bins in place. The
// engine owns the windowing, the hop bookkeeping and the reconstruction; it
// never allocates once prepared.
//
// Windowing: Hann is applied on analysis AND on synthesis. The product of the
// two is a Hann-squared window, which sums to a CONSTANT at 75% overlap
// (hop = size/4) - but that constant is 3/2, not 1. The overlap-add is scaled
// by its reciprocal, so a frame the callback leaves untouched comes back out
// unchanged rather than half again as loud.
class StftEngine
{
public:
    // (channel, real[], imag[], numBins) - bins are 0..size/2 inclusive.
    using FrameFn = std::function<void(int channel, float* real, float* imag, int numBins)>;

    // fftOrder is the power of two: 9 -> 512-point. hopDivisor is size/hop and
    // must be 4 for the Hann-squared COLA above to hold.
    void prepare(int fftOrder, int channels, int hopDivisor = 4);
    void reset();

    // One sample in, one sample out. Output lags the input by one frame.
    float processSample(int channel, float input, const FrameFn& onFrame);

    int latencySamples() const noexcept { return fftSize; }
    int hopSize() const noexcept { return hop; }
    int numBins() const noexcept { return fftSize / 2 + 1; }
    double binWidthHz(double sampleRate) const noexcept
    {
        return fftSize > 0 ? sampleRate / static_cast<double>(fftSize) : 0.0;
    }

private:
    void transformFrame(int channel, const FrameFn& onFrame);

    std::unique_ptr<juce::dsp::FFT> fft;
    int fftSize { 0 };
    int hop { 0 };
    int channelCount { 0 };

    std::vector<float> window;
    // 1 / sum of the squared window across the overlap. See the note above.
    float overlapNormalisation { 1.0f };

    // Per channel: a sliding input ring, an accumulating output ring, and the
    // write cursor they share.
    std::vector<std::vector<float>> inputRing;
    std::vector<std::vector<float>> outputRing;
    std::vector<int> ringPos;
    std::vector<int> hopCounter;

    // Scratch for one frame. juce::dsp::FFT's interleaved-complex form needs
    // 2 * fftSize floats.
    std::vector<float> scratch;
    std::vector<float> real;
    std::vector<float> imag;
};

} // namespace px3
