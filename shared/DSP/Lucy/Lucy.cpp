#include "Lucy.h"

#include <algorithm>
#include <cmath>

namespace px3
{
namespace
{
// The four verb delay lengths, in samples at 48 kHz. Mutually prime, so the
// echoes they produce do not line up and reinforce into a ringing pitch.
constexpr std::array<int, 4> kVerbBaseLengths { { 1153, 1621, 2129, 2833 } };

// Slope in dB per octave -> how many 2-pole sections. The documented set is
// gentle / balanced / intense, which is a section count and a resonance.

float hzToBark(float hz)
{
    // Traunmuller's approximation. Good enough to place critical bands, and it
    // is the shape that matters here rather than the last decimal.
    const auto z = (26.81f * hz) / (1960.0f + hz) - 0.53f;
    return juce::jmax(0.0f, z);
}

float barkToHz(float bark)
{
    const auto z = bark + 0.53f;
    return juce::jmax(0.0f, 1960.0f * z / (26.81f - z));
}
} // namespace

// ============================================================================
// helpers
// ============================================================================

float Lucy::sanitize(float v)
{
    if (! std::isfinite(v))
    {
        return 0.0f;
    }
    return juce::jlimit(-8.0f, 8.0f, v);
}

float Lucy::onePoleCoeff(float hz, float rate)
{
    if (rate <= 0.0f || hz <= 0.0f)
    {
        return 0.0f;
    }
    return 1.0f - std::exp(-juce::MathConstants<float>::twoPi * hz / rate);
}

void Lucy::setSeed(uint32_t seed)
{
    const auto base = seed != 0u ? seed : 0x9E3779B9u;
    rngState[0] = base;
    // A second, decorrelated stream, so the two channels' packet chains are
    // independent runs rather than the same run twice.
    rngState[1] = base * 2654435761u + 1u;
    if (rngState[1] == 0u)
    {
        rngState[1] = 0x85EBCA6Bu;
    }
}

float Lucy::nextRandom(int channel)
{
    auto& state = rngState[static_cast<std::size_t>(juce::jlimit(0, 1, channel))];
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

float Lucy::nextBipolar(int channel)
{
    return nextRandom(channel) * 2.0f - 1.0f;
}

int Lucy::wetLatencySamples() const noexcept
{
    // The whole wet path, not just the transform: the jitter line runs at a
    // nominal half-buffer delay whatever the mode, and the limiter looks ahead.
    // Reporting only the FFT would understate it by a third.
    const auto transform = slowActive ? (1 << kSlowFftOrder) : (1 << kFastFftOrder);
    return transform + jitterSize / 2 + kLimiterLookahead;
}

// ============================================================================
// lifecycle
// ============================================================================

void Lucy::prepare(double sampleRate)
{
    sampleRateHz = sampleRate > 0.0 ? sampleRate : 44100.0;

    // Both plans up front, so toggling SLOW allocates nothing on the audio
    // thread.
    fastStft.prepare(kFastFftOrder, 2);
    slowStft.prepare(kSlowFftOrder, 2);

    jitterSize = juce::jmax(256, static_cast<int>(sampleRateHz * 0.01));
    for (auto& line : jitterLine)
    {
        line.assign(static_cast<std::size_t>(jitterSize), 0.0f);
    }

    const auto rateScale = sampleRateHz / 48000.0;
    for (int line = 0; line < kVerbLines; ++line)
    {
        verbLength[static_cast<std::size_t>(line)] =
            juce::jmax(64, static_cast<int>(kVerbBaseLengths[static_cast<std::size_t>(line)] * rateScale));

        for (int ch = 0; ch < 2; ++ch)
        {
            // The right channel's lines are slightly longer, which is what
            // decorrelates the two tails without any random difference between
            // them.
            const auto length = verbLength[static_cast<std::size_t>(line)]
                                + (ch == 1 ? 37 + line * 11 : 0);
            verbLines[static_cast<std::size_t>(ch)][static_cast<std::size_t>(line)]
                .assign(static_cast<std::size_t>(length + 64), 0.0f);
        }
    }

    const auto rampSeconds = 0.02;
    for (auto* smoother : { &enabledSmoothed, &outputBlendSmoothed,
                            &filterAmountSmoothed, &filterFreqSmoothed, &verbSmoothed,
                            &verbDecaySmoothed, &gateThresholdSmoothed,
                            &limiterThresholdSmoothed, &lossGainSmoothed, &spreadSmoothed })
    {
        smoother->reset(sampleRateHz, rampSeconds);
    }

    buildCriticalBands(fastStft.numBins());
    reset();
}

void Lucy::reset()
{
    fastStft.reset();
    slowStft.reset();

    for (auto& channel : magnitude)          { channel.fill(0.0f); }
    for (auto& channel : phase)              { channel.fill(0.0f); }
    for (auto& channel : coded)              { channel.fill(0.0f); }
    for (auto& channel : maskThreshold)      { channel.fill(0.0f); }
    for (auto& channel : frozenMagnitude)    { channel.fill(0.0f); }
    for (auto& channel : frozenPhase)        { channel.fill(0.0f); }
    for (auto& channel : lastGoodMagnitude)  { channel.fill(0.0f); }
    for (auto& channel : lastGoodPhase)      { channel.fill(0.0f); }
    for (auto& channel : jitterWalk)         { channel.fill(0.0f); }

    for (auto& line : jitterLine)
    {
        std::fill(line.begin(), line.end(), 0.0f);
    }
    jitterWrite = 0;
    jitterOffset = { { 0.0f, 0.0f } };
    jitterTarget = { { 0.0f, 0.0f } };

    for (auto& channel : verbLines)
    {
        for (auto& line : channel)
        {
            std::fill(line.begin(), line.end(), 0.0f);
        }
    }
    for (auto& channel : verbWrite) { channel.fill(0); }
    for (auto& channel : verbDamp)  { channel.fill(0.0f); }
    verbModPhase = 0.0f;

    for (auto& channel : filterState)
    {
        for (auto& section : channel)
        {
            section = {};
        }
    }

    for (auto& channel : limiterDelay) { channel.fill(0.0f); }
    limiterWrite = 0;
    limiterGain = 1.0f;

    decisionCounter = { { 0, 0 } };
    decisionDue = { { true, true } };
    packetBadState = { { false, false } };
    packetStateFrames = { { 0, 0 } };
    freezeLatched = { { false, false } };
    autoGainState = { { 1.0f, 1.0f } };

    gateEnv = { { 0.0f, 0.0f } };
    gateGain = 1.0f;
    gateOpen = false;

    for (auto* smoother : { &enabledSmoothed, &outputBlendSmoothed,
                            &filterAmountSmoothed, &filterFreqSmoothed, &verbSmoothed,
                            &verbDecaySmoothed, &gateThresholdSmoothed,
                            &limiterThresholdSmoothed, &lossGainSmoothed, &spreadSmoothed })
    {
        smoother->setCurrentAndTargetValue(smoother->getTargetValue());
    }
}

void Lucy::updateForBlock(const LucyUserParameters& next)
{
    user = next;

    // ONE translation, once a block. Every stage below reads `derived` and no
    // stage reads a knob, which is what keeps the transfer curves in one
    // readable file instead of spread across four DSP functions.
    derived = deriveLucyParameters(user);

    if (user.slow != slowActive)
    {
        slowActive = user.slow;
        // Switching plan leaves the other one's ring holding stale audio, so it
        // is cleared rather than allowed to reappear on the way back.
        (slowActive ? fastStft : slowStft).reset();
        buildCriticalBands(slowActive ? slowStft.numBins() : fastStft.numBins());
    }

    // The time-domain stages are smoothed per sample, because a step in a
    // filter coefficient or an output gain IS a click. The spectral stages are
    // not: they change once per STFT frame by construction, and overlap-add
    // crossfades one frame into the next.
    enabledSmoothed.setTargetValue(user.enabled ? 1.0f : 0.0f);
    outputBlendSmoothed.setTargetValue(derived.outputBlend);
    filterAmountSmoothed.setTargetValue(derived.filterAmount);
    filterFreqSmoothed.setTargetValue(derived.filterFreq);
    verbSmoothed.setTargetValue(derived.reverbAmount);
    verbDecaySmoothed.setTargetValue(derived.reverbDecay);
    gateThresholdSmoothed.setTargetValue(derived.gateThreshold);
    limiterThresholdSmoothed.setTargetValue(derived.limiterThreshold);
    lossGainSmoothed.setTargetValue(juce::jlimit(-36.0f, 36.0f, derived.lossGainDb));
    spreadSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, user.spread));

    if (user.freeze == LucyFreezeMode::off)
    {
        freezeLatched = { { false, false } };
    }

    if (user.enabled && ! wasEnabled)
    {
        fastStft.reset();
        slowStft.reset();
    }
    wasEnabled = user.enabled;
}

// ============================================================================
// critical bands
// ============================================================================

void Lucy::buildCriticalBands(int numBins)
{
    if (numBins <= 1 || numBins == bandsForBins)
    {
        return;
    }
    bandsForBins = numBins;

    const auto nyquist = static_cast<float>(sampleRateHz) * 0.5f;
    const auto binWidth = nyquist / static_cast<float>(numBins - 1);
    const auto maxBark = hzToBark(nyquist);

    for (int band = 0; band <= kCriticalBands; ++band)
    {
        const auto bark = maxBark * static_cast<float>(band) / static_cast<float>(kCriticalBands);
        const auto hz = barkToHz(bark);
        bandEdges[static_cast<std::size_t>(band)] =
            juce::jlimit(0, numBins - 1, static_cast<int>(hz / juce::jmax(1.0f, binWidth)));
    }

    // Bands must be non-empty and monotonic, or the spreading loop reads
    // backwards ranges at the bottom where Bark bands are narrower than a bin.
    for (int band = 1; band <= kCriticalBands; ++band)
    {
        bandEdges[static_cast<std::size_t>(band)] =
            juce::jmax(bandEdges[static_cast<std::size_t>(band)],
                       bandEdges[static_cast<std::size_t>(band - 1)] + 1);
        bandEdges[static_cast<std::size_t>(band)] =
            juce::jmin(bandEdges[static_cast<std::size_t>(band)], numBins - 1);
    }
}

// ============================================================================
// LOSS - a masking coder
// ============================================================================

void Lucy::applyLoss(int channel, int numBins)
{
    const auto ch = static_cast<std::size_t>(channel);
    const auto weighting = derived.weightingTilt;

    auto& mag = magnitude[ch];
    auto& out = coded[ch];
    auto& threshold = maskThreshold[ch];

    // ---- 1. band energies ------------------------------------------------
    for (int band = 0; band < kCriticalBands; ++band)
    {
        const auto lo = bandEdges[static_cast<std::size_t>(band)];
        const auto hi = bandEdges[static_cast<std::size_t>(band + 1)];
        auto sum = 0.0f;
        for (int k = lo; k < hi && k < numBins; ++k)
        {
            sum += mag[static_cast<std::size_t>(k)] * mag[static_cast<std::size_t>(k)];
        }
        bandEnergy[static_cast<std::size_t>(band)] =
            std::sqrt(sum / static_cast<float>(juce::jmax(1, hi - lo)));
    }

    // ---- 2. spreading ----------------------------------------------------
    // Masking spreads asymmetrically: a loud band masks the bands above it far
    // more than the bands below. A symmetric spread would let bass mask treble
    // as readily as the other way round, which is not how hearing works and
    // makes the coder throw away the wrong things.
    constexpr auto kUpwardSpread = 0.62f;    // toward higher frequencies
    constexpr auto kDownwardSpread = 0.16f;  // toward lower frequencies

    for (int band = 0; band < kCriticalBands; ++band)
    {
        bandSpread[static_cast<std::size_t>(band)] = bandEnergy[static_cast<std::size_t>(band)];
    }
    for (int band = 1; band < kCriticalBands; ++band)
    {
        bandSpread[static_cast<std::size_t>(band)] =
            juce::jmax(bandSpread[static_cast<std::size_t>(band)],
                       bandSpread[static_cast<std::size_t>(band - 1)] * kUpwardSpread);
    }
    for (int band = kCriticalBands - 2; band >= 0; --band)
    {
        bandSpread[static_cast<std::size_t>(band)] =
            juce::jmax(bandSpread[static_cast<std::size_t>(band)],
                       bandSpread[static_cast<std::size_t>(band + 1)] * kDownwardSpread);
    }

    // ---- 3. threshold, weighted -----------------------------------------
    // LOSS raises the threshold, so more of the spectrum falls under it. That
    // is the "strength" half of the control - and the curve that gets from the
    // knob to this number lives in mapLossToMaskingDepth, not here.
    const auto depth = derived.maskingDepth;

    for (int band = 0; band < kCriticalBands; ++band)
    {
        const auto bandNorm = static_cast<float>(band) / static_cast<float>(kCriticalBands - 1);

        // WEIGHTING tilts which end survives: DARK keeps lows by raising the
        // threshold on highs, BRIGHT does the reverse, and zero leaves the
        // psychoacoustic curve alone.
        auto tilt = 1.0f;
        if (weighting < 0.0f)
        {
            tilt = 1.0f + (-weighting) * bandNorm * 3.0f;
        }
        else if (weighting > 0.0f)
        {
            tilt = 1.0f + weighting * (1.0f - bandNorm) * 3.0f;
        }

        const auto lo = bandEdges[static_cast<std::size_t>(band)];
        const auto hi = bandEdges[static_cast<std::size_t>(band + 1)];
        const auto value = bandSpread[static_cast<std::size_t>(band)] * depth * tilt;
        for (int k = lo; k < hi && k < numBins; ++k)
        {
            threshold[static_cast<std::size_t>(k)] = value;
        }
    }

    // ---- 4. coverage -----------------------------------------------------
    // LOSS also sets WHICH frequencies are affected: a narrow strip that widens
    // as it rises, which is the second half of the documented control.
    constexpr auto kCoverageCentre = 0.42f;
    const auto coverageHalfWidth = derived.spectralCoverage;

    // ---- 5. discard and quantise ----------------------------------------
    // The quantiser step widens with LOSS. Coarse steps on the partials that
    // survive is where the chiming comes from: the error lands on the strong
    // components rather than spread across the noise floor.
    const auto quantiseStep = derived.quantisationAmount;

    // How far under the masking threshold a bin has to be before it is thrown
    // away. Below 1 the coder only discards what is clearly inaudible, which
    // is what makes the bottom of LOSS thin the spectrum rather than gouge it.
    const auto discardRatio = derived.discardAmount;

    for (int k = 0; k < numBins; ++k)
    {
        const auto idx = static_cast<std::size_t>(k);
        const auto binNorm = static_cast<float>(k) / static_cast<float>(juce::jmax(1, numBins - 1));

        const auto inside = std::abs(binNorm - kCoverageCentre) < coverageHalfWidth;
        if (! inside)
        {
            out[idx] = mag[idx];
            continue;
        }

        const auto m = mag[idx];
        if (m <= threshold[idx] * discardRatio)
        {
            // Below the masking threshold: discarded, exactly as a coder does.
            out[idx] = 0.0f;
            continue;
        }

        // Logarithmic magnitude grid: perceived level is logarithmic, so a
        // linear grid would quantise loud partials finely and quiet ones into
        // nothing.
        const auto db = 20.0f * std::log10(juce::jmax(1.0e-9f, m));
        const auto stepDb = quantiseStep * 6.0f;
        const auto quantised = std::round(db / stepDb) * stepDb;
        out[idx] = std::pow(10.0f, quantised / 20.0f);
    }
}

// ============================================================================
// PACKETS - Gilbert-Elliott bursts
// ============================================================================

void Lucy::applyPackets(int channel, int numBins)
{
    if (user.packets == LucyPacketMode::clean)
    {
        packetBadState[static_cast<std::size_t>(channel)] = false;
        return;
    }

    const auto ch = static_cast<std::size_t>(channel);

    // Two states with runs, not an independent draw per frame. An independent
    // draw is hiss; a real link loses a cluster, recovers, and loses another -
    // which is what "the skips and spaces of a bad connection" describes.
    //
    // SPEED sets how long a state may persist and LOSS how likely a burst is;
    // both arrive already mapped, from the same SPEED that spaces the coding
    // decisions out.
    if (++packetStateFrames[ch] >= derived.packetStateFrames)
    {
        packetStateFrames[ch] = 0;

        // The exit probability is FIXED, so bursts keep a characteristic
        // length rather than having their length change with the same knob
        // that sets how often they happen.
        constexpr auto leaveBad = 0.55f;

        const auto draw = nextRandom(channel);
        packetBadState[ch] = packetBadState[ch] ? (draw > leaveBad)
                                                : (draw < derived.packetProbability);
    }

    if (! packetBadState[ch])
    {
        // A good frame. Remember it: concealment needs something to conceal
        // WITH.
        for (int k = 0; k < numBins; ++k)
        {
            const auto idx = static_cast<std::size_t>(k);
            lastGoodMagnitude[ch][idx] = coded[ch][idx];
            lastGoodPhase[ch][idx] = phase[ch][idx];
        }
        return;
    }

    if (user.packets == LucyPacketMode::loss)
    {
        // PACKET LOSS - the frame is gone. Overlap-add already crossfades the
        // frame boundaries, so a dropped frame is a gap rather than a click.
        for (int k = 0; k < numBins; ++k)
        {
            coded[ch][static_cast<std::size_t>(k)] = 0.0f;
        }
        return;
    }

    // PACKET REPEAT - concealment by frame repetition, which is what a codec
    // does with a lost packet. Advancing the phase rather than repeating it is
    // what turns a repeat into a smear rather than a stutter.
    const auto fftSize = static_cast<float>(slowActive ? (1 << kSlowFftOrder) : (1 << kFastFftOrder));
    const auto hop = static_cast<float>(slowActive ? slowStft.hopSize() : fastStft.hopSize());

    for (int k = 0; k < numBins; ++k)
    {
        const auto idx = static_cast<std::size_t>(k);
        coded[ch][idx] = lastGoodMagnitude[ch][idx];

        const auto expected = juce::MathConstants<float>::twoPi * static_cast<float>(k) * hop / fftSize;
        lastGoodPhase[ch][idx] = std::fmod(lastGoodPhase[ch][idx] + expected,
                                           juce::MathConstants<float>::twoPi);
        phase[ch][idx] = lastGoodPhase[ch][idx];
    }
}

// ============================================================================
// FREEZE - spectral, not a looper
// ============================================================================

void Lucy::applyFreeze(int channel, int numBins)
{
    const auto ch = static_cast<std::size_t>(channel);

    if (user.freeze == LucyFreezeMode::off)
    {
        return;
    }

    const auto fftSize = static_cast<float>(slowActive ? (1 << kSlowFftOrder) : (1 << kFastFftOrder));
    const auto hop = static_cast<float>(slowActive ? slowStft.hopSize() : fastStft.hopSize());

    if (! freezeLatched[ch])
    {
        for (int k = 0; k < numBins; ++k)
        {
            const auto idx = static_cast<std::size_t>(k);
            frozenMagnitude[ch][idx] = coded[ch][idx];
            frozenPhase[ch][idx] = phase[ch][idx];
        }
        freezeLatched[ch] = true;
    }
    else if (user.freeze == LucyFreezeMode::slushy)
    {
        // The slushy state: the frozen spectrum drifts toward the live one, so
        // it becomes a shifting copy of what is being played and can be refilled
        // by playing something new. SPEED sets how fast - the same SPEED, and
        // the same mapping file, as the coding decisions and the packet chain.
        const auto rate = derived.freezeSlushRate;
        for (int k = 0; k < numBins; ++k)
        {
            const auto idx = static_cast<std::size_t>(k);
            frozenMagnitude[ch][idx] += (coded[ch][idx] - frozenMagnitude[ch][idx]) * rate;
        }
    }

    // Phase keeps advancing whichever state it is in, with a small bounded
    // random component: a frozen chord whose phase is merely held becomes a
    // static metallic tone rather than a pad.
    for (int k = 0; k < numBins; ++k)
    {
        const auto idx = static_cast<std::size_t>(k);
        const auto expected = juce::MathConstants<float>::twoPi * static_cast<float>(k) * hop / fftSize;
        frozenPhase[ch][idx] = std::fmod(frozenPhase[ch][idx] + expected + nextBipolar(channel) * 0.05f,
                                         juce::MathConstants<float>::twoPi);
    }

    // FREEZER balances live against frozen.
    const auto frozenAmount = juce::jlimit(0.0f, 1.0f, derived.freezeBlend);
    for (int k = 0; k < numBins; ++k)
    {
        const auto idx = static_cast<std::size_t>(k);
        coded[ch][idx] += (frozenMagnitude[ch][idx] - coded[ch][idx]) * frozenAmount;
        phase[ch][idx] += (frozenPhase[ch][idx] - phase[ch][idx]) * frozenAmount;
    }
}

// ============================================================================
// one spectral frame
// ============================================================================

void Lucy::spectralFrame(int channel, float* real, float* imag, int numBins)
{
    const auto ch = static_cast<std::size_t>(juce::jlimit(0, 1, channel));
    const auto bins = juce::jmin(numBins, kMaxBins);

    for (int k = 0; k < bins; ++k)
    {
        const auto idx = static_cast<std::size_t>(k);
        const auto re = real[k];
        const auto im = imag[k];
        magnitude[ch][idx] = std::sqrt(re * re + im * im);
        phase[ch][idx] = std::atan2(im, re);
    }

    // SPEED holds one decision across N frames. Slow means the same bins stay
    // discarded for a long time, which IS spectral smearing - and it is the
    // same counter that spaces the packets out.
    decisionDue[ch] = (--decisionCounter[ch] <= 0);
    if (decisionDue[ch])
    {
        decisionCounter[ch] = derived.decisionFrames;
    }

    applyLoss(channel, bins);

    // INVERSE is the residual of the coding, taken in the domain the coding
    // happened in. Everything STANDARD threw away - the sub-threshold bins and
    // the quantisation error - is what is left, and it is brighter and thinner
    // because that is what a coder discards.
    if (user.mode == LucyLossMode::inverse)
    {
        for (int k = 0; k < bins; ++k)
        {
            const auto idx = static_cast<std::size_t>(k);
            coded[ch][idx] = std::abs(magnitude[ch][idx] - coded[ch][idx]);
        }
    }
    else if (user.mode == LucyLossMode::jitter)
    {
        // JITTER's phase half. A bounded random WALK rather than white noise:
        // an unstable clock drifts, it does not jump independently every frame.
        //
        // DEPTH comes from LOSS - it is degradation depth - and STEP from
        // SPEED. Splitting them that way is what keeps the two macros from
        // reaching into each other's territory.
        const auto depth = derived.jitterDepth;
        const auto step = derived.jitterWalkStep;

        for (int k = 0; k < bins; ++k)
        {
            const auto idx = static_cast<std::size_t>(k);
            auto walk = jitterWalk[ch][idx] + nextBipolar(channel) * step;
            walk = juce::jlimit(-1.0f, 1.0f, walk);
            jitterWalk[ch][idx] = walk;
            phase[ch][idx] += walk * depth;
        }
    }

    if (decisionDue[ch])
    {
        applyPackets(channel, bins);
    }
    else if (user.packets != LucyPacketMode::clean && packetBadState[ch])
    {
        // Held decision: a burst spans every frame until the next decision, so
        // it stays lost rather than flickering back at frame rate.
        applyPackets(channel, bins);
    }

    applyFreeze(channel, bins);

    // AUTO GAIN. The loss modes work by manipulating the spectrum, so they
    // change loudness by construction; this compares the energy in and out and
    // compensates, smoothed across frames so it does not pump.
    auto energyIn = 0.0f;
    auto energyOut = 0.0f;
    for (int k = 0; k < bins; ++k)
    {
        const auto idx = static_cast<std::size_t>(k);
        energyIn += magnitude[ch][idx] * magnitude[ch][idx];
        energyOut += coded[ch][idx] * coded[ch][idx];
    }

    // Only when there is something to compensate. Below this floor the ratio
    // is noise over noise, and a gain of up to 8x applied to a decaying tail
    // makes silence audible - which is what it did before this guard.
    constexpr auto kAutoGainFloor = 1.0e-6f;
    const auto wanted = (energyIn > kAutoGainFloor && energyOut > kAutoGainFloor)
                            ? juce::jlimit(0.25f, 8.0f, std::sqrt(energyIn / energyOut))
                            : 1.0f;
    autoGainState[ch] += (wanted - autoGainState[ch]) * 0.08f;
    const auto gain = 1.0f + (autoGainState[ch] - 1.0f) * derived.autoGain;

    for (int k = 0; k < bins; ++k)
    {
        const auto idx = static_cast<std::size_t>(k);
        const auto m = coded[ch][idx] * gain;
        real[k] = m * std::cos(phase[ch][idx]);
        imag[k] = m * std::sin(phase[ch][idx]);
    }
}

// ============================================================================
// JITTER - the timing half
// ============================================================================

float Lucy::applyJitterTiming(int channel, float input)
{
    const auto ch = static_cast<std::size_t>(juce::jlimit(0, 1, channel));
    auto& line = jitterLine[ch];

    line[static_cast<std::size_t>(jitterWrite)] = sanitize(input);

    if (user.mode != LucyLossMode::jitter)
    {
        // Straight through at the nominal delay, so switching modes does not
        // step the signal.
        const auto nominal = static_cast<float>(jitterSize) * 0.5f;
        auto pos = static_cast<float>(jitterWrite) - nominal;
        while (pos < 0.0f)
        {
            pos += static_cast<float>(jitterSize);
        }
        const auto i1 = static_cast<int>(pos) % jitterSize;
        const auto i2 = (i1 + 1) % jitterSize;
        const auto frac = pos - std::floor(pos);
        return line[static_cast<std::size_t>(i1)] * (1.0f - frac)
               + line[static_cast<std::size_t>(i2)] * frac;
    }

    // Clock error is correlated in time: the read position wanders toward a new
    // target rather than jumping to it, which is the difference between a
    // wobbling clock and added noise.
    const auto rate = derived.jitterTimingRate;
    if (nextRandom(channel) < rate)
    {
        jitterTarget[ch] = nextBipolar(channel);
    }
    jitterOffset[ch] += (jitterTarget[ch] - jitterOffset[ch]) * rate * 6.0f;

    const auto depth = derived.jitterTimingDepth * static_cast<float>(jitterSize) * 0.18f;
    const auto nominal = static_cast<float>(jitterSize) * 0.5f;
    auto pos = static_cast<float>(jitterWrite) - nominal - jitterOffset[ch] * depth;
    while (pos < 0.0f)
    {
        pos += static_cast<float>(jitterSize);
    }

    // Cubic Lagrange, matching the rest of this codebase: linear interpolation
    // of a wandering read head is audible as a dull, gritty modulation rather
    // than as timing error.
    const auto i1 = static_cast<int>(pos) % jitterSize;
    const auto frac = pos - std::floor(pos);
    const auto i0 = (i1 - 1 + jitterSize) % jitterSize;
    const auto i2 = (i1 + 1) % jitterSize;
    const auto i3 = (i1 + 2) % jitterSize;

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
// FILTER - a band-pass whose WIDTH is the control
// ============================================================================

Lucy::Frame Lucy::applyFilter(Frame in)
{
    const auto width = filterAmountSmoothed.getNextValue();
    const auto freqNorm = filterFreqSmoothed.getNextValue();

    // At the minimum there is no filtering at all - documented, and it is what
    // makes this a width control rather than a resonance control.
    if (width <= 0.001f)
    {
        return in;
    }

    // 6 / 24 / 96 dB became a section count and a per-section Q in the control
    // model. The filter is handed both; it does not know what the user chose.
    const auto sections = juce::jlimit(1, kFilterSections, derived.filterSections);
    const auto resonance = derived.filterResonance;

    // Exponential, so the sweep feels even across the band.
    const auto centre = 60.0f * std::pow(220.0f, freqNorm);
    const auto nyquist = static_cast<float>(sampleRateHz) * 0.45f;
    const auto clamped = juce::jlimit(20.0f, nyquist, centre);

    // Width narrows the band around the centre as it rises.
    const auto q = juce::jmap(width, 0.35f, 6.0f) * resonance;
    const auto g = std::tan(juce::MathConstants<float>::pi * clamped / static_cast<float>(sampleRateHz));
    const auto k = 1.0f / juce::jmax(0.05f, q);
    const auto a1 = 1.0f / (1.0f + g * (g + k));
    const auto a2 = g * a1;
    const auto a3 = g * a2;

    std::array<float, 2> values { { in.l, in.r } };

    for (int ch = 0; ch < 2; ++ch)
    {
        auto x = values[static_cast<std::size_t>(ch)];
        const auto dry = x;

        for (int section = 0; section < sections; ++section)
        {
            auto& state = filterState[static_cast<std::size_t>(ch)][static_cast<std::size_t>(section)];

            // Topology-preserving transform SVF: stable under modulation, which
            // matters because FREQ is a modulation destination.
            const auto v3 = x - state.ic2;
            const auto v1 = a1 * state.ic1 + a2 * v3;
            const auto v2 = state.ic2 + a2 * state.ic1 + a3 * v3;
            state.ic1 = sanitize(2.0f * v1 - state.ic1);
            state.ic2 = sanitize(2.0f * v2 - state.ic2);

            // The SVF's band-pass output peaks at Q, not at unity, so a
            // cascade multiplies by Q per section. Normalising each section by
            // k (= 1/Q) makes its peak gain 1, which is what lets sections be
            // stacked for slope without the level running away with resonance.
            x = v1 * k;
        }

        // INVERT: the band taken OUT of the input, which is the band-reject the
        // source documents rather than a separate filter.
        values[static_cast<std::size_t>(ch)] = user.filterInvert ? (dry - x) : x;
    }

    return { sanitize(values[0]), sanitize(values[1]) };
}

// ============================================================================
// VERB - as digital as could be
// ============================================================================

Lucy::Frame Lucy::applyVerb(Frame in, float mix)
{
    if (mix <= 0.0005f)
    {
        return in;
    }

    const auto decay = verbDecaySmoothed.getNextValue();
    const auto spread = spreadSmoothed.getNextValue();

    const auto feedback = juce::jlimit(0.0f, 0.93f, juce::jmap(decay, 0.35f, 0.93f));
    const auto damping = onePoleCoeff(juce::jmap(decay, 9000.0f, 2600.0f), static_cast<float>(sampleRateHz));

    verbModPhase += 0.31f / static_cast<float>(sampleRateHz);
    verbModPhase -= std::floor(verbModPhase);

    Frame wet {};

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto c = static_cast<std::size_t>(ch);
        const auto input = ch == 0 ? in.l : in.r;

        std::array<float, kVerbLines> taps {};
        for (int line = 0; line < kVerbLines; ++line)
        {
            const auto l = static_cast<std::size_t>(line);
            auto& buffer = verbLines[c][l];
            const auto size = static_cast<int>(buffer.size());

            // A slow, per-line modulation of the read position. Without it a
            // fixed FDN rings at its own modes; with it the tail breathes.
            const auto modDepth = 6.0f;
            const auto modPhase = verbModPhase + static_cast<float>(line) * 0.25f
                                  + static_cast<float>(ch) * 0.13f;
            const auto offset = modDepth
                                * std::sin(juce::MathConstants<float>::twoPi * (modPhase - std::floor(modPhase)));

            auto pos = static_cast<float>(verbWrite[c][l]) - static_cast<float>(size - 8) + offset;
            while (pos < 0.0f)
            {
                pos += static_cast<float>(size);
            }
            const auto i1 = static_cast<int>(pos) % size;
            const auto i2 = (i1 + 1) % size;
            const auto frac = pos - std::floor(pos);
            taps[l] = buffer[static_cast<std::size_t>(i1)] * (1.0f - frac)
                      + buffer[static_cast<std::size_t>(i2)] * frac;
        }

        // Hadamard mixing: every line feeds every other one equally, which is
        // what makes an FDN diffuse rather than four parallel delays.
        const std::array<float, kVerbLines> mixed { {
            0.5f * (taps[0] + taps[1] + taps[2] + taps[3]),
            0.5f * (taps[0] - taps[1] + taps[2] - taps[3]),
            0.5f * (taps[0] + taps[1] - taps[2] - taps[3]),
            0.5f * (taps[0] - taps[1] - taps[2] + taps[3]),
        } };

        for (int line = 0; line < kVerbLines; ++line)
        {
            const auto l = static_cast<std::size_t>(line);
            auto& buffer = verbLines[c][l];
            const auto size = static_cast<int>(buffer.size());

            verbDamp[c][l] += (mixed[l] - verbDamp[c][l]) * damping;
            auto value = input * 0.35f + verbDamp[c][l] * feedback;

            // The character. A coarse quantiser INSIDE the loop, so the tail
            // loses resolution as it recirculates - which is where the fizzing
            // and sputtering of an early-nineties digital reverb came from, and
            // why this is not the project's existing Reverb.
            constexpr auto kLevels = 512.0f;
            value = std::round(value * kLevels) / kLevels;

            buffer[static_cast<std::size_t>(verbWrite[c][l])] = sanitize(value);
            verbWrite[c][l] = (verbWrite[c][l] + 1) % size;
        }

        const auto out = 0.35f * (taps[0] + taps[1] + taps[2] + taps[3]);
        (ch == 0 ? wet.l : wet.r) = sanitize(out);
    }

    // SPREAD widens the tail as it rises, which is documented behaviour rather
    // than a general width control.
    const auto mid = 0.5f * (wet.l + wet.r);
    const auto side = 0.5f * (wet.l - wet.r) * (1.0f + spread * 1.4f);
    wet = { mid + side, mid - side };

    return { in.l * (1.0f - mix) + wet.l * mix,
             in.r * (1.0f - mix) + wet.r * mix };
}

// ============================================================================
// GATE
// ============================================================================

Lucy::Frame Lucy::applyGate(Frame in)
{
    if (! user.gate)
    {
        gateGain += (1.0f - gateGain) * 0.01f;
        return { in.l * gateGain, in.r * gateGain };
    }

    const auto cutoff = gateThresholdSmoothed.getNextValue();

    const auto attack = onePoleCoeff(220.0f, static_cast<float>(sampleRateHz));
    const auto release = onePoleCoeff(9.0f, static_cast<float>(sampleRateHz));

    const auto level = 0.5f * (std::abs(in.l) + std::abs(in.r));
    const auto coeff = level > gateEnv[0] ? attack : release;
    gateEnv[0] += (level - gateEnv[0]) * coeff;

    const auto openThreshold = cutoff * cutoff * 0.5f;
    // Hysteresis: a signal hovering at the cutoff would otherwise chatter, and
    // chattering is a buzz rather than the documented sputter.
    const auto closeThreshold = openThreshold * 0.6f;

    gateOpen = gateOpen ? (gateEnv[0] > closeThreshold) : (gateEnv[0] > openThreshold);

    const auto wanted = gateOpen ? 1.0f : 0.0f;
    const auto gateCoeff = gateOpen ? onePoleCoeff(400.0f, static_cast<float>(sampleRateHz))
                                    : onePoleCoeff(120.0f, static_cast<float>(sampleRateHz));
    gateGain += (wanted - gateGain) * gateCoeff;

    return { in.l * gateGain, in.r * gateGain };
}

// ============================================================================
// LIMITER
// ============================================================================

Lucy::Frame Lucy::applyLimiter(Frame in)
{
    const auto threshold = juce::jlimit(0.05f, 1.0f, limiterThresholdSmoothed.getNextValue());

    limiterDelay[0][static_cast<std::size_t>(limiterWrite)] = in.l;
    limiterDelay[1][static_cast<std::size_t>(limiterWrite)] = in.r;
    limiterWrite = (limiterWrite + 1) % kLimiterLookahead;

    // The gain has to come from the LOUDEST sample still in the window, not
    // from the newest one. Reading the newest peak and applying it to the
    // oldest sample is a lag, not lookahead: the peak arrives at the output
    // before the gain reduction that was computed for it.
    auto windowPeak = 0.0f;
    for (int i = 0; i < kLimiterLookahead; ++i)
    {
        const auto idx = static_cast<std::size_t>(i);
        windowPeak = juce::jmax(windowPeak, std::abs(limiterDelay[0][idx]), std::abs(limiterDelay[1][idx]));
    }

    const auto wanted = windowPeak > threshold ? threshold / juce::jmax(1.0e-6f, windowPeak) : 1.0f;

    // Attack fast enough to be in place within the lookahead window; release
    // slow, so a single peak does not pump the whole passage after it.
    const auto attack = onePoleCoeff(static_cast<float>(sampleRateHz) / static_cast<float>(kLimiterLookahead),
                                     static_cast<float>(sampleRateHz));
    const auto release = onePoleCoeff(6.0f, static_cast<float>(sampleRateHz));
    const auto coeff = wanted < limiterGain ? attack : release;
    limiterGain += (wanted - limiterGain) * coeff;

    const auto readPos = limiterWrite;
    return { sanitize(limiterDelay[0][static_cast<std::size_t>(readPos)] * limiterGain),
             sanitize(limiterDelay[1][static_cast<std::size_t>(readPos)] * limiterGain) };
}

// ============================================================================
// host-rate entry point
// ============================================================================

void Lucy::processSampleFrame(float inL, float inR, float& outL, float& outR)
{
    const auto enabled = enabledSmoothed.getNextValue();
    const auto blend = outputBlendSmoothed.getNextValue();
    const auto verbMix = verbSmoothed.getNextValue();

    // Two FFTs per hop per channel is the most expensive thing in the plugin;
    // running them to produce a signal nothing hears is the least defensible.
    // The blend reaches zero only when GLOBAL does, so this is still "the user
    // turned it off" rather than "the user turned it down".
    if (blend * enabled <= 1.0e-6f
            && ! outputBlendSmoothed.isSmoothing() && ! enabledSmoothed.isSmoothing())
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

    Frame stage { inL, inR };

    // The reverb comes at the FRONT by default, feeding the loss rather than
    // decorating it - which is what makes the degradation cohesive.
    if (! user.verbPost)
    {
        stage = applyVerb(stage, verbMix);
    }

    // JITTER's timing half, before the transform: the whole point is that the
    // clock feeding the analysis is unstable.
    stage = { applyJitterTiming(0, stage.l), applyJitterTiming(1, stage.r) };
    jitterWrite = (jitterWrite + 1) % jitterSize;

    auto& stft = slowActive ? slowStft : fastStft;
    const auto frame = [this](int channel, float* real, float* imag, int bins)
    {
        spectralFrame(channel, real, imag, bins);
    };

    stage = { sanitize(stft.processSample(0, stage.l, frame)),
              sanitize(stft.processSample(1, stage.r, frame)) };

    // The filter shapes and emphasises the artifacts, so it acts on them.
    stage = applyFilter(stage);

    if (user.verbPost)
    {
        stage = applyVerb(stage, verbMix);
    }

    stage = applyGate(stage);
    stage = applyLimiter(stage);

    const auto gain = juce::Decibels::decibelsToGain(lossGainSmoothed.getNextValue());
    stage = { sanitize(stage.l * gain), sanitize(stage.r * gain) };

    // GLOBAL HAS ALREADY DONE ITS WORK, upstream, in deriveLucyParameters: it
    // scaled the coder's depth and coverage, the packet probability, the
    // filter's amount and the freeze blend. That is what makes it an intensity
    // macro - the character the other controls describe stays recognisable and
    // gets stronger, rather than a fixed wet signal being faded in.
    //
    // What is left here is the bottom few percent of its travel, where the
    // blend ramps 0 -> 1 so the effect can reach genuinely clean and the idle
    // path above has a continuous way in and out. Above that region this is 1
    // and the dry term vanishes. Bypass rides the same blend so it too is a
    // ramp rather than a step.
    const auto amount = blend * enabled;
    outL = inL * (1.0f - amount) + stage.l * amount;
    outR = inR * (1.0f - amount) + stage.r * amount;
}

} // namespace px3
