#include "Mood.h"

#include <cmath>

namespace
{
// Buffer lengths in seconds of *internal* samples. At the slowest clock the
// engine runs eight times under the host rate, so these hold eight times as
// much wall-clock audio as their names suggest - which is how a 4 s buffer
// reaches the half-a-second-to-sixteen-seconds loop range the control implies.
constexpr float kHistorySeconds = 4.0f;
constexpr float kWetSeconds = 3.0f;

// The clock divider is quantised to semitone ratios so that moving CLOCK
// transposes the loop and the wet channel in musical steps rather than sliding
// through every microtone in between.
constexpr int kClockMaxSteps = 36;   // three octaves

// Allpass lengths for the reverb mode's diffusion, mutually incommensurate.
constexpr std::array<int, 4> kDiffusionLengths { { 331, 457, 619, 797 } };

// Playback speeds for TAPE, exactly the harmonised set the mode is specified
// with: quarter, half, unity and double, in each direction.
constexpr std::array<float, 8> kTapeSpeeds { { -4.0f, -2.0f, -1.0f, -0.5f,
                                                0.5f, 1.0f, 2.0f, 4.0f } };
}

float Mood::clamp01(float v)
{
    return juce::jlimit(0.0f, 1.0f, v);
}

float Mood::sanitizeAudioSample(float v)
{
    if (!std::isfinite(v))
    {
        return 0.0f;
    }
    return juce::jlimit(-3.5f, 3.5f, v);
}

float Mood::semitoneRatio(float semitones)
{
    return std::pow(2.0f, semitones / 12.0f);
}

float Mood::softSaturate(float x)
{
    constexpr float quarterCycle = 1.57079633f;
    if (x > quarterCycle) return 1.0f;
    if (x < -quarterCycle) return -1.0f;
    return std::sin(x);
}

float Mood::onePoleCoeff(float hz, float sampleRate)
{
    if (sampleRate <= 0.0f)
    {
        return 1.0f;
    }
    const auto c = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * hz / sampleRate);
    return juce::jlimit(0.0f, 1.0f, c);
}

Mood::Frame Mood::panStereo(float mono, float position)
{
    const auto p = juce::jlimit(-1.0f, 1.0f, position);
    const auto angle = (p + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
    return { mono * std::cos(angle) * 1.41421356f * 0.70710678f,
             mono * std::sin(angle) * 1.41421356f * 0.70710678f };
}

void Mood::prepare(double sampleRate)
{
    sampleRateHz = juce::jmax(1.0, sampleRate);
    internalSampleRate = sampleRateHz;

    constexpr double smoothingSeconds = 0.025;
    enabledSmoothed.reset(sampleRateHz, 0.030);
    mixSmoothed.reset(sampleRateHz, smoothingSeconds);
    clockSmoothed.reset(sampleRateHz, smoothingSeconds);
    routingSmoothed.reset(sampleRateHz, smoothingSeconds);
    wetTimeSmoothed.reset(sampleRateHz, smoothingSeconds);
    wetModifySmoothed.reset(sampleRateHz, smoothingSeconds);
    loopLengthSmoothed.reset(sampleRateHz, smoothingSeconds);
    loopModifySmoothed.reset(sampleRateHz, smoothingSeconds);
    feedbackSmoothed.reset(sampleRateHz, smoothingSeconds);
    spreadSmoothed.reset(sampleRateHz, smoothingSeconds);
    degradeSmoothed.reset(sampleRateHz, smoothingSeconds);

    historySize = juce::jmax(2, static_cast<int>(std::round(sampleRateHz * kHistorySeconds)));
    wetSize = juce::jmax(2, static_cast<int>(std::round(sampleRateHz * kWetSeconds)));

    historyBuffer[0].assign(static_cast<std::size_t>(historySize), 0.0f);
    historyBuffer[1].assign(static_cast<std::size_t>(historySize), 0.0f);
    wetBuffer[0].assign(static_cast<std::size_t>(wetSize), 0.0f);
    wetBuffer[1].assign(static_cast<std::size_t>(wetSize), 0.0f);

    // Longest slice ENV can capture, plus headroom.
    const auto maxSliceSamples = juce::jmax(64, static_cast<int>(std::round(sampleRateHz * 0.45)));
    envSliceBuffer[0].assign(static_cast<std::size_t>(maxSliceSamples), 0.0f);
    envSliceBuffer[1].assign(static_cast<std::size_t>(maxSliceSamples), 0.0f);

    const auto scale = static_cast<float>(sampleRateHz / 48000.0);
    for (int channel = 0; channel < 2; ++channel)
    {
        for (int stage = 0; stage < kDiffusionStages; ++stage)
        {
            const auto base = static_cast<float>(kDiffusionLengths[static_cast<std::size_t>(stage)]);
            // The two channels diffuse over different lengths, or they produce
            // the same smear and the result is a wider-sounding mono.
            const auto skew = channel == 0 ? 1.0f : 1.17f;
            const auto length = juce::jmax(8, static_cast<int>(std::round(base * scale * skew)));
            diffusionLines[static_cast<std::size_t>(channel)][static_cast<std::size_t>(stage)]
                .assign(static_cast<std::size_t>(length), 0.0f);
        }
    }

    reset();
}

void Mood::reset()
{
    for (auto& line : historyBuffer) std::fill(line.begin(), line.end(), 0.0f);
    for (auto& line : wetBuffer) std::fill(line.begin(), line.end(), 0.0f);
    for (auto& channel : diffusionLines)
    {
        for (auto& line : channel) std::fill(line.begin(), line.end(), 0.0f);
    }
    for (auto& channel : diffusionIndices) channel.fill(0);

    historyWritePos = 0;
    wetWritePos = 0;
    loopReadPos = 0.0f;
    loopHeldReadPos = 0.0f;
    envFollower = 0.0f;
    envPanPhase = 0.0f;
    envPanDirection = 1.0f;
    envGateOpen = false;
    envSliceHoldSamples = 0;
    envSliceLength = 0;
    envSliceWritePos = 0;
    envSliceOrigin = 0.0f;
    envSliceReadPos = 0.0f;
    envSliceBlend = 0.0f;
    for (auto& line : envSliceBuffer) std::fill(line.begin(), line.end(), 0.0f);
    stretchSpawnCounter = 0;
    stretchPanPhase = 0.0f;
    slipReadPos = 0.0f;
    slipPanPhase = 0.0f;

    clockPhase = 0.0f;
    clockIncrement = 1.0f;
    clockDivider = 1.0f;
    internalSampleRate = sampleRateHz;
    heldOutput = {};

    wetDampState = { { 0.0f, 0.0f } };
    degradeNoiseState = { { 0.0f, 0.0f } };
    degradeEnvelope = { { 0.0f, 0.0f } };
    degradeHeld = { { 0.0f, 0.0f } };
    degradeHoldCounter = { { 0, 0 } };
    reverbFeedbackStore = { { 0.0f, 0.0f } };

    for (auto& grain : grains) grain.active = false;
    for (auto& tap : wetTaps) tap = CrossfadeTap {};

    mixSmoothed.setCurrentAndTargetValue(currentSettings.mix);
    enabledSmoothed.setCurrentAndTargetValue(currentSettings.enabled ? 1.0f : 0.0f);
    clockSmoothed.setCurrentAndTargetValue(currentSettings.clock);
    routingSmoothed.setCurrentAndTargetValue(currentSettings.routing);
    wetTimeSmoothed.setCurrentAndTargetValue(currentSettings.wetTime);
    wetModifySmoothed.setCurrentAndTargetValue(currentSettings.wetModify);
    loopLengthSmoothed.setCurrentAndTargetValue(currentSettings.loopLength);
    loopModifySmoothed.setCurrentAndTargetValue(currentSettings.loopModify);
    feedbackSmoothed.setCurrentAndTargetValue(currentSettings.feedback);
    spreadSmoothed.setCurrentAndTargetValue(currentSettings.spread);
    degradeSmoothed.setCurrentAndTargetValue(currentSettings.degrade);
}

void Mood::updateForBlock(const MoodSettings& settings)
{
    const auto nextEnabled = settings.enabled;
    if (wasEnabled && !nextEnabled)
    {
        pendingResetOnBypass = true;
    }
    wasEnabled = nextEnabled;

    currentSettings = settings;
    enabledSmoothed.setTargetValue(nextEnabled ? 1.0f : 0.0f);

    mixSmoothed.setTargetValue(clamp01(settings.mix));
    clockSmoothed.setTargetValue(clamp01(settings.clock));
    routingSmoothed.setTargetValue(clamp01(settings.routing));
    wetTimeSmoothed.setTargetValue(clamp01(settings.wetTime));
    wetModifySmoothed.setTargetValue(clamp01(settings.wetModify));
    loopLengthSmoothed.setTargetValue(clamp01(settings.loopLength));
    loopModifySmoothed.setTargetValue(clamp01(settings.loopModify));
    feedbackSmoothed.setTargetValue(clamp01(settings.feedback));
    spreadSmoothed.setTargetValue(clamp01(settings.spread));
    degradeSmoothed.setTargetValue(clamp01(settings.degrade));

    currentSettings.wetModeIndex = juce::jlimit(0, 2, settings.wetModeIndex);
    currentSettings.loopModeIndex = juce::jlimit(0, 2, settings.loopModeIndex);
}

// Four-point Catmull-Rom. Every read pointer in here moves at a speed the user
// is setting - half speed, double speed, reversed, stretched - so the
// interpolator is in the signal path continuously rather than occasionally, and
// linear interpolation's frequency-dependent droop would be audible as a dull,
// wavering top end.
float Mood::readInterp(const std::vector<float>& line, float pos) const
{
    if (line.empty())
    {
        return 0.0f;
    }

    const auto size = static_cast<int>(line.size());
    const auto sizef = static_cast<float>(size);
    auto p = pos;
    if (p < 0.0f || p >= sizef)
    {
        p -= sizef * std::floor(p / sizef);
    }
    if (!(p >= 0.0f && p < sizef))
    {
        p = 0.0f;
    }

    const auto i1 = static_cast<int>(p);
    const auto frac = p - static_cast<float>(i1);
    const auto i0 = (i1 - 1 + size) % size;
    const auto i2 = (i1 + 1) % size;
    const auto i3 = (i1 + 2) % size;

    const auto y0 = line[static_cast<std::size_t>(i0)];
    const auto y1 = line[static_cast<std::size_t>(i1)];
    const auto y2 = line[static_cast<std::size_t>(i2)];
    const auto y3 = line[static_cast<std::size_t>(i3)];

    const auto c0 = y1;
    const auto c1 = 0.5f * (y2 - y0);
    const auto c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

// Blends the last `fadeLength` samples of the loop into its beginning, so that
// when the pointer wraps the signal is already where it is about to land. Read
// pointers running backwards pass through the same region, so one splice covers
// both directions.
//
// The crossfade is equal-gain rather than equal-power. The two sides of a
// splice are different parts of the recording and therefore uncorrelated, which
// is the case where equal-power sums above unity - and this output is recycled
// into the loop, so anything above unity accumulates.
float Mood::readSpliced(const std::vector<float>& line,
                        float startAbs,
                        float pos,
                        float loopLength,
                        float fadeLength) const
{
    const auto here = readInterp(line, startAbs + pos);
    if (fadeLength <= 1.0f || loopLength <= fadeLength * 2.0f)
    {
        return here;
    }

    const auto fadeBegins = loopLength - fadeLength;
    if (pos < fadeBegins)
    {
        return here;
    }

    const auto t = juce::jlimit(0.0f, 1.0f, (pos - fadeBegins) / fadeLength);
    const auto wrapped = readInterp(line, startAbs + pos - loopLength);
    return here * (1.0f - t) + wrapped * t;
}

float Mood::spliceFadeFor(float loopLength) const
{
    // Long enough to remove the step, short enough not to smear a rhythmic
    // loop, and never more than a quarter of the loop itself so that very short
    // slices are still recognisable rather than being all crossfade.
    const auto preferred = 0.006f * static_cast<float>(internalSampleRate);
    return juce::jmin(preferred, loopLength * 0.25f);
}

float Mood::readAllpass(int channel, int stage, float x, float g)
{
    auto& line = diffusionLines[static_cast<std::size_t>(channel)][static_cast<std::size_t>(stage)];
    if (line.empty())
    {
        return x;
    }

    auto& index = diffusionIndices[static_cast<std::size_t>(channel)][static_cast<std::size_t>(stage)];
    auto& d = line[static_cast<std::size_t>(index)];
    const auto coeff = juce::jlimit(-0.78f, 0.78f, g);
    const auto y = -coeff * x + d;
    d = x + coeff * y;

    ++index;
    if (index >= static_cast<int>(line.size()))
    {
        index = 0;
    }
    return std::isfinite(y) ? y : 0.0f;
}

void Mood::writeHistory(float l, float r)
{
    historyBuffer[0][static_cast<std::size_t>(historyWritePos)] = sanitizeAudioSample(l);
    historyBuffer[1][static_cast<std::size_t>(historyWritePos)] = sanitizeAudioSample(r);

    ++historyWritePos;
    if (historyWritePos >= historySize)
    {
        historyWritePos = 0;
    }
}

void Mood::writeWet(float l, float r)
{
    wetBuffer[0][static_cast<std::size_t>(wetWritePos)] = sanitizeAudioSample(l);
    wetBuffer[1][static_cast<std::size_t>(wetWritePos)] = sanitizeAudioSample(r);

    ++wetWritePos;
    if (wetWritePos >= wetSize)
    {
        wetWritePos = 0;
    }
}

// Quantisation noise plus a noise floor that rises as the control does. Applied
// to what is written back into the loop, so it compounds pass over pass rather
// than sitting as a fixed veneer over the output - loops are supposed to
// degrade the longer they go round.
float Mood::applyDegradation(float x, int channel)
{
    const auto d = clamp01(currentSettings.degrade);
    if (d <= 0.0001f)
    {
        return x;
    }

    const auto c = static_cast<std::size_t>(channel);

    // Bit depth from 16 down to about 3. Quantising the wrong way round - to a
    // fixed step regardless of level - is what makes a lo-fi effect sound like
    // a broken gate rather than an old sampler, so the step is scaled by a
    // nominal level, not by the sample itself.
    const auto bits = juce::jmap(d, 16.0f, 3.0f);
    const auto levels = std::pow(2.0f, bits);
    const auto step = 2.0f / levels;
    auto y = std::round(x / step) * step;

    // Sample-rate reduction on top of the bit reduction. Bit-crushing alone
    // only ever adds a noise floor; it is the downsampling that produces the
    // aliased, metallic ring that reads as a degraded recording. Held here
    // rather than by dividing the engine clock, so DEGRADE roughens the loop
    // without also transposing it - that is CLOCK's job.
    const auto holdLength = 1 + static_cast<int>(d * d * 11.0f);
    if (holdLength > 1)
    {
        if (degradeHoldCounter[c] <= 0)
        {
            degradeHeld[c] = y;
            degradeHoldCounter[c] = holdLength;
        }
        --degradeHoldCounter[c];
        y = degradeHeld[c];
    }

    // Rising noise floor, lowpassed so it is hiss rather than fizz, and gated
    // by how much signal is actually going through. Ungated it is a permanent
    // hiss that the effect emits with nothing playing - and because this
    // function is applied to what gets written back into the loop, that hiss
    // then recirculates, so bypassing and re-enabling replayed it too.
    degradeEnvelope[c] += 0.002f * (std::abs(x) - degradeEnvelope[c]);
    const auto gate = juce::jmin(1.0f, degradeEnvelope[c] * 12.0f);
    degradeNoiseState[c] = 0.88f * degradeNoiseState[c]
                         + 0.12f * (juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f);
    y += degradeNoiseState[c] * d * d * gate * 0.045f;

    // Asymmetric drive, so heavy settings distort as well as hiss.
    const auto drive = 1.0f + d * 4.5f;
    y = softSaturate(y * drive) / drive;
    return y;
}

// ---------------------------------------------------------------------------
// Micro-looper channel
// ---------------------------------------------------------------------------

// Tape-style looper. Speed and direction move in the harmonised steps the mode
// is specified with, and with SPREAD up the right channel plays the loop
// forward while the left plays the same loop in reverse - which is the mode's
// own way of making a stereo image out of a mono loop.
Mood::Frame Mood::renderLoopTape(float spread)
{
    const auto loopSeconds = juce::jmap(currentSettings.loopLength, 0.05f, 2.2f);
    const auto loopSamples = juce::jlimit(64.0f,
                                          static_cast<float>(historySize - 4),
                                          loopSeconds * static_cast<float>(internalSampleRate));

    const auto index = juce::jlimit(0,
                                    static_cast<int>(kTapeSpeeds.size()) - 1,
                                    static_cast<int>(currentSettings.loopModify
                                                     * static_cast<float>(kTapeSpeeds.size())));
    const auto rate = kTapeSpeeds[static_cast<std::size_t>(index)];

    const auto loopStart = static_cast<float>(historyWritePos) - loopSamples;

    // The forward head, and a second head running the same loop backwards.
    const auto forwardPos = loopStart + loopReadPos;
    const auto reversePos = loopStart + (loopSamples - loopReadPos);

    const auto fade = spliceFadeFor(loopSamples);
    const auto forwardOffset = loopReadPos;
    const auto reverseOffset = loopSamples - loopReadPos;
    const auto forwardL = readSpliced(historyBuffer[0], loopStart, forwardOffset, loopSamples, fade);
    const auto forwardR = readSpliced(historyBuffer[1], loopStart, forwardOffset, loopSamples, fade);
    const auto reverseL = readSpliced(historyBuffer[0], loopStart, reverseOffset, loopSamples, fade);
    const auto reverseR = readSpliced(historyBuffer[1], loopStart, reverseOffset, loopSamples, fade);
    juce::ignoreUnused(forwardPos, reversePos);

    Frame out;
    out.l = forwardL + (reverseL - forwardL) * spread;
    out.r = forwardR;

    loopReadPos += rate;
    while (loopReadPos < 0.0f) loopReadPos += loopSamples;
    while (loopReadPos >= loopSamples) loopReadPos -= loopSamples;
    return out;
}

// Audio-controlled looper: the loop runs until the input crosses the detector
// threshold, at which point the current slice repeats until the input falls
// back below it. With SPREAD up, the held slice also pans side to side, at a
// speed set by the slice length.
Mood::Frame Mood::renderLoopEnv(float inL, float inR, float spread)
{
    const auto envIn = 0.5f * (std::abs(inL) + std::abs(inR));
    const auto envCoeff = onePoleCoeff(45.0f, static_cast<float>(internalSampleRate));
    envFollower += (envIn - envFollower) * envCoeff;

    const auto loopSeconds = juce::jmap(currentSettings.loopLength, 0.03f, 0.40f);
    const auto sliceSamples = juce::jlimit(32,
                                           historySize - 4,
                                           static_cast<int>(std::round(loopSeconds
                                                                       * static_cast<float>(internalSampleRate))));

    // MODIFY is sensitivity: turning it up makes the detector fire on quieter
    // input, so the threshold has to fall as the knob rises.
    //
    // The range is set against the levels this actually sees. A one-pole
    // average of |x| for a sine of amplitude A settles at 0.64A, and the FX bus
    // here runs at a few tenths, so the envelope peaks around 0.16 on ordinary
    // material. A threshold of 0.35 at the top of the knob - never mind 0.18 at
    // noon - means the detector simply never fires, and the mode does nothing
    // at any setting a player would use.
    const auto threshold = juce::jmap(currentSettings.loopModify, 0.14f, 0.002f);
    envGateOpen = envFollower > threshold;

    // A rolling copy of the most recent audio, written one sample per step and
    // simply STOPPED while the gate is open. The slice is then frozen without
    // anything being copied at the moment it is captured.
    //
    // Capturing on the rising edge instead means memcpy-ing up to twenty
    // thousand frames inside one sample's worth of processing, which is a
    // dropout on the audio thread however cheap it looks written down.
    const auto capacity = static_cast<int>(envSliceBuffer[0].size());
    if (capacity > 0 && ! envGateOpen)
    {
        envSliceBuffer[0][static_cast<std::size_t>(envSliceWritePos)] = inL;
        envSliceBuffer[1][static_cast<std::size_t>(envSliceWritePos)] = inR;
        envSliceWritePos = (envSliceWritePos + 1) % capacity;
    }

    if (envGateOpen && envSliceBlend <= 0.001f && capacity > 0)
    {
        envSliceLength = juce::jlimit(32, capacity - 4, sliceSamples);
        envSliceOrigin = static_cast<float>(envSliceWritePos - envSliceLength);
        envSliceReadPos = 0.0f;
    }

    // Blend between live playback and the captured slice rather than switching,
    // so the gate opening and closing is not itself a step.
    const auto blendRate = onePoleCoeff(90.0f, static_cast<float>(internalSampleRate));
    envSliceBlend += ((envGateOpen ? 1.0f : 0.0f) - envSliceBlend) * blendRate;

    const auto loopStart = static_cast<float>(historyWritePos) - static_cast<float>(sliceSamples * 2);
    const auto sliceLength = static_cast<float>(sliceSamples);
    const auto fade = spliceFadeFor(sliceLength);
    auto l = readSpliced(historyBuffer[0], loopStart, loopReadPos, sliceLength, fade);
    auto r = readSpliced(historyBuffer[1], loopStart, loopReadPos, sliceLength, fade);

    if (envSliceBlend > 0.001f && envSliceLength > 0)
    {
        const auto held = static_cast<float>(envSliceLength);
        const auto heldFade = spliceFadeFor(held);
        const auto sl = readSpliced(envSliceBuffer[0], envSliceOrigin, envSliceReadPos, held, heldFade);
        const auto sr = readSpliced(envSliceBuffer[1], envSliceOrigin, envSliceReadPos, held, heldFade);
        l += (sl - l) * envSliceBlend;
        r += (sr - r) * envSliceBlend;

        envSliceReadPos += 1.0f;
        if (envSliceReadPos >= held)
        {
            envSliceReadPos -= held;
        }
    }

    loopReadPos += 1.0f;
    if (loopReadPos >= static_cast<float>(sliceSamples))
    {
        loopReadPos = 0.0f;
    }

    // The pan only moves while the gate is open; below the threshold the
    // incoming image is left exactly as it arrived.
    //
    // Each opening drives one full traverse across the field rather than
    // sampling a free-running LFO. A free-running phase means the excursion
    // depends on where the oscillator happened to be when the gate opened, so
    // a short slice might only move a few degrees and the control reads as
    // doing almost nothing however far it is turned up. The direction
    // alternates, so successive stutters throw to opposite sides.
    if (envGateOpen)
    {
        const auto traverseSamples = static_cast<float>(juce::jmax(1, sliceSamples));
        envPanPhase += 1.0f / traverseSamples;
        if (envPanPhase >= 1.0f)
        {
            envPanPhase -= 1.0f;
            envPanDirection = -envPanDirection;
        }
    }
    else
    {
        envPanPhase = 0.0f;
    }

    Frame plain { l, r };
    if (spread <= 0.0001f || !envGateOpen)
    {
        return plain;
    }

    const auto position = envPanDirection * (2.0f * envPanPhase - 1.0f);
    const auto panned = panStereo(0.5f * (l + r), position);
    return { plain.l + (panned.l - plain.l) * spread,
             plain.r + (panned.r - plain.r) * spread };
}

void Mood::maybeSpawnStretchGrain(float spread)
{
    for (auto& grain : grains)
    {
        if (grain.active)
        {
            continue;
        }

        // LENGTH is slice size: longer slices carry recognisable phrases,
        // shorter ones blur into grain.
        const auto baseLenMs = juce::jmap(currentSettings.loopLength, 22.0f, 210.0f);
        grain.lengthSamples = juce::jmax(12, static_cast<int>(std::round((baseLenMs / 1000.0f) * internalSampleRate)));

        // MODIFY is direction and stretch amount, with a frozen point at noon:
        // below it the playhead walks backwards through the loop, above it
        // forwards, and at the top it keeps pace with the recording so nothing
        // is stretched at all.
        const auto modify = currentSettings.loopModify;
        const auto stretchWalk = (modify - 0.5f) * 2.0f;

        grain.increment = 1.0f;
        const auto spanSamples = 0.35f * static_cast<float>(historySize);
        const auto walkOffset = stretchWalk * spanSamples;
        grain.readPos = static_cast<float>(historyWritePos) - spanSamples * 0.5f + walkOffset;

        const auto panSpread = spread;
        grain.pan = juce::jlimit(0.0f, 1.0f,
                                 0.5f + (juce::Random::getSystemRandom().nextFloat() - 0.5f) * panSpread * 1.0f);
        grain.sourceBalance = clamp01(0.5f + (grain.pan - 0.5f) * 0.7f);
        grain.gain = 0.34f;
        grain.ageSamples = 0;
        grain.active = true;
        return;
    }
}

// Time-stretching looper. Grains are read out of the history at their recorded
// speed while the window they are taken from moves at whatever rate MODIFY
// asks for, which is what separates stretching from resampling. With SPREAD up
// the whole cloud drifts slowly from side to side.
Mood::Frame Mood::renderLoopStretch(float spread)
{
    ++stretchSpawnCounter;
    const auto spawnEvery = juce::jlimit(16,
                                         2048,
                                         static_cast<int>(std::round((0.010f + currentSettings.loopLength * 0.05f)
                                                                     * internalSampleRate)));
    if (stretchSpawnCounter >= spawnEvery)
    {
        stretchSpawnCounter = 0;
        maybeSpawnStretchGrain(spread);
    }

    // Panning speed follows MODIFY, as the mode is specified: the further from
    // frozen, the faster the image moves.
    const auto panHz = 0.05f + std::abs(currentSettings.loopModify - 0.5f) * 0.6f;
    stretchPanPhase += juce::MathConstants<float>::twoPi * panHz
                     / static_cast<float>(juce::jmax(1.0, internalSampleRate));
    while (stretchPanPhase >= juce::MathConstants<float>::twoPi)
    {
        stretchPanPhase -= juce::MathConstants<float>::twoPi;
    }

    Frame out;
    for (auto& grain : grains)
    {
        if (!grain.active)
        {
            continue;
        }

        const auto phase = static_cast<float>(grain.ageSamples) / static_cast<float>(juce::jmax(1, grain.lengthSamples));
        if (phase >= 1.0f)
        {
            grain.active = false;
            continue;
        }

        const auto window = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * phase);
        const auto l = readInterp(historyBuffer[0], grain.readPos);
        const auto r = readInterp(historyBuffer[1], grain.readPos);

        // With SPREAD down the grain hands back what it found, left to left and
        // right to right, so a source that arrived hard left leaves hard left.
        // Summing to mono and panning to the centre - which is what a pan of
        // 0.5 amounts to - throws the incoming image away before SPREAD has
        // been asked whether it wanted it changed.
        const auto mono = 0.5f * (l + r);
        const auto panAngle = clamp01(grain.pan) * juce::MathConstants<float>::halfPi;
        const auto pannedL = mono * std::cos(panAngle) * 1.41421356f;
        const auto pannedR = mono * std::sin(panAngle) * 1.41421356f;

        const auto g = grain.gain * window;
        out.l += (l + (pannedL - l) * spread) * g;
        out.r += (r + (pannedR - r) * spread) * g;

        grain.readPos += grain.increment;
        ++grain.ageSamples;
    }

    if (spread <= 0.0001f)
    {
        return out;
    }

    // Slow, smooth side-to-side motion of the whole cloud.
    const auto position = std::sin(stretchPanPhase) * spread;
    const auto mid = 0.5f * (out.l + out.r);
    const auto side = 0.5f * (out.l - out.r);
    const auto moved = panStereo(mid, position);
    // The side component is kept and widened rather than traded away against
    // the panning, so the cloud both moves and spreads.
    const auto sideGain = 1.0f + spread * 0.8f;
    const auto compensation = 1.0f / (1.0f + spread * 0.28f);
    return { (moved.l + side * sideGain) * compensation,
             (moved.r - side * sideGain) * compensation };
}

// ---------------------------------------------------------------------------
// Wet channel
// ---------------------------------------------------------------------------

// Multi-tap at one end of MODIFY, reverb at the other, with the space in
// between being the useful part. TIME sets decay and size together. With
// SPREAD up the two channels take their taps from different places, which is
// what puts the reflections in different spots rather than making one smear
// that happens to be louder on one side.
Mood::Frame Mood::renderWetReverb(float inL, float inR, float spread)
{
    static constexpr std::array<float, 8> tapsSecL { { 0.017f, 0.023f, 0.031f, 0.043f,
                                                       0.059f, 0.071f, 0.089f, 0.113f } };
    static constexpr std::array<float, 8> tapsSecR { { 0.019f, 0.029f, 0.037f, 0.047f,
                                                       0.053f, 0.079f, 0.097f, 0.107f } };

    const auto smear = clamp01(currentSettings.wetModify);
    const auto tScale = juce::jmap(currentSettings.wetTime, 0.4f, 3.0f);
    const auto limit = static_cast<float>(wetSize - 4);

    float sumL = 0.0f;
    float sumR = 0.0f;
    for (int i = 0; i < 8; ++i)
    {
        const auto idx = static_cast<std::size_t>(i);
        // Tap positions are static. They used to be modulated by
        // sin(0.13 * writePosition), which is not a modulator at all - the
        // write position advances one per sample, so that expression is a
        // ~1 kHz oscillator and it was frequency-modulating the tap positions
        // at audio rate.
        const auto tapL = juce::jlimit(4.0f, limit, tapsSecL[idx] * tScale * static_cast<float>(internalSampleRate));
        const auto tapR = juce::jlimit(4.0f, limit, tapsSecR[idx] * tScale * static_cast<float>(internalSampleRate));

        // With spread down both channels take the same tap times from their own
        // side, so a stereo input keeps its image. With spread up the times
        // diverge AND each side picks up some of the other's reflections -
        // without that cross-feed the mode is two independent mono reverbs, and
        // a source hard to one side produces no reflections at all on the
        // other, which is not how a room behaves.
        const auto usedR = tapL + (tapR - tapL) * spread;
        const auto ownL = readInterp(wetBuffer[0], static_cast<float>(wetWritePos) - tapL);
        const auto ownR = readInterp(wetBuffer[1], static_cast<float>(wetWritePos) - usedR);
        const auto crossL = readInterp(wetBuffer[1], static_cast<float>(wetWritePos) - tapR);
        const auto crossR = readInterp(wetBuffer[0], static_cast<float>(wetWritePos) - tapL * 1.09f);
        // Anti-symmetric: what one channel gains, the other loses. Adding the
        // same cross term to both sides only redistributes energy - it leaves
        // the two channels carrying the same thing at different levels, which
        // still measures as correlated. Opposing signs make them genuinely
        // different signals, which is what a pair of ears reads as a space.
        const auto cross = spread * 0.80f;
        sumL += ownL * (1.0f - cross) + crossL * cross;
        sumR += ownR * (1.0f - cross) - crossR * cross;
    }

    auto wetL = sumL * 0.125f;
    auto wetR = sumR * 0.125f;

    // Smear: allpass diffusion turns the discrete taps into a wash. At zero it
    // is bypassed entirely, which is the multi-tap end of the control.
    if (smear > 0.001f)
    {
        const auto g = smear * 0.72f;
        auto dl = wetL;
        auto dr = wetR;
        for (int stage = 0; stage < kDiffusionStages; ++stage)
        {
            const auto sign = (stage % 2 == 0) ? 1.0f : -1.0f;
            dl = readAllpass(0, stage, dl, sign * g);
            dr = readAllpass(1, stage, dr, -sign * g);
        }
        wetL += (dl - wetL) * smear;
        wetR += (dr - wetR) * smear;
    }

    // TIME sets decay as well as size.
    const auto fb = juce::jlimit(0.0f, 0.86f, 0.18f + 0.68f * currentSettings.wetTime);
    const auto dampCoeff = onePoleCoeff(juce::jmap(smear, 8000.0f, 3000.0f),
                                        static_cast<float>(internalSampleRate));
    wetDampState[0] += dampCoeff * (wetL - wetDampState[0]);
    wetDampState[1] += dampCoeff * (wetR - wetDampState[1]);

    reverbFeedbackStore[0] = wetDampState[0];
    reverbFeedbackStore[1] = wetDampState[1];

    writeWet(applyDegradation(inL + wetDampState[0] * fb, 0),
             applyDegradation(inR + wetDampState[1] * fb, 1));
    return { wetL, wetR };
}

float Mood::readCrossfadedWetTap(int channel, CrossfadeTap& tap, float targetSamples)
{
    const auto limit = static_cast<float>(wetSize - 4);
    const auto target = juce::jlimit(4.0f, limit, targetSamples);
    const auto fadeLength = juce::jmax(64, static_cast<int>(internalSampleRate * 0.020));

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
        tap.fadeRemaining = fadeLength;
    }

    const auto head = static_cast<float>(wetWritePos);
    const auto current = readInterp(wetBuffer[static_cast<std::size_t>(channel)], head - tap.activeSamples);
    if (tap.fadeRemaining <= 0)
    {
        return current;
    }

    const auto previous = readInterp(wetBuffer[static_cast<std::size_t>(channel)], head - tap.fadingSamples);
    const auto progress = 1.0f - static_cast<float>(tap.fadeRemaining) / static_cast<float>(fadeLength);
    const auto angle = progress * juce::MathConstants<float>::halfPi;
    --tap.fadeRemaining;
    return current * std::sin(angle) + previous * std::cos(angle);
}

// Clean looping delay. TIME crossfades between delay times rather than sliding
// the read pointer, so changing it does not pitch-bend the echoes already in
// the line. MODIFY is feedback, and at the top the repeats hold rather than
// decay. With SPREAD up the feedback path swaps channels each pass, so an
// input panned part-way to one side alternates the same distance to the other.
Mood::Frame Mood::renderWetDelay(float inL, float inR, float spread)
{
    const auto baseSeconds = juce::jmap(currentSettings.wetTime, 0.03f, 1.6f);
    const auto delaySamples = juce::jlimit(8.0f,
                                           static_cast<float>(wetSize - 4),
                                           baseSeconds * static_cast<float>(internalSampleRate));

    const auto wetL = readCrossfadedWetTap(0, wetTaps[0], delaySamples);
    const auto wetR = readCrossfadedWetTap(1, wetTaps[1], delaySamples);

    // "At max, repeats are stable and pile up like a looper" - so the top of
    // the control is unity, held there by the saturator rather than by a
    // coefficient below one.
    const auto fb = juce::jlimit(0.0f, 1.0f, currentSettings.wetModify);

    // At spread 0 each channel feeds itself and the incoming image is kept; at
    // spread 1 the paths cross fully and every repeat lands on the other side.
    const auto fedL = wetL + (wetR - wetL) * spread;
    const auto fedR = wetR + (wetL - wetR) * spread;

    const auto writeL = softSaturate(inL + fedL * fb);
    const auto writeR = softSaturate(inR + fedR * fb);
    writeWet(applyDegradation(writeL, 0), applyDegradation(writeR, 1));
    return { wetL, wetR };
}

// Auto-sampler. It samples continuously and plays back at a speed and
// direction of the user's choosing, quantised to semitones so the harmonies it
// generates are in tune with what went in. With SPREAD up the output pans
// smoothly, at a rate set by the sampling window.
Mood::Frame Mood::renderWetSlip(float inL, float inR, float spread)
{
    const auto windowSeconds = juce::jmap(currentSettings.wetTime, 0.05f, 0.55f);
    const auto windowSamples = juce::jlimit(64.0f,
                                            static_cast<float>(historySize - 4),
                                            windowSeconds * static_cast<float>(internalSampleRate));

    // The input is written to the wet buffer, so ROUTING reaches this mode -
    // it used to read the history directly and ignore its arguments entirely,
    // which meant routing the micro-loop into it did nothing.
    writeWet(applyDegradation(inL, 0), applyDegradation(inR, 1));

    // Playback speed in semitone steps, from an octave down through neutral to
    // an octave up, in each direction.
    const auto modify = clamp01(currentSettings.wetModify);
    const auto semitones = std::round(juce::jmap(modify, -24.0f, 24.0f));
    auto speed = semitoneRatio(std::abs(semitones) > 24.0f ? 0.0f : semitones);
    if (modify < 0.5f)
    {
        speed = -speed;
    }
    speed = juce::jlimit(-4.0f, 4.0f, speed);

    const auto windowStart = static_cast<float>(wetWritePos) - windowSamples;
    const auto fade = spliceFadeFor(windowSamples);
    const auto l = readSpliced(wetBuffer[0], windowStart, slipReadPos, windowSamples, fade);
    const auto r = readSpliced(wetBuffer[1], windowStart, slipReadPos, windowSamples, fade);

    slipReadPos += speed;
    while (slipReadPos < 0.0f) slipReadPos += windowSamples;
    while (slipReadPos >= windowSamples) slipReadPos -= windowSamples;

    if (spread <= 0.0001f)
    {
        return { l, r };
    }

    const auto panHz = static_cast<float>(internalSampleRate) / juce::jmax(1.0f, windowSamples * 6.0f);
    slipPanPhase += juce::MathConstants<float>::twoPi * panHz
                  / static_cast<float>(juce::jmax(1.0, internalSampleRate));
    while (slipPanPhase >= juce::MathConstants<float>::twoPi)
    {
        slipPanPhase -= juce::MathConstants<float>::twoPi;
    }

    // Widened as well as panned: panning a mono sum moves the image but leaves
    // the two channels carrying the same thing, which is not what the control
    // is being asked for.
    const auto mid = 0.5f * (l + r);
    const auto side = 0.5f * (l - r);
    // Compensated so widening does not also raise the level. Scaling both
    // channels by the same factor leaves the side-to-mid ratio - the actual
    // width - untouched, so this costs nothing but the extra peak.
    const auto panned = panStereo(mid, std::sin(slipPanPhase) * spread);
    const auto sideGain = 1.0f + spread * 0.7f;
    const auto compensation = 1.0f / (1.0f + spread * 0.38f);
    const auto wideL = (panned.l + side * sideGain) * compensation;
    const auto wideR = (panned.r - side * sideGain) * compensation;
    return { l + (wideL - l) * spread, r + (wideR - r) * spread };
}

// ---------------------------------------------------------------------------

Mood::Frame Mood::processInternalStep(float inL, float inR)
{
    const auto spread = clamp01(currentSettings.spread);

    // The always-listening history. Written exactly once per internal step -
    // it used to be written twice, once with the input and once with the
    // feedback, which advanced the write pointer at double rate and left the
    // buffer holding input and feedback in alternating slots.
    const auto loopFeedback = juce::jmap(clamp01(currentSettings.feedback), 0.0f, 0.98f);

    Frame loop;
    switch (currentSettings.loopModeIndex)
    {
        case 0: loop = renderLoopEnv(inL, inR, spread); break;
        case 1: loop = renderLoopTape(spread); break;
        case 2: loop = renderLoopStretch(spread); break;
        default: break;
    }

    // ROUTING decides what the wet channel is fed, and the order follows the
    // labels the user is shown: DRY->WET, LOOP->WET, PARALLEL. The routing
    // parameter arrives as index/2, so the thresholds are at 0.5 and 1.0.
    //
    // These two were the wrong way round: 0.5 ("LOOP->WET") fed the wet channel
    // the input as well as the loop, and 1.0 ("PARALLEL") fed it the loop
    // alone - the opposite of what each setting says on the control.
    float wetInL = inL;
    float wetInR = inR;
    if (currentSettings.routing > 0.66f)          // PARALLEL: input and loop
    {
        wetInL += loop.l;
        wetInR += loop.r;
    }
    else if (currentSettings.routing > 0.33f)     // LOOP->WET: loop alone
    {
        wetInL = loop.l;
        wetInR = loop.r;
    }

    Frame wet;
    switch (currentSettings.wetModeIndex)
    {
        case 0: wet = renderWetReverb(wetInL, wetInR, spread); break;
        case 1: wet = renderWetDelay(wetInL, wetInR, spread); break;
        case 2: wet = renderWetSlip(wetInL, wetInR, spread); break;
        default: break;
    }

    if (!currentSettings.freeze)
    {
        // One write, carrying the input plus whatever the two channels are
        // feeding back. Degradation is applied here so it compounds with each
        // pass round the loop rather than sitting as a fixed layer on the
        // output.
        // Scaled so the top of the knob genuinely piles material up the way a
        // looper does. At 0.55 the loop lost nearly half its level every pass
        // however far the control was turned, which put the whole audible range
        // of the knob into its last few degrees of travel.
        const auto recycledL = loop.l * loopFeedback * 0.88f + wet.l * loopFeedback * 0.58f;
        const auto recycledR = loop.r * loopFeedback * 0.88f + wet.r * loopFeedback * 0.58f;
        writeHistory(applyDegradation(inL + recycledL, 0),
                     applyDegradation(inR + recycledR, 1));
    }

    return { sanitizeAudioSample(wet.l + loop.l), sanitizeAudioSample(wet.r + loop.r) };
}

void Mood::processSampleFrame(float inL, float inR, float& outL, float& outR)
{
    const auto enabledMix = enabledSmoothed.getNextValue();

    if (enabledMix <= 0.0001f)
    {
        outL = inL;
        outR = inR;

        if (pendingResetOnBypass)
        {
            reset();
            pendingResetOnBypass = false;
        }
        return;
    }

    currentSettings.mix = mixSmoothed.getNextValue();
    currentSettings.clock = clockSmoothed.getNextValue();
    currentSettings.routing = routingSmoothed.getNextValue();
    currentSettings.wetTime = wetTimeSmoothed.getNextValue();
    currentSettings.wetModify = wetModifySmoothed.getNextValue();
    currentSettings.loopLength = loopLengthSmoothed.getNextValue();
    currentSettings.loopModify = loopModifySmoothed.getNextValue();
    currentSettings.feedback = feedbackSmoothed.getNextValue();
    currentSettings.spread = spreadSmoothed.getNextValue();
    currentSettings.degrade = degradeSmoothed.getNextValue();

    // CLOCK is the engine's sample rate, and it is what makes the two channels
    // move together: audio recorded at one rate and played back at another
    // changes speed and pitch at the same time, so dropping the clock an
    // octave half-speeds the micro-loop and the wet channel alike. Quantised
    // to semitones so the transposition lands on musical intervals.
    //
    // The previous version computed sampleRate / (sampleRate * rate), which is
    // just 1/rate, and used it as a sample-and-hold count that never exceeded
    // a couple of samples at usable settings. It was a decimator, not a clock,
    // and it changed neither the loop length nor the pitch.
    {
        const auto steps = std::round((1.0f - clamp01(currentSettings.clock))
                                      * static_cast<float>(kClockMaxSteps));
        clockDivider = juce::jlimit(1.0f, 8.0f, semitoneRatio(steps));
        clockIncrement = 1.0f / clockDivider;
        internalSampleRate = juce::jmax(1.0, sampleRateHz / static_cast<double>(clockDivider));
    }

    clockPhase += clockIncrement;
    if (clockPhase >= 1.0f)
    {
        clockPhase -= std::floor(clockPhase);
        heldOutput = processInternalStep(inL, inR);
    }

    // Zero-order hold on the way back up to the host rate. The aliasing this
    // leaves behind at low clock settings is the character of the control, not
    // an artifact to be filtered away.
    const auto wetMix = clamp01(currentSettings.mix);
    const auto dryMix = 1.0f - wetMix;

    const auto processedL = dryMix * inL + wetMix * heldOutput.l;
    const auto processedR = dryMix * inR + wetMix * heldOutput.r;

    outL = sanitizeAudioSample(juce::jmap(enabledMix, inL, processedL));
    outR = sanitizeAudioSample(juce::jmap(enabledMix, inR, processedR));
}
