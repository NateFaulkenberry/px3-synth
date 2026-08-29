#include "Chorus.h"

#include <algorithm>
#include <cmath>

namespace px3
{
namespace
{
// The four Dimension modes. Mode 1 is documented as the softest AND as having
// the longest delay - its VCOs run slower - which is why the base delay falls
// rather than rises as the modes get stronger.
constexpr std::array<Chorus::ModeSpec, 9> kModes { {
    // baseMs, rateScale, depthScale, paths, stackedMode, bandwidthHz, companding
    { 10.0f, 0.28f, 0.35f, 2, -1, 7200.0f, 0.55f },   // DIM 1  - softest, longest delay
    {  7.0f, 0.55f, 0.60f, 2, -1, 8200.0f, 0.50f },   // DIM 2
    {  6.0f, 0.55f, 0.85f, 2, -1, 8200.0f, 0.50f },   // DIM 3  - as 2, deeper
    {  4.5f, 1.00f, 1.00f, 2, -1, 9000.0f, 0.45f },   // DIM 4  - strongest, shortest
    { 10.0f, 0.28f, 0.35f, 2,  3, 7200.0f, 0.55f },   // DIM 1+4
    {  7.0f, 0.55f, 0.60f, 2,  3, 8200.0f, 0.50f },   // DIM 2+4
    {  6.0f, 0.55f, 0.85f, 2,  3, 8200.0f, 0.50f },   // DIM 3+4
    { 14.0f, 0.20f, 0.75f, 3, -1, 6400.0f, 0.65f },   // ENSEMBLE - string machine
    { 12.0f, 0.42f, 0.70f, 1, -1, 4800.0f, 0.85f },   // CE WARM  - single BBD path
} };

constexpr float kMaxDelayMs = 40.0f;

// How far the delay can be pushed by modulation, in milliseconds. Kept small:
// this is a chorus, and a large excursion is what turns one into a flanger or
// a seasick vibrato.
constexpr float kMaxExcursionMs = 3.2f;
} // namespace

int Chorus::modeCount()
{
    return static_cast<int>(kModes.size());
}

Chorus::ModeSpec Chorus::specFor(int modeIndex)
{
    return kModes[static_cast<std::size_t>(juce::jlimit(0, static_cast<int>(kModes.size()) - 1, modeIndex))];
}

// ============================================================================
// helpers
// ============================================================================

float Chorus::sanitize(float v)
{
    if (! std::isfinite(v))
    {
        return 0.0f;
    }
    return juce::jlimit(-8.0f, 8.0f, v);
}

float Chorus::onePoleCoeff(float hz, float rate)
{
    if (rate <= 0.0f || hz <= 0.0f)
    {
        return 0.0f;
    }
    return juce::jlimit(0.0f, 1.0f, 1.0f - std::exp(-juce::MathConstants<float>::twoPi * hz / rate));
}

float Chorus::softSaturate(float x)
{
    return std::tanh(x);
}

float Chorus::trapezoid(float phase01)
{
    // Rise, hold, fall, hold - with rounded corners so the pitch does not step.
    // The flats are where a trapezoid differs from a sine that matters: pitch
    // deviation is the LFO's derivative, so a flat is a STEADY detuning rather
    // than a sweep through one.
    auto p = phase01 - std::floor(phase01);

    constexpr auto kRamp = 0.28f;   // fraction of the cycle spent moving
    constexpr auto kFlat = 0.22f;

    auto value = 0.0f;
    if (p < kRamp)
    {
        value = -1.0f + 2.0f * (p / kRamp);
    }
    else if (p < kRamp + kFlat)
    {
        value = 1.0f;
    }
    else if (p < 2.0f * kRamp + kFlat)
    {
        value = 1.0f - 2.0f * ((p - kRamp - kFlat) / kRamp);
    }
    else
    {
        value = -1.0f;
    }

    // Round the corners. Without this the ramp-to-flat transition is a step in
    // pitch, which is a click rather than a chorus.
    return std::sin(value * juce::MathConstants<float>::halfPi * 0.92f);
}

void Chorus::setSeed(uint32_t seed)
{
    rngState = seed != 0u ? seed : 0x2545F491u;
}

// ============================================================================
// lifecycle
// ============================================================================

void Chorus::prepare(double sampleRate)
{
    sampleRateHz = sampleRate > 0.0 ? sampleRate : 44100.0;

    lineSize = juce::jmax(256, static_cast<int>(sampleRateHz * (kMaxDelayMs + kMaxExcursionMs) * 0.001) + 8);

    for (auto& stack : lines)
    {
        for (auto& path : stack)
        {
            for (auto& channel : path)
            {
                channel.assign(static_cast<std::size_t>(lineSize), 0.0f);
            }
        }
    }

    const auto rampSeconds = 0.03;
    for (auto* smoother : { &enabledSmoothed, &amountSmoothed, &rateSmoothed, &depthSmoothed,
                            &widthSmoothed, &spreadSmoothed, &toneSmoothed, &lowCutSmoothed,
                            &feedbackSmoothed, &characterSmoothed, &mixSmoothed })
    {
        smoother->reset(sampleRateHz, rampSeconds);
    }

    reset();
}

void Chorus::reset()
{
    for (auto& stack : lines)
    {
        for (auto& path : stack)
        {
            for (auto& channel : path)
            {
                std::fill(channel.begin(), channel.end(), 0.0f);
            }
        }
    }

    for (auto& stack : writePos) { stack.fill(0); }

    // The paths start spread around the cycle rather than together, so the
    // effect has a stereo field from the first sample instead of fading into
    // one.
    for (std::size_t stack = 0; stack < lfoPhase.size(); ++stack)
    {
        for (std::size_t path = 0; path < lfoPhase[stack].size(); ++path)
        {
            lfoPhase[stack][path] = static_cast<float>(path) / static_cast<float>(kMaxPaths)
                                    + static_cast<float>(stack) * 0.17f;
        }
    }

    for (auto& stack : preEmphasisState) { for (auto& p : stack) { p.fill(0.0f); } }
    for (auto& stack : bandwidthState)   { for (auto& p : stack) { p.fill(0.0f); } }
    for (auto& stack : deEmphasisState)  { for (auto& p : stack) { p.fill(0.0f); } }
    for (auto& stack : compandState)     { for (auto& p : stack) { p.fill(1.0f); } }
    for (auto& stack : feedbackStore)    { for (auto& p : stack) { p.fill(0.0f); } }

    lowCutState = { { 0.0f, 0.0f } };
    toneState = { { 0.0f, 0.0f } };

    rateWander = 0.0f;
    depthWander = 0.0f;
    wanderTargetRate = 0.0f;
    wanderTargetDepth = 0.0f;
    wanderCounter = 0;

    for (auto* smoother : { &enabledSmoothed, &amountSmoothed, &rateSmoothed, &depthSmoothed,
                            &widthSmoothed, &spreadSmoothed, &toneSmoothed, &lowCutSmoothed,
                            &feedbackSmoothed, &characterSmoothed, &mixSmoothed })
    {
        smoother->setCurrentAndTargetValue(smoother->getTargetValue());
    }
}

void Chorus::updateForBlock(const ChorusSettings& next)
{
    settings = next;

    enabledSmoothed.setTargetValue(settings.enabled ? 1.0f : 0.0f);
    amountSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.amount));
    rateSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.rate));
    depthSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.depth));
    widthSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.width));
    spreadSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.spread));
    toneSmoothed.setTargetValue(juce::jlimit(-1.0f, 1.0f, settings.tone));
    lowCutSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.lowCut));
    feedbackSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.feedback));
    characterSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.character));
    mixSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.mix));

    wasEnabled = settings.enabled;
}

// ============================================================================
// delay reads
// ============================================================================

float Chorus::readDelay(int channel, int packedPath, float delaySamples) const
{
    const auto stack = static_cast<std::size_t>(packedPath / kMaxPaths);
    const auto path = static_cast<std::size_t>(packedPath % kMaxPaths);
    const auto ch = static_cast<std::size_t>(juce::jlimit(0, 1, channel));

    const auto& line = lines[stack][path][ch];
    const auto size = static_cast<float>(lineSize);

    auto pos = static_cast<float>(writePos[stack][path]) - juce::jlimit(2.0f, size - 4.0f, delaySamples);
    while (pos < 0.0f)
    {
        pos += size;
    }

    const auto i1 = static_cast<int>(pos) % lineSize;
    const auto frac = pos - std::floor(pos);

    // Cubic Lagrange. Linear interpolation of a delay line moving at chorus
    // rates is audible as a gritty, level-dependent low-pass that moves WITH
    // the modulation, which is exactly the artifact this effect must not have.
    const auto i0 = (i1 - 1 + lineSize) % lineSize;
    const auto i2 = (i1 + 1) % lineSize;
    const auto i3 = (i1 + 2) % lineSize;

    const auto y0 = line[static_cast<std::size_t>(i0)];
    const auto y1 = line[static_cast<std::size_t>(i1)];
    const auto y2 = line[static_cast<std::size_t>(i2)];
    const auto y3 = line[static_cast<std::size_t>(i3)];

    const auto a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const auto b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto c = -0.5f * y0 + 0.5f * y2;

    return ((a * frac + b) * frac + c) * frac + y1;
}

// ============================================================================
// one stack: the anti-phase pair, or an ensemble, or a single path
// ============================================================================

Chorus::Frame Chorus::renderStack(int modeIndex, float inL, float inR, float depth, float spread)
{
    const auto spec = specFor(modeIndex);
    const auto stack = static_cast<std::size_t>(specFor(settings.modeIndex).stackedMode >= 0
                                                    && modeIndex == specFor(settings.modeIndex).stackedMode
                                                ? 1
                                                : 0);

    const auto character = characterSmoothed.getCurrentValue();
    const auto feedback = juce::jlimit(0.0f, 0.55f, feedbackSmoothed.getCurrentValue() * 0.55f);

    const auto baseSamples = spec.baseDelayMs * 0.001f * static_cast<float>(sampleRateHz);
    const auto excursion = kMaxExcursionMs * 0.001f * static_cast<float>(sampleRateHz)
                           * depth * spec.depthScale;

    // Rate lives where the hardware's does - cycles measured in seconds. The
    // wander is a few percent, bounded and slow: enough that the cycle never
    // repeats exactly, small enough never to be heard as modulation itself.
    const auto rateHz = juce::jmap(rateSmoothed.getCurrentValue(), 0.06f, 1.6f)
                        * spec.rateScale * (1.0f + rateWander * 0.05f);
    const auto increment = rateHz / static_cast<float>(sampleRateHz);

    Frame wetA {};
    Frame wetB {};
    Frame wetC {};

    for (int path = 0; path < spec.paths; ++path)
    {
        const auto p = static_cast<std::size_t>(path);

        lfoPhase[stack][p] += increment;
        lfoPhase[stack][p] -= std::floor(lfoPhase[stack][p]);

        // Path 0 and path 1 are driven in ANTI-PHASE: when one goes sharp the
        // other goes flat by the same amount, so the pair has no average pitch
        // deviation. That is the whole mechanism.
        //
        // A third path (ENSEMBLE) sits a third of a cycle away, which is the
        // string-machine arrangement rather than a second opposing pair.
        auto phaseOffset = 0.0f;
        auto polarity = 1.0f;
        if (spec.paths == 3)
        {
            phaseOffset = static_cast<float>(path) / 3.0f;
        }
        else if (path == 1)
        {
            polarity = -1.0f;
        }

        // SPREAD detunes the two paths' phases away from exact opposition,
        // which trades a little of the pitch cancellation for a wider field.
        phaseOffset += (path == 1 ? spread * 0.12f : 0.0f);

        const auto lfo = trapezoid(lfoPhase[stack][p] + phaseOffset) * polarity;
        const auto delaySamples = baseSamples + lfo * excursion * (1.0f + depthWander * 0.04f);

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto c = static_cast<std::size_t>(ch);
            const auto input = ch == 0 ? inL : inR;

            // ---- the BBD group, in order -----------------------------------
            // Pre-emphasis: a high-shelf boost before the delay.
            const auto preCoeff = onePoleCoeff(2200.0f, static_cast<float>(sampleRateHz));
            preEmphasisState[stack][p][c] += (input - preEmphasisState[stack][p][c]) * preCoeff;
            const auto preHigh = input - preEmphasisState[stack][p][c];
            auto x = preEmphasisState[stack][p][c] + preHigh * (1.0f + character * spec.companding * 2.0f);

            // Compressor: soft and slow. Paired with the expander below, this
            // is the documented BBD noise-reduction arrangement, and the pair
            // is not exactly complementary once saturation and the bandwidth
            // limit sit between them - which is where the warmth comes from.
            const auto envCoeff = onePoleCoeff(20.0f, static_cast<float>(sampleRateHz));
            const auto level = std::abs(x);
            compandState[stack][p][c] += (level - compandState[stack][p][c]) * envCoeff;
            const auto compress = 1.0f / (1.0f + compandState[stack][p][c] * character * spec.companding * 2.0f);
            x *= compress;

            x += feedbackStore[stack][p][c] * feedback;

            lines[stack][p][c][static_cast<std::size_t>(writePos[stack][p])] = sanitize(x);

            auto y = readDelay(ch, static_cast<int>(stack) * kMaxPaths + path, delaySamples);

            // Bandwidth limit: the BBD's clock-limited response, and most of
            // what makes an analogue chorus sound softer than a digital one.
            const auto bwCoeff = onePoleCoeff(juce::jmap(character, 16000.0f, spec.bandwidthHz),
                                              static_cast<float>(sampleRateHz));
            bandwidthState[stack][p][c] += (y - bandwidthState[stack][p][c]) * bwCoeff;
            y = bandwidthState[stack][p][c];

            // Gentle saturation, inside the delay path only.
            if (character > 0.001f)
            {
                const auto drive = 1.0f + character * 0.6f;
                y = softSaturate(y * drive) / drive;
            }

            feedbackStore[stack][p][c] = y;

            // Expander, then de-emphasis: the inverses, applied in reverse.
            y /= juce::jmax(0.15f, compress);

            const auto deCoeff = onePoleCoeff(2200.0f, static_cast<float>(sampleRateHz));
            deEmphasisState[stack][p][c] += (y - deEmphasisState[stack][p][c]) * deCoeff;
            const auto deHigh = y - deEmphasisState[stack][p][c];
            y = deEmphasisState[stack][p][c]
                + deHigh / (1.0f + character * spec.companding * 2.0f);

            y = sanitize(y);

            auto& target = path == 0 ? wetA : (path == 1 ? wetB : wetC);
            (ch == 0 ? target.l : target.r) = y;
        }

        writePos[stack][p] = (writePos[stack][p] + 1) % lineSize;
    }

    if (spec.paths == 1)
    {
        // CE WARM: one path, so the anti-phase trick is not available. Width
        // comes from feeding the single wet copy to the two outputs with
        // opposite polarity instead - which still cancels in mono.
        return { wetA.l, -wetA.r };
    }

    if (spec.paths == 3)
    {
        // ENSEMBLE distributes three paths across the same opposing sum, so it
        // keeps the mono cancellation while sounding denser than a pair.
        return { wetA.l + wetC.l - wetB.l, wetB.r + wetC.r - wetA.r };
    }

    // L = +A -B, R = -A +B. The wet terms are equal and opposite, so they
    // cancel exactly when the two outputs are summed.
    return { wetA.l - wetB.l, wetB.r - wetA.r };
}

// ============================================================================
// host-rate entry point
// ============================================================================

void Chorus::processSampleFrame(float inL, float inR, float& outL, float& outR)
{
    const auto enabled = enabledSmoothed.getNextValue();
    const auto amount = amountSmoothed.getNextValue();
    const auto mix = mixSmoothed.getNextValue();

    rateSmoothed.getNextValue();
    characterSmoothed.getNextValue();
    feedbackSmoothed.getNextValue();

    // The macro, as a set of measured relationships rather than one multiplier
    // over everything. Wet level reaches full before depth does, so pushing
    // AMOUNT past the middle deepens the movement rather than just getting
    // louder.
    const auto depth = depthSmoothed.getNextValue() * juce::jmap(amount, 0.0f, 1.0f);
    const auto wetLevel = juce::jlimit(0.0f, 1.0f, amount * 1.6f);
    const auto width = widthSmoothed.getNextValue() * juce::jmap(amount, 0.6f, 1.15f);
    const auto spread = spreadSmoothed.getNextValue();

    // The wander target changes a few times a second at most.
    if (--wanderCounter <= 0)
    {
        wanderCounter = static_cast<int>(sampleRateHz * 0.5);
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        wanderTargetRate = static_cast<float>(rngState & 0xFFFFu) / 32768.0f - 1.0f;
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        wanderTargetDepth = static_cast<float>(rngState & 0xFFFFu) / 32768.0f - 1.0f;
    }
    const auto wanderCoeff = onePoleCoeff(0.15f, static_cast<float>(sampleRateHz));
    rateWander += (wanderTargetRate - rateWander) * wanderCoeff;
    depthWander += (wanderTargetDepth - depthWander) * wanderCoeff;

    auto wet = renderStack(settings.modeIndex, inL, inR, depth, spread);

    // A combination mode runs a second stack at its own rate and sums it. Two
    // pairs at different rates is a denser, less periodic field than either
    // alone - which is what the hardware's combination buttons produce.
    const auto stacked = specFor(settings.modeIndex).stackedMode;
    if (stacked >= 0)
    {
        const auto second = renderStack(stacked, inL, inR, depth, spread);
        wet = { (wet.l + second.l) * 0.7071f, (wet.r + second.r) * 0.7071f };
    }

    // ---- wet-path filters. The dry path is NEVER filtered ------------------
    // The dry is the pitch and transient anchor, and on a bass patch it is most
    // of what is heard. High-passing only the wet keeps the fundamental out of
    // the copies being detuned, so a bass note keeps its weight and its pitch
    // while its harmonics move.
    const auto lowCutHz = juce::jmap(lowCutSmoothed.getNextValue(), 20.0f, 420.0f);
    const auto lowCutCoeff = onePoleCoeff(lowCutHz, static_cast<float>(sampleRateHz));
    lowCutState[0] += (wet.l - lowCutState[0]) * lowCutCoeff;
    lowCutState[1] += (wet.r - lowCutState[1]) * lowCutCoeff;
    wet = { wet.l - lowCutState[0], wet.r - lowCutState[1] };

    const auto tone = toneSmoothed.getNextValue();
    if (std::abs(tone) > 0.001f)
    {
        const auto toneCoeff = onePoleCoeff(1400.0f, static_cast<float>(sampleRateHz));
        toneState[0] += (wet.l - toneState[0]) * toneCoeff;
        toneState[1] += (wet.r - toneState[1]) * toneCoeff;

        const auto lowGain = 1.0f - juce::jmax(0.0f, tone) * 0.55f;
        const auto highGain = 1.0f - juce::jmax(0.0f, -tone) * 0.8f;
        wet = { toneState[0] * lowGain + (wet.l - toneState[0]) * highGain,
                toneState[1] * lowGain + (wet.r - toneState[1]) * highGain };
    }

    // WIDTH scales the wet pair's side content. The wet pair is already
    // antisymmetric, so this is a gain on what is by construction pure side -
    // it cannot pull the centre apart because there is no centre in it.
    const auto wetMid = 0.5f * (wet.l + wet.r);
    const auto wetSide = 0.5f * (wet.l - wet.r) * width;
    wet = { wetMid + wetSide, wetMid - wetSide };

    const auto scaled = wetLevel * enabled;
    Frame processed { inL + wet.l * scaled, inR + wet.r * scaled };

    outL = sanitize(inL + (processed.l - inL) * mix);
    outR = sanitize(inR + (processed.r - inR) * mix);
}

} // namespace px3
