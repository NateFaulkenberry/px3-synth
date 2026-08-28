#include "CombResonator.h"

#include <cmath>

namespace px3
{
namespace
{
// How fast the smoothed loop parameters chase their targets, in seconds. Short
// enough to feel immediate, long enough that a modulated Tune sweeps rather
// than steps.
constexpr float kSmoothingSeconds = 0.02f;

// Guard band at each end of the delay line. The interpolator reads one sample
// behind and two ahead of the write head, and a delay of zero would read the
// sample being written this instant - so the shortest usable delay is offset
// away from zero, as the extended Karplus-Strong literature recommends.
constexpr float kMinDelaySamples = 4.0f;
constexpr int kInterpolationMargin = 4;

float softSaturate(float x) noexcept
{
    // tanh-like, without the call. Bounded by +/-1 whatever the input, which is
    // what keeps the loop from running away when the feedback gain approaches
    // unity.
    return x / (1.0f + std::abs(x));
}

float sanitize(float x) noexcept
{
    return std::isfinite(x) ? x : 0.0f;
}
} // namespace

void CombResonator::prepare(double sampleRate)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    // Sized from the lowest tuning and the ACTUAL sample rate, not a worst-case
    // one. The synth holds 512 of these, so paying for 96 kHz while running at
    // 48 would double the resonator's whole memory footprint for nothing.
    const auto longest = static_cast<int>(std::ceil(currentSampleRate / kMinTuneHz));
    lineSize = juce::jmax(64, longest + kInterpolationMargin);

    line.assign(static_cast<std::size_t>(lineSize), 0.0f);
    writePos = 0;

    smoothingCoeff = 1.0f - std::exp(-1.0f / static_cast<float>(kSmoothingSeconds * currentSampleRate));

    reset();
}

void CombResonator::reset()
{
    std::fill(line.begin(), line.end(), 0.0f);
    writePos = 0;
    dampingState = 0.0f;
    dispersionStateX = { 0.0f, 0.0f };
    dispersionStateY = { 0.0f, 0.0f };
}

void CombResonator::updateLoopCoefficients()
{
    const auto tune = juce::jlimit(kMinTuneHz, kMaxTuneHz, target.tuneHz);

    // Tune is a frequency; the delay line's length is its period. Everything
    // downstream - the loop gain, the damping correction - is derived from this
    // one number, which is why tuning stays correct across sample rates.
    const auto periodSamples = static_cast<float>(currentSampleRate) / tune;
    const auto longest = static_cast<float>(lineSize - kInterpolationMargin);
    targetDelaySamples = juce::jlimit(kMinDelaySamples, longest, periodSamples);

    // Jot's rule: a loop of length D seconds that must fall 60 dB in T60
    // seconds needs a per-pass gain of 10^(-3 D / T60). Expressed this way the
    // decay time means the same thing at every pitch, which a raw feedback
    // control cannot do - the loop simply runs more often at higher tunings.
    const auto decay = juce::jlimit(kMinDecaySeconds, kMaxDecaySeconds, target.decaySeconds);
    const auto loopSeconds = targetDelaySamples / static_cast<float>(currentSampleRate);
    auto gain = std::pow(10.0f, -3.0f * loopSeconds / decay);

    // The damping filter is normalised to unity DC gain, so it takes nothing
    // from the fundamental and the loop gain above needs no correction for it.
    // Damping changes the tail's colour, not the time the fundamental takes to
    // decay - which is what makes the two controls independent.
    const auto damping = juce::jlimit(0.0f, 1.0f, target.damping);
    targetDampingCoeff = damping * 0.92f;

    // Ceiling below unity, and further limited at short delays.
    //
    // Decay expressed as a time asks for an enormous gain when the loop is
    // short: a 6-sample loop at 12 seconds works out to 0.99996, which is a
    // near-lossless integrator. It does not diverge on its own, but any
    // sustained input accumulates in it. Capping the gain caps how long the
    // shortest loops can ring, which is the honest trade - the alternative is a
    // resonator that is only conditionally stable.
    targetFeedbackGain = juce::jlimit(0.0f, 0.995f, gain);

    // Dispersion: an allpass pair in the loop delays high frequencies more than
    // low ones, so the partials stop being integer multiples of the
    // fundamental. That is what turns a string into a bell.
    targetDispersionCoeff = juce::jlimit(0.0f, 1.0f, target.dispersion) * 0.72f;

    targetDriveAmount = juce::jlimit(0.0f, 1.0f, target.drive);
    targetMixAmount = juce::jlimit(0.0f, 1.0f, target.mix);
    targetPolaritySign = target.invertPolarity ? -1.0f : 1.0f;
}

void CombResonator::setTargetSettings(const CombSettings& settings)
{
    target = settings;
    updateLoopCoefficients();
}

void CombResonator::setCurrentSettingsImmediate(const CombSettings& settings)
{
    setTargetSettings(settings);

    delaySamples = targetDelaySamples;
    feedbackGain = targetFeedbackGain;
    dampingCoeff = targetDampingCoeff;
    dispersionCoeff = targetDispersionCoeff;
    driveAmount = targetDriveAmount;
    mixAmount = targetMixAmount;
    polaritySign = targetPolaritySign;
}

float CombResonator::readDelay(float delayInSamples) const
{
    // Four-point Catmull-Rom, the same interpolator the delay effect uses.
    //
    // Non-recursive, which is the deciding property: an allpass fractional
    // delay is lossless and would suit a feedback loop better on paper, but it
    // carries state, so changing the delay length produces a transient. Tune is
    // a modulation destination here, so that transient would fire on every
    // sweep.
    auto readPos = static_cast<float>(writePos) - delayInSamples;
    const auto size = static_cast<float>(lineSize);
    while (readPos < 0.0f)
    {
        readPos += size;
    }
    if (readPos >= size)
    {
        readPos -= size * std::floor(readPos / size);
    }

    const auto i1 = static_cast<int>(readPos);
    const auto frac = readPos - static_cast<float>(i1);
    const auto i0 = (i1 - 1 + lineSize) % lineSize;
    const auto i2 = (i1 + 1) % lineSize;
    const auto i3 = (i1 + 2) % lineSize;

    const auto y0 = line[static_cast<std::size_t>(i0)];
    const auto y1 = line[static_cast<std::size_t>(i1)];
    const auto y2 = line[static_cast<std::size_t>(i2)];
    const auto y3 = line[static_cast<std::size_t>(i3)];

    const auto a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const auto a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto a2 = -0.5f * y0 + 0.5f * y2;

    return ((a0 * frac + a1) * frac + a2) * frac + y1;
}

float CombResonator::processSample(float inputSample)
{
    if (lineSize <= 0)
    {
        return inputSample;
    }

    const auto in = sanitize(inputSample);

    // Per-sample smoothing on everything that moves. Delay length above all:
    // stepping it would move the read pointer to unrelated samples, which is a
    // click rather than a pitch change.
    delaySamples += (targetDelaySamples - delaySamples) * smoothingCoeff;
    feedbackGain += (targetFeedbackGain - feedbackGain) * smoothingCoeff;
    dampingCoeff += (targetDampingCoeff - dampingCoeff) * smoothingCoeff;
    dispersionCoeff += (targetDispersionCoeff - dispersionCoeff) * smoothingCoeff;
    driveAmount += (targetDriveAmount - driveAmount) * smoothingCoeff;
    mixAmount += (targetMixAmount - mixAmount) * smoothingCoeff;
    polaritySign += (targetPolaritySign - polaritySign) * smoothingCoeff;

    const auto delayed = sanitize(readDelay(delaySamples));

    // ---- loop: damping -> dispersion -> saturation -> gain ----------------
    // One-pole lowpass, normalised to unity DC gain so damping darkens the
    // resonance without also quietening it.
    dampingState = delayed * (1.0f - dampingCoeff) + dampingState * dampingCoeff;
    auto loop = sanitize(dampingState);

    if (dispersionCoeff > 0.0001f)
    {
        // Two first-order allpasses. A real stiff string needs a far higher
        // order to place its partials correctly; two is enough to bend them off
        // the harmonic series in a controllable way, which is what is wanted
        // from a synth control rather than a piano model.
        for (std::size_t stage = 0; stage < dispersionStateX.size(); ++stage)
        {
            const auto x = loop;
            const auto y = -dispersionCoeff * x + dispersionStateX[stage]
                           + dispersionCoeff * dispersionStateY[stage];
            dispersionStateX[stage] = x;
            dispersionStateY[stage] = y;
            loop = y;
        }
        loop = sanitize(loop);
    }

    // The loop is ALWAYS taken through the saturator, not only when Drive is up.
    //
    // Drive decides how hard it is pushed and therefore how much character it
    // adds, but the bounding itself is not optional: it is the only thing that
    // stops a long decay at a short delay from accumulating without limit, and
    // a resonator that is stable only for some settings is not stable.
    //
    // At Drive 0 the input is scaled down before the nonlinearity and back up
    // after, so the stage is nearly transparent for normal levels and still
    // bounded for abnormal ones.
    {
        // Scaled into the nonlinearity and back out again. A small scale keeps
        // the curve's near-linear region wide, so at Drive 0 the stage passes
        // normal levels essentially unchanged while still clamping the loop to
        // 1/scale. Raising Drive narrows that region, which is the character.
        const auto scale = 0.25f + driveAmount * 4.0f;
        const auto makeup = 1.0f + driveAmount * 0.6f;
        loop = sanitize(softSaturate(loop * scale) / scale * makeup);
    }

    const auto feedback = loop * feedbackGain * polaritySign;

    line[static_cast<std::size_t>(writePos)] = sanitize(in + feedback);
    writePos = (writePos + 1) % lineSize;

    return in * (1.0f - mixAmount) + delayed * mixAmount;
}

} // namespace px3
