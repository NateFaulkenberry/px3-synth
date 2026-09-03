#include "ParametricEQ.h"

#include <cmath>

namespace px3
{
namespace
{
float sanitize(float v)
{
    return std::isfinite(v) ? v : 0.0f;
}
} // namespace

void ParametricEQ::prepare(double sampleRate)
{
    sampleRateHz = juce::jmax(1000.0, sampleRate);

    // ~8 ms, the same order the delay and filter controls use. Fast enough that
    // a knob feels attached to the sound, slow enough that a jump between two
    // preset values is a glide rather than a step.
    constexpr double tauSeconds = 0.008;
    smoothingCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRateHz * tauSeconds)));

    reset();

    // Force a rebuild: the coefficients cached against the old rate are wrong.
    for (auto& band : bands)
    {
        band.builtFrequency = -1.0f;
        band.builtQ = -1.0f;
    }
    primed = false;
}

void ParametricEQ::reset()
{
    for (auto& band : bands)
    {
        band.z1 = { { 0.0f, 0.0f } };
        band.z2 = { { 0.0f, 0.0f } };
    }
}

void ParametricEQ::setSettings(const EqSettings& settings)
{
    target = settings;

    for (auto& band : target.bands)
    {
        band.frequencyHz = juce::jlimit(kMinFrequencyHz, kMaxFrequencyHz, band.frequencyHz);
        band.gainDb = juce::jlimit(kMinGainDb, kMaxGainDb, band.gainDb);
        band.q = juce::jlimit(kMinQ, kMaxQ, band.q);
    }

    if (! primed)
    {
        smoothed = target.bands;
        primed = true;
    }
}

// Every band whose gain is zero AND whose type cannot change the response
// without gain - a bell or a shelf at 0 dB is a wire - is skipped entirely.
// A pass filter always does something, so it never counts as identity.
void ParametricEQ::updateBand(int index)
{
    const auto i = static_cast<std::size_t>(index);
    auto& band = bands[i];
    const auto& s = smoothed[i];

    const auto gainless = s.type == EqBandType::lowShelf
                          || s.type == EqBandType::bell
                          || s.type == EqBandType::highShelf;

    if (gainless && std::abs(s.gainDb) < 0.01f)
    {
        band.identity = true;
        band.builtType = s.type;
        band.builtGainDb = s.gainDb;
        band.builtFrequency = s.frequencyHz;
        band.builtQ = s.q;
        return;
    }

    // Rebuild only when something moved. A biquad's coefficients are a few
    // transcendentals; doing that per sample per band per channel would be the
    // most expensive thing on the bus by a wide margin.
    if (! band.identity
        && band.builtType == s.type
        && std::abs(band.builtFrequency - s.frequencyHz) < 0.01f
        && std::abs(band.builtGainDb - s.gainDb) < 0.001f
        && std::abs(band.builtQ - s.q) < 0.0005f)
    {
        return;
    }

    band.identity = false;
    band.builtType = s.type;
    band.builtFrequency = s.frequencyHz;
    band.builtGainDb = s.gainDb;
    band.builtQ = s.q;

    // Clamped below Nyquist as VoiceFilter does: the bilinear transform warps
    // frequency, and a corner at or above Nyquist has nowhere to go.
    const auto nyquist = static_cast<float>(sampleRateHz * 0.5);
    const auto f0 = juce::jlimit(kMinFrequencyHz, nyquist * 0.98f, s.frequencyHz);
    const auto w0 = juce::MathConstants<float>::twoPi * f0 / static_cast<float>(sampleRateHz);
    const auto cosw0 = std::cos(w0);
    const auto sinw0 = std::sin(w0);
    const auto q = juce::jmax(0.05f, s.q);

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a0 = 1.0f, a1 = 0.0f, a2 = 0.0f;

    switch (s.type)
    {
        case EqBandType::bell:
        {
            // RBJ peaking. A is the square root of the linear gain because the
            // cookbook's bandwidth is defined at the HALF-gain point - which is
            // exactly what makes this proportional-Q.
            const auto A = std::pow(10.0f, s.gainDb / 40.0f);
            const auto alpha = sinw0 / (2.0f * q);
            b0 = 1.0f + alpha * A;
            b1 = -2.0f * cosw0;
            b2 = 1.0f - alpha * A;
            a0 = 1.0f + alpha / A;
            a1 = -2.0f * cosw0;
            a2 = 1.0f - alpha / A;
            break;
        }
        case EqBandType::lowShelf:
        case EqBandType::highShelf:
        {
            const auto A = std::pow(10.0f, s.gainDb / 40.0f);
            // S is the shelf slope. Held at or below 1: S = 1 is the steepest
            // slope that stays monotonic, and beyond it the response dips
            // before the shelf.
            const auto S = juce::jlimit(0.3f, 1.0f, q);
            const auto alpha = sinw0 * 0.5f
                               * std::sqrt(juce::jmax(0.0f, (A + 1.0f / A) * (1.0f / S - 1.0f) + 2.0f));
            const auto twoSqrtAAlpha = 2.0f * std::sqrt(A) * alpha;

            if (s.type == EqBandType::lowShelf)
            {
                b0 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 + twoSqrtAAlpha);
                b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0);
                b2 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 - twoSqrtAAlpha);
                a0 = (A + 1.0f) + (A - 1.0f) * cosw0 + twoSqrtAAlpha;
                a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0);
                a2 = (A + 1.0f) + (A - 1.0f) * cosw0 - twoSqrtAAlpha;
            }
            else
            {
                b0 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 + twoSqrtAAlpha);
                b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0);
                b2 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 - twoSqrtAAlpha);
                a0 = (A + 1.0f) - (A - 1.0f) * cosw0 + twoSqrtAAlpha;
                a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0);
                a2 = (A + 1.0f) - (A - 1.0f) * cosw0 - twoSqrtAAlpha;
            }
            break;
        }
        case EqBandType::highPass:
        {
            const auto alpha = sinw0 / (2.0f * q);
            b0 = (1.0f + cosw0) * 0.5f;
            b1 = -(1.0f + cosw0);
            b2 = (1.0f + cosw0) * 0.5f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cosw0;
            a2 = 1.0f - alpha;
            break;
        }
        case EqBandType::lowPass:
        {
            const auto alpha = sinw0 / (2.0f * q);
            b0 = (1.0f - cosw0) * 0.5f;
            b1 = 1.0f - cosw0;
            b2 = (1.0f - cosw0) * 0.5f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cosw0;
            a2 = 1.0f - alpha;
            break;
        }
    }

    const auto inv = 1.0f / (std::abs(a0) > 1.0e-9f ? a0 : 1.0e-9f);
    band.coeffs = { { b0 * inv, b1 * inv, b2 * inv, a1 * inv, a2 * inv } };
}

float ParametricEQ::processBandSample(Band& band, int channel, float x)
{
    const auto c = static_cast<std::size_t>(channel);
    const auto& k = band.coeffs;

    // Transposed direct form II: one state pair per channel, and better
    // numerical behaviour at low frequencies than DF-I with float state, which
    // matters for a 30 Hz shelf.
    const auto y = k[0] * x + band.z1[c];
    band.z1[c] = k[1] * x - k[3] * y + band.z2[c];
    band.z2[c] = k[2] * x - k[4] * y;
    return y;
}

void ParametricEQ::processSample(float& left, float& right)
{
    if (! target.enabled)
    {
        return;
    }

    // Parameters first, coefficients from the smoothed values second.
    anyBandActive = false;
    for (int i = 0; i < kEqBandCount; ++i)
    {
        const auto idx = static_cast<std::size_t>(i);
        auto& s = smoothed[idx];
        const auto& t = target.bands[idx];

        s.frequencyHz += (t.frequencyHz - s.frequencyHz) * smoothingCoeff;
        s.gainDb += (t.gainDb - s.gainDb) * smoothingCoeff;
        s.q += (t.q - s.q) * smoothingCoeff;
        // A type change is a different filter, not a value to glide toward.
        // Taken immediately; the state carries over, which is a far smaller
        // discontinuity than crossfading two filters would be.
        s.type = t.type;

        updateBand(i);
        if (! bands[idx].identity)
        {
            anyBandActive = true;
        }
    }

    if (! anyBandActive)
    {
        return;
    }

    auto l = left;
    auto r = right;
    for (int i = 0; i < kEqBandCount; ++i)
    {
        auto& band = bands[static_cast<std::size_t>(i)];
        if (band.identity)
        {
            continue;
        }
        l = processBandSample(band, 0, l);
        r = processBandSample(band, 1, r);
    }

    left = sanitize(l);
    right = sanitize(r);
}

float ParametricEQ::magnitudeDb(double frequencyHz) const
{
    const auto hz = juce::jlimit(1.0, sampleRateHz * 0.5 - 1.0, frequencyHz);
    auto magnitude = 1.0;

    for (const auto& band : bands)
    {
        if (band.identity)
        {
            continue;
        }

        juce::dsp::IIR::Coefficients<float> c { band.coeffs[0], band.coeffs[1], band.coeffs[2],
                                                1.0f, band.coeffs[3], band.coeffs[4] };
        magnitude *= c.getMagnitudeForFrequency(hz, sampleRateHz);
    }

    return juce::Decibels::gainToDecibels(static_cast<float>(magnitude), -96.0f);
}

} // namespace px3
