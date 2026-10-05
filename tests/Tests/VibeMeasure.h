#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <vector>

// Black-box measurements of a Uni-Vibe-style effect, shared by the VIBE suite
// and the sonic-validation renders. They use only prepare / updateForBlock /
// processSampleFrame, so they measure what the effect DOES rather than how it
// is built - which is what lets them tell a Uni-Vibe from a generic phaser.
namespace px3tests::vibemeasure
{
constexpr double kRate = 48000.0;
constexpr int kFrame = 960;           // 20 ms: every probe tone is a whole number of cycles
constexpr double kFrameHz = kRate / kFrame;

// Probe tones: multiples of 50 Hz (orthogonal over a 20 ms frame), roughly
// third-octave spaced from 100 Hz to 12.5 kHz.
inline const std::vector<double>& probeTones()
{
    static const std::vector<double> tones { 100, 150, 200, 250, 300, 400, 500, 650, 800, 1000, 1250, 1600,
                                             2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500 };
    return tones;
}

inline std::complex<double> dft(const float* x, int n, double hz)
{
    std::complex<double> acc { 0.0, 0.0 };
    const auto w = juce::MathConstants<double>::twoPi * hz / kRate;
    for (int i = 0; i < n; ++i)
    {
        acc += static_cast<double>(x[i]) * std::polar(1.0, -w * i);
    }
    return acc;
}

// Runs `process(in, out)` on a stream built by `source(n)`, mono into both
// channels; returns the left output.
template <typename Process>
std::vector<float> run(int samples, const std::function<float(int)>& source, Process&& process,
                       std::vector<float>* inputOut = nullptr)
{
    std::vector<float> out(static_cast<std::size_t>(samples));
    if (inputOut != nullptr) { inputOut->resize(static_cast<std::size_t>(samples)); }
    for (int n = 0; n < samples; ++n)
    {
        const auto x = source(n);
        if (inputOut != nullptr) { (*inputOut)[static_cast<std::size_t>(n)] = x; }
        out[static_cast<std::size_t>(n)] = process(n, x);
    }
    return out;
}

inline float probeSignal(int n)
{
    double acc = 0.0;
    int k = 0;
    for (const auto hz : probeTones())
    {
        // Fixed, spread phases so the tones do not all peak together.
        acc += 0.012 * std::sin(juce::MathConstants<double>::twoPi * hz * n / kRate + 0.7 * k * k);
        ++k;
    }
    return static_cast<float>(acc);
}

// Per frame, the magnitude response at every probe tone (output / input).
inline std::vector<std::vector<double>> responseFrames(const std::vector<float>& in,
                                                       const std::vector<float>& out,
                                                       int fromSample)
{
    std::vector<std::vector<double>> frames;
    for (int start = fromSample; start + kFrame <= static_cast<int>(out.size()); start += kFrame)
    {
        std::vector<double> mags;
        for (const auto hz : probeTones())
        {
            const auto o = dft(out.data() + start, kFrame, hz);
            const auto i = dft(in.data() + start, kFrame, hz);
            mags.push_back(std::abs(o) / juce::jmax(1.0e-12, std::abs(i)));
        }
        frames.push_back(std::move(mags));
    }
    return frames;
}

// The deepest notch in a frame, as log2 of its frequency (parabolic
// interpolation across the neighbouring probe tones).
inline double notchLog2Hz(const std::vector<double>& mags)
{
    const auto& tones = probeTones();
    std::size_t best = 0;
    for (std::size_t i = 1; i < mags.size(); ++i) { if (mags[i] < mags[best]) best = i; }
    if (best == 0 || best + 1 >= mags.size()) { return std::log2(tones[best]); }
    const auto a = 20.0 * std::log10(mags[best - 1] + 1e-9);
    const auto b = 20.0 * std::log10(mags[best] + 1e-9);
    const auto c = 20.0 * std::log10(mags[best + 1] + 1e-9);
    const auto denom = a - 2.0 * b + c;
    const auto offset = std::abs(denom) > 1e-9 ? juce::jlimit(-0.5, 0.5, 0.5 * (a - c) / denom) : 0.0;
    const auto l0 = std::log2(tones[best]);
    const auto step = offset >= 0.0 ? std::log2(tones[best + 1]) - l0 : l0 - std::log2(tones[best - 1]);
    return l0 + offset * step;
}

// Deep notches in a frame: local minima at least `depthDb` below the frame's
// median level.
inline int notchCount(const std::vector<double>& mags, double depthDb)
{
    std::vector<double> db;
    for (const auto m : mags) db.push_back(20.0 * std::log10(m + 1e-9));
    auto sorted = db;
    std::sort(sorted.begin(), sorted.end());
    const auto median = sorted[sorted.size() / 2];
    int count = 0;
    for (std::size_t i = 1; i + 1 < db.size(); ++i)
    {
        if (db[i] < db[i - 1] && db[i] <= db[i + 1] && db[i] < median - depthDb) ++count;
    }
    return count;
}

// Cycle-synchronous average of a per-frame track whose period is an exact
// number of frames.
inline std::vector<double> cycleAverage(const std::vector<double>& track, int framesPerCycle)
{
    std::vector<double> avg(static_cast<std::size_t>(juce::jmax(1, framesPerCycle)), 0.0);
    const auto cycles = static_cast<int>(track.size()) / juce::jmax(1, framesPerCycle);
    for (int c = 0; c < cycles; ++c)
        for (int i = 0; i < framesPerCycle; ++i)
            avg[static_cast<std::size_t>(i)] += track[static_cast<std::size_t>(c * framesPerCycle + i)] / cycles;
    return avg;
}

// The share of the cycle the track spends RISING through the middle of its
// range: the 10%-to-90% rise time over (rise time + fall time). A symmetric
// sweep gives 0.5; the lamp's fast-bright, slow-dark sweep gives less. Times
// are interpolated between frames.
inline double riseFraction(const std::vector<double>& track, int framesPerCycle)
{
    if (framesPerCycle < 4 || static_cast<int>(track.size()) < framesPerCycle) return 0.5;
    const auto avg = cycleAverage(track, framesPerCycle);
    const auto [loIt, hiIt] = std::minmax_element(avg.begin(), avg.end());
    const auto lo = *loIt, hi = *hiIt;
    if (hi - lo < 1e-6) return 0.5;
    const auto n = static_cast<int>(avg.size());
    auto crossing = [&](double level, bool upward)
    {
        for (int i = 0; i < n; ++i)
        {
            const auto a = avg[static_cast<std::size_t>(i)];
            const auto b = avg[static_cast<std::size_t>((i + 1) % n)];
            if (upward ? (a < level && b >= level) : (a > level && b <= level))
                return i + (level - a) / (b - a);
        }
        return 0.0;
    };
    const auto l10 = lo + 0.1 * (hi - lo), l90 = lo + 0.9 * (hi - lo);
    auto span = [n](double from, double to) { auto d = to - from; while (d < 0) d += n; return d; };
    const auto rise = span(crossing(l10, true), crossing(l90, true));
    const auto fall = span(crossing(l90, false), crossing(l10, false));
    return rise / juce::jmax(1e-9, rise + fall);
}

// Peak-to-trough swing, in dB, of a tone's level across the frames.
inline double toneSwingDb(const std::vector<float>& out, double hz, int fromSample)
{
    double lo = 1e30, hi = 0.0;
    for (int start = fromSample; start + kFrame <= static_cast<int>(out.size()); start += kFrame)
    {
        const auto m = std::abs(dft(out.data() + start, kFrame, hz));
        lo = juce::jmin(lo, m);
        hi = juce::jmax(hi, m);
    }
    return 20.0 * std::log10((hi + 1e-12) / (lo + 1e-12));
}

// Mean level of a tone, in dB, relative to its input level.
inline double meanToneGainDb(const std::vector<float>& in, const std::vector<float>& out, double hz, int fromSample)
{
    double sumO = 0.0, sumI = 0.0;
    for (int start = fromSample; start + kFrame <= static_cast<int>(out.size()); start += kFrame)
    {
        sumO += std::norm(dft(out.data() + start, kFrame, hz));
        sumI += std::norm(dft(in.data() + start, kFrame, hz));
    }
    return 10.0 * std::log10((sumO + 1e-30) / (sumI + 1e-30));
}
} // namespace px3tests::vibemeasure
