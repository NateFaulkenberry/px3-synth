#include "Reverb.h"

#include <cmath>

float ::Reverb::clamp01(float v)
{
    return juce::jlimit(0.0f, 1.0f, v);
}

float ::Reverb::lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

float ::Reverb::smoothstep(float x)
{
    const auto t = clamp01(x);
    return t * t * (3.0f - 2.0f * t);
}

namespace
{
// Jon Dattorro, "Effect Design Part 1: Reverberator and Other Filters", JAES
// vol 45 no 9 (1997) - a simplified plate topology in the style of Griesinger,
// itself derived from reverse-engineering a Lexicon 224. The delay lengths are
// quoted for a 29761 Hz reference rate.
constexpr float kDattorroReferenceRate = 29761.0f;
constexpr float kPlateSizeMax = 1.6f;

// [0..3] input diffusion, [4..11] the tank: two modulated allpasses, two
// delays, two decay-diffusion allpasses, two delays.
constexpr std::array<float, 12> kPlateBaseDelays { {
    142.0f, 107.0f, 379.0f, 277.0f,
    672.0f, 4453.0f, 1800.0f, 3720.0f,
    908.0f, 4217.0f, 2656.0f, 3163.0f
} };

// The seven-tap output pickup. Taking the tank out at a single point leaves it
// sounding like a delay; these positions are chosen so the two channels are
// decorrelated and the response is dense. Tap positions are also in reference-
// rate samples, and carry the signs from the paper.
// Fons Adriaensen's zita-rev1 delay set, in seconds. These are mutually
// incommensurate by design: an FDN whose delay lengths share factors produces
// coincident echoes, which is heard as flutter and periodic ringing. Each line
// also carries an allpass, which is what gives zita its smooth tail.
constexpr std::array<float, 8> kFdnDelaySeconds { {
    0.153129f, 0.210389f, 0.127837f, 0.256891f,
    0.174713f, 0.192303f, 0.125000f, 0.219991f
} };
constexpr std::array<float, 8> kFdnAllpassSeconds { {
    0.020346f, 0.024421f, 0.031604f, 0.027333f,
    0.022904f, 0.029291f, 0.013458f, 0.019123f
} };

// Short, mutually prime input diffusion lengths (in samples at 48 kHz).
constexpr std::array<float, 4> kInputDiffusionSamples { { 293.0f, 439.0f, 647.0f, 911.0f } };


// Small-room early reflections. Times are milliseconds and deliberately not
// harmonically related, with a different pattern per channel so the two ears
// never hear the same arrival sequence. Gains fall roughly as the inverse of
// path length, the way real reflections lose energy, and alternate in sign
// because real surfaces invert on reflection.
struct EarlyTap { float ms; float gain; };
constexpr std::array<EarlyTap, 9> kRoomEarlyLeft { {
    { 4.3f, 0.841f }, { 7.9f, -0.615f }, { 12.7f, 0.514f }, { 17.3f, -0.428f },
    { 22.1f, 0.363f }, { 28.9f, -0.301f }, { 35.3f, 0.252f }, { 43.7f, -0.201f },
    { 52.3f, 0.161f }
} };
constexpr std::array<EarlyTap, 9> kRoomEarlyRight { {
    { 5.9f, 0.807f }, { 9.7f, -0.647f }, { 14.3f, 0.489f }, { 19.9f, -0.401f },
    { 25.7f, 0.347f }, { 31.1f, -0.288f }, { 38.9f, 0.239f }, { 47.3f, -0.191f },
    { 56.9f, 0.153f }
} };
constexpr float kRoomEarlyMaxMs = 60.0f;

constexpr float kRoomSizeMax = 1.8f;

// The three algorithms are the same network at three scales. These bounds are
// used for BOTH allocation and processing, so a buffer can never be shorter
// than the delay that will be asked of it.
constexpr float kRoomScaleMin = 0.24f, kRoomScaleMax = 0.68f;   // ~20-118 ms
constexpr float kHallScaleMin = 0.30f, kHallScaleMax = 0.90f;   // ~37-231 ms
constexpr float kCloudScaleMin = 0.60f, kCloudScaleMax = 1.90f; // ~75-488 ms

// Householder-style 8x8 mixing via three butterfly stages. This is the
// Hadamard matrix, which is orthogonal - it redistributes energy between the
// lines without creating or destroying any, so the decay is set purely by the
// per-line gains and never runs away. The previous matrix mixed two arbitrary
// neighbours with weights 0.58/0.42, which is not orthogonal and therefore had
// no predictable gain at all.
inline void hadamard8(std::array<float, 8>& v)
{
    for (int stride = 1; stride < 8; stride *= 2)
    {
        for (int i = 0; i < 8; i += stride * 2)
        {
            for (int j = i; j < i + stride; ++j)
            {
                const auto a = v[static_cast<std::size_t>(j)];
                const auto b = v[static_cast<std::size_t>(j + stride)];
                v[static_cast<std::size_t>(j)] = a + b;
                v[static_cast<std::size_t>(j + stride)] = a - b;
            }
        }
    }
    // 1/sqrt(8) keeps the transform unitary.
    constexpr float norm = 0.35355339f;
    for (auto& x : v) x *= norm;
}

// Jot's design rule: the per-line gain for a target RT60 depends on that line's
// own length, g = 10^(-3 * M / (RT60 * fs)). Giving every line the same gain -
// as the previous code did - makes short lines decay far slower in time than
// long ones, which is what produced the lurching, uneven tail.
inline float rt60Gain(float delaySamples, float rt60Seconds, float sampleRate)
{
    const auto n60 = juce::jmax(1.0f, rt60Seconds * sampleRate);
    return std::pow(10.0f, -3.0f * delaySamples / n60);
}

struct PlateTap { int line; float position; float sign; };
constexpr std::array<PlateTap, 7> kPlateTapsLeft { {
    { 9, 266.0f, +1.0f }, { 9, 2974.0f, +1.0f }, { 10, 1913.0f, -1.0f },
    { 11, 1996.0f, +1.0f }, { 5, 1990.0f, -1.0f }, { 6, 187.0f, -1.0f },
    { 7, 1066.0f, -1.0f }
} };
constexpr std::array<PlateTap, 7> kPlateTapsRight { {
    { 5, 353.0f, +1.0f }, { 5, 3627.0f, +1.0f }, { 6, 1228.0f, -1.0f },
    { 7, 2673.0f, +1.0f }, { 9, 2111.0f, -1.0f }, { 10, 335.0f, -1.0f },
    { 11, 121.0f, -1.0f }
} };
}

float ::Reverb::sanitizeAudioSample(float x)
{
    if (!std::isfinite(x))
    {
        return 0.0f;
    }

    return juce::jlimit(-4.0f, 4.0f, x);
}

void ::Reverb::resizeLine(DelayLine& line, int size)
{
    line.buffer.assign(static_cast<std::size_t>(juce::jmax(2, size)), 0.0f);
    line.writePos = 0;
    line.lpState = 0.0f;
}

void ::Reverb::writeLine(DelayLine& line, float sample)
{
    if (line.buffer.empty())
    {
        return;
    }

    line.buffer[static_cast<std::size_t>(line.writePos)] = sample;
    line.writePos = (line.writePos + 1) % static_cast<int>(line.buffer.size());
}

float ::Reverb::readLine(const DelayLine& line, float delaySamples)
{
    if (line.buffer.empty())
    {
        return 0.0f;
    }

    const auto size = static_cast<int>(line.buffer.size());
    auto readPos = static_cast<float>(line.writePos) - delaySamples;
    while (readPos < 0.0f)
    {
        readPos += static_cast<float>(size);
    }
    while (readPos >= static_cast<float>(size))
    {
        readPos -= static_cast<float>(size);
    }

    const auto i0 = static_cast<int>(readPos);
    const auto i1 = (i0 + 1) % size;
    const auto frac = readPos - static_cast<float>(i0);
    return line.buffer[static_cast<std::size_t>(i0)]
           + (line.buffer[static_cast<std::size_t>(i1)] - line.buffer[static_cast<std::size_t>(i0)]) * frac;
}

float ::Reverb::processAllpass(DelayLine& line, float in, float delaySamples, float gain)
{
    const auto delayed = readLine(line, delaySamples);
    const auto v = in - gain * delayed;
    writeLine(line, v);
    return delayed + gain * v;
}

void ::Reverb::allocateFdn(std::array<DelayLine, 8>& delays,
                           std::array<DelayLine, 8>& allpasses,
                           float maxScale)
{
    for (std::size_t i = 0; i < delays.size(); ++i)
    {
        resizeLine(delays[i], juce::jmax(32, static_cast<int>(std::round(sampleRateHz * kFdnDelaySeconds[i] * maxScale)) + 64));
        resizeLine(allpasses[i], juce::jmax(16, static_cast<int>(std::round(sampleRateHz * kFdnAllpassSeconds[i] * maxScale)) + 16));
        delays[i].modPhase = static_cast<float>(i) * 0.7853982f;
        delays[i].lpState = 0.0f;
        allpasses[i].lpState = 0.0f;
    }
}

void ::Reverb::processFdn8(std::array<DelayLine, 8>& delays,
                           std::array<DelayLine, 8>& allpasses,
                           std::array<float, 8>& readCache,
                           const FdnConfig& config,
                           float input,
                           float& wetL,
                           float& wetR)
{
    const auto sr = static_cast<float>(sampleRateHz);
    const auto modStep = juce::MathConstants<float>::twoPi * config.modHz / juce::jmax(1.0f, sr);

    std::array<float, 8> taps { };
    for (int i = 0; i < 8; ++i)
    {
        const auto idx = static_cast<std::size_t>(i);
        auto& line = delays[idx];

        // Slightly different modulation rate per line, so the lines never drift
        // into step and produce a common pitch wobble.
        line.modPhase += modStep * (1.0f + 0.037f * static_cast<float>(i));
        if (line.modPhase > juce::MathConstants<float>::twoPi)
        {
            line.modPhase -= juce::MathConstants<float>::twoPi;
        }

        const auto delaySamples = kFdnDelaySeconds[idx] * sr * config.sizeScale;
        const auto mod = std::sin(line.modPhase) * config.modSamples;
        auto v = readLine(line, juce::jmax(4.0f, delaySamples + mod));

        // One-pole lowpass per line: highs must die faster than lows, which is
        // what makes a space sound like air rather than metal.
        line.lpState += config.dampingCoeff * (v - line.lpState);
        v = line.lpState;

        readCache[idx] = v;
        taps[idx] = v * rt60Gain(delaySamples, config.rt60Seconds, sr);
    }

    hadamard8(taps);

    for (int i = 0; i < 8; ++i)
    {
        const auto idx = static_cast<std::size_t>(i);
        const auto injected = input * ((i & 1) ? -config.inputGain : config.inputGain) + taps[idx];
        const auto apDelay = kFdnAllpassSeconds[idx] * sr * config.sizeScale;
        const auto shaped = processAllpass(allpasses[idx], injected, apDelay, config.allpassGain);
        writeLine(delays[idx], sanitizeAudioSample(shaped));
    }

    // Disjoint line sets give decorrelation without inverting one channel
    // against the other, so the result survives a mono sum. The two sets are
    // chosen to have near-equal total delay length (0.727 s against 0.733 s of
    // the zita set) because a line holds energy in proportion to its length -
    // grouping the long lines on one side puts the whole reverb off centre.
    wetL = (readCache[0] - readCache[3] + readCache[5] - readCache[6]) * 0.5f;
    wetR = (readCache[1] - readCache[2] + readCache[4] - readCache[7]) * 0.5f;
}

float ::Reverb::processInputDiffusion(float in, float amount)
{
    const auto scale = static_cast<float>(sampleRateHz) / 48000.0f;
    auto x = in;
    for (std::size_t i = 0; i < inputDiffusionLines.size(); ++i)
    {
        // Alternating signs stop the four stages from lining up into a single
        // long allpass with an audible resonance.
        const auto gain = (i & 1) ? -amount : amount;
        x = processAllpass(inputDiffusionLines[i], x, kInputDiffusionSamples[i] * scale, gain);
    }
    return x;
}

float ::Reverb::processDelay(DelayLine& line, float in, float delaySamples)
{
    const auto delayed = readLine(line, delaySamples);
    writeLine(line, in);
    return delayed;
}

void ::Reverb::prepare(double sampleRate)
{
    sampleRateHz = juce::jmax(1.0, sampleRate);

    constexpr float reverbAmountTauSec = 0.020f;
    const auto sr = static_cast<float>(sampleRateHz);
    amountSmoothingCoeff = 1.0f - std::exp(-1.0f / (sr * reverbAmountTauSec));

    reset();
}

void ::Reverb::reset()
{
    outputCompGain = 1.0f;
    blockPreEnergy = 0.0;
    blockPostEnergy = 0.0;
    blockSampleCount = 0;

    inputDcX1 = { { 0.0f, 0.0f } };
    inputDcY1 = { { 0.0f, 0.0f } };
    wetDcX1 = { { 0.0f, 0.0f } };
    wetDcY1 = { { 0.0f, 0.0f } };
    wetSlewState = { { 0.0f, 0.0f } };

    // Must hold the LONGEST delay processCore can ask for, which is
    // 1.0 + preDelay * sampleRate * 0.30 at preDelay = 1, plus room for the
    // interpolator's second tap. Sized at exactly sampleRate * 0.30 the request
    // overran the line by one sample at the top of the range, and readLine
    // wraps rather than clamps, so maximum pre-delay collapsed to a one-sample
    // delay - the control read as "off" at both ends of its travel.
    const auto maxPreDelaySamples = juce::jmax(8, static_cast<int>(std::round(sampleRateHz * 0.30)) + 8);
    for (auto& line : preDelayLines)
    {
        resizeLine(line, maxPreDelaySamples);
    }

    // Dattorro's plate, in his own delay-line lengths. They are quoted for a
    // 29761 Hz reference rate, so they are scaled to the running rate here and
    // then given headroom for the SIZE control and the tank modulation.
    for (std::size_t i = 0; i < plateLines.size(); ++i)
    {
        const auto base = kPlateBaseDelays[i] * static_cast<float>(sampleRateHz) / kDattorroReferenceRate;
        resizeLine(plateLines[i], juce::jmax(16, static_cast<int>(std::round(base * kPlateSizeMax)) + 64));
        plateLines[i].modPhase = 0.0f;
        plateLines[i].lpState = 0.0f;
    }
    plateTankState = { { 0.0f, 0.0f } };
    plateModPhase = 0.0f;

    {
        const auto earlySamples = juce::jmax(64, static_cast<int>(std::round(sampleRateHz * kRoomEarlyMaxMs * 0.001 * kRoomSizeMax)) + 16);
        for (auto& line : roomEarlyLines)
        {
            resizeLine(line, earlySamples);
        }
        allocateFdn(roomLines, roomAllpassLines, kRoomScaleMax);
        roomReadCache.fill(0.0f);
    }

    for (std::size_t i = 0; i < inputDiffusionLines.size(); ++i)
    {
        const auto base = kInputDiffusionSamples[i] * static_cast<float>(sampleRateHz) / 48000.0f;
        resizeLine(inputDiffusionLines[i], juce::jmax(16, static_cast<int>(std::round(base)) + 8));
        inputDiffusionLines[i].lpState = 0.0f;
    }

    allocateFdn(hallLines, hallAllpassLines, kHallScaleMax);
    hallReadCache.fill(0.0f);

    allocateFdn(cloudLines, cloudAllpassLines, kCloudScaleMax);
    cloudReadCache.fill(0.0f);
}

void ::Reverb::updateForBlock(const ReverbSettings& settings, int numSamples)
{
    currentSettings.amount = clamp01(settings.amount);
    currentSettings.enabled = settings.enabled;
    currentSettings.algorithmIndex = juce::jlimit(0, 3, settings.algorithmIndex);
    currentSettings.size = clamp01(settings.size);
    currentSettings.decay = clamp01(settings.decay);
    currentSettings.damping = clamp01(settings.damping);
    currentSettings.preDelay = clamp01(settings.preDelay);
    currentSettings.modDepth = clamp01(settings.modDepth);
    currentSettings.modRate = clamp01(settings.modRate);
    currentSettings.width = clamp01(settings.width);
    currentSettings.cloudFeedback = clamp01(settings.cloudFeedback);
    currentSettings.cloudDiffusion = clamp01(settings.cloudDiffusion);

    blockSampleCount = juce::jmax(0, numSamples);
    blockPreEnergy = 0.0;
    blockPostEnergy = 0.0;
}

void ::Reverb::processSampleFrame(float inL, float inR, float& outL, float& outR)
{
    const auto amountTarget = currentSettings.enabled ? currentSettings.amount : 0.0f;
    const auto smoothCoeff = juce::jlimit(0.0001f, 1.0f, amountSmoothingCoeff);
    amountSmoothed += smoothCoeff * (amountTarget - amountSmoothed);
    const auto amountForSample = clamp01(amountSmoothed);

    if (amountForSample > 0.0001f)
    {
        const auto beforeL = inL;
        const auto beforeR = inR;
        processCore(inL, inR, amountForSample, currentSettings.algorithmIndex, outL, outR);
        blockPreEnergy += 0.5 * (static_cast<double>(beforeL) * static_cast<double>(beforeL)
                                 + static_cast<double>(beforeR) * static_cast<double>(beforeR));
        blockPostEnergy += 0.5 * (static_cast<double>(outL) * static_cast<double>(outL)
                                  + static_cast<double>(outR) * static_cast<double>(outR));
        return;
    }

    wetSlewState[0] += 0.04f * (0.0f - wetSlewState[0]);
    wetSlewState[1] += 0.04f * (0.0f - wetSlewState[1]);
    outL = inL;
    outR = inR;
}

void ::Reverb::applyPostBlockCompensation(juce::AudioBuffer<float>& buffer)
{
    if (amountSmoothed > 0.0001f && blockSampleCount > 0)
    {
        const auto invN = 1.0 / static_cast<double>(blockSampleCount);
        const auto preRms = static_cast<float>(std::sqrt(juce::jmax(1.0e-12, blockPreEnergy * invN)));
        const auto postRms = static_cast<float>(std::sqrt(juce::jmax(1.0e-12, blockPostEnergy * invN)));
        const auto rawComp = juce::jlimit(0.72f, 1.45f, preRms / juce::jmax(1.0e-5f, postRms));

        const auto compBlend = smoothstep(clamp01(amountSmoothed));
        const auto targetComp = 1.0f + (rawComp - 1.0f) * compBlend;
        outputCompGain += 0.03f * (targetComp - outputCompGain);
    }
    else
    {
        outputCompGain += 0.02f * (1.0f - outputCompGain);
    }

    if (std::abs(outputCompGain - 1.0f) > 0.001f)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            buffer.applyGain(channel, 0, buffer.getNumSamples(), outputCompGain);
        }
    }
}

void ::Reverb::processCore(float inL,
                                  float inR,
                                  float amount,
                                  int algorithmIndex,
                                  float& outL,
                                  float& outR)
{
    const auto mode = juce::jlimit(0, 3, algorithmIndex);
    const auto mix = smoothstep(amount);

    const auto hpCoeff = std::exp(-2.0f * juce::MathConstants<float>::pi * 28.0f
                                  / static_cast<float>(juce::jmax(1.0, sampleRateHz)));
    const auto inputHpL = inL - inputDcX1[0] + hpCoeff * inputDcY1[0];
    const auto inputHpR = inR - inputDcX1[1] + hpCoeff * inputDcY1[1];
    inputDcX1[0] = inL;
    inputDcX1[1] = inR;
    inputDcY1[0] = inputHpL;
    inputDcY1[1] = inputHpR;

    const auto predelaySamples = 1.0f + currentSettings.preDelay * static_cast<float>(sampleRateHz) * 0.30f;
    const auto inPredelayedL = processDelay(preDelayLines[0], inputHpL, predelaySamples);
    const auto inPredelayedR = processDelay(preDelayLines[1], inputHpR, predelaySamples);

    float wetL = 0.0f;
    float wetR = 0.0f;

    if (mode == 0)
    {
        // ROOM. A small space is defined far more by the timing of its first
        // arrivals than by its tail, so an explicit early-reflection pattern
        // feeds a short network. This replaces a stock Freeverb comb bank,
        // which had fixed tunings, no modulation, and - measurably - no
        // response at all to the DECAY control.
        const auto sr = static_cast<float>(sampleRateHz);
        const auto earlyScale = sr * 0.001f * lerp(0.35f, kRoomSizeMax, currentSettings.size);

        writeLine(roomEarlyLines[0], inPredelayedL);
        writeLine(roomEarlyLines[1], inPredelayedR);

        float earlyL = 0.0f;
        float earlyR = 0.0f;
        for (std::size_t i = 0; i < kRoomEarlyLeft.size(); ++i)
        {
            earlyL += kRoomEarlyLeft[i].gain * readLine(roomEarlyLines[0], kRoomEarlyLeft[i].ms * earlyScale);
            earlyR += kRoomEarlyRight[i].gain * readLine(roomEarlyLines[1], kRoomEarlyRight[i].ms * earlyScale);
        }
        earlyL *= 0.26f;
        earlyR *= 0.26f;

        FdnConfig config;
        config.sizeScale = lerp(kRoomScaleMin, kRoomScaleMax, currentSettings.size);
        config.rt60Seconds = lerp(0.18f, 3.2f, currentSettings.decay * currentSettings.decay);
        config.dampingCoeff = lerp(0.04f, 0.62f, currentSettings.damping);
        config.modHz = lerp(0.10f, 1.3f, currentSettings.modRate);
        config.modSamples = 0.3f + 3.0f * currentSettings.modDepth;
        config.allpassGain = 0.5f;
        config.inputGain = 0.40f;

        const auto diffused = processInputDiffusion(0.5f * (earlyL + earlyR), 0.62f);
        float lateL = 0.0f, lateR = 0.0f;
        processFdn8(roomLines, roomAllpassLines, roomReadCache, config, diffused, lateL, lateR);

        wetL = earlyL + lateL * 0.86f;
        wetR = earlyR + lateR * 0.86f;
    }
    else if (mode == 1)
    {
        // Dattorro plate. Four input diffusion allpasses feed a figure-of-eight
        // tank; each half is a modulated allpass, a delay, damping, a second
        // decay-diffusion allpass and another delay, and each half feeds the
        // other. Output is taken from seven points spread across both halves,
        // which is what makes it dense and stereo rather than a delay.
        const auto scale = static_cast<float>(sampleRateHz) / kDattorroReferenceRate
                           * lerp(0.55f, kPlateSizeMax, currentSettings.size);
        const auto tap = [scale](std::size_t index) { return kPlateBaseDelays[index] * scale; };

        // Bandwidth: one-pole lowpass on the input. Rolling the top off before
        // the tank is what stops a plate sounding brittle.
        const auto bandwidth = lerp(0.62f, 0.9995f, 1.0f - currentSettings.damping);
        plateLines[0].lpState += bandwidth * (0.5f * (inPredelayedL + inPredelayedR) - plateLines[0].lpState);
        auto x = plateLines[0].lpState;

        constexpr float inputDiffusion1 = 0.75f;
        constexpr float inputDiffusion2 = 0.625f;
        x = processAllpass(plateLines[0], x, tap(0), inputDiffusion1);
        x = processAllpass(plateLines[1], x, tap(1), inputDiffusion1);
        x = processAllpass(plateLines[2], x, tap(2), inputDiffusion2);
        x = processAllpass(plateLines[3], x, tap(3), inputDiffusion2);

        const auto decay = juce::jlimit(0.0f, 0.92f, lerp(0.30f, 0.92f, currentSettings.decay));
        constexpr float decayDiffusion1 = 0.70f;
        constexpr float decayDiffusion2 = 0.50f;
        const auto dampCoeff = lerp(0.0005f, 0.45f, currentSettings.damping);

        // Excursion. Modulating the two tank input allpasses spreads the
        // eigentones, which is what keeps a plate from ringing on one pitch.
        const auto modHz = lerp(0.05f, 1.6f, currentSettings.modRate);
        const auto excursion = (0.5f + 15.5f * currentSettings.modDepth)
                               * static_cast<float>(sampleRateHz) / kDattorroReferenceRate;
        plateModPhase += juce::MathConstants<float>::twoPi * modHz / static_cast<float>(juce::jmax(1.0, sampleRateHz));
        if (plateModPhase > juce::MathConstants<float>::twoPi)
        {
            plateModPhase -= juce::MathConstants<float>::twoPi;
        }
        const auto excursionA = std::sin(plateModPhase) * excursion;
        const auto excursionB = std::sin(plateModPhase * 0.995f + 1.7f) * excursion;

        auto a = x + plateTankState[1] * decay;
        a = processAllpass(plateLines[4], a, tap(4) + excursionA, -decayDiffusion1);
        a = processDelay(plateLines[5], a, tap(5));
        plateLines[5].lpState += dampCoeff * (a - plateLines[5].lpState);
        a = plateLines[5].lpState * decay;
        a = processAllpass(plateLines[6], a, tap(6), decayDiffusion2);
        plateTankState[0] = processDelay(plateLines[7], a, tap(7));

        auto b = x + plateTankState[0] * decay;
        b = processAllpass(plateLines[8], b, tap(8) + excursionB, -decayDiffusion1);
        b = processDelay(plateLines[9], b, tap(9));
        plateLines[9].lpState += dampCoeff * (b - plateLines[9].lpState);
        b = plateLines[9].lpState * decay;
        b = processAllpass(plateLines[10], b, tap(10), decayDiffusion2);
        plateTankState[1] = processDelay(plateLines[11], b, tap(11));

        for (const auto& t : kPlateTapsLeft)
        {
            wetL += t.sign * readLine(plateLines[static_cast<std::size_t>(t.line)], t.position * scale);
        }
        for (const auto& t : kPlateTapsRight)
        {
            wetR += t.sign * readLine(plateLines[static_cast<std::size_t>(t.line)], t.position * scale);
        }
        wetL *= 0.6f;
        wetR *= 0.6f;
    }
    else if (mode == 2)
    {
        // HALL. The same network at hall scale, in the style of Jot and of
        // Adriaensen's zita-rev1: diffuse the input, then circulate it through
        // eight incommensurate delays mixed by an orthogonal matrix.
        FdnConfig config;
        config.sizeScale = lerp(kHallScaleMin, kHallScaleMax, currentSettings.size);
        config.rt60Seconds = lerp(0.35f, 9.0f, currentSettings.decay * currentSettings.decay);
        config.dampingCoeff = lerp(0.02f, 0.55f, currentSettings.damping);
        config.modHz = lerp(0.08f, 1.1f, currentSettings.modRate);
        config.modSamples = 0.5f + 6.0f * currentSettings.modDepth;
        config.allpassGain = 0.55f;
        config.inputGain = 0.35f;

        const auto diffused = processInputDiffusion(0.5f * (inPredelayedL + inPredelayedR), 0.72f);
        float lateL = 0.0f, lateR = 0.0f;
        processFdn8(hallLines, hallAllpassLines, hallReadCache, config, diffused, lateL, lateR);

        // The diffuser output doubles as the early field. Without it the
        // response is silent until the first delay line comes round, which at
        // hall scale is tens of milliseconds of nothing. Same sign on both
        // channels so it survives a mono sum.
        const auto early = diffused * 0.30f;
        wetL = lateL + early;
        wetR = lateR + early;
    }
    else
    {
        // CLOUD. The same network again, scaled long and diffused hard, for
        // ambient washes. CLOUD FEEDBACK extends the decay time and CLOUD
        // DIFFUSION sets how much allpass smearing happens per circulation.
        const auto diffusionAmount = lerp(0.45f, 0.78f, currentSettings.cloudDiffusion);

        FdnConfig config;
        config.sizeScale = lerp(kCloudScaleMin, kCloudScaleMax, currentSettings.size);
        config.rt60Seconds = lerp(0.5f, 26.0f, currentSettings.decay * currentSettings.cloudFeedback);
        config.dampingCoeff = lerp(0.01f, 0.40f, currentSettings.damping);
        config.modHz = lerp(0.03f, 0.55f, currentSettings.modRate);
        config.modSamples = 3.0f + 60.0f * currentSettings.modDepth;
        config.allpassGain = diffusionAmount;
        config.inputGain = 0.30f;

        const auto diffused = processInputDiffusion(0.5f * (inPredelayedL + inPredelayedR), diffusionAmount);
        float lateL = 0.0f, lateR = 0.0f;
        processFdn8(cloudLines, cloudAllpassLines, cloudReadCache, config, diffused, lateL, lateR);

        const auto early = diffused * 0.22f;
        wetL = lateL + early;
        wetR = lateR + early;
    }

    const auto mid = 0.5f * (wetL + wetR);
    const auto side = 0.5f * (wetL - wetR);
    const auto widthAmt = lerp(0.35f, 1.35f, currentSettings.width);
    wetL = mid + side * widthAmt;
    wetR = mid - side * widthAmt;

    const auto wetHpL = wetL - wetDcX1[0] + hpCoeff * wetDcY1[0];
    const auto wetHpR = wetR - wetDcX1[1] + hpCoeff * wetDcY1[1];
    wetDcX1[0] = wetL;
    wetDcX1[1] = wetR;
    wetDcY1[0] = wetHpL;
    wetDcY1[1] = wetHpR;
    wetL = wetHpL;
    wetR = wetHpR;

    if (mix > 0.30f)
    {
        const auto wetPeak = juce::jmax(std::abs(wetL), std::abs(wetR));
        if (wetPeak > 1.35f)
        {
            const auto scale = 1.35f / wetPeak;
            wetL *= scale;
            wetR *= scale;
        }

        const auto satAmount = juce::jlimit(0.0f, 0.42f, (mix - 0.30f) * 0.70f);
        const auto satDrive = 1.0f + satAmount;
        wetL = std::tanh(wetL * satDrive);
        wetR = std::tanh(wetR * satDrive);
    }

    const auto maxWetDelta = lerp(0.45f, 0.16f, mix);
    const auto deltaL = juce::jlimit(-maxWetDelta, maxWetDelta, wetL - wetSlewState[0]);
    const auto deltaR = juce::jlimit(-maxWetDelta, maxWetDelta, wetR - wetSlewState[1]);
    wetSlewState[0] += deltaL;
    wetSlewState[1] += deltaR;
    wetL = wetSlewState[0];
    wetR = wetSlewState[1];

    const auto dryGain = 1.0f - mix * 0.88f;
    outL = sanitizeAudioSample(inL * dryGain + wetL * mix);
    outR = sanitizeAudioSample(inR * dryGain + wetR * mix);
}
