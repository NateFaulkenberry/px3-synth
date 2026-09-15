#pragma once

#include <JuceHeader.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// The oscillator DSP primitives every mode and the sub are built from. Each one
// is chosen and measured in docs/OSCILLATOR_DSP_DESIGN.md; the reasons live
// there, and the conventions they share are stated here once.
//
//   phase      double, in cycles, [0, 1); pitch moves the increment, never the
//              phase; ratios get their own accumulators
//   latency    every mode's output lags its phase by kOscillatorLatencySamples
//   time       every per-sample constant is derived from a time or frequency,
//              keeping the value it had at kReferenceSampleRate
namespace px3::dsp
{

inline constexpr int kOscillatorLatencySamples = 5;

// The rate PX3's per-sample constants were tuned and are tested at.
inline constexpr double kReferenceSampleRate = 48000.0;

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTwoPi = 2.0 * kPi;

// ---- phase ------------------------------------------------------------------

// Cycles per sample, clamped below Nyquist so an accumulator wraps at most once
// per sample.
inline double phaseIncrement(double hz, double sampleRate) noexcept
{
    return juce::jlimit(0.0, 0.499, hz / juce::jmax(1.0, sampleRate));
}

inline double wrapPhase(double phase) noexcept
{
    return phase - std::floor(phase);
}

// Advances an accumulator and reports whether it wrapped. On a wrap, the time
// since the wrap in samples is phase / increment.
inline bool advancePhase(double& phase, double increment) noexcept
{
    phase += increment;
    if (phase >= 1.0)
    {
        phase -= 1.0;
        return true;
    }
    return false;
}

// ---- sample-rate independence -----------------------------------------------

// A one-pole coefficient tuned at 48 kHz (y += (x - y) * c), as the coefficient
// with the same time constant at `sampleRate`.
inline double onePoleCoefficient(double coefficientAt48k, double sampleRate) noexcept
{
    const auto c = juce::jlimit(0.0, 1.0, coefficientAt48k);
    return 1.0 - std::pow(1.0 - c, kReferenceSampleRate / juce::jmax(1.0, sampleRate));
}

// A per-sample decay or phase rate tuned at 48 kHz, at `sampleRate`.
inline double perSampleRate(double rateAt48k, double sampleRate) noexcept
{
    return rateAt48k * kReferenceSampleRate / juce::jmax(1.0, sampleRate);
}

// A sample count tuned at 48 kHz, as the same duration at `sampleRate`.
inline int sampleCount(double samplesAt48k, double sampleRate) noexcept
{
    return juce::jmax(1, static_cast<int>(std::lround(samplesAt48k * sampleRate / kReferenceSampleRate)));
}

// ---- band-limited steps and ramps ---------------------------------------------

// Residual of the band-limited step built from an integrated cubic B-spline:
// S(tau) - H(tau), where tau is the sample's time relative to the step, in
// samples. Non-zero on (-2, 2). Valimaki, Pekonen and Nam 2012.
inline double blepResidual(double tau) noexcept
{
    if (tau <= -2.0 || tau >= 2.0)
    {
        return 0.0;
    }
    const auto x = -std::abs(tau);
    double lower;   // S(x), x in [-2, 0]
    if (x <= -1.0)
    {
        const auto u = x + 2.0;
        lower = u * u * u * u / 24.0;
    }
    else
    {
        const auto x2 = x * x;
        lower = 0.5 + 2.0 * x / 3.0 - x2 * x / 3.0 - x2 * x2 / 8.0;
    }
    return tau < 0.0 ? lower : -lower;
}

// Residual of the band-limited ramp: the integral of blepResidual, for a slope
// change of one per sample. Even in tau.
inline double blampResidual(double tau) noexcept
{
    const auto s = std::abs(tau);
    if (s >= 2.0)
    {
        return 0.0;
    }
    const auto x = -s;
    if (x <= -1.0)
    {
        const auto u = x + 2.0;
        return u * u * u * u * u / 120.0;
    }
    const auto x2 = x * x;
    return 7.0 / 30.0 + x / 2.0 + x2 / 3.0 - x2 * x2 / 12.0 - x2 * x2 * x / 40.0;
}

// A streaming PolyBLEP / PolyBLAMP line with passband compensation.
//
// Per sample: report every discontinuity that happened since the previous
// sample - step() for a jump in value, ramp() for a jump in slope, with `tau`
// the time since it in samples, in [0, 1) - then push() the naive value. The
// return is the corrected, compensated signal kOscillatorLatencySamples late.
//
// Corrections superpose, so two edges closer than the kernel (a narrow pulse, a
// sync reset beside a slave wrap) are both exact.
class BlepLine
{
public:
    // The cubic B-spline rolls the passband off as sinc^4 of the normalised
    // frequency; [-a, 1 + 2a, -a] buys it back (-2.5 dB -> +0.2 dB at 10 kHz at
    // 48 kHz, measured, which also covers the ADAA stage's half-sample droop).
    static constexpr double kCompensation = 0.25;

    void reset(double value = 0.0) noexcept
    {
        history.fill(value);
        pendingNow = 0.0;
        pendingNext = 0.0;
    }

    void step(double tau, double height) noexcept
    {
        pendingNow += height * blepResidual(tau);
        history[0] += height * blepResidual(tau - 1.0);
        history[1] += height * blepResidual(tau - 2.0);
        pendingNext += height * blepResidual(tau + 1.0);
    }

    void ramp(double tau, double slopeChange) noexcept
    {
        pendingNow += slopeChange * blampResidual(tau);
        history[0] += slopeChange * blampResidual(tau - 1.0);
        history[1] += slopeChange * blampResidual(tau - 2.0);
        pendingNext += slopeChange * blampResidual(tau + 1.0);
    }

    double push(double naive) noexcept
    {
        const auto current = naive + pendingNow;
        pendingNow = pendingNext;
        pendingNext = 0.0;
        for (std::size_t i = history.size() - 1; i > 0; --i)
        {
            history[i] = history[i - 1];
        }
        history[0] = current;
        // Samples 4..6 back are final: a correction reaches at most two samples
        // into the past. Centred on five back, which is the common latency.
        return -kCompensation * history[6] + (1.0 + 2.0 * kCompensation) * history[5] - kCompensation * history[4];
    }

private:
    std::array<double, 7> history {};   // [0] = the previous sample
    double pendingNow { 0.0 };
    double pendingNext { 0.0 };
};

// The common latency for modes that have none of their own.
class LatencyDelay
{
public:
    void reset(double value = 0.0) noexcept
    {
        line.fill(value);
        index = 0;
    }

    double push(double value) noexcept
    {
        const auto out = line[static_cast<std::size_t>(index)];
        line[static_cast<std::size_t>(index)] = value;
        index = (index + 1) % kOscillatorLatencySamples;
        return out;
    }

private:
    std::array<double, kOscillatorLatencySamples> line {};
    int index { 0 };
};

// ---- 2x decimation (FM) -------------------------------------------------------

// A 23-tap Kaiser (beta 6) halfband as a polyphase 2:1 decimator. Fed the two
// oversampled points of each sample at t = n and t = n + 1/2, its latency is
// exactly kOscillatorLatencySamples.
class HalfbandDecimator
{
public:
    static constexpr int kSideTaps = 6;   // non-zero taps either side of centre

    void reset() noexcept
    {
        line.fill(0.0);
        position = 0;
    }

    double process(double first, double second) noexcept
    {
        push(first);
        push(second);
        const auto* newest = line.data() + position + kLength - 1;
        const auto centre = -(2 * kSideTaps - 1);
        auto out = 0.5 * newest[centre];
        const auto& taps = sideTaps();
        for (int j = 0; j < kSideTaps; ++j)
        {
            out += taps[static_cast<std::size_t>(j)] * (newest[centre + 2 * j + 1] + newest[centre - 2 * j - 1]);
        }
        return out;
    }

    static std::array<double, kSideTaps> designSideTaps() noexcept
    {
        const auto besselI0 = [](double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 40; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        };
        constexpr int length = 4 * kSideTaps - 1;
        constexpr double beta = 6.0;
        const auto centre = length / 2;
        std::array<double, kSideTaps> taps {};
        for (int j = 0; j < kSideTaps; ++j)
        {
            const auto k = 2 * j + 1;
            const auto n = centre + k;
            const auto r = 2.0 * n / (length - 1) - 1.0;
            taps[static_cast<std::size_t>(j)] = (std::sin(kPi * k / 2.0) / (kPi * k))
                                                * besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / besselI0(beta);
        }
        return taps;
    }

private:
    static constexpr int kLength = 4 * kSideTaps;

    static const std::array<double, kSideTaps>& sideTaps() noexcept;

    void push(double value) noexcept
    {
        line[static_cast<std::size_t>(position)] = value;
        line[static_cast<std::size_t>(position + kLength)] = value;
        position = (position + 1) % kLength;
    }

    std::array<double, 2 * kLength> line {};
    int position { 0 };
};

// Built at load, never on the audio thread.
inline const std::array<double, HalfbandDecimator::kSideTaps> kHalfbandSideTaps = [] { return HalfbandDecimator::designSideTaps(); }();
inline const std::array<double, HalfbandDecimator::kSideTaps>& HalfbandDecimator::sideTaps() noexcept { return kHalfbandSideTaps; }

// Keeps FM's sidebands inside what the 2x decimator can still remove: the
// Carson bandwidth, carrier + (index + 1) * modulator, stays below the sample
// rate (the oversampled Nyquist). Untouched until 70% of that limit, then eased
// onto it - musical FM behaves exactly as before until its sidebands would fold.
inline double carsonLimitedIndex(double index, double carrierIncrement, double modulatorIncrement) noexcept
{
    if (modulatorIncrement <= 1.0e-9)
    {
        return index;
    }
    const auto limit = (0.95 - carrierIncrement) / modulatorIncrement - 1.0;
    if (limit <= 0.0)
    {
        return 0.0;
    }
    const auto knee = 0.7 * limit;
    if (index <= knee)
    {
        return index;
    }
    return knee + (limit - knee) * std::tanh((index - knee) / (limit - knee));
}

// A partial's gain by its own frequency: 1 below 0.42 cycles/sample, a raised
// cosine to 0 at 0.48. A partial never reaches Nyquist, and never pops in or out
// as the pitch crosses it.
inline float nyquistFade(double increment) noexcept
{
    if (increment <= 0.42)
    {
        return 1.0f;
    }
    if (increment >= 0.48)
    {
        return 0.0f;
    }
    const auto t = (increment - 0.42) / 0.06;
    return static_cast<float>(0.5 + 0.5 * std::cos(kPi * t));
}

// A sine of a phase in cycles, for the hot paths: 4,096 table values with an
// exact-derivative cubic Hermite (the derivative is the same table a quarter
// cycle on). Worst error 4e-11 against std::sin - about -200 dB - at half its
// cost, which matters in modes that take a dozen sines a sample.
namespace detail
{
inline constexpr int kSineTableSize = 4096;
inline const std::array<double, kSineTableSize + kSineTableSize / 4 + 2> kSineTable = []
{
    std::array<double, kSineTableSize + kSineTableSize / 4 + 2> table {};
    for (std::size_t i = 0; i < table.size(); ++i)
    {
        table[i] = std::sin(kTwoPi * static_cast<double>(i) / kSineTableSize);
    }
    return table;
}();
} // namespace detail

inline constexpr double kInverseTwoPi = 1.0 / kTwoPi;

inline double fastSine(double cycles) noexcept
{
    using detail::kSineTable;
    using detail::kSineTableSize;
    const auto position = (cycles - std::floor(cycles)) * kSineTableSize;
    const auto i = static_cast<std::size_t>(position);
    const auto t = position - static_cast<double>(i);
    const auto y0 = kSineTable[i];
    const auto y1 = kSineTable[i + 1];
    const auto step = kTwoPi / kSineTableSize;
    const auto d0 = kSineTable[i + kSineTableSize / 4] * step;
    const auto d1 = kSineTable[i + 1 + kSineTableSize / 4] * step;
    const auto b = 3.0 * (y1 - y0) - 2.0 * d0 - d1;
    const auto a = 2.0 * (y0 - y1) + d0 + d1;
    return ((a * t + b) * t + d0) * t + y0;
}

// ---- nonlinear processing ------------------------------------------------------

// First-order antiderivative anti-aliasing (Parker, Zavalishin and Le Bivic 2016)
// for ANY static curve. The antiderivative is tabulated: cubic Hermite through
// exact values and exact slopes - the slope of the antiderivative is the curve
// itself - so the error is O(h^4) and C1 across cells.
class AdaaTable
{
public:
    template <typename Curve, typename CurveSlope>
    AdaaTable(Curve curve, CurveSlope curveSlope)
    {
        step = 2.0 * kRange / (kNodes - 1);
        inverse = 1.0 / step;
        antiderivativeNodes.assign(kNodes, 0.0);
        curveNodes.assign(kNodes, 0.0);
        slopeNodes.assign(kNodes, 0.0);
        for (int i = 0; i < kNodes; ++i)
        {
            const auto x = -kRange + i * step;
            curveNodes[static_cast<std::size_t>(i)] = curve(x);
            slopeNodes[static_cast<std::size_t>(i)] = curveSlope(x);
        }
        for (int i = 1; i < kNodes; ++i)
        {
            const auto a = -kRange + (i - 1) * step;
            constexpr int sub = 16;
            const auto h = step / sub;
            double sum = curve(a) + curve(a + step);
            for (int k = 1; k < sub; ++k)
            {
                sum += (k % 2 ? 4.0 : 2.0) * curve(a + k * h);
            }
            antiderivativeNodes[static_cast<std::size_t>(i)] = antiderivativeNodes[static_cast<std::size_t>(i - 1)] + sum * h / 3.0;
        }

        // Each cell's Hermite cubic, expanded once into powers of t, so a lookup
        // is three multiplies rather than the ten the Hermite basis takes.
        const auto expand = [this](std::vector<double>& out, const std::vector<double>& value, const std::vector<double>& slope)
        {
            out.assign(static_cast<std::size_t>(4 * (kNodes - 1)), 0.0);
            for (int i = 0; i < kNodes - 1; ++i)
            {
                const auto idx = static_cast<std::size_t>(i);
                const auto y0 = value[idx];
                const auto y1 = value[idx + 1];
                const auto d0 = slope[idx] * step;
                const auto d1 = slope[idx + 1] * step;
                out[4 * idx] = y0;
                out[4 * idx + 1] = d0;
                out[4 * idx + 2] = 3.0 * (y1 - y0) - 2.0 * d0 - d1;
                out[4 * idx + 3] = 2.0 * (y0 - y1) + d0 + d1;
            }
        };
        expand(antiderivativeCells, antiderivativeNodes, curveNodes);
        expand(curveCells, curveNodes, slopeNodes);
    }

    double antiderivative(double x) const noexcept
    {
        if (x >= kRange) { return antiderivativeNodes.back() + curveNodes.back() * (x - kRange); }
        if (x <= -kRange) { return antiderivativeNodes.front() + curveNodes.front() * (x + kRange); }
        const auto [i, t] = cell(x);
        const auto* c = antiderivativeCells.data() + 4 * i;
        return ((c[3] * t + c[2]) * t + c[1]) * t + c[0];
    }

    double curve(double x) const noexcept
    {
        if (x >= kRange) { return curveNodes.back(); }
        if (x <= -kRange) { return curveNodes.front(); }
        const auto [i, t] = cell(x);
        const auto* c = curveCells.data() + 4 * i;
        return ((c[3] * t + c[2]) * t + c[1]) * t + c[0];
    }

private:
    static constexpr int kNodes = 2049;
    static constexpr double kRange = 12.0;

    std::pair<std::size_t, double> cell(double x) const noexcept
    {
        const auto position = (x + kRange) * inverse;
        const auto i = juce::jmin(static_cast<int>(position), kNodes - 2);
        return { static_cast<std::size_t>(i), position - i };
    }

    double step { 0.0 };
    double inverse { 0.0 };
    std::vector<double> antiderivativeNodes, curveNodes, slopeNodes;
    std::vector<double> antiderivativeCells, curveCells;   // 4 power-basis coefficients per cell
};

// tanh(x).
inline const AdaaTable kTanhAdaa { [](double x) { return std::tanh(x); },
                                   [](double x) { const auto y = std::tanh(x); return 1.0 - y * y; } };

// The curve every source passes through: the oscillator's soft clip tanh(x),
// then the voice's tanh(0.92 x). Two stages in series are one static curve, so
// one table anti-aliases both.
inline constexpr double kVoiceSourceDrive = 0.92;
inline const AdaaTable kSourceClipAdaa { [](double x) { return std::tanh(kVoiceSourceDrive * std::tanh(x)); },
                                         [](double x)
                                         {
                                             const auto inner = std::tanh(x);
                                             const auto outer = std::tanh(kVoiceSourceDrive * inner);
                                             return kVoiceSourceDrive * (1.0 - outer * outer) * (1.0 - inner * inner);
                                         } };

// One ADAA stage's state. Half a sample of delay, like every first-order ADAA.
class Adaa
{
public:
    void reset() noexcept { primed = false; }

    double process(const AdaaTable& table, double x) noexcept
    {
        const auto big = table.antiderivative(x);
        if (!primed)
        {
            primed = true;
            previousX = x;
            previousBig = big;
            return table.curve(x);
        }
        const auto dx = x - previousX;
        const auto y = std::abs(dx) < 1.0e-5 ? table.curve(0.5 * (x + previousX)) : (big - previousBig) / dx;
        previousX = x;
        previousBig = big;
        return y;
    }

private:
    double previousX { 0.0 };
    double previousBig { 0.0 };
    bool primed { false };
};

// ---- DC ----------------------------------------------------------------------

class DcBlocker
{
public:
    void prepare(double sampleRate, double cutoffHz = 5.0) noexcept
    {
        pole = std::exp(-kTwoPi * cutoffHz / juce::jmax(1.0, sampleRate));
    }

    void reset() noexcept
    {
        x1 = 0.0;
        y1 = 0.0;
    }

    double process(double x) noexcept
    {
        const auto y = x - x1 + pole * y1;
        x1 = x;
        y1 = y;
        return y;
    }

private:
    double pole { 0.9993 };
    double x1 { 0.0 };
    double y1 { 0.0 };
};

// ---- noise -------------------------------------------------------------------

// One independent, reproducible stream per oscillator per voice per note.
struct NoiseStream
{
    std::uint32_t state { 0x9E3779B9u };

    void seed(std::uint32_t value) noexcept { state = value; }

    std::uint32_t next() noexcept
    {
        std::uint32_t z = (state += 0x9E3779B9u);
        z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
        z = (z ^ (z >> 13)) * 0xC2B2AE35u;
        return z ^ (z >> 16);
    }

    // Uniform in [-1, 1).
    float white() noexcept
    {
        return static_cast<float>(static_cast<double>(next()) * (2.0 / 4294967296.0) - 1.0);
    }

    // Uniform in [0, 1).
    double unit() noexcept
    {
        return static_cast<double>(next()) / 4294967296.0;
    }
};

inline std::uint32_t streamSeed(std::uint32_t voice, std::uint32_t noteSequence, std::uint32_t stream) noexcept
{
    std::uint32_t h = (voice + 1u) * 747796405u;
    h ^= noteSequence * 2891336453u;
    h ^= (stream + 1u) * 277803737u;
    h ^= h >> 16;
    h *= 2246822519u;
    h ^= h >> 13;
    return h;
}

// Paul Kellet's refined pink filter. Kept at its published coefficients: its
// slope measures -3.02 dB/oct with <= 0.25 dB ripple from 25 Hz to 16 kHz at
// 44.1, 48, 88.2 and 96 kHz alike (docs/OSCILLATOR_DSP_DESIGN.md, section 6).
class PinkFilter
{
public:
    void reset() noexcept { b.fill(0.0f); }

    float process(float white) noexcept
    {
        b[0] = 0.99886f * b[0] + white * 0.0555179f;
        b[1] = 0.99332f * b[1] + white * 0.0750759f;
        b[2] = 0.96900f * b[2] + white * 0.1538520f;
        b[3] = 0.86650f * b[3] + white * 0.3104856f;
        b[4] = 0.55000f * b[4] + white * 0.5329522f;
        b[5] = -0.7616f * b[5] - white * 0.0168980f;
        const auto pink = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + white * 0.5362f;
        b[6] = white * 0.115926f;
        return pink * 0.11f;
    }

private:
    std::array<float, 7> b {};
};

} // namespace px3::dsp
