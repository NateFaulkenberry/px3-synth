#include "Doom.h"
#include "DoomControlModel.h"

#include <algorithm>
#include <cmath>

namespace px3
{
namespace
{
// The longest micro-loop, in internal samples. At the lowest clock this is the
// loop's real duration multiplied by sixteen, which is where the very long,
// very low loops come from.
constexpr float kMaxLoopInternalSamples = 48000.0f;
constexpr float kMinLoopInternalSamples = 900.0f;

constexpr float kHistorySeconds = 4.0f;

// FLIP's harmony table. Widens as MODIFY rises, drawn from fourths, fifths and
// octaves in both directions, exactly as the source describes.
struct HarmonySet
{
    int count;
    std::array<float, 4> semitones;
};

constexpr std::array<HarmonySet, 8> kHarmonies { {
    { 1, { { 12.0f, 0.0f, 0.0f, 0.0f } } },
    { 1, { { -12.0f, 0.0f, 0.0f, 0.0f } } },
    { 1, { { 7.0f, 0.0f, 0.0f, 0.0f } } },
    { 1, { { -5.0f, 0.0f, 0.0f, 0.0f } } },
    { 2, { { 7.0f, 12.0f, 0.0f, 0.0f } } },
    { 2, { { -5.0f, -12.0f, 0.0f, 0.0f } } },
    { 3, { { 5.0f, 7.0f, 12.0f, 0.0f } } },
    { 4, { { -12.0f, -5.0f, 7.0f, 12.0f } } }
} };

float hannAt(float phase01)
{
    return 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * juce::jlimit(0.0f, 1.0f, phase01));
}
} // namespace

// ============================================================================
// helpers
// ============================================================================

float Doom::sanitize(float v)
{
    if (! std::isfinite(v))
    {
        return 0.0f;
    }
    return juce::jlimit(-8.0f, 8.0f, v);
}

float Doom::softSaturate(float x)
{
    return std::tanh(x);
}

float Doom::semitoneRatio(float semitones)
{
    return std::exp2(semitones / 12.0f);
}

float Doom::onePoleCoeff(float hz, float rate)
{
    if (rate <= 0.0f || hz <= 0.0f)
    {
        return 0.0f;
    }
    return 1.0f - std::exp(-juce::MathConstants<float>::twoPi * hz / rate);
}

Doom::Frame Doom::panStereo(float mono, float position)
{
    // Equal power, taking -1..+1 so "no pan" is zero and the per-mode stereo
    // treatments can be scaled by SPREAD directly.
    const auto angle = (juce::jlimit(-1.0f, 1.0f, position) * 0.5f + 0.5f)
                       * juce::MathConstants<float>::halfPi;
    return { mono * std::cos(angle), mono * std::sin(angle) };
}

void Doom::setSeed(uint32_t seed)
{
    // Never zero: xorshift is stuck at zero forever.
    rngState = seed != 0u ? seed : 0x9E3779B9u;
}

float Doom::nextRandom()
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return static_cast<float>(rngState & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

float Doom::nextBipolar()
{
    return nextRandom() * 2.0f - 1.0f;
}

// Both live in the control model now - the ratio table is a mapping from a
// knob to a rate, not a property of the engine. These forward so the existing
// callers and tests keep one place to ask.
int Doom::clockStepCount()
{
    return doom_control::clockStepCount();
}

float Doom::clockRatioFor(float clockNormalised, bool smooth)
{
    return doom_control::mapClockToRatio(clockNormalised, smooth);
}

// ============================================================================
// lifecycle
// ============================================================================

void Doom::prepare(double sampleRate)
{
    hostSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    historySize = juce::jmax(1024, static_cast<int>(hostSampleRate * kHistorySeconds));
    for (auto& channel : history)
    {
        channel.assign(static_cast<std::size_t>(historySize), 0.0f);
    }

    // RELAY holds eight taps of the longest delay, so the buffer has to reach
    // the last one. Sized at the host rate because the internal rate only ever
    // makes the same span cheaper in internal samples.
    relaySize = juce::jmax(1024, static_cast<int>(hostSampleRate * 2.0));
    for (auto& channel : relayBuffer)
    {
        channel.assign(static_cast<std::size_t>(relaySize), 0.0f);
    }

    flipSize = juce::jmax(1024, static_cast<int>(hostSampleRate * 1.0));
    for (auto& channel : flipBuffer)
    {
        channel.assign(static_cast<std::size_t>(flipSize), 0.0f);
    }

    freezeSize = juce::jmax(1024, static_cast<int>(hostSampleRate * 1.5));
    for (auto& channel : freezeBuffer)
    {
        channel.assign(static_cast<std::size_t>(freezeSize), 0.0f);
    }

    soupStft.prepare(kSoupFftOrder, 2);
    const auto bins = static_cast<std::size_t>(soupStft.numBins());
    for (auto& channel : soupMagnitude)
    {
        channel.assign(bins, 0.0f);
    }
    for (auto& channel : soupPhase)
    {
        channel.assign(bins, 0.0f);
    }
    for (auto& channel : soupBlur)
    {
        channel.assign(bins, 0.0f);
    }
    soupPrepared = true;

    const auto rampSeconds = 0.02;
    for (auto* smoother : { &enabledSmoothed, &mixSmoothed, &clockSmoothed, &loopLengthSmoothed,
                            &loopModifySmoothed, &wetTimeSmoothed, &wetModifySmoothed, &crossSmoothed,
                            &glueSmoothed, &eqSmoothed, &balanceSmoothed, &blendSmoothed,
                            &spreadSmoothed, &overdubSmoothed, &wetActiveSmoothed, &loopActiveSmoothed })
    {
        smoother->reset(hostSampleRate, rampSeconds);
    }

    reset();
}

void Doom::reset()
{
    for (auto& channel : history)
    {
        std::fill(channel.begin(), channel.end(), 0.0f);
    }
    for (auto& channel : relayBuffer)
    {
        std::fill(channel.begin(), channel.end(), 0.0f);
    }
    for (auto& channel : flipBuffer)
    {
        std::fill(channel.begin(), channel.end(), 0.0f);
    }
    for (auto& channel : freezeBuffer)
    {
        std::fill(channel.begin(), channel.end(), 0.0f);
    }
    for (auto& channel : soupMagnitude)
    {
        std::fill(channel.begin(), channel.end(), 0.0f);
    }
    for (auto& channel : soupPhase)
    {
        std::fill(channel.begin(), channel.end(), 0.0f);
    }
    for (auto& channel : soupBlur)
    {
        std::fill(channel.begin(), channel.end(), 0.0f);
    }

    soupStft.reset();

    historyWrite = 0;
    historyAbsolute = 0.0;
    relayWrite = 0;
    flipWrite = 0;
    freezeWrite = 0;
    freezeRead = 0.0f;
    freezeLatched = false;

    loopStartAbs = 0.0f;
    loopLengthSamples = 12000.0f;
    loopLatched = false;
    wasLoopActive = false;
    loopPhase = 0.0f;

    grains = {};
    flipGrains = {};
    grainSpawnCounter = 0;
    flipSpawnCounter = 0;

    sliceOffsets = {};
    sliceCount = 1;
    burstStep = 0;
    burstOrder = {};
    burstStepPhase = 0.0f;
    burstEnv = 0.0f;
    burstGrainPhase = 0.0f;

    radioStatic = 0.0f;
    radioNoiseState = { { 0.0f, 0.0f } };
    danceRotationPhase = 0.0f;
    danceStage = 0;
    tapeReadPos = 0.0f;
    shoegazeOrigin = 0.0f;

    maskEnv = 0.0f;
    maskGate = 0.0f;
    maskRingPhase = 0.0f;
    maskPitchPhase = 0.0f;
    maskReversePhase = 0.0f;
    maskResonator = { { 0.0f, 0.0f } };
    maskResonatorPrev = { { 0.0f, 0.0f } };

    relayHold = { { 0.0f, 0.0f } };

    crossEnv = 0.0f;
    crossMean = 0.0f;
    crossSlewed = 0.0f;
    crossAm = 1.0f;
    crossFm = 0.0f;

    eqLowState = { { 0.0f, 0.0f } };
    eqHighState = { { 0.0f, 0.0f } };
    glueDcState = { { 0.0f, 0.0f } };
    glueDcPrev = { { 0.0f, 0.0f } };

    clockPhase = 0.0f;
    heldOutput = {};
    decimateAccum = {};
    decimateCount = 0;
    reconstructState = { { 0.0f, 0.0f } };

    for (auto* smoother : { &enabledSmoothed, &mixSmoothed, &clockSmoothed, &loopLengthSmoothed,
                            &loopModifySmoothed, &wetTimeSmoothed, &wetModifySmoothed, &crossSmoothed,
                            &glueSmoothed, &eqSmoothed, &balanceSmoothed, &blendSmoothed,
                            &spreadSmoothed, &overdubSmoothed, &wetActiveSmoothed, &loopActiveSmoothed })
    {
        smoother->setCurrentAndTargetValue(smoother->getTargetValue());
    }
}

void Doom::updateForBlock(const DoomUserParameters& next)
{
    // One translation a block. The per-sample stages below still map their own
    // smoothed knob through the same named functions, so automating a macro
    // glides rather than steps; this is the block-rate view of the same model,
    // and what the control-model tests assert against.
    derived = deriveDoomParameters(next, kMaxRelayTaps,
                                   static_cast<int>(kHarmonies.size()), kRadioStations);

    settings = next;

    enabledSmoothed.setTargetValue(settings.enabled ? 1.0f : 0.0f);
    mixSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.mix));
    clockSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.clock));
    loopLengthSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.loopLength));
    loopModifySmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.loopModify));
    wetTimeSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.wetTime));
    wetModifySmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.wetModify));
    crossSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.cross));
    glueSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.glue));
    eqSmoothed.setTargetValue(juce::jlimit(-1.0f, 1.0f, settings.eq));
    balanceSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.balance));
    blendSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.blend));
    spreadSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.spread));
    overdubSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.overdub));
    wetActiveSmoothed.setTargetValue(settings.wetActive ? 1.0f : 0.0f);
    loopActiveSmoothed.setTargetValue(settings.loopActive ? 1.0f : 0.0f);

    // Engaging the looper captures what has already happened. Nothing starts
    // recording, because the history has been filling the whole time.
    if (settings.loopActive && ! wasLoopActive)
    {
        latchLoop();
    }
    wasLoopActive = settings.loopActive;

    if (! settings.freeze)
    {
        freezeLatched = false;
    }

    if (settings.enabled && ! wasEnabled)
    {
        // Only the tails are cleared. The history is not: the looper is always
        // listening, so its buffer has to survive the effect being switched on.
        soupStft.reset();
        for (auto& channel : soupMagnitude)
        {
            std::fill(channel.begin(), channel.end(), 0.0f);
        }
    }
    wasEnabled = settings.enabled;
}

// ============================================================================
// history / clock
// ============================================================================

void Doom::updateClock()
{
    // Smoothed per sample and mapped here, so sweeping CLOCK glides through
    // the ratios rather than stepping between them at block boundaries.
    clockRatio = juce::jlimit(doom_control::mapClockToRatio(0.0f, false), 1.0f,
                              doom_control::mapClockToRatio(clockSmoothed.getNextValue(),
                                                            settings.clockSmooth));
    internalRate = hostSampleRate * static_cast<double>(clockRatio);

    // Reconstruction filter at the internal Nyquist. The zero-order hold's
    // imaging is most of what makes a low clock sound digital rather than
    // merely dull, so this tames it rather than removing it: a first-order
    // pole leaves plenty of the character behind.
    reconstructCoeff = onePoleCoeff(static_cast<float>(internalRate) * 0.5f,
                                    static_cast<float>(hostSampleRate));
}

void Doom::writeHistory(float l, float r)
{
    history[0][static_cast<std::size_t>(historyWrite)] = sanitize(l);
    history[1][static_cast<std::size_t>(historyWrite)] = sanitize(r);
    historyWrite = (historyWrite + 1) % historySize;
    historyAbsolute += 1.0;
}

float Doom::readHistory(int channel, float pos) const
{
    const auto& line = history[static_cast<std::size_t>(juce::jlimit(0, 1, channel))];
    const auto size = static_cast<float>(historySize);

    auto wrapped = std::fmod(pos, size);
    if (wrapped < 0.0f)
    {
        wrapped += size;
    }

    const auto i1 = static_cast<int>(wrapped);
    const auto frac = wrapped - static_cast<float>(i1);

    // Cubic Lagrange (Catmull-Rom). Allpass interpolation is rejected here for
    // the same reason as in Delay and CombResonator: it is recursive, so it
    // produces transients under modulation, and this read head is modulated
    // constantly.
    const auto i0 = (i1 - 1 + historySize) % historySize;
    const auto i2 = (i1 + 1) % historySize;
    const auto i3 = (i1 + 2) % historySize;

    const auto y0 = line[static_cast<std::size_t>(i0)];
    const auto y1 = line[static_cast<std::size_t>(i1 % historySize)];
    const auto y2 = line[static_cast<std::size_t>(i2)];
    const auto y3 = line[static_cast<std::size_t>(i3)];

    const auto a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const auto b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto c = -0.5f * y0 + 0.5f * y2;

    return ((a * frac + b) * frac + c) * frac + y1;
}

float Doom::readHistorySpliced(int channel, float startAbs, float pos,
                               float loopLength, float fade) const
{
    // A pointer that simply wraps from the end of the loop to its start steps,
    // and that step is a click once per loop. Reading the tail and the head
    // together across a short equal-power crossfade removes it without
    // smearing the loop.
    if (loopLength <= 1.0f)
    {
        return 0.0f;
    }

    const auto wrapped = std::fmod(std::fmod(pos, loopLength) + loopLength, loopLength);
    const auto base = readHistory(channel, startAbs + wrapped);

    if (fade <= 1.0f || wrapped >= loopLength - fade)
    {
        if (fade > 1.0f && wrapped >= loopLength - fade)
        {
            const auto t = (wrapped - (loopLength - fade)) / fade;
            const auto head = readHistory(channel, startAbs + wrapped - loopLength);
            const auto gTail = std::cos(t * juce::MathConstants<float>::halfPi);
            const auto gHead = std::sin(t * juce::MathConstants<float>::halfPi);
            return base * gTail + head * gHead;
        }
        return base;
    }

    return base;
}

void Doom::latchLoop()
{
    // The loop length lives in INTERNAL samples, which is what makes CLOCK do
    // two things at once: lower the clock and the same buffer takes longer to
    // play and comes out lower.
    const auto lengthNorm = juce::jlimit(0.0f, 1.0f, settings.loopLength);
    auto length = kMinLoopInternalSamples
                  + lengthNorm * lengthNorm * (kMaxLoopInternalSamples - kMinLoopInternalSamples);

    if (settings.loopHalf)
    {
        length *= 0.5f;
    }

    // Never ask for more history than exists.
    loopLengthSamples = juce::jlimit(kMinLoopInternalSamples,
                                     static_cast<float>(historySize) * 0.9f,
                                     length);
    loopStartAbs = static_cast<float>(historyAbsolute) - loopLengthSamples;
    loopPhase = 0.0f;
    loopLatched = true;

    detectSlices();
}

// ============================================================================
// BURST - onset-sliced sequencer
// ============================================================================

void Doom::detectSlices()
{
    // Wherever the loop has a "unique" sound there is a step. A grid would
    // divide the loop; this divides the music in it.
    //
    // Half-wave-rectified energy flux over short windows, peak-picked against
    // an adaptive threshold. Cheap, and it runs once per capture rather than
    // per sample.
    constexpr int kWindow = 256;
    const auto length = static_cast<int>(loopLengthSamples);
    const auto windows = juce::jlimit(4, 512, length / kWindow);
    if (windows < 4)
    {
        sliceCount = 1;
        sliceOffsets[0] = 0.0f;
        burstOrder[0] = 0;
        return;
    }

    std::array<float, 512> energy {};
    for (int w = 0; w < windows; ++w)
    {
        auto sum = 0.0f;
        for (int i = 0; i < kWindow; ++i)
        {
            const auto pos = loopStartAbs + static_cast<float>(w * kWindow + i);
            const auto l = readHistory(0, pos);
            const auto r = readHistory(1, pos);
            sum += l * l + r * r;
        }
        energy[static_cast<std::size_t>(w)] = std::sqrt(sum / static_cast<float>(kWindow));
    }

    // Flux, then a mean-plus-deviation threshold. An absolute threshold would
    // find every onset in a loud loop and none in a quiet one.
    std::array<float, 512> flux {};
    auto fluxMean = 0.0f;
    for (int w = 1; w < windows; ++w)
    {
        const auto d = energy[static_cast<std::size_t>(w)] - energy[static_cast<std::size_t>(w - 1)];
        flux[static_cast<std::size_t>(w)] = juce::jmax(0.0f, d);
        fluxMean += flux[static_cast<std::size_t>(w)];
    }
    fluxMean /= static_cast<float>(juce::jmax(1, windows - 1));

    auto fluxDev = 0.0f;
    for (int w = 1; w < windows; ++w)
    {
        const auto d = flux[static_cast<std::size_t>(w)] - fluxMean;
        fluxDev += d * d;
    }
    fluxDev = std::sqrt(fluxDev / static_cast<float>(juce::jmax(1, windows - 1)));

    const auto threshold = fluxMean + fluxDev;
    const auto minSpacing = juce::jmax(2, windows / (kMaxSlices * 2));

    sliceCount = 0;
    auto lastPeak = -minSpacing;

    // A slice always starts at the top of the loop, so the sequence has a
    // downbeat even when the loop begins mid-note.
    sliceOffsets[static_cast<std::size_t>(sliceCount++)] = 0.0f;

    for (int w = 1; w < windows && sliceCount < kMaxSlices; ++w)
    {
        if (flux[static_cast<std::size_t>(w)] > threshold && (w - lastPeak) >= minSpacing)
        {
            sliceOffsets[static_cast<std::size_t>(sliceCount++)] =
                static_cast<float>(w * kWindow);
            lastPeak = w;
        }
    }

    if (sliceCount < 1)
    {
        sliceCount = 1;
    }

    for (int i = 0; i < sliceCount; ++i)
    {
        burstOrder[static_cast<std::size_t>(i)] = i;
    }
    burstStep = 0;
    burstStepPhase = 0.0f;
    burstGrainPhase = 0.0f;
}

Doom::Frame Doom::renderBurst()
{
    if (! loopLatched || sliceCount < 1)
    {
        return {};
    }

    const auto lengthNorm = loopLengthSmoothed.getNextValue();
    const auto spread = spreadSmoothed.getNextValue();

    // LENGTH sets the pace of the sequence, and therefore the size of each
    // step: fast steps take a short bite out of each slice.
    const auto stepSeconds = doom_control::mapLengthToBurstStep(lengthNorm);
    const auto stepSamples = juce::jmax(64.0f, static_cast<float>(internalRate) * stepSeconds);

    burstStepPhase += 1.0f;
    if (burstStepPhase >= stepSamples)
    {
        burstStepPhase -= stepSamples;
        burstStep = (burstStep + 1) % sliceCount;
        burstGrainPhase = 0.0f;

        // Live input above the threshold scrambles the pattern - the "fills"
        // that appear when you play along. Seeded, so a test run repeats.
        if (burstEnv > doom_control::mapLoopModifyToBurstSensitivity(loopModifySmoothed.getNextValue()))
        {
            const auto a = static_cast<int>(nextRandom() * static_cast<float>(sliceCount)) % sliceCount;
            const auto b = static_cast<int>(nextRandom() * static_cast<float>(sliceCount)) % sliceCount;
            std::swap(burstOrder[static_cast<std::size_t>(a)], burstOrder[static_cast<std::size_t>(b)]);
        }
    }

    const auto slice = burstOrder[static_cast<std::size_t>(burstStep % sliceCount)] % sliceCount;
    const auto sliceStart = sliceOffsets[static_cast<std::size_t>(slice)];

    burstGrainPhase += crossReadRate();
    const auto readOffset = sliceStart + burstGrainPhase;

    // A short attack/decay on each step, so slice boundaries do not click.
    const auto env = hannAt(juce::jlimit(0.0f, 1.0f, burstStepPhase / stepSamples));

    // Spliced, not a bare wrap. A step long enough to run off the end of the
    // loop used to jump straight back to its start at whatever the envelope
    // happened to be - a click once per lap, and the loudest artifact in the
    // whole engine at 27x the local slope.
    const auto fade = juce::jmin(512.0f, loopLengthSamples * 0.1f);
    const auto l = readHistorySpliced(0, loopStartAbs, readOffset, loopLengthSamples, fade) * env;
    const auto r = readHistorySpliced(1, loopStartAbs, readOffset, loopLengthSamples, fade) * env;

    // Stereo by step index, so consecutive steps land in different places.
    const auto pan = spread * (static_cast<float>(slice % 3) - 1.0f) * 0.6f;
    const auto panned = panStereo(0.5f * (l + r), pan);

    return { l * (1.0f - spread) + panned.l * spread,
             r * (1.0f - spread) + panned.r * spread };
}

// ============================================================================
// RADIO - five stations with interference between them
// ============================================================================

Doom::Frame Doom::renderStation(int station, float length)
{
    const auto spread = settings.spread;

    switch (station)
    {
        case 0:
        {
            // TAPE - the loop played straight, but at a speed and direction you
            // choose. LENGTH is bipolar around a stop at centre.
            const auto rate = juce::jmap(length, -2.0f, 2.0f);
            tapeReadPos += rate * crossReadRate();
            const auto pos = std::fmod(std::fmod(tapeReadPos, loopLengthSamples) + loopLengthSamples,
                                       loopLengthSamples);
            const auto fade = juce::jmin(512.0f, loopLengthSamples * 0.1f);
            return { readHistorySpliced(0, loopStartAbs, pos, loopLengthSamples, fade),
                     readHistorySpliced(1, loopStartAbs, pos, loopLengthSamples, fade) };
        }

        case 1:
        {
            // AMBIENT - pitch held, time dilated. Overlapping Hann grains read
            // at unity rate while the grain ORIGIN advances slowly; that is
            // what separates a time-stretch from a slow tape.
            const auto stretch = juce::jmap(length, 1.0f, 0.06f);
            loopPhase += stretch * crossReadRate();

            const auto grainLength = juce::jmax(256, static_cast<int>(internalRate * 0.08));
            const auto spawnInterval = grainLength / 4;   // 75% overlap -> constant power

            if (++grainSpawnCounter >= spawnInterval)
            {
                grainSpawnCounter = 0;
                for (auto& grain : grains)
                {
                    if (! grain.active)
                    {
                        grain.active = true;
                        grain.age = 0;
                        grain.length = grainLength;
                        grain.rate = 1.0f;
                        grain.readPos = std::fmod(loopPhase, loopLengthSamples);
                        grain.gain = 1.0f;
                        grain.pan = spread * nextBipolar() * 0.5f;
                        break;
                    }
                }
            }

            Frame sum {};
            for (auto& grain : grains)
            {
                if (! grain.active)
                {
                    continue;
                }

                const auto env = hannAt(static_cast<float>(grain.age) / static_cast<float>(grain.length));
                const auto pos = std::fmod(grain.readPos + static_cast<float>(grain.age) * grain.rate,
                                           loopLengthSamples);
                const auto mono = 0.5f * (readHistory(0, loopStartAbs + pos)
                                          + readHistory(1, loopStartAbs + pos));
                const auto panned = panStereo(mono * env * grain.gain, grain.pan);
                sum.l += panned.l;
                sum.r += panned.r;

                if (++grain.age >= grain.length)
                {
                    grain.active = false;
                }
            }

            // Four grains overlap at any moment; without this the stretch is
            // four times louder than the source.
            return { sum.l * 0.5f, sum.r * 0.5f };
        }

        case 2:
        {
            // ORCHESTRAL - a set of voices at harmonic intervals, each with its
            // own slow envelope so they come in and out rather than all
            // sounding at once.
            static constexpr std::array<float, 5> kVoiceSemitones { { 0.0f, 12.0f, 7.0f, -12.0f, -5.0f } };
            const auto voices = juce::jlimit(1, 5, 1 + static_cast<int>(length * 4.99f));

            loopPhase += crossReadRate();
            Frame sum {};

            for (int v = 0; v < voices; ++v)
            {
                const auto ratio = semitoneRatio(kVoiceSemitones[static_cast<std::size_t>(v)]);
                const auto pos = std::fmod(loopPhase * ratio + static_cast<float>(v) * 977.0f,
                                           loopLengthSamples);

                // Each voice breathes at its own slow rate, from a prime-ish
                // multiple so they never line up.
                const auto rate = 0.07f + 0.031f * static_cast<float>(v);
                const auto phase = std::fmod(static_cast<float>(historyAbsolute)
                                                 * rate / static_cast<float>(internalRate),
                                             1.0f);
                const auto env = 0.5f + 0.5f * std::sin(juce::MathConstants<float>::twoPi * phase);

                const auto fade = juce::jmin(512.0f, loopLengthSamples * 0.1f);
                const auto mono = 0.5f * (readHistorySpliced(0, loopStartAbs, pos, loopLengthSamples, fade)
                                          + readHistorySpliced(1, loopStartAbs, pos, loopLengthSamples, fade));

                const auto pan = spread * (static_cast<float>(v) / 4.0f * 2.0f - 1.0f) * 0.8f;
                const auto panned = panStereo(mono * env, pan);
                sum.l += panned.l;
                sum.r += panned.r;
            }

            const auto norm = 1.0f / std::sqrt(static_cast<float>(voices));
            return { sum.l * norm, sum.r * norm };
        }

        case 3:
        {
            // SHOEGAZE - moments taken out of the loop and sustained forever,
            // stacked in layers. LENGTH chooses which moment.
            const auto target = length * loopLengthSamples;
            // The origin creeps toward the selection rather than jumping, so
            // sweeping LENGTH scans rather than stutters.
            shoegazeOrigin += (target - shoegazeOrigin) * 0.0008f;

            const auto momentLength = juce::jmax(512, static_cast<int>(internalRate * 0.12));
            const auto spawnInterval = momentLength / 4;

            if (++grainSpawnCounter >= spawnInterval)
            {
                grainSpawnCounter = 0;
                for (auto& grain : grains)
                {
                    if (! grain.active)
                    {
                        grain.active = true;
                        grain.age = 0;
                        grain.length = momentLength;
                        grain.rate = 1.0f;
                        // Layers: each new grain takes a slightly different
                        // moment, so the stack is a chord of instants rather
                        // than one instant repeated.
                        grain.readPos = std::fmod(shoegazeOrigin + nextBipolar() * 400.0f
                                                      + loopLengthSamples,
                                                  loopLengthSamples);
                        grain.gain = 0.7f + 0.3f * nextRandom();
                        grain.pan = spread * nextBipolar();
                        break;
                    }
                }
            }

            Frame sum {};
            for (auto& grain : grains)
            {
                if (! grain.active)
                {
                    continue;
                }

                const auto env = hannAt(static_cast<float>(grain.age) / static_cast<float>(grain.length));
                const auto pos = std::fmod(grain.readPos + static_cast<float>(grain.age),
                                           loopLengthSamples);
                const auto mono = 0.5f * (readHistory(0, loopStartAbs + pos)
                                          + readHistory(1, loopStartAbs + pos));
                const auto panned = panStereo(mono * env * grain.gain, grain.pan);
                sum.l += panned.l;
                sum.r += panned.r;

                if (++grain.age >= grain.length)
                {
                    grain.active = false;
                }
            }

            return { sum.l * 0.5f, sum.r * 0.5f };
        }

        default:
        {
            // DANCE - rotates steadily between half speed, double speed and
            // normal. LENGTH sets how fast it turns.
            static constexpr std::array<float, 3> kStageRates { { 0.5f, 2.0f, 1.0f } };
            const auto rotationSeconds = juce::jmap(length, 4.0f, 0.15f);
            const auto rotationSamples = juce::jmax(64.0f,
                                                    static_cast<float>(internalRate) * rotationSeconds);

            danceRotationPhase += 1.0f;
            if (danceRotationPhase >= rotationSamples)
            {
                danceRotationPhase -= rotationSamples;
                danceStage = (danceStage + 1) % 3;
            }

            loopPhase += kStageRates[static_cast<std::size_t>(danceStage)] * crossReadRate();
            const auto pos = std::fmod(std::fmod(loopPhase, loopLengthSamples) + loopLengthSamples,
                                       loopLengthSamples);

            // Crossfade across the stage change, so the speed switch is a turn
            // rather than a cut.
            const auto changeFade = juce::jmin(rotationSamples * 0.15f, 800.0f);
            auto stageGain = 1.0f;
            if (danceRotationPhase < changeFade)
            {
                stageGain = danceRotationPhase / changeFade;
            }
            else if (danceRotationPhase > rotationSamples - changeFade)
            {
                stageGain = (rotationSamples - danceRotationPhase) / changeFade;
            }
            stageGain = 0.35f + 0.65f * stageGain;

            const auto fade = juce::jmin(512.0f, loopLengthSamples * 0.1f);
            return { readHistorySpliced(0, loopStartAbs, pos, loopLengthSamples, fade) * stageGain,
                     readHistorySpliced(1, loopStartAbs, pos, loopLengthSamples, fade) * stageGain };
        }
    }
}

Doom::Frame Doom::renderRadio()
{
    if (! loopLatched)
    {
        return {};
    }

    const auto length = loopLengthSmoothed.getNextValue();
    const auto scan = loopModifySmoothed.getNextValue();

    // MODIFY scans a station axis. Between two centres both stations are
    // audible and static rises; at a centre one station is alone and the static
    // falls to nothing - which is what "scan until the static parts" means.
    int lower = 0, upper = 0;
    float blend = 0.0f;
    doom_control::mapLoopModifyToStation(scan, kRadioStations, lower, upper, blend);

    const auto a = renderStation(lower, length);
    const auto b = lower == upper ? a : renderStation(upper, length);

    const auto gA = std::cos(blend * juce::MathConstants<float>::halfPi);
    const auto gB = std::sin(blend * juce::MathConstants<float>::halfPi);

    Frame tuned { a.l * gA + b.l * gB, a.r * gA + b.r * gB };

    // Distance from the nearest centre, 0 at a station and 1 halfway between.
    const auto offCentre = 1.0f - std::abs(blend * 2.0f - 1.0f);

    // Interference on a signal, not a noise generator underneath it: the static
    // is gated by the loop's own envelope, so it appears where there is
    // programme material and goes quiet where there is not.
    const auto envCoeff = onePoleCoeff(12.0f, static_cast<float>(internalRate));
    const auto level = 0.5f * (std::abs(tuned.l) + std::abs(tuned.r));
    radioStatic += (level - radioStatic) * envCoeff;

    const auto staticGain = offCentre * offCentre * 0.7f;
    for (int ch = 0; ch < 2; ++ch)
    {
        // Band-limited: white noise here would read as hiss rather than as a
        // signal breaking up.
        const auto coeff = onePoleCoeff(3000.0f, static_cast<float>(internalRate));
        radioNoiseState[static_cast<std::size_t>(ch)] +=
            (nextBipolar() - radioNoiseState[static_cast<std::size_t>(ch)]) * coeff;
    }

    tuned.l += radioNoiseState[0] * radioStatic * staticGain * 2.0f;
    tuned.r += radioNoiseState[1] * radioStatic * staticGain * 2.0f;

    // Between stations the tuned signal itself weakens, the way an off-station
    // signal does.
    const auto tunedGain = 1.0f - offCentre * 0.35f;
    return { tuned.l * tunedGain, tuned.r * tunedGain };
}

// ============================================================================
// MASK - threshold-driven substitution
// ============================================================================

Doom::Frame Doom::renderMask()
{
    if (! loopLatched)
    {
        return {};
    }

    const auto character = loopLengthSmoothed.getNextValue();
    const auto threshold = loopModifySmoothed.getNextValue();
    const auto spread = spreadSmoothed.getNextValue();

    loopPhase += crossReadRate();
    const auto pos = std::fmod(std::fmod(loopPhase, loopLengthSamples) + loopLengthSamples,
                               loopLengthSamples);
    const auto fade = juce::jmin(512.0f, loopLengthSamples * 0.1f);

    Frame clean { readHistorySpliced(0, loopStartAbs, pos, loopLengthSamples, fade),
                  readHistorySpliced(1, loopStartAbs, pos, loopLengthSamples, fade) };

    // MODIFY fully down is the untouched loop - the documented "good listen"
    // position, and a useful place to build a loop up before mangling it.
    if (threshold <= 0.001f)
    {
        return clean;
    }

    const auto envCoeff = onePoleCoeff(25.0f, static_cast<float>(internalRate));
    const auto level = 0.5f * (std::abs(clean.l) + std::abs(clean.r));
    maskEnv += (level - maskEnv) * envCoeff;

    // The threshold falls as MODIFY rises, so turning it up disguises more of
    // the loop, and at maximum the mask is always on.
    const auto gateThreshold = (1.0f - threshold) * 0.35f;
    const auto wanted = maskEnv > gateThreshold ? 1.0f : 0.0f;

    // Smoothed, so the mask turns on and off musically rather than switching.
    const auto gateCoeff = onePoleCoeff(30.0f, static_cast<float>(internalRate));
    maskGate += (wanted - maskGate) * gateCoeff;

    Frame disguised {};

    if (character < 0.25f)
    {
        // Ring modulation - the loop keeps its shape and loses its identity.
        maskRingPhase += juce::jmap(character, 0.0f, 0.25f, 40.0f, 320.0f)
                         / static_cast<float>(internalRate);
        maskRingPhase -= std::floor(maskRingPhase);
        const auto carrier = std::sin(juce::MathConstants<float>::twoPi * maskRingPhase);
        disguised = { clean.l * carrier, clean.r * carrier };
    }
    else if (character < 0.5f)
    {
        // Reversal - the loop read backwards, on its OWN phase. Derived from
        // the playback position it inherited that position's wrap, which threw
        // it from one end of the loop to the other in a single sample.
        maskReversePhase -= crossReadRate();
        disguised = { readHistorySpliced(0, loopStartAbs, maskReversePhase, loopLengthSamples, fade),
                      readHistorySpliced(1, loopStartAbs, maskReversePhase, loopLengthSamples, fade) };
    }
    else if (character < 0.75f)
    {
        // Pitch displacement - a fixed interval, read at a different rate and
        // on its own phase. Scaling the playback position instead made it wrap
        // wherever that position did rather than at the end of its own lap.
        const auto semis = juce::jmap(character, 0.5f, 0.75f, -12.0f, 12.0f);
        maskPitchPhase += semitoneRatio(semis) * crossReadRate();
        disguised = { readHistorySpliced(0, loopStartAbs, maskPitchPhase, loopLengthSamples, fade),
                      readHistorySpliced(1, loopStartAbs, maskPitchPhase, loopLengthSamples, fade) };
    }
    else
    {
        // Resonant excitation - the loop drives a resonator rather than being
        // heard, so what comes out is pitched by the mask, not by the loop.
        const auto freq = juce::jmap(character, 0.75f, 1.0f, 110.0f, 880.0f);
        const auto w = juce::MathConstants<float>::twoPi * freq / static_cast<float>(internalRate);
        const auto q = 0.996f;

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto idx = static_cast<std::size_t>(ch);
            const auto excite = ch == 0 ? clean.l : clean.r;
            const auto next = 2.0f * q * std::cos(w) * maskResonator[idx]
                              - q * q * maskResonatorPrev[idx]
                              + excite * 0.05f;
            maskResonatorPrev[idx] = maskResonator[idx];
            maskResonator[idx] = sanitize(next);
        }

        disguised = { maskResonator[0], maskResonator[1] };
    }

    Frame out { clean.l + (disguised.l - clean.l) * maskGate,
                clean.r + (disguised.r - clean.r) * maskGate };

    if (spread > 0.0f)
    {
        // The mask is what moves in the field; the clean loop stays put.
        const auto mid = 0.5f * (out.l + out.r);
        const auto side = 0.5f * (out.l - out.r) + maskGate * spread * 0.4f * (disguised.l - disguised.r);
        out = { mid + side, mid - side };
    }

    return out;
}

Doom::Frame Doom::renderLoop(float inL, float inR)
{
    // The envelope of what is being played, used by BURST to decide when to
    // scramble. Tracked whatever the mode, so switching into BURST does not
    // start from silence.
    const auto envCoeff = onePoleCoeff(20.0f, static_cast<float>(internalRate));
    const auto level = 0.5f * (std::abs(inL) + std::abs(inR));
    burstEnv += (level - burstEnv) * envCoeff;

    switch (settings.loopMode)
    {
        case DoomLoopMode::burst: return renderBurst();
        case DoomLoopMode::mask:  return renderMask();
        case DoomLoopMode::radio:
        default:                  return renderRadio();
    }
}

// ============================================================================
// SOUP - spectral resynthesis reverb
// ============================================================================

void Doom::soupFrame(int channel, float* real, float* imag, int numBins)
{
    const auto ch = static_cast<std::size_t>(juce::jlimit(0, 1, channel));
    auto& magnitude = soupMagnitude[ch];
    auto& phase = soupPhase[ch];
    auto& blur = soupBlur[ch];

    const auto time = settings.wetTime;
    const auto character = settings.wetModify;

    // Decay per hop from a T60. This is reverberation by spectral magnitude
    // decay: the accumulator falls exponentially and is re-excited by the
    // input, which resynthesises the signal rather than reflecting it.
    const auto t60Base = doom_control::mapWetTimeToSoupT60(time);
    const auto hopSeconds = static_cast<float>(soupStft.hopSize()) / static_cast<float>(internalRate);

    const auto frozen = settings.freeze;

    for (int k = 0; k < numBins; ++k)
    {
        const auto idx = static_cast<std::size_t>(k);
        const auto binNorm = static_cast<float>(k) / static_cast<float>(juce::jmax(1, numBins - 1));

        // Highs decay faster, so the tail darkens as it fades the way a real
        // one does - without this, a long decay is a bright, ringing pad.
        const auto t60 = juce::jmax(0.05f, t60Base * (1.0f - 0.72f * binNorm * binNorm));
        auto g = frozen ? 1.0f : std::pow(10.0f, -3.0f * hopSeconds / t60);
        g = juce::jlimit(0.0f, 1.0f, g);

        const auto re = real[k];
        const auto im = imag[k];
        const auto inputMag = std::sqrt(re * re + im * im);
        const auto inputPhase = std::atan2(im, re);

        // Freeze stops listening; otherwise the accumulator takes the louder of
        // its decayed self and the new input, so a note re-excites the tail
        // without erasing it.
        auto held = magnitude[idx] * g;
        if (! frozen)
        {
            held = juce::jmax(held, inputMag);
        }
        magnitude[idx] = std::isfinite(held) ? held : 0.0f;

        // Phase is the synthetic axis. Coherent phase keeps partials lined up
        // and the tail sounds like a (strange) room; randomised phase destroys
        // the transient structure and leaves only the spectral envelope, which
        // is what "a distant memory of your instrument" sounds like.
        const auto expected = juce::MathConstants<float>::twoPi
                              * static_cast<float>(k) * static_cast<float>(soupStft.hopSize())
                              / static_cast<float>(1 << kSoupFftOrder);

        const auto coherent = frozen ? phase[idx] + expected : inputPhase;
        const auto scattered = phase[idx] + expected
                               + nextBipolar() * juce::MathConstants<float>::pi * character;

        auto nextPhase = coherent + (scattered - coherent) * character;
        // Keep it bounded; an unwrapped phase accumulator loses precision.
        nextPhase = std::fmod(nextPhase, juce::MathConstants<float>::twoPi);
        phase[idx] = nextPhase;
    }

    // Blur the magnitude across neighbouring bins as character rises. Smearing
    // the spectrum is what turns a recognisable note into a cloud, and it is
    // the other half of the synthetic axis.
    if (character > 0.01f)
    {
        for (int k = 0; k < numBins; ++k)
        {
            const auto lo = magnitude[static_cast<std::size_t>(juce::jmax(0, k - 1))];
            const auto hi = magnitude[static_cast<std::size_t>(juce::jmin(numBins - 1, k + 1))];
            const auto mid = magnitude[static_cast<std::size_t>(k)];
            blur[static_cast<std::size_t>(k)] = 0.25f * lo + 0.5f * mid + 0.25f * hi;
        }
        for (int k = 0; k < numBins; ++k)
        {
            const auto idx = static_cast<std::size_t>(k);
            magnitude[idx] += (blur[idx] - magnitude[idx]) * character;
        }
    }

    for (int k = 0; k < numBins; ++k)
    {
        const auto idx = static_cast<std::size_t>(k);
        const auto m = magnitude[idx];
        real[k] = m * std::cos(phase[idx]);
        imag[k] = m * std::sin(phase[idx]);
    }
}

Doom::Frame Doom::renderSoup(float inL, float inR)
{
    if (! soupPrepared)
    {
        return {};
    }

    const auto frame = [this](int channel, float* real, float* imag, int bins)
    {
        soupFrame(channel, real, imag, bins);
    };

    // The overlap-add reconstructs at unity on its own. This trim is a level
    // choice, not a correction: a long spectral decay accumulates energy the
    // input never had, and it has to sit under the rest of the chain.
    constexpr auto kSoupTrim = 0.45f;

    return { sanitize(soupStft.processSample(0, inL, frame) * kSoupTrim),
             sanitize(soupStft.processSample(1, inR, frame) * kSoupTrim) };
}

// ============================================================================
// RELAY - a countable number of repeats that do not fade
// ============================================================================

Doom::Frame Doom::renderRelay(float inL, float inR)
{
    const auto time = wetTimeSmoothed.getNextValue();
    const auto modify = wetModifySmoothed.getNextValue();
    const auto spread = spreadSmoothed.getNextValue();

    const auto delaySeconds = doom_control::mapWetTimeToRelayDelay(time);
    const auto delaySamples = juce::jlimit(4.0f,
                                           static_cast<float>(relaySize) / static_cast<float>(kMaxRelayTaps + 1),
                                           static_cast<float>(internalRate) * delaySeconds);

    // MODIFY selects how many repeats there are, not how loud they are. The
    // last position is the "pile up like a looper" one.
    const auto taps = doom_control::mapWetModifyToRelayTaps(modify, kMaxRelayTaps);
    const auto infinite = doom_control::mapWetModifyToRelayInfinite(modify);

    // Parallel taps, not feedback. Feedback produces a geometric decay by
    // construction; equal-level countable repeats cannot come from a loop.
    Frame sum {};
    for (int t = 1; t <= taps; ++t)
    {
        const auto offset = delaySamples * static_cast<float>(t);

        std::array<float, 2> tap {};
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto& line = relayBuffer[static_cast<std::size_t>(ch)];
            auto pos = static_cast<float>(relayWrite) - offset;
            while (pos < 0.0f)
            {
                pos += static_cast<float>(relaySize);
            }

            const auto i1 = static_cast<int>(pos) % relaySize;
            const auto i2 = (i1 + 1) % relaySize;
            const auto frac = pos - std::floor(pos);
            tap[static_cast<std::size_t>(ch)] = line[static_cast<std::size_t>(i1)] * (1.0f - frac)
                                                + line[static_cast<std::size_t>(i2)] * frac;
        }

        // Repeats alternate across the field by TRADING the two channels, not
        // by panning. An equal-power pan attenuates one side, and with the pan
        // widening per tap that reads as a decay - which is the one thing this
        // mode must not do. A swap is exactly level-preserving.
        if (t % 2 == 0)
        {
            sum.l += tap[0] * (1.0f - spread) + tap[1] * spread;
            sum.r += tap[1] * (1.0f - spread) + tap[0] * spread;
        }
        else
        {
            sum.l += tap[0];
            sum.r += tap[1];
        }
    }

    // Adding repeats must not add level.
    const auto norm = 1.0f / std::sqrt(static_cast<float>(taps));
    sum.l *= norm;
    sum.r *= norm;

    auto writeL = inL;
    auto writeR = inR;

    if (infinite || settings.freeze)
    {
        // The looper position. A plain feedback of 1.0 has no energy sink and
        // grows without bound; routing the recirculation through the saturator
        // gives it one, so it sustains instead of exploding.
        relayHold[0] = softSaturate(relayHold[0] * 0.9995f + sum.l * 0.25f);
        relayHold[1] = softSaturate(relayHold[1] * 0.9995f + sum.r * 0.25f);

        if (settings.freeze)
        {
            // Frozen: stop listening, keep circulating.
            writeL = relayHold[0] * 0.6f;
            writeR = relayHold[1] * 0.6f;
        }
        else
        {
            writeL += relayHold[0] * 0.35f;
            writeR += relayHold[1] * 0.35f;
        }
    }
    else
    {
        relayHold = { { 0.0f, 0.0f } };
    }

    relayBuffer[0][static_cast<std::size_t>(relayWrite)] = sanitize(writeL);
    relayBuffer[1][static_cast<std::size_t>(relayWrite)] = sanitize(writeR);
    relayWrite = (relayWrite + 1) % relaySize;

    return { sanitize(sum.l), sanitize(sum.r) };
}

// ============================================================================
// FLIP - layered harmonies spread across time
// ============================================================================

Doom::Frame Doom::renderFlip(float inL, float inR)
{
    const auto lag = wetTimeSmoothed.getNextValue();
    const auto modify = wetModifySmoothed.getNextValue();
    const auto spread = spreadSmoothed.getNextValue();

    if (! settings.freeze)
    {
        flipBuffer[0][static_cast<std::size_t>(flipWrite)] = sanitize(inL);
        flipBuffer[1][static_cast<std::size_t>(flipWrite)] = sanitize(inR);
        flipWrite = (flipWrite + 1) % flipSize;
    }
    else
    {
        // Frozen, the write head stops and the grains keep circling what is
        // already in the buffer - a repeating chord.
        flipWrite = (flipWrite + 1) % flipSize;
    }

    const auto set = kHarmonies[static_cast<std::size_t>(
        doom_control::mapWetModifyToFlipHarmony(modify, static_cast<int>(kHarmonies.size())))];

    // Granular rather than phase-vocoder shifting: the artifacts of a short
    // window ARE this mode, per the source's own "laggy character of older
    // pitch shifters" note - and it costs a fraction of the CPU.
    const auto grainLength = juce::jmax(256, static_cast<int>(internalRate * 0.055));
    const auto spawnInterval = grainLength / 4;   // 4 grains per voice, 25% offsets

    const auto lagSeconds = juce::jmap(lag, 0.0f, 0.35f);
    const auto lagSamples = static_cast<float>(internalRate) * lagSeconds;

    if (++flipSpawnCounter >= spawnInterval)
    {
        flipSpawnCounter = 0;

        for (int v = 0; v < set.count; ++v)
        {
            for (auto& grain : flipGrains)
            {
                if (! grain.active)
                {
                    grain.active = true;
                    grain.age = 0;
                    grain.length = grainLength;
                    grain.rate = semitoneRatio(set.semitones[static_cast<std::size_t>(v)]);

                    // Each note enters a little later than the one before it -
                    // the harmony is spread across time rather than struck as a
                    // block.
                    const auto voiceLag = lagSamples * static_cast<float>(v + 1);
                    grain.readPos = static_cast<float>(flipWrite)
                                    - static_cast<float>(grainLength) * grain.rate
                                    - voiceLag;
                    while (grain.readPos < 0.0f)
                    {
                        grain.readPos += static_cast<float>(flipSize);
                    }

                    grain.gain = 1.0f;
                    grain.pan = spread * (static_cast<float>(v) / juce::jmax(1.0f, static_cast<float>(set.count - 1))
                                          * 1.6f - 0.8f);
                    break;
                }
            }
        }
    }

    Frame sum {};
    for (auto& grain : flipGrains)
    {
        if (! grain.active)
        {
            continue;
        }

        const auto env = hannAt(static_cast<float>(grain.age) / static_cast<float>(grain.length));
        const auto pos = grain.readPos + static_cast<float>(grain.age) * grain.rate;

        auto mono = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto& line = flipBuffer[static_cast<std::size_t>(ch)];
            auto p = std::fmod(pos, static_cast<float>(flipSize));
            if (p < 0.0f)
            {
                p += static_cast<float>(flipSize);
            }
            const auto i1 = static_cast<int>(p) % flipSize;
            const auto i2 = (i1 + 1) % flipSize;
            const auto frac = p - std::floor(p);
            mono += 0.5f * (line[static_cast<std::size_t>(i1)] * (1.0f - frac)
                            + line[static_cast<std::size_t>(i2)] * frac);
        }

        const auto panned = panStereo(mono * env * grain.gain, grain.pan);
        sum.l += panned.l;
        sum.r += panned.r;

        if (++grain.age >= grain.length)
        {
            grain.active = false;
        }
    }

    const auto norm = 0.5f / std::sqrt(static_cast<float>(juce::jmax(1, set.count)));
    return { sanitize(sum.l * norm), sanitize(sum.r * norm) };
}

Doom::Frame Doom::renderWet(float inL, float inR)
{
    switch (settings.wetMode)
    {
        case DoomWetMode::relay: return renderRelay(inL, inR);
        case DoomWetMode::flip:  return renderFlip(inL, inR);
        case DoomWetMode::soup:
        default:                 return renderSoup(inL, inR);
    }
}

// ============================================================================
// CROSS - signal-dependent interference in pitch and loudness
// ============================================================================

void Doom::updateCross(float inL, float inR, const Frame& loop, const Frame& wet)
{
    const auto depth = crossSmoothed.getNextValue();
    if (depth <= 0.0001f)
    {
        crossAm = 1.0f;
        crossFm = 0.0f;
        return;
    }

    // The source is the music, which is what makes this organic rather than a
    // random-number generator wired to a knob: it squiggles where you play.
    auto source = 0.0f;
    if (settings.crossSource == DoomCrossSource::input)
    {
        source = 0.5f * (std::abs(inL) + std::abs(inR));
    }
    else
    {
        // Each channel modulates the other. No audio crosses here - only a
        // control value - so there is no feedback loop to go unstable.
        source = 0.25f * (std::abs(loop.l) + std::abs(loop.r) + std::abs(wet.l) + std::abs(wet.r));
    }

    // Fast attack, slow release: it should jump at a note and relax after it.
    const auto attack = onePoleCoeff(90.0f, static_cast<float>(internalRate));
    const auto release = onePoleCoeff(3.0f, static_cast<float>(internalRate));
    const auto coeff = source > crossEnv ? attack : release;
    crossEnv += (source - crossEnv) * coeff;

    // A long-term mean, so the FM term is the follower's DEVIATION rather than
    // its level. Without this a sustained pad simply detunes; with it, it
    // wavers.
    const auto meanCoeff = onePoleCoeff(0.35f, static_cast<float>(internalRate));
    crossMean += (crossEnv - crossMean) * meanCoeff;

    // Slew limiting is the difference between organic and jumpy. A jump in read
    // rate is a click; a bounded ramp is a bend.
    const auto maxStep = 12.0f / static_cast<float>(internalRate);
    const auto wanted = juce::jlimit(0.0f, 1.0f, crossEnv * 3.0f);
    crossSlewed += juce::jlimit(-maxStep, maxStep, wanted - crossSlewed);

    crossAm = 1.0f - depth * crossSlewed * 0.85f;
    crossFm = depth * juce::jlimit(-1.0f, 1.0f, (crossEnv - crossMean) * 8.0f) * 0.06f;
}

// ============================================================================
// EQ - a tilt, not two cuts
// ============================================================================

Doom::Frame Doom::applyEq(Frame in)
{
    const auto tilt = eqSmoothed.getNextValue();
    if (std::abs(tilt) < 0.001f)
    {
        return in;
    }

    // One pivot, two shelves moving in opposition. Documented as having no
    // effect at noon and removing lows one way and highs the other, which is a
    // tilt rather than a pair of independent filters.
    const auto coeff = onePoleCoeff(700.0f, static_cast<float>(internalRate));

    std::array<float, 2> values { { in.l, in.r } };
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto idx = static_cast<std::size_t>(ch);
        eqLowState[idx] += (values[idx] - eqLowState[idx]) * coeff;
        const auto low = eqLowState[idx];
        const auto high = values[idx] - low;

        const auto lowGain = 1.0f - juce::jmax(0.0f, tilt) * 0.9f;
        const auto highGain = 1.0f - juce::jmax(0.0f, -tilt) * 0.9f;
        values[idx] = low * lowGain + high * highGain;
    }

    return { values[0], values[1] };
}

// ============================================================================
// GLUE - warm up, or ruin everything
// ============================================================================

Doom::Frame Doom::applyGlue(Frame in)
{
    const auto glue = glueSmoothed.getNextValue();
    if (glue <= 0.0001f)
    {
        return in;
    }

    std::array<float, 2> values { { in.l, in.r } };

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto idx = static_cast<std::size_t>(ch);
        auto x = values[idx];

        // Region one: asymmetric soft saturation. A purely odd-symmetric shaper
        // makes only odd harmonics and sounds sterile at low drive - the small
        // even-harmonic term is what "warm" means spectrally.
        const auto drive = 1.0f + glue * 7.0f;
        const auto asymmetry = glue * 0.18f;
        auto y = softSaturate(x * drive + asymmetry) - softSaturate(asymmetry);

        // Region two: past halfway the shaper starts to fold. This is where it
        // stops being distortion and starts being destruction.
        if (glue > 0.5f)
        {
            const auto foldAmount = (glue - 0.5f) * 2.0f;
            const auto foldDrive = 1.0f + foldAmount * 3.0f;
            auto folded = y * foldDrive;

            // Triangle fold: reflect back at +/-1 rather than clipping there.
            folded = std::fmod(std::abs(folded + 1.0f), 4.0f);
            folded = folded > 2.0f ? 4.0f - folded : folded;
            folded -= 1.0f;

            y += (folded - y) * foldAmount;
        }

        // Region three: downward compression at the very top, so maximum GLUE
        // is dense rather than merely loud.
        if (glue > 0.8f)
        {
            const auto squash = (glue - 0.8f) * 5.0f;
            y = y * (1.0f - squash) + softSaturate(y * 2.2f) * 0.55f * squash;
        }

        // Asymmetric shaping generates DC by definition; it has to come back
        // out before this reaches a speaker or the next stage's feedback.
        const auto dcIn = y;
        y = dcIn - glueDcPrev[idx] + 0.9985f * glueDcState[idx];
        glueDcPrev[idx] = dcIn;
        glueDcState[idx] = y;

        // Level-compensated per region, so turning GLUE up changes character
        // rather than loudness.
        const auto makeup = 1.0f / (1.0f + glue * 2.2f);
        values[idx] = sanitize(y * makeup * (1.0f + glue * 0.55f));
    }

    return { values[0], values[1] };
}

// ============================================================================
// one internal step
// ============================================================================

Doom::Frame Doom::processInternalStep(float inL, float inR)
{
    const auto routing = settings.routing;
    const auto loopOn = loopActiveSmoothed.getNextValue();
    const auto wetOn = wetActiveSmoothed.getNextValue();
    const auto balance = balanceSmoothed.getNextValue();
    const auto blend = blendSmoothed.getNextValue();
    const auto overdub = overdubSmoothed.getNextValue();

    // ---- micro-looper ---------------------------------------------------
    const auto loop = settings.loopActive ? renderLoop(inL, inR) : Frame {};

    // Cross reads the state BEFORE it is used, so a channel modulating the
    // other one is reading last step's value rather than this step's - which is
    // what keeps the two-way case from being an algebraic loop.
    const auto amGain = crossAm;

    Frame loopOut { loop.l * loopOn, loop.r * loopOn };

    // ---- what the wet channel is fed ------------------------------------
    // ROUTING only means anything when both channels are on, as documented.
    Frame wetIn { inL, inR };
    if (settings.loopActive && settings.wetActive)
    {
        switch (routing)
        {
            case DoomRouting::inputPlusLoop: wetIn = { inL + loopOut.l, inR + loopOut.r }; break;
            case DoomRouting::loop:          wetIn = { loopOut.l, loopOut.r }; break;
            case DoomRouting::input:
            default:                         break;
        }
    }

    const auto wet = settings.wetActive ? renderWet(wetIn.l, wetIn.r) : Frame {};
    Frame wetOut { wet.l * wetOn, wet.r * wetOn };

    // A loop sent through the wet channel becomes fully wet by default; BLEND
    // puts some of the clean loop back.
    Frame loopContribution = loopOut;
    if (settings.loopActive && settings.wetActive && routing == DoomRouting::loop)
    {
        loopContribution = { loopOut.l * blend, loopOut.r * blend };
    }

    updateCross(inL, inR, loopOut, wetOut);

    // ---- balance --------------------------------------------------------
    const auto loopGain = std::cos(balance * juce::MathConstants<float>::halfPi)
                          * juce::MathConstants<float>::sqrt2;
    const auto wetGain = std::sin(balance * juce::MathConstants<float>::halfPi)
                         * juce::MathConstants<float>::sqrt2;

    Frame mixed { loopContribution.l * loopGain + wetOut.l * wetGain,
                  loopContribution.r * loopGain + wetOut.r * wetGain };

    // CROSS's amplitude term: the dropouts and sputters.
    mixed.l *= amGain;
    mixed.r *= amGain;

    // ---- the always-listening write -------------------------------------
    // In its bypassed state the looper records the wet channel too, regardless
    // of routing - which is how a loop ends up with the reverb trails in it.
    // While playing, only the clean input is overdubbed, because recording the
    // wet channel into a loop that feeds it is a feedback loop.
    if (! settings.loopActive)
    {
        writeHistory(inL + wetOut.l * 0.7f, inR + wetOut.r * 0.7f);
    }
    else if (overdub > 0.001f)
    {
        const auto pos = static_cast<int>(std::fmod(loopStartAbs + loopPhase
                                                        + static_cast<float>(historySize) * 4.0f,
                                                    static_cast<float>(historySize)));
        const auto idx = static_cast<std::size_t>(juce::jlimit(0, historySize - 1, pos));

        // FADE decides how much of the previous lap survives. Below unity the
        // loop evolves, and at low settings the looper behaves like a delay.
        const auto retain = juce::jlimit(0.0f, 1.0f, settings.fade);
        history[0][idx] = sanitize(history[0][idx] * retain + inL * overdub);
        history[1][idx] = sanitize(history[1][idx] * retain + inR * overdub);

        // The write head keeps running even while playing, so switching back to
        // the listening state has fresh material rather than a gap.
        historyWrite = (historyWrite + 1) % historySize;
        historyAbsolute += 1.0;
    }
    else
    {
        writeHistory(inL, inR);
    }

    // ---- global stages ---------------------------------------------------
    return applyGlue(applyEq(mixed));
}

// ============================================================================
// host-rate entry point
// ============================================================================

void Doom::processSampleFrame(float inL, float inR, float& outL, float& outR)
{
    const auto enabled = enabledSmoothed.getNextValue();
    const auto mix = mixSmoothed.getNextValue();

    // Contributing nothing, and not on the way to contributing something. The
    // engine is skipped entirely and its tails are cleared once, so coming back
    // starts from silence rather than from whatever was in flight.
    const auto amountNow = mix * enabled;
    if (amountNow <= 1.0e-6f && ! mixSmoothed.isSmoothing() && ! enabledSmoothed.isSmoothing())
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

    updateClock();

    // Down-conversion by box average over each internal step. Point sampling
    // folds everything above the internal Nyquist straight into the band, which
    // is noise rather than character.
    decimateAccum.l += inL;
    decimateAccum.r += inR;
    ++decimateCount;

    clockPhase += clockRatio;
    if (clockPhase >= 1.0f)
    {
        clockPhase -= 1.0f;

        const auto norm = 1.0f / static_cast<float>(juce::jmax(1, decimateCount));
        const auto stepIn = Frame { decimateAccum.l * norm, decimateAccum.r * norm };
        decimateAccum = {};
        decimateCount = 0;

        heldOutput = processInternalStep(stepIn.l, stepIn.r);
    }

    // Zero-order hold plus a one-pole at the internal Nyquist. The hold's
    // imaging is a large part of what makes a low clock sound digital rather
    // than merely dull, so it is tamed rather than removed.
    reconstructState[0] += (heldOutput.l - reconstructState[0]) * reconstructCoeff;
    reconstructState[1] += (heldOutput.r - reconstructState[1]) * reconstructCoeff;

    const auto wetL = sanitize(reconstructState[0]);
    const auto wetR = sanitize(reconstructState[1]);

    const auto amount = mix * enabled;
    outL = inL * (1.0f - amount) + wetL * amount;
    outR = inR * (1.0f - amount) + wetR * amount;
}

} // namespace px3
