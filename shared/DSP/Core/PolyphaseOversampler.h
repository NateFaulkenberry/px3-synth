#pragma once

#include <JuceHeader.h>

#include <array>

namespace px3::dsp
{
// Per-sample polyphase IIR oversampling: a cascade of 2x halfband stages, each a
// pair of allpass chains (one per polyphase branch).
//
// JUCE's dsp::Oversampling uses the same structure and the same filter design
// (FilterDesign::designIIRLowpassHalfBandPolyphaseAllpassMethod), but works on
// blocks, and the FX chain this serves runs one stereo frame at a time. The
// stage parameters below are JUCE's "maximum quality" set, so the response is
// the one JUCE would give; PX3Tests checks the two agree sample for sample.
//
// Why IIR and not linear-phase FIR: a FIR halfband pair at these specs is about
// 50 samples of latency, which slides an FX-chain stage against the dry signal
// it is summed with. The allpass pair is about 5 samples at low frequencies.
//
// Coefficients are designed in prepare(), off the audio thread; processing
// never allocates.
class PolyphaseOversampler
{
public:
    static constexpr int kMaxStages = 3;   // up to 8x
    static constexpr int kMaxCoefficients = 12;
    static constexpr int kMaxChannels = 4;

    // Stages from the host rate: the oversampled rate stays at or above
    // 352.8 kHz, so the protection is the same at 44.1, 48, 96 and 192 kHz.
    static int stagesForSampleRate(double sampleRate) noexcept
    {
        if (sampleRate <= 50000.0) { return 3; }
        if (sampleRate <= 100000.0) { return 2; }
        return 1;
    }

    void prepare(int numStages, int numChannels)
    {
        stages = juce::jlimit(1, kMaxStages, numStages);
        channels = juce::jlimit(1, kMaxChannels, numChannels);
        for (int s = 0; s < stages; ++s)
        {
            // JUCE's maximum-quality parameters for stage s.
            const auto twUp = 0.10f * (s == 0 ? 0.5f : 1.0f);
            const auto twDown = 0.12f * (s == 0 ? 0.5f : 1.0f);
            design(up[static_cast<std::size_t>(s)], twUp, -90.0f + 10.0f * static_cast<float>(s));
            design(down[static_cast<std::size_t>(s)], twDown, -75.0f + 10.0f * static_cast<float>(s));
        }
        reset();
    }

    void reset() noexcept
    {
        for (auto& st : up) { for (auto& ch : st.state) { ch.fill(0.0f); } }
        for (auto& st : down) { for (auto& ch : st.state) { ch.fill(0.0f); } st.delay.fill(0.0f); }
    }

    int factor() const noexcept { return 1 << stages; }

    // One input sample to factor() samples at the oversampled rate.
    void upsample(int channel, float x, float* out) noexcept
    {
        // Each stage reads its inputs in time order - the allpass state runs
        // from one sample to the next - so it reads a copy, not the slots it
        // is writing.
        std::array<float, 1 << kMaxStages> previous {};
        out[0] = x;
        auto count = 1;
        for (int s = 0; s < stages; ++s)
        {
            auto& st = up[static_cast<std::size_t>(s)];
            auto& state = st.state[static_cast<std::size_t>(channel)];
            std::copy(out, out + count, previous.begin());
            for (int i = 0; i < count; ++i)
            {
                const auto input = previous[static_cast<std::size_t>(i)];
                out[2 * i] = allpassChain(st, state, 0, st.direct, input);
                out[2 * i + 1] = allpassChain(st, state, st.direct, st.count, input);
            }
            count *= 2;
        }
    }

    // factor() samples at the oversampled rate back to one.
    float downsample(int channel, float* in) noexcept
    {
        auto count = factor();
        for (int s = stages - 1; s >= 0; --s)
        {
            auto& st = down[static_cast<std::size_t>(s)];
            auto& state = st.state[static_cast<std::size_t>(channel)];
            auto& delay = st.delay[static_cast<std::size_t>(channel)];
            for (int i = 0; i < count / 2; ++i)
            {
                const auto directOut = allpassChain(st, state, 0, st.direct, in[2 * i]);
                const auto delayedOut = allpassChain(st, state, st.direct, st.count, in[2 * i + 1]);
                in[i] = (delay + directOut) * 0.5f;
                delay = delayedOut;
            }
            count /= 2;
        }
        return in[0];
    }

private:
    struct Stage
    {
        std::array<float, kMaxCoefficients> coefficients {};
        int count { 0 };
        int direct { 0 };
        std::array<std::array<float, kMaxCoefficients>, kMaxChannels> state {};
        std::array<float, kMaxChannels> delay {};
    };

    static void design(Stage& stage, float transitionWidth, float stopbandDb)
    {
        auto structure = juce::dsp::FilterDesign<float>::designIIRLowpassHalfBandPolyphaseAllpassMethod(transitionWidth, stopbandDb);
        stage.count = 0;
        for (int i = 0; i < structure.directPath.size() && stage.count < kMaxCoefficients; ++i)
        {
            stage.coefficients[static_cast<std::size_t>(stage.count++)] = structure.directPath.getObjectPointer(i)->coefficients[0];
        }
        for (int i = 1; i < structure.delayedPath.size() && stage.count < kMaxCoefficients; ++i)
        {
            stage.coefficients[static_cast<std::size_t>(stage.count++)] = structure.delayedPath.getObjectPointer(i)->coefficients[0];
        }
        // JUCE splits the list this way: the first ceil(n/2) are the direct path.
        stage.direct = stage.count - stage.count / 2;
    }

    static float allpassChain(const Stage& st, std::array<float, kMaxCoefficients>& state, int from, int to, float input) noexcept
    {
        for (int n = from; n < to; ++n)
        {
            const auto alpha = st.coefficients[static_cast<std::size_t>(n)];
            const auto output = alpha * input + state[static_cast<std::size_t>(n)];
            state[static_cast<std::size_t>(n)] = input - alpha * output;
            input = output;
        }
        return input;
    }

    std::array<Stage, kMaxStages> up {}, down {};
    int stages { 3 };
    int channels { 2 };
};
} // namespace px3::dsp
