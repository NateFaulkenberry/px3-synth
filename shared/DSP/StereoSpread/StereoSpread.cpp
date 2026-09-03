#include "StereoSpread.h"

#include <algorithm>
#include <cmath>

namespace px3
{
namespace
{
// Allpass coefficients. The two channels get DIFFERENT sets, which is what
// makes them diverge in phase while each stays magnitude-flat. Spread across
// the range rather than clustered, so the divergence is gradual with frequency
// - a set of similar coefficients would behave like a delay, and a delay is
// what combs.
constexpr std::array<float, 8> kAllpassLeft  { { 0.6512f, -0.5347f, 0.4189f, -0.3211f,
                                                 0.2704f, -0.1893f, 0.1421f, -0.0977f } };
constexpr std::array<float, 8> kAllpassRight { { -0.5891f, 0.4736f, -0.3822f, 0.2988f,
                                                 -0.2311f, 0.1642f, -0.1108f, 0.0764f } };
} // namespace

int StereoSpread::modeCount()
{
    return 4;
}

StereoSpread::ModeSpec StereoSpread::specFor(int modeIndex)
{
    switch (juce::jlimit(0, 3, modeIndex))
    {
        case 1:  return { 6.50f, 6, 0.85f, -0.25f, 0.09f };   // WIDE
        case 2:  return { 5.20f, 8, 1.00f, -0.15f, 0.04f };   // DEEP
        case 3:  return { 1.10f, 3, 1.60f,  0.10f, 0.06f };   // MONO SAFE
        default: return { 4.40f, 4, 1.00f, -0.10f, 0.07f };   // CLASSIC
    }
}

// ============================================================================
// helpers
// ============================================================================

float StereoSpread::sanitize(float v)
{
    if (! std::isfinite(v))
    {
        return 0.0f;
    }
    return juce::jlimit(-8.0f, 8.0f, v);
}

float StereoSpread::onePoleCoeff(float hz, float rate)
{
    if (rate <= 0.0f || hz <= 0.0f)
    {
        return 0.0f;
    }
    return juce::jlimit(0.0f, 0.999f, 1.0f - std::exp(-juce::MathConstants<float>::twoPi * hz / rate));
}

// ============================================================================
// lifecycle
// ============================================================================

void StereoSpread::prepare(double sampleRate)
{
    sampleRateHz = sampleRate > 0.0 ? sampleRate : 44100.0;

    const auto rampSeconds = 0.03;
    for (auto* smoother : { &enabledSmoothed, &amountSmoothed, &widthSmoothed, &depthSmoothed,
                            &centerSmoothed, &lowWidthSmoothed, &highWidthSmoothed,
                            &lowFreqSmoothed, &highFreqSmoothed, &toneSmoothed, &mixSmoothed })
    {
        smoother->reset(sampleRateHz, rampSeconds);
    }

    reset();
}

void StereoSpread::reset()
{
    lowSplit = {};
    highSplit = {};

    for (auto& channel : allpassState)
    {
        channel.fill(0.0f);
    }

    modPhase = 0.0f;
    correlation = 1.0f;
    sumLR = 0.0f;
    sumLL = 0.0f;
    sumRR = 0.0f;
    guardGain = 1.0f;
    toneState = { { 0.0f, 0.0f } };

    for (auto* smoother : { &enabledSmoothed, &amountSmoothed, &widthSmoothed, &depthSmoothed,
                            &centerSmoothed, &lowWidthSmoothed, &highWidthSmoothed,
                            &lowFreqSmoothed, &highFreqSmoothed, &toneSmoothed, &mixSmoothed })
    {
        smoother->setCurrentAndTargetValue(smoother->getTargetValue());
    }
}

void StereoSpread::updateForBlock(const StereoSpreadSettings& next)
{
    settings = next;

    enabledSmoothed.setTargetValue(settings.enabled ? 1.0f : 0.0f);
    amountSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.amount));
    widthSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.width));
    depthSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.depth));
    centerSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.center));
    lowWidthSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.lowWidth));
    highWidthSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.highWidth));
    lowFreqSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.lowFreq));
    highFreqSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.highFreq));
    toneSmoothed.setTargetValue(juce::jlimit(-1.0f, 1.0f, settings.tone));
    mixSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.mix));
}

// ============================================================================
// crossover
// ============================================================================

StereoSpread::Bands StereoSpread::split(float inL, float inR)
{
    const auto spec = specFor(settings.modeIndex);

    const auto lowHz = juce::jlimit(30.0f, 600.0f,
                                    juce::jmap(lowFreqSmoothed.getNextValue(), 60.0f, 400.0f)
                                        * spec.lowFreqScale);
    const auto highHz = juce::jlimit(600.0f,
                                     static_cast<float>(sampleRateHz) * 0.40f,
                                     juce::jmap(highFreqSmoothed.getNextValue(), 1000.0f, 8000.0f));

    // Four cascaded one-poles per split: 24 dB/octave, and the low and high
    // outputs sum flat in magnitude because the high band is defined as what
    // the low band is not.
    //
    // Cascading identical stages moves the -3 dB point DOWN - for N stages it
    // lands at fc * sqrt(2^(1/N) - 1), which for four stages is 0.435 * fc. So
    // each stage is set 1/0.435 higher than the crossover being asked for.
    // Without this the low band is far narrower than its label, and the bass
    // that leaks past it is bass being widened.
    constexpr auto kCascadeCompensation = 1.0f / 0.4350f;
    const auto lowCoeff = onePoleCoeff(lowHz * kCascadeCompensation, static_cast<float>(sampleRateHz));
    const auto highCoeff = onePoleCoeff(highHz * kCascadeCompensation, static_cast<float>(sampleRateHz));

    Bands bands {};
    const std::array<float, 2> input { { inL, inR } };

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto c = static_cast<std::size_t>(ch);
        const auto x = input[c];

        lowSplit.s1[c] += (x - lowSplit.s1[c]) * lowCoeff;
        lowSplit.s2[c] += (lowSplit.s1[c] - lowSplit.s2[c]) * lowCoeff;
        lowSplit.s3[c] += (lowSplit.s2[c] - lowSplit.s3[c]) * lowCoeff;
        lowSplit.s4[c] += (lowSplit.s3[c] - lowSplit.s4[c]) * lowCoeff;
        const auto low = lowSplit.s4[c];
        const auto aboveLow = x - low;

        highSplit.s1[c] += (aboveLow - highSplit.s1[c]) * highCoeff;
        highSplit.s2[c] += (highSplit.s1[c] - highSplit.s2[c]) * highCoeff;
        highSplit.s3[c] += (highSplit.s2[c] - highSplit.s3[c]) * highCoeff;
        highSplit.s4[c] += (highSplit.s3[c] - highSplit.s4[c]) * highCoeff;
        const auto mid = highSplit.s4[c];
        const auto high = aboveLow - mid;

        (ch == 0 ? bands.low.l : bands.low.r) = low;
        (ch == 0 ? bands.mid.l : bands.mid.r) = mid;
        (ch == 0 ? bands.high.l : bands.high.r) = high;
    }

    return bands;
}

// ============================================================================
// allpass decorrelation
// ============================================================================

float StereoSpread::allpassChain(int channel, float x, int sections, float spreadAmount)
{
    const auto ch = static_cast<std::size_t>(juce::jlimit(0, 1, channel));
    const auto& coefficients = ch == 0 ? kAllpassLeft : kAllpassRight;

    auto y = x;
    for (int i = 0; i < juce::jlimit(0, kMaxAllpass, sections); ++i)
    {
        const auto idx = static_cast<std::size_t>(i);

        // First-order allpass: unity magnitude at every frequency, so the
        // channel is never coloured - only its phase moves, and only relative
        // to the other channel.
        const auto a = coefficients[idx] * spreadAmount;
        const auto state = allpassState[ch][idx];
        const auto out = a * y + state;
        allpassState[ch][idx] = sanitize(y - a * out);
        y = out;
    }

    return sanitize(y);
}

// ============================================================================
// host-rate entry point
// ============================================================================

void StereoSpread::processSampleFrame(float inL, float inR, float& outL, float& outR)
{
    const auto enabled = enabledSmoothed.getNextValue();
    const auto amount = amountSmoothed.getNextValue();
    const auto mix = mixSmoothed.getNextValue();

    if (amount * enabled * mix <= 1.0e-6f
        && ! amountSmoothed.isSmoothing() && ! enabledSmoothed.isSmoothing()
        && ! mixSmoothed.isSmoothing())
    {
        if (! idle)
        {
            idle = true;
            reset();
        }
        outL = inL;
        outR = inR;
        return;
    }
    idle = false;

    const auto spec = specFor(settings.modeIndex);

    const auto width = widthSmoothed.getNextValue();
    const auto depth = depthSmoothed.getNextValue();
    const auto centre = centerSmoothed.getNextValue();
    const auto lowWidth = lowWidthSmoothed.getNextValue();
    const auto highWidth = highWidthSmoothed.getNextValue();
    const auto tone = toneSmoothed.getNextValue();

    const auto bands = split(inL, inR);

    // ---- LOW: summed to mono ------------------------------------------------
    // There is no width available at a wavelength longer than any room's stereo
    // geometry, and side energy down here is exactly what destroys mono
    // compatibility and headroom. A bass patch keeps its weight because its
    // fundamental is never widened at all.
    // Every band gain below starts from UNITY at amount = 0, so the bands
    // recombine to exactly the input. Scaling them by amount directly would
    // make amount = 0 a hard mono collapse rather than a bypass - the band
    // split sums flat, but only if nothing has been done to the parts.
    const auto lowMono = 0.5f * (bands.low.l + bands.low.r);
    const auto lowSide = 0.5f * (bands.low.l - bands.low.r)
                         * juce::jmap(amount, 1.0f, lowWidth);
    Frame low { lowMono + lowSide, lowMono - lowSide };

    // ---- MID: allpass decorrelation ----------------------------------------
    // The mechanism. Two channels made to differ in PHASE while each stays
    // magnitude-flat - which works on a mono source, where M/S gain has nothing
    // to amplify.
    // Fixed per mode. DEPTH varies the coefficient scaling below, which is
    // continuous; varying the number of sections would change the path length
    // in one sample, and that is a click rather than a deepening.
    const auto sections = juce::jlimit(1, kMaxAllpass, spec.allpassSections);

    // Very slow, correlated modulation of the allpass spread. Correlated
    // between the channels, so the image breathes rather than wandering.
    modPhase += spec.modulationRate / static_cast<float>(sampleRateHz);
    modPhase -= std::floor(modPhase);
    const auto wobble = 1.0f + 0.06f * std::sin(juce::MathConstants<float>::twoPi * modPhase);

    const auto spreadAmount = juce::jlimit(0.0f, 0.92f, (0.35f + depth * 0.55f) * amount * wobble);

    // Mono sources have no side content, so the decorrelated copy is built from
    // the band's own mono sum. On a stereo source the existing image is
    // preserved and the decorrelation adds to it.
    const auto midMono = 0.5f * (bands.mid.l + bands.mid.r);
    const auto decorL = allpassChain(0, midMono, sections, spreadAmount);
    const auto decorR = allpassChain(1, midMono, sections, spreadAmount);

    // The DIFFERENCE between the two decorrelated copies is pure side content
    // that did not exist before. Adding it symmetrically keeps the mid intact.
    const auto createdSide = 0.5f * (decorL - decorR);

    // The existing image is preserved and scaled; the created side is ADDED on
    // top by amount. That is the difference between widening a signal and
    // replacing it with a decorrelated copy of itself.
    const auto midSideExisting = 0.5f * (bands.mid.l - bands.mid.r);
    const auto midSideKept = midSideExisting * juce::jmap(amount, 1.0f, width);
    auto midSideCreated = createdSide * spec.sideGain * width * amount;
    auto midSide = midSideKept + midSideCreated;
    const auto midMid = midMono * juce::jmap(amount, 1.0f, juce::jmap(centre, 0.75f, 1.0f));

    // ---- HIGH: allpass plus level ------------------------------------------
    // Above roughly 1.5 kHz a wavelength is shorter than the head, so phase is
    // ambiguous and the ear localises by LEVEL. A level difference is also
    // mono-safe by construction: summing two differently-scaled copies changes
    // level, never spectrum.
    const auto highMono = 0.5f * (bands.high.l + bands.high.r);
    const auto highDecorL = allpassChain(0, highMono, juce::jmax(1, sections / 2), spreadAmount * 0.7f);
    const auto highDecorR = allpassChain(1, highMono, juce::jmax(1, sections / 2), spreadAmount * 0.7f);

    const auto ildTrade = 0.5f * (highDecorL - highDecorR);
    const auto highSideExisting = 0.5f * (bands.high.l - bands.high.r);
    const auto highSideKept = highSideExisting * juce::jmap(amount, 1.0f, highWidth * width);
    auto highSideCreated = ildTrade * spec.sideGain * highWidth * width * amount;
    auto highSide = highSideKept + highSideCreated;

    // ---- side tone ---------------------------------------------------------
    // Tilts the SIDE only. Tilting the sum would be an EQ; tilting the side
    // changes where the width sits without moving the centre's tone.
    if (std::abs(tone) > 0.001f)
    {
        const auto toneCoeff = onePoleCoeff(900.0f, static_cast<float>(sampleRateHz));
        toneState[0] += (midSideCreated - toneState[0]) * toneCoeff;
        toneState[1] += (highSideCreated - toneState[1]) * toneCoeff;

        const auto lowGain = 1.0f - juce::jmax(0.0f, tone) * 0.7f;
        const auto highGain = 1.0f - juce::jmax(0.0f, -tone) * 0.7f;
        midSideCreated = toneState[0] * lowGain + (midSideCreated - toneState[0]) * highGain;
        highSideCreated = toneState[1] * lowGain + (highSideCreated - toneState[1]) * highGain;
        midSide = midSideKept + midSideCreated;
        highSide = highSideKept + highSideCreated;
    }

    // ---- correlation guard -------------------------------------------------
    // Not a limiter on the sound - a limiter on the MECHANISM. It acts only on
    // the side content this effect created, never on the input's own image:
    // guarding the whole side signal would mean a bypassed widener narrowing an
    // already-wide source, which is the opposite of its job.
    const auto candidateL = low.l + midMid + midSide + highMono + highSide;
    const auto candidateR = low.r + midMid - midSide + highMono - highSide;

    const auto followCoeff = onePoleCoeff(2.0f, static_cast<float>(sampleRateHz));
    sumLR += (candidateL * candidateR - sumLR) * followCoeff;
    sumLL += (candidateL * candidateL - sumLL) * followCoeff;
    sumRR += (candidateR * candidateR - sumRR) * followCoeff;

    const auto denom = std::sqrt(juce::jmax(1.0e-12f, sumLL * sumRR));
    correlation = denom > 1.0e-9f ? juce::jlimit(-1.0f, 1.0f, sumLR / denom) : 1.0f;

    const auto floorValue = spec.correlationFloor;
    const auto wantedGuard = correlation < floorValue
                                 ? juce::jlimit(0.25f, 1.0f, 1.0f - (floorValue - correlation) * 1.5f)
                                 : 1.0f;

    // Smoothed over hundreds of milliseconds, so it is a slow safety rather
    // than an audible pumping.
    const auto guardCoeff = onePoleCoeff(1.5f, static_cast<float>(sampleRateHz));
    guardGain += (wantedGuard - guardGain) * guardCoeff;

    // Scaled by amount as well, so at zero the guard cannot reach the signal
    // at all and the effect is exactly a bypass.
    const auto guard = juce::jmap(amount, 1.0f, guardGain);
    midSideCreated *= guard;
    highSideCreated *= guard;

    midSide = midSideKept + midSideCreated;
    highSide = highSideKept + highSideCreated;

    Frame processed { low.l + midMid + midSide + highMono + highSide,
                      low.r + midMid - midSide + highMono - highSide };

    const auto scale = enabled * mix;
    outL = sanitize(inL + (processed.l - inL) * scale);
    outR = sanitize(inR + (processed.r - inR) * scale);
}

} // namespace px3
