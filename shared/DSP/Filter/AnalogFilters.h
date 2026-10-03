#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <cmath>
#include <complex>

// Zero-delay-feedback (topology-preserving transform) analog filter models.
//
// All three share one idea: integrate with trapezoidal one-poles so cutoff is
// exact (prewarped) and the response holds up near Nyquist, solve the global
// feedback loop instantaneously from its linear estimate, and put the
// nonlinearity where the hardware has it so resonance can run into
// self-oscillation without running away. They are allocation-free and
// stateless apart from their integrators.
//
// - Ladder: the transistor ladder. Four identical one-poles in a negative
//   feedback loop, the input stage saturating. Resonance thins the bass, as
//   the hardware does; a partial bass compensation keeps it usable.
// - Curtis: a CEM3320-style OTA cascade. Each stage saturates on its own, so it
//   distorts more gently and evenly than the ladder, and the bass is held up
//   at high resonance.
// - Arp: an aggressive 2-pole in the spirit of the ARP 2-pole designs. A state
//   variable core whose resonant feedback path saturates, with damping allowed
//   to go slightly negative at the top so it screams into a bounded
//   self-oscillation. Inspired-by: not a component-level emulation.
namespace px3::analogfilter
{
// One user resonance, mapped per model. 0..1 is the knob's travel derived from
// the Q parameter (see resonanceAmountFromQ); the top few per cent of travel is
// where the analog models self-oscillate.
inline constexpr float kMinUserQ = 0.25f;
inline constexpr float kMaxUserQ = 20.0f;

inline float resonanceAmountFromQ(float q)
{
    const auto clamped = std::clamp(q, kMinUserQ, kMaxUserQ);
    return std::log(clamped / kMinUserQ) / std::log(kMaxUserQ / kMinUserQ);
}

inline float ladderFeedback(float amount) { return 4.4f * std::pow(std::clamp(amount, 0.0f, 1.0f), 1.5f); }
inline float curtisFeedback(float amount) { return 4.3f * std::pow(std::clamp(amount, 0.0f, 1.0f), 1.4f); }
inline float arpDamping(float amount) { return 1.0f - 1.04f * std::pow(std::clamp(amount, 0.0f, 1.0f), 1.2f); }

inline float prewarp(float cutoffHz, double sampleRate)
{
    const auto nyquistSafe = std::clamp(static_cast<double>(cutoffHz), 10.0, sampleRate * 0.49);
    return static_cast<float>(std::tan(juce::MathConstants<double>::pi * nyquistSafe / sampleRate));
}

class Ladder
{
public:
    void reset() noexcept { s = { 0.0f, 0.0f, 0.0f, 0.0f }; }

    // poles: 2 or 4. drive: 1 = clean input level.
    void set(float cutoffHz, float amount, double sampleRate, int polesIn, float driveIn = 1.0f) noexcept
    {
        g = prewarp(cutoffHz, sampleRate);
        G = g / (1.0f + g);
        k = ladderFeedback(amount);
        poles = polesIn;
        drive = driveIn;
    }

    float process(float x) noexcept
    {
        // Each TPT one-pole is y = G*u + S with S = s * (1 - G) / ... ; written
        // out, y = G*u + beta where beta = s / (1 + g).
        const auto b0 = s[0] / (1.0f + g);
        const auto b1 = s[1] / (1.0f + g);
        const auto b2 = s[2] / (1.0f + g);
        const auto b3 = s[3] / (1.0f + g);
        const auto G2 = G * G;
        const auto G4 = G2 * G2;
        const auto sigma = G2 * G * b0 + G2 * b1 + G * b2 + b3;

        // Linear instantaneous solve of the loop, then the input stage saturates.
        const auto compensated = x * drive * (1.0f + 0.45f * k);
        const auto y4 = (G4 * compensated + sigma) / (1.0f + k * G4);
        const auto u = std::tanh(compensated - k * y4);

        auto v = stage(0, u);
        v = stage(1, v);
        const auto twoPole = v;
        v = stage(2, v);
        v = stage(3, v);
        return (poles == 2 ? twoPole : v) / std::max(1.0f, drive * 0.85f);
    }

    // Linear response for the display: the TPT one-pole is exactly the analog
    // one-pole at the prewarped frequency.
    static float magnitudeDb(double hz, float cutoffHz, float amount, double sampleRate, int poles)
    {
        const auto s = std::complex<double>(0.0, std::tan(juce::MathConstants<double>::pi * hz / sampleRate)
                                                       / std::max(1.0e-9, static_cast<double>(prewarp(cutoffHz, sampleRate))));
        const auto p = 1.0 / (1.0 + s);
        const auto kk = static_cast<double>(ladderFeedback(amount));
        const auto loop = 1.0 + kk * std::pow(p, 4);
        const auto h = (1.0 + 0.45 * kk) * std::pow(p, poles) / loop;
        return static_cast<float>(20.0 * std::log10(std::max(1.0e-9, std::abs(h))));
    }

private:
    float stage(int i, float in) noexcept
    {
        const auto v = (in - s[static_cast<std::size_t>(i)]) * G;
        const auto y = v + s[static_cast<std::size_t>(i)];
        s[static_cast<std::size_t>(i)] = y + v;
        return y;
    }

    std::array<float, 4> s { { 0.0f, 0.0f, 0.0f, 0.0f } };
    float g { 0.1f }, G { 0.09f }, k { 0.0f }, drive { 1.0f };
    int poles { 4 };
};

class Curtis
{
public:
    void reset() noexcept { s = { 0.0f, 0.0f, 0.0f, 0.0f }; }

    void set(float cutoffHz, float amount, double sampleRate) noexcept
    {
        g = prewarp(cutoffHz, sampleRate);
        G = g / (1.0f + g);
        k = curtisFeedback(amount);
    }

    float process(float x) noexcept
    {
        const auto G2 = G * G;
        const auto G4 = G2 * G2;
        const auto sigma = G2 * G * s[0] / (1.0f + g) + G2 * s[1] / (1.0f + g) + G * s[2] / (1.0f + g) + s[3] / (1.0f + g);
        // Full bass compensation: the 3320's response keeps its low end as the
        // resonance rises, where the ladder thins out.
        const auto compensated = x * (1.0f + k);
        const auto y4 = (G4 * compensated + sigma) / (1.0f + k * G4);
        auto v = compensated - k * y4;
        // Every OTA stage saturates gently on its own.
        for (std::size_t i = 0; i < 4; ++i)
        {
            const auto in = 1.4f * std::tanh(v * (1.0f / 1.4f));
            const auto w = (in - s[i]) * G;
            v = w + s[i];
            s[i] = v + w;
        }
        return v;
    }

    static float magnitudeDb(double hz, float cutoffHz, float amount, double sampleRate)
    {
        const auto s = std::complex<double>(0.0, std::tan(juce::MathConstants<double>::pi * hz / sampleRate)
                                                       / std::max(1.0e-9, static_cast<double>(prewarp(cutoffHz, sampleRate))));
        const auto p4 = std::pow(1.0 / (1.0 + s), 4);
        const auto kk = static_cast<double>(curtisFeedback(amount));
        const auto h = (1.0 + kk) * p4 / (1.0 + kk * p4);
        return static_cast<float>(20.0 * std::log10(std::max(1.0e-9, std::abs(h))));
    }

private:
    std::array<float, 4> s { { 0.0f, 0.0f, 0.0f, 0.0f } };
    float g { 0.1f }, G { 0.09f }, k { 0.0f };
};

class Arp
{
public:
    void reset() noexcept { s1 = s2 = 0.0f; }

    void set(float cutoffHz, float amount, double sampleRate) noexcept
    {
        g = prewarp(cutoffHz, sampleRate);
        R = arpDamping(amount);
    }

    float process(float x) noexcept
    {
        // Input stage pushed a little hot: this filter is meant to bite.
        const auto in = 1.25f * std::tanh(x * 0.9f);
        // Damping grows with amplitude: the (s1 - tanh s1) term is cubic for
        // small signals, so negative damping at the top of the knob builds a
        // scream that settles into a stable limit cycle rather than diverging,
        // and loud signals get the squeezed, aggressive response.
        const auto damping = R * s1 + 0.5f * (s1 - std::tanh(s1));
        const auto h = 1.0f / (1.0f + g * g + 2.0f * std::max(R, 0.0f) * g);
        const auto hp = (in - 2.0f * damping - g * s1 - s2) * h;
        const auto v1 = g * hp;
        const auto bp = v1 + s1;
        s1 = bp + v1;
        const auto v2 = g * bp;
        const auto lp = v2 + s2;
        s2 = lp + v2;
        s1 = std::clamp(s1, -8.0f, 8.0f);
        s2 = std::clamp(s2, -8.0f, 8.0f);
        return lp;
    }

    static float magnitudeDb(double hz, float cutoffHz, float amount, double sampleRate)
    {
        const auto s = std::complex<double>(0.0, std::tan(juce::MathConstants<double>::pi * hz / sampleRate)
                                                       / std::max(1.0e-9, static_cast<double>(prewarp(cutoffHz, sampleRate))));
        const auto r = std::max(0.02, static_cast<double>(arpDamping(amount)));
        const auto h = 1.25 * 0.9 / (s * s + 2.0 * r * s + 1.0);
        return static_cast<float>(20.0 * std::log10(std::max(1.0e-9, std::abs(h))));
    }

private:
    float s1 { 0.0f }, s2 { 0.0f }, g { 0.1f }, R { 1.0f };
};
}
