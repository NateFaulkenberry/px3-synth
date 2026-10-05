#pragma once

// Building blocks for the reverb types. Deliberately small and concrete: a
// delay line, an allpass, a frequency-dependent decay filter, a random LFO, a
// couple of filters and the Hadamard mix. Everything is set in seconds or Hz
// at the running rate, so no constant changes meaning with the sample rate.
//
// Storage: every line is a view into one Arena the Reverb allocates at
// prepare() time. Nothing here allocates.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace px3::reverb
{
constexpr double kTwoPi = 6.283185307179586476925;

// Carves line storage out of one preallocated float block. take() is plain
// pointer arithmetic, so binding a type to its storage is real-time safe.
struct Arena
{
    float* base { nullptr };
    std::size_t capacity { 0 };
    std::size_t used { 0 };

    float* take(std::size_t count) noexcept
    {
        if (base == nullptr || used + count > capacity) { return nullptr; }
        auto* p = base + used;
        used += count;
        return p;
    }
};

// A circular delay line of exact length. Read BEFORE writing the current
// sample: tap(d) then returns x[n - d].
struct Line
{
    float* buf { nullptr };
    int size { 0 };
    int w { 0 };

    // Room for delays up to maxDelay, plus the interpolator's guard points.
    static std::size_t floatsFor(int maxDelay) noexcept { return static_cast<std::size_t>(std::max(8, maxDelay + 6)); }

    bool bind(Arena& arena, int maxDelay) noexcept
    {
        const auto n = floatsFor(maxDelay);
        buf = arena.take(n);
        size = buf != nullptr ? static_cast<int>(n) : 0;
        w = 0;
        return buf != nullptr;
    }

    void push(float x) noexcept
    {
        buf[w] = x;
        if (++w == size) { w = 0; }
    }

    float tap(int d) const noexcept
    {
        d = std::clamp(d, 1, size - 1);
        auto p = w - d;
        if (p < 0) { p += size; }
        return buf[p];
    }

    // Third-order Lagrange interpolation. Its magnitude response never exceeds
    // unity at any fractional position - unlike Catmull-Rom - which is what the
    // feedback networks' stability argument needs; linear interpolation's
    // fraction-dependent lowpass would make a modulated line a wobbling filter.
    float lagrange(float d) const noexcept
    {
        d = std::clamp(d, 2.0f, static_cast<float>(size - 4));
        const auto di = static_cast<int>(d);
        const auto f = d - static_cast<float>(di);
        auto p = w - (di + 2);   // oldest of the four points
        if (p < 0) { p += size; }
        float y3, y2, y1, y0;
        if (p + 3 < size)
        {
            y3 = buf[p]; y2 = buf[p + 1]; y1 = buf[p + 2]; y0 = buf[p + 3];
        }
        else
        {
            const auto at = [this](int i) { return buf[i >= size ? i - size : i]; };
            y3 = at(p); y2 = at(p + 1); y1 = at(p + 2); y0 = at(p + 3);
        }
        // Nodes at delays di-1 (y0), di (y1), di+1 (y2), di+2 (y3).
        const auto fm1 = f - 1.0f, fm2 = f - 2.0f, fp1 = f + 1.0f;
        const auto c0 = -f * fm1 * fm2 * (1.0f / 6.0f);
        const auto c1 = fp1 * fm1 * fm2 * 0.5f;
        const auto c2 = -fp1 * f * fm2 * 0.5f;
        const auto c3 = fp1 * f * fm1 * (1.0f / 6.0f);
        return c0 * y0 + c1 * y1 + c2 * y2 + c3 * y3;
    }

    // Linear interpolation: for delays that only glide (SIZE) and are not
    // modulated, where Lagrange's cost buys nothing audible.
    float linear(float d) const noexcept
    {
        d = std::clamp(d, 1.0f, static_cast<float>(size - 3));
        const auto di = static_cast<int>(d);
        const auto f = d - static_cast<float>(di);
        auto p = w - di;
        if (p < 0) { p += size; }
        auto q = p - 1;
        if (q < 0) { q += size; }
        return buf[p] + f * (buf[q] - buf[p]);
    }

    void clear() noexcept
    {
        if (buf != nullptr) { std::fill(buf, buf + size, 0.0f); }
        w = 0;
    }
};

// Schroeder allpass. Lossless at every frequency for |g| < 1: it changes the
// timing of what passes through it (diffusion) and never its level.
struct Allpass
{
    Line line;
    float delay { 1.0f };
    float gain { 0.5f };

    float process(float x) noexcept
    {
        const auto v = line.tap(static_cast<int>(delay));
        const auto u = x + gain * v;
        line.push(u);
        return v - gain * u;
    }

    float processModulated(float x, float d) noexcept
    {
        const auto v = line.lagrange(d);
        const auto u = x + gain * v;
        line.push(u);
        return v - gain * u;
    }

    // A gliding but unmodulated length: settled lengths are whole samples
    // (see glideTo) and read as such; only while SIZE moves is the read
    // interpolated - Lagrange, because linear interpolation at a fractional
    // delay is a lowpass, and inside a loop that lowpass is applied on every
    // pass (measured: a duller tail and DAMPING 0 no longer flat).
    float processGliding(float x, float d) noexcept
    {
        const auto v = d == std::floor(d) ? line.tap(static_cast<int>(d)) : line.lagrange(d);
        const auto u = x + gain * v;
        line.push(u);
        return v - gain * u;
    }
};

// One per-sample step of a SIZE glide toward a (whole-sample) target, landing
// on it exactly once within a thousandth of a sample.
inline float glideTo(float current, float target, float k) noexcept
{
    if (current == target) { return current; }
    current += k * (target - current);
    return std::abs(target - current) < 1.0e-3f ? target : current;
}

inline float onePoleCoefficient(double hz, double sampleRate) noexcept
{
    return static_cast<float>(std::exp(-kTwoPi * std::min(hz, 0.45 * sampleRate) / sampleRate));
}

// Jot's per-line gain for a target reverberation time: after RT60 seconds the
// signal must have lost 60 dB, whatever the length of the line it is in.
inline float decayGain(double lineSeconds, double rt60Seconds) noexcept
{
    return static_cast<float>(std::pow(10.0, -3.0 * lineSeconds / std::max(0.01, rt60Seconds)));
}

// Three-band decay: gain gLow below the low corner, gMid in between, gHigh
// above the high corner. Built as gMid x a first-order low shelf x a
// first-order high shelf. Each shelf is monotone between its two plateaus, so
// the magnitude never exceeds max(gLow, gMid) - which is below 1 - at any
// frequency. With a lossless mixing matrix that bounds the loop gain below 1:
// the network cannot run away at any decay setting.
struct DecayFilter
{
    float gMid { 1.0f }, kLow { 1.0f }, kHigh { 1.0f };
    float pLow { 0.0f }, pHigh { 0.0f };
    float sLow { 0.0f }, sHigh { 0.0f };

    void set(float gLow, float gMidIn, float gHigh, float lowPole, float highPole) noexcept
    {
        gMid = gMidIn;
        kLow = gLow / std::max(1.0e-6f, gMidIn);
        kHigh = gHigh / std::max(1.0e-6f, gMidIn);
        pLow = lowPole;
        pHigh = highPole;
    }

    float process(float x) noexcept
    {
        sLow = (1.0f - pLow) * x + pLow * sLow;
        const auto y = x + (kLow - 1.0f) * sLow;
        sHigh = (1.0f - pHigh) * y + pHigh * sHigh;
        return gMid * (sHigh + kHigh * (y - sHigh));
    }

    void clear() noexcept { sLow = sHigh = 0.0f; }
};

// Deterministic random LFO, -1..1: moves between random targets along a C1
// curve (smoothstep), so there is no corner in the delay it drives. Random
// rather than sinusoidal modulation is what keeps a modulated network from
// sounding chorused (Lexicon "random hall"; Costello).
struct SmoothRandom
{
    std::uint32_t state { 1 };
    float a { 0.0f }, b { 0.0f }, phase { 0.0f }, increment { 0.0f };

    float next01() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state >> 8) * (1.0f / 16777216.0f);
    }

    void seed(std::uint32_t s, float startPhase) noexcept
    {
        state = s * 2654435761u + 0x9e3779b9u;
        if (state == 0) { state = 1; }
        a = 2.0f * next01() - 1.0f;
        b = 2.0f * next01() - 1.0f;
        phase = startPhase - std::floor(startPhase);
    }

    void setRate(double hz, double sampleRate) noexcept { increment = static_cast<float>(hz / sampleRate); }

    float process() noexcept
    {
        phase += increment;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            a = b;
            b = 2.0f * next01() - 1.0f;
        }
        const auto t = phase * phase * (3.0f - 2.0f * phase);
        return a + (b - a) * t;
    }
};

struct OnePoleLowpass
{
    float pole { 0.0f }, state { 0.0f };
    void setCutoff(double hz, double sampleRate) noexcept { pole = onePoleCoefficient(hz, sampleRate); }
    float process(float x) noexcept { state = (1.0f - pole) * x + pole * state; return state; }
    void clear() noexcept { state = 0.0f; }
};

// Transposed direct form II biquad (RBJ cookbook).
struct Biquad
{
    float b0 { 1.0f }, b1 { 0.0f }, b2 { 0.0f }, a1 { 0.0f }, a2 { 0.0f };
    float z1 { 0.0f }, z2 { 0.0f };

    void setLowpass(double hz, double q, double sampleRate) noexcept { design(hz, q, sampleRate, true); }
    void setHighpass(double hz, double q, double sampleRate) noexcept { design(hz, q, sampleRate, false); }

    void design(double hz, double q, double sampleRate, bool lowpass) noexcept
    {
        const auto w = kTwoPi * std::min(hz, 0.45 * sampleRate) / sampleRate;
        const auto cw = std::cos(w), alpha = std::sin(w) / (2.0 * q);
        const auto a0 = 1.0 + alpha;
        const auto k = lowpass ? (1.0 - cw) * 0.5 : (1.0 + cw) * 0.5;
        b0 = static_cast<float>(k / a0);
        b1 = static_cast<float>((lowpass ? 1.0 - cw : -(1.0 + cw)) / a0);
        b2 = b0;
        a1 = static_cast<float>(-2.0 * cw / a0);
        a2 = static_cast<float>((1.0 - alpha) / a0);
    }

    float process(float x) noexcept
    {
        const auto y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void clear() noexcept { z1 = z2 = 0.0f; }
};

// Orthonormal Hadamard mix for N = 2^k: lossless, and every output carries
// every input with equal weight - the "maximally diffusive" choice.
template <std::size_t N>
inline void hadamard(std::array<float, N>& v) noexcept
{
    static_assert((N & (N - 1)) == 0, "N must be a power of two");
    for (std::size_t h = 1; h < N; h *= 2)
        for (std::size_t i = 0; i < N; i += h * 2)
            for (std::size_t j = i; j < i + h; ++j)
            {
                const auto a = v[j], b = v[j + h];
                v[j] = a + b;
                v[j + h] = a - b;
            }
    const auto norm = static_cast<float>(1.0 / std::sqrt(static_cast<double>(N)));
    for (auto& x : v) { x *= norm; }
}

// Balanced +-1 vectors from a fixed seed. Injection and pickup vectors must
// NOT be rows of the Hadamard matrix: H times one of its own rows is a single
// line, so all the energy would recirculate through one delay - a comb.
template <std::size_t N>
inline std::array<float, N> balancedSigns(std::uint32_t seed) noexcept
{
    std::array<float, N> v {};
    for (std::size_t i = 0; i < N; ++i) { v[i] = i < N / 2 ? 1.0f : -1.0f; }
    auto s = seed * 2654435761u + 12345u;
    for (std::size_t i = N - 1; i > 0; --i)
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        std::swap(v[i], v[s % (i + 1)]);
    }
    return v;
}

// A second balanced vector orthogonal to the first, so the two output
// channels share no common component: decorrelated, and equally loud.
template <std::size_t N>
inline std::array<float, N> orthogonalSigns(const std::array<float, N>& to, std::uint32_t seed) noexcept
{
    for (std::uint32_t attempt = 0; attempt < 4096; ++attempt)
    {
        const auto v = balancedSigns<N>(seed + attempt * 7919u);
        float dot = 0.0f;
        for (std::size_t i = 0; i < N; ++i) { dot += v[i] * to[i]; }
        if (dot == 0.0f) { return v; }
    }
    return balancedSigns<N>(seed);
}

// sin/cos for |x| <= ~1.6 by Taylor series; the time-varying matrix's angles
// stay inside that, and per-sample std::sin/std::cos for 8 pairs would cost
// more than the network.
inline void sinCosSmall(float x, float& s, float& c) noexcept
{
    const auto x2 = x * x;
    s = x * (1.0f - x2 * (1.0f / 6.0f) * (1.0f - x2 * (1.0f / 20.0f) * (1.0f - x2 * (1.0f / 42.0f))));
    c = 1.0f - x2 * 0.5f * (1.0f - x2 * (1.0f / 12.0f) * (1.0f - x2 * (1.0f / 30.0f) * (1.0f - x2 * (1.0f / 56.0f))));
}

inline float smoothstep01(float t) noexcept
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
} // namespace px3::reverb
