#include "Delay.h"

#include <cmath>

namespace
{
// Longest delay the buffer can hold. Free mode reaches 2 s; tempo sync reaches
// this, which is one bar at 30 BPM. Slower than that and the sync division is
// clamped, which is a better trade than carrying a buffer sized for tempos
// nothing is written at - the old 16 s allocation was 6 MB per instance at
// 48 kHz and twice that at 96 kHz.
constexpr float kMaxDelaySeconds = 8.0f;

// Free-run delay range. The knob is squared on the way in so the short,
// slap-back end of the range gets most of the travel, which is where small
// changes matter musically.
constexpr float kFreeDelayMinSeconds = 0.015f;
constexpr float kFreeDelayMaxSeconds = 2.0f;

// A bucket-brigade chip of this many stages. MN3005 is 4096; the delay time it
// can reach is stages / (2 * clock), so the clock - and with it the usable
// bandwidth - is fixed by the delay setting.
constexpr float kBbdStages = 4096.0f;

// The longest decay the FEEDBACK control can ask for. Long enough to be a
// held, ambient wash; short enough that it is a decay and not a drone.
constexpr float kMaxDecaySeconds = 30.0f;

// Diffusion allpass lengths in samples at 48 kHz, mutually incommensurate.
constexpr std::array<int, 4> kDiffusionLengths { { 617, 887, 1229, 1523 } };
}

float Delay::clamp01(float v)
{
    return juce::jlimit(0.0f, 1.0f, v);
}

float Delay::lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

float Delay::smoothstep(float x)
{
    const auto t = clamp01(x);
    return t * t * (3.0f - 2.0f * t);
}

float Delay::sanitizeAudioSample(float x)
{
    if (!std::isfinite(x))
    {
        return 0.0f;
    }

    return juce::jlimit(-4.0f, 4.0f, x);
}

float Delay::onePoleCoeff(float hz, float sampleRate)
{
    if (sampleRate <= 0.0f)
    {
        return 1.0f;
    }

    const auto c = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * hz / sampleRate);
    return juce::jlimit(0.0f, 1.0f, c);
}

// Folds to a hard ceiling rather than approaching one asymptotically, so a
// feedback loop pushed past unity settles at a defined level instead of
// creeping upwards for minutes.
float Delay::softSaturate(float x)
{
    constexpr float quarterCycle = 1.57079633f;
    if (x > quarterCycle) return 1.0f;
    if (x < -quarterCycle) return -1.0f;
    return std::sin(x);
}

float Delay::divisionBeatsForIndex(int index)
{
    static constexpr std::array<float, 8> beatDivisions { 0.0f, 4.0f, 2.0f, 1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f };
    const auto clamped = juce::jlimit(0, static_cast<int>(beatDivisions.size()) - 1, index);
    return beatDivisions[static_cast<std::size_t>(clamped)];
}

void Delay::prepare(double sampleRate)
{
    currentSampleRateHz = juce::jmax(1.0, sampleRate);
    const auto sr = static_cast<float>(currentSampleRateHz);
    constexpr float delayControlTauSec = 0.008f;
    delayControlSmoothingCoeff = 1.0f - std::exp(-1.0f / (sr * delayControlTauSec));

    delayBufferSize = juce::jmax(2, static_cast<int>(std::round(currentSampleRateHz * kMaxDelaySeconds)));
    delayBuffer[0].assign(static_cast<std::size_t>(delayBufferSize), 0.0f);
    delayBuffer[1].assign(static_cast<std::size_t>(delayBufferSize), 0.0f);

    constexpr int diffusionA = 127;
    constexpr int diffusionB = 211;
    isaacDiffusionLineA[0].assign(diffusionA, 0.0f);
    isaacDiffusionLineA[1].assign(diffusionA, 0.0f);
    isaacDiffusionLineB[0].assign(diffusionB, 0.0f);
    isaacDiffusionLineB[1].assign(diffusionB, 0.0f);

    // Diffusion allpass lengths scale with sample rate so the smear lasts the
    // same amount of time rather than the same number of samples. The right
    // channel is offset so the two sides do not smear identically.
    const auto scale = static_cast<float>(currentSampleRateHz / 48000.0);
    for (int channel = 0; channel < 2; ++channel)
    {
        for (int stage = 0; stage < diffusionStages; ++stage)
        {
            const auto base = static_cast<float>(kDiffusionLengths[static_cast<std::size_t>(stage)]);
            const auto skew = channel == 0 ? 1.0f : 1.13f;
            const auto length = juce::jmax(8, static_cast<int>(std::round(base * scale * skew)));
            diffusionLines[static_cast<std::size_t>(channel)][static_cast<std::size_t>(stage)]
                .assign(static_cast<std::size_t>(length), 0.0f);
        }
    }

    // A crossfade long enough to be inaudible on a tonal signal but short
    // enough that a tempo change lands promptly.
    crossfadeLengthSamples = juce::jmax(64, static_cast<int>(currentSampleRateHz * 0.020));

    reset();
}

void Delay::reset()
{
    for (auto& line : delayBuffer)
    {
        std::fill(line.begin(), line.end(), 0.0f);
    }

    isaacFeedbackFilter = { { 0.0f, 0.0f } };
    isaacShimmerSmooth = { { 0.0f, 0.0f } };
    clearGranularDiffusionState();

    for (auto& channel : diffusionLines)
    {
        for (auto& line : channel)
        {
            std::fill(line.begin(), line.end(), 0.0f);
        }
    }
    for (auto& channel : diffusionIndices)
    {
        channel.fill(0);
    }

    tapeWowPhase = 0.0f;
    tapeFlutterPhase = 0.0f;
    tapeScrapePhase = 0.0f;
    tapeDriftPhase = 0.0f;
    tapeGapLoss = { { 0.0f, 0.0f } };
    tapeDcX1 = { { 0.0f, 0.0f } };
    tapeDcY1 = { { 0.0f, 0.0f } };
    tapeHysteresis = { { 0.0f, 0.0f } };
    for (auto& s : tapeHeadBump) s.reset();

    for (auto& s : bbdAntiAlias) s.reset();
    for (auto& s : bbdReconstruct) s.reset();
    bbdCompressorEnv = { { 0.0f, 0.0f } };
    bbdExpanderEnv = { { 0.0f, 0.0f } };
    bbdNoiseState = { { 0.0f, 0.0f } };

    for (auto& tap : algorithmTaps)
    {
        tap = CrossfadeTap {};
    }

    writePos = 0;
    isaacSpawnCounter = 0;
    isaacRhythmicStepIndex = 0;
    isaacRhythmicSamplesUntilNext = 0;
    isaacRhythmicSwingToggle = false;
    isaacPanPhase = 0.0f;

    delayAmountSmoothed = 0.0f;
    delayTimeControlSmoothed = 0.5f;
    delayFeedbackControlSmoothed = 0.35f;
    delayModPhaseA = 0.0f;
    delayModPhaseB = 0.0f;
    delayModPhaseC = 0.0f;
    lastDelayAlgorithmIndex = -1;
    lastGranularModeIndex = -1;
    smoothingPrimed = false;

    for (auto& grain : isaacGrains)
    {
        grain.active = false;
    }
}

void Delay::updateForBlock(const DelaySettings& settings)
{
    currentSettings = settings;
    currentSettings.amount = clamp01(settings.amount);
    currentSettings.timeControl = clamp01(settings.timeControl);
    currentSettings.feedbackControl = clamp01(settings.feedbackControl);
    currentSettings.syncDivisionIndex = juce::jlimit(0, 7, settings.syncDivisionIndex);
    currentSettings.algorithmIndex = juce::jlimit(0, 6, settings.algorithmIndex);
    currentSettings.granularModeIndex = juce::jlimit(0, 3, settings.granularModeIndex);
    currentSettings.enabled = settings.enabled;
    currentBpm = juce::jmax(20.0, settings.bpm);

    if (!smoothingPrimed)
    {
        delayAmountSmoothed = currentSettings.amount;
        delayTimeControlSmoothed = currentSettings.timeControl;
        delayFeedbackControlSmoothed = currentSettings.feedbackControl;
        smoothingPrimed = true;
    }

    // Bypassing clears the lines. Without this the delay keeps whatever was in
    // its buffers, and switching it back on replays a tail from before it was
    // turned off - audible as an echo of something the user has moved past.
    if (!currentSettings.enabled)
    {
        if (!bypassCleared)
        {
            reset();
            bypassCleared = true;
        }
        return;
    }
    bypassCleared = false;

    if (currentSettings.algorithmIndex != lastDelayAlgorithmIndex
        || currentSettings.granularModeIndex != lastGranularModeIndex)
    {
        lastDelayAlgorithmIndex = currentSettings.algorithmIndex;
        lastGranularModeIndex = currentSettings.granularModeIndex;
        isaacSpawnCounter = 0;
        isaacRhythmicStepIndex = 0;
        isaacRhythmicSamplesUntilNext = 0;
        isaacRhythmicSwingToggle = false;
        isaacFeedbackFilter = { { 0.0f, 0.0f } };
        isaacShimmerSmooth = { { 0.0f, 0.0f } };
        clearGranularDiffusionState();
        for (auto& s : bbdAntiAlias) s.reset();
        for (auto& s : bbdReconstruct) s.reset();
        for (auto& s : tapeHeadBump) s.reset();
        for (auto& tap : algorithmTaps)
        {
            tap = CrossfadeTap {};
        }
        for (auto& grain : isaacGrains)
        {
            grain.active = false;
        }
    }
}

void Delay::processSampleFrame(float inL, float inR, float& outL, float& outR)
{
    if (!currentSettings.enabled)
    {
        outL = inL;
        outR = inR;
        return;
    }

    processDelayAlgorithmSample(inL,
                                inR,
                                currentSettings.amount,
                                currentSettings.algorithmIndex,
                                currentSettings.timeControl,
                                currentSettings.feedbackControl,
                                currentSettings.syncDivisionIndex,
                                outL,
                                outR);
}

// Four-point Catmull-Rom. Linear interpolation is only 26 dB down on its
// imaging products and its gain droops with the fractional part, so a delay
// whose read position is moving - which is every algorithm in here, either
// from modulation or from a time change - gets a lowpass that wobbles in step
// with the modulation. Cubic is non-recursive, so unlike an allpass
// interpolator it stays well behaved when the delay length changes.
float Delay::readDelaySample(int channel, float readPos) const
{
    const auto& buffer = delayBuffer[static_cast<std::size_t>(channel)];
    const auto size = static_cast<float>(delayBufferSize);
    auto rp = readPos;
    if (rp < 0.0f || rp >= size)
    {
        rp -= size * std::floor(rp / size);
    }
    if (!(rp >= 0.0f && rp < size))
    {
        rp = 0.0f;
    }

    const auto i1 = static_cast<int>(rp);
    const auto frac = rp - static_cast<float>(i1);
    const auto i0 = (i1 - 1 + delayBufferSize) % delayBufferSize;
    const auto i2 = (i1 + 1) % delayBufferSize;
    const auto i3 = (i1 + 2) % delayBufferSize;

    const auto y0 = buffer[static_cast<std::size_t>(i0)];
    const auto y1 = buffer[static_cast<std::size_t>(i1)];
    const auto y2 = buffer[static_cast<std::size_t>(i2)];
    const auto y3 = buffer[static_cast<std::size_t>(i3)];

    const auto c0 = y1;
    const auto c1 = 0.5f * (y2 - y0);
    const auto c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

// Reads a tap that has changed length by fading from the old position to the
// new one. Sliding the read pointer instead - which is what the previous code
// did for every algorithm - pitch-shifts the repeats, which is correct for
// tape and wrong for everything else.
float Delay::readCrossfadedTap(int channel, CrossfadeTap& tap, float targetSamples)
{
    const auto limit = static_cast<float>(delayBufferSize - 4);
    const auto target = juce::jlimit(4.0f, limit, targetSamples);

    if (!tap.primed)
    {
        tap.activeSamples = target;
        tap.fadingSamples = target;
        tap.fadeRemaining = 0;
        tap.primed = true;
    }
    else if (tap.fadeRemaining <= 0 && std::abs(target - tap.activeSamples) > 1.0f)
    {
        tap.fadingSamples = tap.activeSamples;
        tap.activeSamples = target;
        tap.fadeRemaining = crossfadeLengthSamples;
    }

    const auto writeHead = static_cast<float>(writePos);
    const auto current = readDelaySample(channel, writeHead - tap.activeSamples);

    if (tap.fadeRemaining <= 0)
    {
        return current;
    }

    const auto previous = readDelaySample(channel, writeHead - tap.fadingSamples);
    const auto progress = 1.0f - static_cast<float>(tap.fadeRemaining)
                                     / static_cast<float>(juce::jmax(1, crossfadeLengthSamples));
    // Equal-GAIN, deliberately, not equal-power. This tap sits inside a
    // feedback loop, and during a TIME sweep the two positions are only a few
    // milliseconds apart, so what they carry is strongly correlated. An
    // equal-power pair sums to 1.41 on correlated content, which multiplied by
    // a 0.985 feedback coefficient puts the loop over unity: measured as peaks
    // of 1.76 and tails that never died. Linear gains sum to exactly one on
    // correlated content and dip to 0.71 on uncorrelated content, and a
    // momentary dip is the right trade against a runaway.
    --tap.fadeRemaining;
    return current * progress + previous * (1.0f - progress);
}

// Topology-preserving transform state-variable filter (Zavalishin). Stable
// when the cutoff is modulated, which matters because the BBD's cutoff tracks
// the delay time and therefore moves whenever the knob does.
float Delay::processSvfLowpass(Svf& state, float x, float cutoffHz, float q) const
{
    const auto sr = static_cast<float>(currentSampleRateHz);
    const auto fc = juce::jlimit(20.0f, sr * 0.45f, cutoffHz);
    const auto g = std::tan(juce::MathConstants<float>::pi * fc / sr);
    const auto k = 1.0f / juce::jmax(0.05f, q);
    const auto a1 = 1.0f / (1.0f + g * (g + k));
    const auto a2 = g * a1;

    const auto v3 = x - state.ic2;
    const auto v1 = a1 * state.ic1 + a2 * v3;
    const auto v2 = state.ic2 + a2 * state.ic1 + g * a2 * v3;
    state.ic1 = 2.0f * v1 - state.ic1;
    state.ic2 = 2.0f * v2 - state.ic2;
    return std::isfinite(v2) ? v2 : 0.0f;
}

float Delay::processSvfBandpass(Svf& state, float x, float cutoffHz, float q) const
{
    const auto sr = static_cast<float>(currentSampleRateHz);
    const auto fc = juce::jlimit(20.0f, sr * 0.45f, cutoffHz);
    const auto g = std::tan(juce::MathConstants<float>::pi * fc / sr);
    const auto k = 1.0f / juce::jmax(0.05f, q);
    const auto a1 = 1.0f / (1.0f + g * (g + k));
    const auto a2 = g * a1;

    const auto v3 = x - state.ic2;
    const auto v1 = a1 * state.ic1 + a2 * v3;
    const auto v2 = state.ic2 + a2 * state.ic1 + g * a2 * v3;
    state.ic1 = 2.0f * v1 - state.ic1;
    state.ic2 = 2.0f * v2 - state.ic2;
    // Scaled by k so the peak gain is one. The raw v1 output of this topology
    // peaks at Q, not at unity, so a caller that normalises assuming unity -
    // as the tape head bump did - leaves Q times more gain in the loop than it
    // budgeted for, and the loop oscillates at the bump frequency.
    const auto out = k * v1;
    return std::isfinite(out) ? out : 0.0f;
}

float Delay::baseDelaySamplesFor(float timeControl, int syncDivisionIndex) const
{
    const auto syncBeats = divisionBeatsForIndex(syncDivisionIndex);
    float seconds;
    if (syncDivisionIndex > 0 && syncBeats > 0.0f)
    {
        seconds = static_cast<float>(60.0 / currentBpm) * syncBeats;
    }
    else
    {
        // Squared taper: the first half of the knob covers 15-500 ms, where a
        // few milliseconds is the difference between a slap and a doubling.
        const auto t = clamp01(timeControl);
        seconds = kFreeDelayMinSeconds + (kFreeDelayMaxSeconds - kFreeDelayMinSeconds) * t * t;
    }

    seconds = juce::jlimit(0.001f, kMaxDelaySeconds - 0.05f, seconds);
    return seconds * static_cast<float>(currentSampleRateHz);
}

float Delay::processDiffusionChain(int channel, float x, float amount)
{
    auto signal = x;
    const auto c = static_cast<std::size_t>(channel);
    for (int stage = 0; stage < diffusionStages; ++stage)
    {
        const auto s = static_cast<std::size_t>(stage);
        // Alternating signs across the chain, as in Schroeder's original
        // arrangement: same-sign stages build a resonance at the common
        // fundamental of the delay lengths.
        const auto sign = (stage % 2 == 0) ? 1.0f : -1.0f;
        const auto g = sign * lerp(0.30f, 0.68f, amount);
        signal = processAllpassSample(signal,
                                      diffusionLines[c][s],
                                      diffusionIndices[c][s],
                                      g);
    }
    return signal;
}

void Delay::clearGranularDiffusionState()
{
    for (auto& line : isaacDiffusionLineA)
    {
        std::fill(line.begin(), line.end(), 0.0f);
    }
    for (auto& line : isaacDiffusionLineB)
    {
        std::fill(line.begin(), line.end(), 0.0f);
    }
    isaacDiffusionIndexA = { { 0, 0 } };
    isaacDiffusionIndexB = { { 0, 0 } };
}

float Delay::processAllpassSample(float x,
                                  std::vector<float>& line,
                                  int& index,
                                  float feedback) const
{
    if (line.empty())
    {
        return x;
    }

    auto& d = line[static_cast<std::size_t>(index)];
    const auto g = juce::jlimit(-0.82f, 0.82f, feedback);
    const auto y = -g * x + d;
    d = x + g * y;

    ++index;
    if (index >= static_cast<int>(line.size()))
    {
        index = 0;
    }

    return std::isfinite(y) ? y : 0.0f;
}

void Delay::processGranularDiffusion(float& wetL,
                                     float& wetR,
                                     float diffusionAmount,
                                     float stereoAmount)
{
    const auto d = juce::jlimit(0.0f, 1.0f, diffusionAmount);
    if (d <= 0.0001f)
    {
        return;
    }

    const auto gA = lerp(0.38f, 0.74f, d);
    const auto gB = lerp(0.32f, 0.68f, d);

    auto dl = processAllpassSample(wetL, isaacDiffusionLineA[0], isaacDiffusionIndexA[0], gA);
    dl = processAllpassSample(dl, isaacDiffusionLineB[0], isaacDiffusionIndexB[0], gB);

    // Opposite signs on the right channel, so the two sides diffuse into
    // different patterns instead of the same one. Matching chains on both
    // channels produce a perfectly correlated - that is, mono - smear.
    auto dr = processAllpassSample(wetR, isaacDiffusionLineA[1], isaacDiffusionIndexA[1], -gA);
    dr = processAllpassSample(dr, isaacDiffusionLineB[1], isaacDiffusionIndexB[1], gB);

    const auto width = juce::jlimit(0.0f, 1.0f, stereoAmount);
    const auto cross = 0.08f + 0.28f * d;
    const auto widenedL = dl * (1.0f - cross) + dr * cross;
    const auto widenedR = dr * (1.0f - cross) + dl * cross;
    wetL = lerp(wetL, widenedL, d * (0.50f + 0.40f * width));
    wetR = lerp(wetR, widenedR, d * (0.50f + 0.40f * width));
}

void Delay::renderActiveGranularGrains(float& wetL, float& wetR)
{
    wetL = 0.0f;
    wetR = 0.0f;

    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    for (auto& grain : isaacGrains)
    {
        if (!grain.active)
        {
            continue;
        }

        const auto age = static_cast<float>(grain.ageSamples);
        const auto len = static_cast<float>(juce::jmax(1, grain.lengthSamples));
        const auto phase = age / len;

        if (phase >= 1.0f)
        {
            grain.active = false;
            continue;
        }

        const auto window = 0.5f - 0.5f * std::cos(phase * twoPi);
        const auto g = grain.gain * window;

        const auto left = readDelaySample(0, grain.readPos);
        const auto right = readDelaySample(1, grain.readPos);
        // Each grain draws from its own point in the stereo field rather than
        // from a mono sum, so a wide input is still wide after granulation.
        const auto source = left * (1.0f - grain.sourceBalance) + right * grain.sourceBalance;

        const auto pan = clamp01(grain.pan);
        const auto panAngle = pan * juce::MathConstants<float>::halfPi;
        const auto gainL = std::cos(panAngle);
        const auto gainR = std::sin(panAngle);

        wetL += source * g * gainL;
        wetR += source * g * gainR;

        grain.readPos += grain.reverse ? -std::abs(grain.increment) : std::abs(grain.increment);
        while (grain.readPos < 0.0f)
        {
            grain.readPos += static_cast<float>(delayBufferSize);
        }
        while (grain.readPos >= static_cast<float>(delayBufferSize))
        {
            grain.readPos -= static_cast<float>(delayBufferSize);
        }

        ++grain.ageSamples;
    }
}

void Delay::spawnIsaacGrain(float amount,
                            float timeControl,
                            float feedbackControl,
                            int syncDivisionIndex,
                            GranularMode mode,
                            int rhythmicStep)
{
    for (auto& grain : isaacGrains)
    {
        if (grain.active)
        {
            continue;
        }

        auto& random = juce::Random::getSystemRandom();
        grain.active = true;
        grain.reverse = false;

        const auto a = smoothstep(amount);
        const auto macro = std::pow(a, 0.62f);
        const auto sizeCtrl = juce::jlimit(0.0f, 1.0f, timeControl);
        const auto feedbackCtrl = juce::jlimit(0.0f, 1.0f, feedbackControl);

        float grainMs = lerp(35.0f, 170.0f, a);
        float semitone = 0.0f;
        float pitchMicro = 0.0f;
        float reverseChance = 0.0f;
        float panSpread = lerp(0.14f, 0.88f, macro);
        float baseDelayBeats = lerp(0.125f, 0.75f, macro);
        float jitterWidth = 0.06f + 0.22f * macro;
        float gain = lerp(0.11f, 0.30f, macro);

        if (mode == GranularMode::classic)
        {
            const std::array<int, 7> intervals { -12, -7, -5, 0, 5, 7, 12 };
            const auto chooseWide = random.nextFloat() < (0.22f + 0.70f * macro);
            const auto idx = chooseWide ? random.nextInt(static_cast<int>(intervals.size())) : 3;
            semitone = static_cast<float>(intervals[static_cast<std::size_t>(idx)]);
            pitchMicro = (random.nextFloat() - 0.5f) * (0.16f + 0.26f * macro);
            reverseChance = 0.0f;
        }
        else if (mode == GranularMode::cloud)
        {
            const std::array<int, 9> intervals { 0, 0, 0, 7, -7, 12, -12, 5, -5 };
            semitone = static_cast<float>(intervals[static_cast<std::size_t>(random.nextInt(static_cast<int>(intervals.size())))]);
            pitchMicro = (random.nextFloat() - 0.5f) * (0.10f + 0.20f * macro);
            grainMs = lerp(45.0f, 260.0f, sizeCtrl);
            panSpread = lerp(0.35f, 0.98f, a);
            reverseChance = 0.08f + 0.52f * feedbackCtrl;
            baseDelayBeats = lerp(0.18f, 0.95f, sizeCtrl);
            jitterWidth = 0.03f + 0.14f * a;
            gain = lerp(0.08f, 0.22f, macro);
        }
        else if (mode == GranularMode::shimmer)
        {
            const std::array<int, 5> shimmerIntervals { 12, 7, 5, 19, 24 };
            const auto idx = juce::jlimit(0,
                                          static_cast<int>(shimmerIntervals.size()) - 1,
                                          static_cast<int>(std::round(sizeCtrl * static_cast<float>(shimmerIntervals.size() - 1))));
            semitone = static_cast<float>(shimmerIntervals[static_cast<std::size_t>(idx)]);
            pitchMicro = (random.nextFloat() - 0.5f) * 0.10f;
            grainMs = lerp(70.0f, 230.0f, sizeCtrl);
            panSpread = lerp(0.24f, 0.80f, a);
            reverseChance = 0.04f + 0.20f * feedbackCtrl;
            baseDelayBeats = lerp(0.25f, 1.0f, sizeCtrl);
            jitterWidth = 0.01f + 0.06f * a;
            gain = lerp(0.10f, 0.24f, macro);
        }
        else
        {
            static constexpr std::array<std::array<int, 8>, 3> pitchPatterns { {
                { 0, 7, 12, 7, 0, 7, 12, 7 },
                { 0, 12, 0, 7, 0, 12, 0, 7 },
                { 0, -12, 12, 0, 0, -12, 12, 0 }
            } };
            const auto patternIndex = juce::jlimit(0, 2, static_cast<int>(std::floor(sizeCtrl * 2.99f)));
            semitone = static_cast<float>(pitchPatterns[static_cast<std::size_t>(patternIndex)][static_cast<std::size_t>(rhythmicStep & 7)]);
            pitchMicro = 0.0f;
            grainMs = lerp(22.0f, 110.0f, sizeCtrl);
            panSpread = lerp(0.30f, 0.92f, a);
            reverseChance = juce::jlimit(0.0f, 0.9f, feedbackCtrl);
            baseDelayBeats = juce::jmax(0.0625f, divisionBeatsForIndex(syncDivisionIndex));
            jitterWidth = 0.0f;
            gain = lerp(0.10f, 0.26f, macro);
        }

        grain.lengthSamples = juce::jmax(24,
                                         static_cast<int>(std::round(grainMs * 0.001f * static_cast<float>(currentSampleRateHz))));
        grain.ageSamples = 0;
        grain.reverse = random.nextFloat() < reverseChance;

        const auto ratio = std::pow(2.0f, (semitone + pitchMicro) / 12.0f);
        grain.increment = juce::jlimit(0.25f, 4.0f, std::abs(ratio));

        const auto secPerBeat = static_cast<float>(60.0 / juce::jmax(20.0, currentBpm));
        const auto syncBeats = divisionBeatsForIndex(syncDivisionIndex);
        const auto syncEnabled = syncDivisionIndex > 0 && syncBeats > 0.0f;
        if (syncEnabled)
        {
            baseDelayBeats = syncBeats;
            if (mode == GranularMode::rhythmic)
            {
                jitterWidth = 0.0f;
            }
            else
            {
                jitterWidth = juce::jmin(jitterWidth, 0.07f);
            }
        }

        const auto baseDelaySamples = baseDelayBeats * secPerBeat * static_cast<float>(currentSampleRateHz);
        const auto jitter = (random.nextFloat() - 0.5f) * jitterWidth * baseDelaySamples;
        auto readPos = static_cast<float>(writePos) - (baseDelaySamples + jitter);
        while (readPos < 0.0f)
        {
            readPos += static_cast<float>(delayBufferSize);
        }
        while (readPos >= static_cast<float>(delayBufferSize))
        {
            readPos -= static_cast<float>(delayBufferSize);
        }

        if (grain.reverse)
        {
            readPos += static_cast<float>(grain.lengthSamples) * grain.increment;
            while (readPos >= static_cast<float>(delayBufferSize))
            {
                readPos -= static_cast<float>(delayBufferSize);
            }
        }
        grain.readPos = readPos;

        isaacPanPhase += lerp(0.13f, 0.36f, macro);
        if (isaacPanPhase > juce::MathConstants<float>::twoPi)
        {
            isaacPanPhase -= juce::MathConstants<float>::twoPi;
        }

        if (mode == GranularMode::rhythmic)
        {
            const auto alt = ((rhythmicStep & 1) == 0) ? -1.0f : 1.0f;
            grain.pan = 0.5f + alt * 0.5f * panSpread * 0.82f;
        }
        else
        {
            grain.pan = 0.5f + std::sin(isaacPanPhase) * 0.5f * panSpread;
        }

        // Where in the stereo field this grain draws from. Correlated with its
        // destination pan, so a grain placed left mostly carries what was on
        // the left, but not perfectly, so the field stays populated.
        grain.sourceBalance = clamp01(lerp(0.5f, grain.pan, 0.65f));
        grain.gain = gain;
        return;
    }
}

void Delay::processIsaacGranularSample(float inL,
                                       float inR,
                                       float amount,
                                       float timeControl,
                                       float feedbackControl,
                                       int syncDivisionIndex,
                                       float& outL,
                                       float& outR)
{
    if (delayBufferSize <= 1)
    {
        outL = inL;
        outR = inR;
        return;
    }

    const auto dryL = sanitizeAudioSample(inL);
    const auto dryR = sanitizeAudioSample(inR);

    if (amount <= 0.0001f)
    {
        delayBuffer[0][static_cast<std::size_t>(writePos)] = dryL;
        delayBuffer[1][static_cast<std::size_t>(writePos)] = dryR;
        writePos = (writePos + 1) % delayBufferSize;
        outL = dryL;
        outR = dryR;
        return;
    }

    const auto mode = static_cast<GranularMode>(juce::jlimit(0, 3, currentSettings.granularModeIndex));
    const auto a = smoothstep(amount);
    const auto macro = std::pow(a, 0.62f);
    const auto secPerBeat = static_cast<float>(60.0 / juce::jmax(20.0, currentBpm));
    const auto syncBeats = divisionBeatsForIndex(syncDivisionIndex);

    if (mode == GranularMode::rhythmic)
    {
        if (isaacRhythmicSamplesUntilNext <= 0)
        {
            const std::array<std::array<int, 16>, 3> triggerPatterns { {
                { 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 0 },
                { 1, 0, 1, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 1, 0, 0 },
                { 1, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1 }
            } };

            const auto patternIndex = juce::jlimit(0, 2, static_cast<int>(std::floor(timeControl * 2.99f)));
            const auto step = isaacRhythmicStepIndex & 15;
            const auto shouldTrigger = triggerPatterns[static_cast<std::size_t>(patternIndex)][static_cast<std::size_t>(step)] != 0;
            const auto densityChance = juce::jlimit(0.0f, 1.0f, lerp(0.15f, 0.98f, a));

            if (shouldTrigger || juce::Random::getSystemRandom().nextFloat() < densityChance * 0.22f)
            {
                const auto layers = 1 + (juce::Random::getSystemRandom().nextFloat() < densityChance * 0.26f ? 1 : 0);
                for (int i = 0; i < layers; ++i)
                {
                    spawnIsaacGrain(amount,
                                    timeControl,
                                    feedbackControl,
                                    syncDivisionIndex,
                                    mode,
                                    step + i);
                }
            }

            const auto baseBeats = syncDivisionIndex > 0 && syncBeats > 0.0f
                                       ? syncBeats
                                       : lerp(1.0f, 0.125f, juce::jlimit(0.0f, 1.0f, timeControl));
            const auto swingAmount = juce::jlimit(0.0f, 0.48f, feedbackControl * 0.42f);
            auto stepBeats = baseBeats;
            if (isaacRhythmicSwingToggle)
            {
                stepBeats *= (1.0f + swingAmount);
            }
            else
            {
                stepBeats *= (1.0f - swingAmount);
            }
            isaacRhythmicSwingToggle = !isaacRhythmicSwingToggle;
            isaacRhythmicStepIndex = (isaacRhythmicStepIndex + 1) & 15;
            isaacRhythmicSamplesUntilNext = juce::jmax(8,
                                                       static_cast<int>(std::round(stepBeats * secPerBeat
                                                                                   * static_cast<float>(currentSampleRateHz))));
        }

        --isaacRhythmicSamplesUntilNext;
    }
    else
    {
        auto spawnEverySec = lerp(0.085f, 0.028f, macro) * secPerBeat * 2.0f;
        if (mode == GranularMode::cloud)
        {
            spawnEverySec = lerp(0.040f, 0.009f, a) * lerp(1.08f, 0.72f, juce::jlimit(0.0f, 1.0f, timeControl));
        }
        else if (mode == GranularMode::shimmer)
        {
            spawnEverySec = lerp(0.072f, 0.016f, a);
        }

        if (syncDivisionIndex > 0 && syncBeats > 0.0f)
        {
            auto syncMul = lerp(0.38f, 0.25f, macro);
            if (mode == GranularMode::cloud)
            {
                syncMul = lerp(0.22f, 0.14f, a);
            }
            else if (mode == GranularMode::shimmer)
            {
                syncMul = lerp(0.44f, 0.28f, a);
            }
            spawnEverySec = juce::jmax(0.008f, secPerBeat * syncBeats * syncMul);
        }

        const auto spawnEverySamples = juce::jmax(8,
                                                   static_cast<int>(std::round(spawnEverySec * static_cast<float>(currentSampleRateHz))));
        if (++isaacSpawnCounter >= spawnEverySamples)
        {
            isaacSpawnCounter = 0;
            const auto layerCount = mode == GranularMode::cloud
                                        ? 1 + (juce::Random::getSystemRandom().nextFloat() < a * 0.62f ? 1 : 0)
                                        : 1;
            for (int i = 0; i < layerCount; ++i)
            {
                spawnIsaacGrain(amount,
                                timeControl,
                                feedbackControl,
                                syncDivisionIndex,
                                mode,
                                0);
            }
        }
    }

    float wetL = 0.0f;
    float wetR = 0.0f;
    renderActiveGranularGrains(wetL, wetR);

    float feedback = lerp(0.16f, 0.74f, macro);
    float diffusion = 0.0f;
    float stereo = 0.5f;
    float dampHz = lerp(1400.0f, 5200.0f, macro);

    if (mode == GranularMode::cloud)
    {
        feedback = juce::jlimit(0.0f, 0.86f, 0.20f + 0.66f * feedbackControl);
        diffusion = juce::jlimit(0.0f, 1.0f, 0.22f + 0.72f * feedbackControl);
        stereo = juce::jlimit(0.0f, 1.0f, 0.35f + 0.60f * timeControl);
        dampHz = lerp(1000.0f, 3600.0f, a);
    }
    else if (mode == GranularMode::shimmer)
    {
        feedback = juce::jlimit(0.0f, 0.88f, 0.34f + 0.50f * feedbackControl);
        diffusion = juce::jlimit(0.0f, 1.0f, 0.28f + 0.58f * a);
        stereo = juce::jlimit(0.0f, 1.0f, 0.24f + 0.56f * a);
        dampHz = lerp(800.0f, 2600.0f, a);
    }
    else if (mode == GranularMode::rhythmic)
    {
        feedback = juce::jlimit(0.0f, 0.80f, 0.10f + 0.70f * feedbackControl);
        diffusion = juce::jlimit(0.0f, 0.40f, 0.05f + 0.35f * a);
        stereo = juce::jlimit(0.0f, 1.0f, 0.35f + 0.54f * a);
        dampHz = lerp(1300.0f, 4800.0f, a);
    }

    processGranularDiffusion(wetL, wetR, diffusion, stereo);

    // Sample-rate referenced, so the tail is as dark at 96 kHz as at 44.1.
    const auto dampCoeff = onePoleCoeff(dampHz, static_cast<float>(currentSampleRateHz));
    isaacFeedbackFilter[0] += dampCoeff * (wetL - isaacFeedbackFilter[0]);
    isaacFeedbackFilter[1] += dampCoeff * (wetR - isaacFeedbackFilter[1]);

    if (mode == GranularMode::shimmer)
    {
        const auto shimmerTone = 1.0f + 0.32f * a;
        wetL = std::tanh((wetL * 0.76f + isaacFeedbackFilter[0] * 0.24f) * shimmerTone);
        wetR = std::tanh((wetR * 0.76f + isaacFeedbackFilter[1] * 0.24f) * shimmerTone);

        const auto smoothHz = lerp(2200.0f, 950.0f, a);
        const auto shimmerSmoothCoeff = onePoleCoeff(smoothHz, static_cast<float>(currentSampleRateHz));
        isaacShimmerSmooth[0] += shimmerSmoothCoeff * (wetL - isaacShimmerSmooth[0]);
        isaacShimmerSmooth[1] += shimmerSmoothCoeff * (wetR - isaacShimmerSmooth[1]);
        wetL = isaacShimmerSmooth[0];
        wetR = isaacShimmerSmooth[1];
    }
    else
    {
        wetL = std::tanh((wetL * 0.82f + isaacFeedbackFilter[0] * 0.18f) * (1.0f + 0.25f * macro));
        wetR = std::tanh((wetR * 0.82f + isaacFeedbackFilter[1] * 0.18f) * (1.0f + 0.25f * macro));
    }

    const auto writeL = sanitizeAudioSample(dryL + wetL * feedback);
    const auto writeR = sanitizeAudioSample(dryR + wetR * feedback);

    delayBuffer[0][static_cast<std::size_t>(writePos)] = writeL;
    delayBuffer[1][static_cast<std::size_t>(writePos)] = writeR;
    writePos = (writePos + 1) % delayBufferSize;

    auto wetMix = lerp(0.0f, 0.92f, std::pow(macro, 1.02f));
    auto dryMix = lerp(1.0f, 0.12f, macro);

    if (mode == GranularMode::cloud)
    {
        wetMix = lerp(0.0f, 0.97f, a);
        dryMix = lerp(1.0f, 0.08f, a);
    }
    else if (mode == GranularMode::shimmer)
    {
        wetMix = lerp(0.0f, 0.94f, a);
        dryMix = lerp(1.0f, 0.10f, a);
    }
    else if (mode == GranularMode::rhythmic)
    {
        wetMix = lerp(0.0f, 0.90f, a);
        dryMix = lerp(1.0f, 0.22f, a);
    }

    auto outLeft = sanitizeAudioSample(dryL * dryMix + wetL * wetMix);
    auto outRight = sanitizeAudioSample(dryR * dryMix + wetR * wetMix);

    if (mode == GranularMode::shimmer)
    {
        outLeft = std::tanh(outLeft * 0.94f);
        outRight = std::tanh(outRight * 0.94f);
    }

    outL = outLeft;
    outR = outRight;
}

void Delay::processDelayAlgorithmSample(float inL,
                                        float inR,
                                        float amount,
                                        int algorithmIndex,
                                        float timeControl,
                                        float feedbackControl,
                                        int syncDivisionIndex,
                                        float& outL,
                                        float& outR)
{
    const auto coeff = juce::jlimit(0.0001f, 1.0f, delayControlSmoothingCoeff);
    delayAmountSmoothed += coeff * (clamp01(amount) - delayAmountSmoothed);
    delayTimeControlSmoothed += coeff * (clamp01(timeControl) - delayTimeControlSmoothed);
    delayFeedbackControlSmoothed += coeff * (clamp01(feedbackControl) - delayFeedbackControlSmoothed);

    const auto amountSmooth = clamp01(delayAmountSmoothed);
    const auto timeControlSmooth = clamp01(delayTimeControlSmoothed);
    const auto feedbackControlSmooth = clamp01(delayFeedbackControlSmoothed);

    const auto algo = juce::jlimit(0, 6, algorithmIndex);

    if (algo == 0)
    {
        processIsaacGranularSample(inL,
                                   inR,
                                   amountSmooth,
                                   timeControlSmooth,
                                   feedbackControlSmooth,
                                   syncDivisionIndex,
                                   outL,
                                   outR);
        return;
    }

    if (delayBufferSize <= 1)
    {
        outL = inL;
        outR = inR;
        return;
    }

    const auto dryL = sanitizeAudioSample(inL);
    const auto dryR = sanitizeAudioSample(inR);

    // At zero amount the delay must be a wire. The previous mix law bottomed
    // out at 6% wet, so an untouched DELAY knob still coloured the output.
    if (amountSmooth <= 0.0001f)
    {
        delayBuffer[0][static_cast<std::size_t>(writePos)] = dryL;
        delayBuffer[1][static_cast<std::size_t>(writePos)] = dryR;
        writePos = (writePos + 1) % delayBufferSize;
        outL = dryL;
        outR = dryR;
        return;
    }

    const auto sr = static_cast<float>(currentSampleRateHz);
    const auto a = smoothstep(amountSmooth);
    const auto baseSamples = baseDelaySamplesFor(timeControlSmooth, syncDivisionIndex);

    // FEEDBACK sets a decay TIME, not a per-repeat coefficient, and the
    // coefficient is derived from it with Jot's rule - the same delay
    // compensation the reverb's feedback lines use.
    //
    // A raw coefficient means the knob does something different at every delay
    // setting: 0.98 per repeat is a thirty-second decay on a 100 ms delay and
    // an eleven-minute one on a 2 s delay, which is why the long settings felt
    // like they never stopped. Asking for a decay time instead makes the same
    // knob position mean the same thing across the whole TIME range, and puts a
    // finite bound on the top of the control.
    const auto feedbackShaped = std::pow(feedbackControlSmooth, 0.62f);
    const auto decaySeconds = juce::jmap(feedbackShaped, 0.05f, kMaxDecaySeconds);
    const auto delaySecondsForDecay = juce::jmax(0.002f, baseSamples / juce::jmax(1.0f, sr));
    const auto decayCoefficient = juce::jlimit(0.0f, 0.9995f,
                                               std::pow(10.0f, -3.0f * delaySecondsForDecay
                                                                   / juce::jmax(0.01f, decaySeconds)));

    // Free-running modulators. Three rates rather than one, because a single
    // sine is recognisable as a single sine.
    const auto advance = [sr](float& phase, float hz)
    {
        phase += juce::MathConstants<float>::twoPi * hz / juce::jmax(1.0f, sr);
        while (phase >= juce::MathConstants<float>::twoPi)
        {
            phase -= juce::MathConstants<float>::twoPi;
        }
    };

    float wetL = 0.0f;
    float wetR = 0.0f;
    float writeL = dryL;
    float writeR = dryR;
    float feedback = 0.0f;

    if (algo == 1)
    {
        // ---- TAPE ------------------------------------------------------
        // Wow is capstan eccentricity (about one rotation per second),
        // flutter is roller and motor cogging (several Hz to tens of Hz), and
        // scrape is the tape rubbing across the head. They modulate the tape
        // *speed*, which is to say the delay length - the previous code added
        // its wow term to the sample value instead, which injected a sub-audio
        // tone into the feedback path. Ninety-nine per cent of that
        // algorithm's steady-state energy was below 30 Hz.
        advance(tapeWowPhase, 0.83f);
        advance(tapeFlutterPhase, 7.4f);
        advance(tapeScrapePhase, 38.9f);
        advance(tapeDriftPhase, 0.11f);

        // Fixed, NOT scaled by the mix. This read "0.4 + 0.6 * a", so turning
        // the DELAY amount up drove the wow and flutter depth 2.5x deeper at
        // the same time as it raised the wet level - the modulation sidebands
        // grew fastest exactly where they were most audible. Measured on a
        // 5 kHz tone, the non-harmonic content at full amount was -18.6 dB
        // against the tone; decoupled it is -28.1 dB.
        //
        // A tape machine's wow does not change because you turned up the
        // return, and the amount control is a MIX. 0.55 is what the old
        // expression produced at the amount the factory presets ship with, so
        // they sound as they did.
        constexpr auto depth = 0.55f;
        const auto wow = std::sin(tapeWowPhase) * 0.0035f
                       + std::sin(tapeWowPhase * 2.7f + 1.1f) * 0.0011f;
        const auto flutter = std::sin(tapeFlutterPhase) * 0.0009f
                           + std::sin(tapeFlutterPhase * 1.63f + 0.4f) * 0.0004f;
        const auto scrape = std::sin(tapeScrapePhase) * 0.00012f;
        const auto drift = std::sin(tapeDriftPhase) * 0.0022f;
        // Expressed as a fraction of the delay length, because a longer tape
        // loop accumulates proportionally more speed error.
        const auto speedError = (wow + flutter + scrape + drift) * depth;

        const auto samplesL = baseSamples * (1.0f + speedError);
        const auto samplesR = baseSamples * (1.0f + speedError * 0.87f);

        // Read the pointer directly rather than crossfading: a tape machine
        // *does* pitch-shift when its speed changes, and that is the sound.
        const auto limit = static_cast<float>(delayBufferSize - 4);
        wetL = readDelaySample(0, static_cast<float>(writePos) - juce::jlimit(4.0f, limit, samplesL));
        wetR = readDelaySample(1, static_cast<float>(writePos) - juce::jlimit(4.0f, limit, samplesR));

        for (int channel = 0; channel < 2; ++channel)
        {
            auto& wet = channel == 0 ? wetL : wetR;
            const auto c = static_cast<std::size_t>(channel);

            // Head bump: the record and playback gaps form a low-frequency
            // resonance that every tape echo has and no digital delay does.
            // Normalised so the resonance costs the rest of the spectrum
            // instead of adding gain. Left un-normalised it puts about 3 dB
            // into the loop at 92 Hz, which is more than the feedback headroom,
            // and the algorithm sits there oscillating at 92 Hz - measured as
            // 87x RMS growth over twenty seconds.
            const auto bumpGain = 0.22f + 0.20f * a;
            const auto bump = processSvfBandpass(tapeHeadBump[c], wet, 92.0f, 1.1f);
            wet = (wet + bump * bumpGain) / (1.0f + bumpGain);

            // Gap loss: the playback head cannot resolve wavelengths shorter
            // than its gap, so each pass loses top end. Cumulative across
            // repeats, which is why a tape echo turns to mud rather than
            // getting quieter.
            const auto lossHz = lerp(6500.0f, 2600.0f, a);
            const auto lossCoeff = onePoleCoeff(lossHz, sr);
            tapeGapLoss[c] += lossCoeff * (wet - tapeGapLoss[c]);
            wet = tapeGapLoss[c];

            // Magnetic hysteresis, approximated by biasing the saturation with
            // a little of the previous output. Real tape's transfer curve
            // depends on where it has just been, not only on the input.
            // Scaled so the bias path has unity DC gain. Summing 0.16 of the
            // previous output without compensating is a one-pole with a gain of
            // 1/(1-0.16), i.e. +1.5 dB every pass, which on its own puts the
            // feedback loop over unity no matter what the knob says.
            constexpr float hysteresisBias = 0.16f;
            const auto drive = 0.9f + 1.5f * a;
            const auto biased = (wet + tapeHysteresis[c] * hysteresisBias) * (1.0f - hysteresisBias);
            wet = softSaturate(biased * drive) / drive;
            tapeHysteresis[c] = wet;

            // Series capacitor. Asymmetric saturation makes DC by definition,
            // and DC inside a feedback loop integrates.
            const auto dcCoeff = 1.0f - onePoleCoeff(12.0f, sr);
            const auto x = wet;
            wet = x - tapeDcX1[c] + dcCoeff * tapeDcY1[c];
            tapeDcX1[c] = x;
            tapeDcY1[c] = wet;
        }

        // Just under unity. Tape's saturation would hold a runaway loop at a
        // finite level rather than letting it blow up, but "finite" is not the
        // same as "musical" - what it actually settles into is a sustained tone
        // at whatever frequency has the most loop gain. A long finite decay is
        // the useful end of the control.
        feedback = juce::jlimit(0.0f, 0.995f, decayCoefficient);
        writeL = dryL + wetL * feedback;
        writeR = dryR + wetR * feedback;
    }
    else if (algo == 2)
    {
        // ---- ANALOG / BBD ----------------------------------------------
        // A bucket-brigade chip has a fixed stage count and is clocked at
        // whatever rate gives the wanted delay, so bandwidth and delay time
        // are locked together: short settings are bright, long settings are
        // dark and grainy. That coupling is the format's signature and the
        // previous implementation - a fixed one-pole and a tanh - had none of
        // it. BBD delay times are also short by construction; the chip runs
        // out of stages long before a digital line runs out of buffer.
        const auto bbdSeconds = juce::jlimit(0.005f,
                                             0.62f,
                                             baseSamples / juce::jmax(1.0f, sr));
        const auto clockHz = kBbdStages / (2.0f * bbdSeconds);
        const auto bbdNyquist = juce::jlimit(1200.0f, sr * 0.45f, clockHz * 0.5f);
        const auto filterHz = bbdNyquist * 0.78f;
        const auto samples = juce::jlimit(4.0f,
                                          static_cast<float>(delayBufferSize - 4),
                                          bbdSeconds * sr);

        wetL = readCrossfadedTap(0, algorithmTaps[0], samples);
        wetR = readCrossfadedTap(1, algorithmTaps[1], samples);

        for (int channel = 0; channel < 2; ++channel)
        {
            auto& wet = channel == 0 ? wetL : wetR;
            const auto c = static_cast<std::size_t>(channel);

            // Reconstruction filter on the way out of the chip.
            wet = processSvfLowpass(bbdReconstruct[c], wet, filterHz, 0.707f);

            // Expander, the second half of the compander. Its envelope is
            // tracked separately from the compressor's, and the mismatch
            // between them is what makes a BBD breathe on decays - a faithful
            // artifact, not a defect.
            //
            // The expander gain is LINEAR in the envelope, not its square root.
            // A 2:1 compressor produces x^0.5, so undoing it needs a squaring,
            // which means a gain proportional to the level itself. Using a
            // square root on both halves makes the round trip x^0.75 - and a
            // sub-linear loop gain has a stable non-zero attractor: below
            // A = (0.551*sqrt(fb))^4 it grows, above it decays, so the
            // algorithm converged on a permanent tone at A = 0.078 no matter
            // what was played into it and never decayed to silence.
            const auto expanderCoeff = onePoleCoeff(24.0f, sr);
            bbdExpanderEnv[c] += expanderCoeff * (std::abs(wet) - bbdExpanderEnv[c]);
            const auto expandGain = juce::jlimit(0.25f, 2.0f, bbdExpanderEnv[c] * 6.0f);
            wet *= expandGain;

            bbdNoiseState[c] = 0.86f * bbdNoiseState[c]
                             + 0.14f * (juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f);
        }

        feedback = juce::jlimit(0.0f, 0.995f, decayCoefficient);

        // Compressor on the way in. Companding is what let these chips get a
        // usable noise floor, and it is audible: transients get squashed on
        // the way in and the noise floor lifts behind them on the way out.
        auto compressChannel = [&](float x, std::size_t c)
        {
            const auto compressorCoeff = onePoleCoeff(38.0f, sr);
            bbdCompressorEnv[c] += compressorCoeff * (std::abs(x) - bbdCompressorEnv[c]);
            const auto compressGain = juce::jlimit(0.6f, 3.2f,
                                                   1.0f / std::sqrt(juce::jmax(0.02f, bbdCompressorEnv[c] * 6.0f)));
            const auto compressed = softSaturate(x * compressGain * 0.55f) * 1.4f;
            // Anti-alias filter ahead of the chip's sampling stage.
            return processSvfLowpass(bbdAntiAlias[c], compressed, filterHz, 0.707f);
        };

        writeL = compressChannel(dryL + wetL * feedback, 0);
        writeR = compressChannel(dryR + wetR * feedback, 1);

        // Chip noise is added to what comes out, not to what goes back round,
        // and it is gated by the signal the chip is actually passing.
        //
        // Inside the feedback loop it accumulated to noise/(1-g) and stayed
        // there - a floor that never decayed to silence. Ungated on the output
        // it was quieter but permanent, which is worse: the algorithm hissed
        // at -74 dB with nothing playing and nothing to mask it. Scaling by the
        // expander's envelope keeps what is actually characteristic - hiss
        // lifting as the expander opens on a decay - and lets it go when the
        // delay does.
        const auto noiseGateL = juce::jmin(1.0f, bbdExpanderEnv[0] * 6.0f);
        const auto noiseGateR = juce::jmin(1.0f, bbdExpanderEnv[1] * 6.0f);
        wetL += bbdNoiseState[0] * noiseGateL * 0.0020f;
        wetR += bbdNoiseState[1] * noiseGateR * 0.0020f;
    }
    else if (algo == 3)
    {
        // ---- PING-PONG --------------------------------------------------
        // The previous version read the opposite channel at the same position
        // from a symmetric input, which produced two identical channels - it
        // measured +1.000 correlation, meaning it was not ping-ponging at all.
        // A real ping-pong sums the input to one side and then hands each
        // repeat to the other channel, so the echoes alternate in time.
        const auto samples = baseSamples;
        wetL = readCrossfadedTap(0, algorithmTaps[0], samples);
        wetR = readCrossfadedTap(1, algorithmTaps[1], samples);

        const auto dampHz = lerp(9000.0f, 3200.0f, a);
        const auto dampCoeff = onePoleCoeff(dampHz, sr);
        isaacFeedbackFilter[0] += dampCoeff * (wetL - isaacFeedbackFilter[0]);
        isaacFeedbackFilter[1] += dampCoeff * (wetR - isaacFeedbackFilter[1]);
        const auto dampedL = isaacFeedbackFilter[0];
        const auto dampedR = isaacFeedbackFilter[1];

        feedback = juce::jlimit(0.0f, 0.985f, decayCoefficient);

        // Input enters on the left only; each side feeds the other, so repeat
        // 1 is left, repeat 2 is right, repeat 3 is left, and so on.
        const auto monoIn = 0.5f * (dryL + dryR);
        writeL = monoIn + dampedR * feedback;
        writeR = dampedL * feedback;
    }
    else if (algo == 4)
    {
        // ---- STEREO -----------------------------------------------------
        // Two independent lines at a musical ratio rather than the same line
        // scaled by 0.82 and 1.28. The dotted-versus-straight relationship is
        // what makes a dual-time delay sound like a rhythm rather than a
        // smeared single tap.
        const auto samplesL = baseSamples * 0.6667f;   // two thirds
        const auto samplesR = baseSamples;
        wetL = readCrossfadedTap(0, algorithmTaps[0], samplesL);
        wetR = readCrossfadedTap(1, algorithmTaps[1], samplesR);

        const auto dampHz = lerp(11000.0f, 4200.0f, a);
        const auto dampCoeff = onePoleCoeff(dampHz, sr);
        isaacFeedbackFilter[0] += dampCoeff * (wetL - isaacFeedbackFilter[0]);
        isaacFeedbackFilter[1] += dampCoeff * (wetR - isaacFeedbackFilter[1]);

        feedback = juce::jlimit(0.0f, 0.985f, decayCoefficient);
        // A little cross-coupling so the two sides converse instead of running
        // as two unrelated mono delays, but well short of collapsing them.
        constexpr float cross = 0.18f;
        writeL = dryL + (isaacFeedbackFilter[0] * (1.0f - cross) + isaacFeedbackFilter[1] * cross) * feedback;
        writeR = dryR + (isaacFeedbackFilter[1] * (1.0f - cross) + isaacFeedbackFilter[0] * cross) * feedback;
    }
    else if (algo == 5)
    {
        // ---- MODULATED --------------------------------------------------
        // Three modulators at incommensurate rates, opposed between the
        // channels. One sine per side reads as vibrato; several beating
        // against each other read as an ensemble.
        advance(delayModPhaseA, 0.27f);
        advance(delayModPhaseB, 0.41f);
        advance(delayModPhaseC, 1.13f);

        // Fixed for the same reason as TAPE's wow depth: this was
        // "0.0009 + 0.0042 * a", a 5.7x swing driven by the wet mix, so the
        // modulation got deepest exactly as it got loudest. 0.0018 is what the
        // old expression gave at the amount the presets ship with.
        constexpr auto depthSeconds = 0.0018f;
        const auto depthSamples = depthSeconds * sr;
        const auto modL = std::sin(delayModPhaseA)
                        + 0.6f * std::sin(delayModPhaseB * 1.31f + 1.2f)
                        + 0.25f * std::sin(delayModPhaseC);
        const auto modR = std::sin(delayModPhaseA + 2.09f)
                        + 0.6f * std::sin(delayModPhaseB * 1.31f - 0.7f)
                        + 0.25f * std::sin(delayModPhaseC * 0.87f + 1.9f);

        const auto limit = static_cast<float>(delayBufferSize - 4);
        const auto samplesL = juce::jlimit(4.0f, limit, baseSamples + depthSamples * modL * 0.55f);
        const auto samplesR = juce::jlimit(4.0f, limit, baseSamples + depthSamples * modR * 0.55f);

        // Modulated taps slide rather than crossfade - the pitch movement is
        // the point - which is exactly the case cubic interpolation exists for.
        wetL = readDelaySample(0, static_cast<float>(writePos) - samplesL);
        wetR = readDelaySample(1, static_cast<float>(writePos) - samplesR);

        const auto dampHz = lerp(10000.0f, 3800.0f, a);
        const auto dampCoeff = onePoleCoeff(dampHz, sr);
        isaacFeedbackFilter[0] += dampCoeff * (wetL - isaacFeedbackFilter[0]);
        isaacFeedbackFilter[1] += dampCoeff * (wetR - isaacFeedbackFilter[1]);

        feedback = juce::jlimit(0.0f, 0.98f, decayCoefficient);
        writeL = dryL + isaacFeedbackFilter[0] * feedback;
        writeR = dryR + isaacFeedbackFilter[1] * feedback;
    }
    else
    {
        // ---- DIFFUSION ---------------------------------------------------
        // Was a four-tap read mixed together, which is a multitap, not a
        // diffuser: the repeats stayed discrete and the two channels measured
        // +0.950 correlated. An allpass chain in the feedback path smears each
        // repeat a little more than the last, so the echoes dissolve into a
        // wash instead of ticking.
        const auto samples = baseSamples;
        wetL = readCrossfadedTap(0, algorithmTaps[0], samples);
        wetR = readCrossfadedTap(1, algorithmTaps[1], samples);

        const auto dampHz = lerp(9000.0f, 3000.0f, a);
        const auto dampCoeff = onePoleCoeff(dampHz, sr);
        isaacFeedbackFilter[0] += dampCoeff * (wetL - isaacFeedbackFilter[0]);
        isaacFeedbackFilter[1] += dampCoeff * (wetR - isaacFeedbackFilter[1]);

        const auto diffusedL = processDiffusionChain(0, isaacFeedbackFilter[0], a);
        const auto diffusedR = processDiffusionChain(1, isaacFeedbackFilter[1], a);

        wetL = diffusedL;
        wetR = diffusedR;

        feedback = juce::jlimit(0.0f, 0.97f, decayCoefficient);
        writeL = dryL + diffusedL * feedback;
        writeR = dryR + diffusedR * feedback;
    }

    // One shared safety net on the way into the buffer. Every algorithm above
    // either saturates in its own loop or keeps feedback below unity, so this
    // should never engage on musical material; it is here so that a pathological
    // automation sweep cannot leave the line ringing at the clamp.
    writeL = softSaturate(writeL * 0.72f) / 0.72f;
    writeR = softSaturate(writeR * 0.72f) / 0.72f;

    delayBuffer[0][static_cast<std::size_t>(writePos)] = sanitizeAudioSample(writeL);
    delayBuffer[1][static_cast<std::size_t>(writePos)] = sanitizeAudioSample(writeR);
    writePos = (writePos + 1) % delayBufferSize;

    // Reaches zero wet at zero amount, and stays roughly level-constant across
    // the range rather than getting louder as it gets wetter.
    const auto wetMix = 0.92f * std::pow(a, 0.85f);
    const auto dryMix = std::sqrt(juce::jmax(0.0f, 1.0f - 0.88f * a));

    outL = sanitizeAudioSample(dryL * dryMix + wetL * wetMix);
    outR = sanitizeAudioSample(dryR * dryMix + wetR * wetMix);
}
