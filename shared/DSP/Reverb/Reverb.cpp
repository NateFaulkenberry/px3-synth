#include "Reverb.h"

#include "ReverbEngines.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace px3::reverb;

namespace
{
enum Smoothed : std::size_t { kDecay = 0, kSize, kDamping, kLow, kDiffusion, kModulation, kEarly, kShimmer, kDrip, kShape, kCount };

// Per-type wet level, so a type switch at the same MIX lands at a comparable
// loudness: measured as the wet RMS for steady pink-ish noise at the default
// controls (PX3Tests reverb: Reverb_TypesAreLevelMatched).
constexpr std::array<float, kTypeCount> kTypeLevel { 1.251f, 0.828f, 1.130f, 1.363f, 0.722f, 0.992f };

// Longer decays hold more energy (proportional to the decay time). A quarter
// power of it is taken back, so DECAY still sounds like "more room" without a
// long setting being 10 dB louder than a short one.
float decayLoudness(int type, float seconds) noexcept
{
    const auto reference = decaySeconds(type, 0.45f);
    return std::pow(reference / std::max(0.01f, seconds), type == gated ? 0.5f : 0.25f);
}

float sanitize(float x) noexcept
{
    return std::isfinite(x) ? std::clamp(x, -8.0f, 8.0f) : 0.0f;
}
} // namespace

struct Reverb::Engines
{
    RoomEngine room;
    PlateEngine plate;
    HallEngine hall;
    CloudEngine cloud;
    SpringEngine spring;
    GatedEngine gated;
    Controls controls;
};

Reverb::Reverb() : engines(std::make_unique<Engines>()) {}
Reverb::~Reverb() = default;

std::size_t Reverb::arenaFloatsFor(double sampleRate)
{
    const auto probe = std::make_unique<Engines>();
    return std::max({ probe->room.floatsNeeded(sampleRate), probe->plate.floatsNeeded(sampleRate),
                      probe->hall.floatsNeeded(sampleRate), probe->cloud.floatsNeeded(sampleRate),
                      probe->spring.floatsNeeded(sampleRate), probe->gated.floatsNeeded(sampleRate) })
           + 64;
}

void Reverb::prepare(double sampleRate)
{
    sampleRateHz = std::max(8000.0, sampleRate);
    const auto sr = sampleRateHz;

    const auto floats = arenaFloatsFor(sr);
    if (arena.size() != floats) { arena.assign(floats, 0.0f); }

    preDelaySize = static_cast<int>(std::ceil(kMaxPreDelayMs * 0.001 * sr)) + 8;
    preDelayBuffer.assign(static_cast<std::size_t>(preDelaySize) * 2u, 0.0f);

    amountCoefficient = static_cast<float>(1.0 - std::exp(-1.0 / (0.020 * sr)));
    widthCoefficient = static_cast<float>(1.0 - std::exp(-1.0 / (0.015 * sr)));
    wetGainCoefficient = static_cast<float>(1.0 - std::exp(-1.0 / (0.030 * sr)));
    // Control-rate one-pole, ~60 ms, at one step per kControlInterval samples.
    controlCoefficient = static_cast<float>(1.0 - std::exp(-static_cast<double>(kControlInterval) / (0.060 * sr)));
    fadeInStep = static_cast<float>(1.0 / (0.020 * sr));
    fadeOutStep = static_cast<float>(1.0 / (0.050 * sr));
    inputFadeStep = static_cast<float>(1.0 / (0.010 * sr));
    preDelayFadeStep = static_cast<float>(1.0 / (0.020 * sr));
    dcCoefficient = static_cast<float>(std::exp(-kTwoPi * 28.0 / sr));
    silentTicksToSleep = static_cast<int>(0.3 * sr / kControlInterval);

    reset();
}

void Reverb::reset()
{
    std::fill(arena.begin(), arena.end(), 0.0f);
    std::fill(preDelayBuffer.begin(), preDelayBuffer.end(), 0.0f);
    dirtyFloats = 0;
    clearCursor = 0;
    preDelayWrite = 0;
    preDelayFade = 1.0f;
    dcX = dcY = { 0.0f, 0.0f };
    amountSmoothed = 0.0f;
    widthSmoothed = target.width;
    fade = 0.0f;
    inputFade = 1.0f;
    switching = false;
    tickCounter = 0;
    silentTicks = 0;
    controlsPrimed = false;
    phase = Phase::idle;
    boundTypeIndex = -1;
    if (! arena.empty()) { bindType(clampType(target.algorithmIndex)); }
}

void Reverb::updateForBlock(const ReverbSettings& settings, int)
{
    target = settings;
    target.algorithmIndex = clampType(settings.algorithmIndex);
    const auto c01 = [](float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; };
    target.amount = c01(settings.amount);
    target.width = c01(settings.width);
    target.preDelay = c01(settings.preDelay);
    smoothedTarget[kDecay] = c01(settings.decay);
    smoothedTarget[kSize] = c01(settings.size);
    smoothedTarget[kDamping] = c01(settings.damping);
    smoothedTarget[kLow] = c01(settings.low);
    smoothedTarget[kDiffusion] = c01(settings.diffusion);
    smoothedTarget[kModulation] = c01(settings.modulation);
    smoothedTarget[kEarly] = c01(settings.early);
    smoothedTarget[kShimmer] = c01(settings.shimmer);
    smoothedTarget[kDrip] = c01(settings.drip);
    smoothedTarget[kShape] = c01(settings.shape);
    if (! controlsPrimed)
    {
        smoothed = smoothedTarget;
        widthSmoothed = target.width;
        controlsPrimed = true;
        if (boundTypeIndex >= 0) { bindType(boundTypeIndex); }
    }
}

void Reverb::bindType(int type)
{
    // Only ever called on a zeroed arena (prepare, reset, or after clearing).
    auto& e = *engines;
    Arena a { arena.data(), arena.size(), 0 };
    const auto sr = sampleRateHz;
    const auto scale = sizeScale(type, smoothed[kSize]);
    switch (type)
    {
        case room:   e.room.bind(a, sr, scale); break;
        case plate:  e.plate.bind(a, sr, scale); break;
        case hall:   e.hall.bind(a, sr, scale); break;
        case cloud:  e.cloud.bind(a, sr, scale); break;
        case spring: e.spring.bind(a, sr, scale); break;
        default:     e.gated.bind(a, sr, scale, decaySeconds(gated, smoothed[kDecay])); break;
    }
    dirtyFloats = a.used;
    boundTypeIndex = type;
    tickCounter = 0;   // run a control tick before the first sample
}

void Reverb::controlTick()
{
    for (std::size_t i = 0; i < kCount; ++i) { smoothed[i] += controlCoefficient * (smoothedTarget[i] - smoothed[i]); }

    const auto desired = target.algorithmIndex;

    // Sleep detection: silence in and silence out for 0.3 s.
    const auto inputPeak = tickInputPeak;
    if (phase == Phase::running)
    {
        silentTicks = (inputPeak < 1.0e-7f && tickWetPeak < 1.0e-6f) ? silentTicks + 1 : 0;
    }
    tickInputPeak = tickWetPeak = 0.0f;

    switch (phase)
    {
        case Phase::running:
            if (desired != boundTypeIndex) { phase = Phase::fadingOut; switching = true; }
            else if (amountSmoothed <= 0.0f || silentTicks > silentTicksToSleep)
            {
                // Nothing of the wet is audible any more: let the lines go.
                phase = Phase::clearing;
                clearCursor = 0;
            }
            break;
        case Phase::fadingOut:
            if (fade <= 0.0f) { phase = Phase::clearing; clearCursor = 0; }
            break;
        case Phase::clearing:
        {
            // ~32 KB per tick: a few microseconds, and a 1 MB arena is clean
            // in ~25 ms whatever the sample rate.
            const auto chunk = std::min<std::size_t>(8192, dirtyFloats - clearCursor);
            if (chunk > 0) { std::memset(arena.data() + clearCursor, 0, chunk * sizeof(float)); }
            clearCursor += chunk;
            if (clearCursor >= dirtyFloats)
            {
                bindType(desired);
                silentTicks = 0;
                phase = Phase::idle;
                if (switching) { inputFade = 0.0f; switching = false; }
            }
            break;
        }
        case Phase::idle:
            // The arena is still clean, so a different type can bind at once.
            if (desired != boundTypeIndex) { bindType(desired); }
            break;
    }

    if (boundTypeIndex < 0) { return; }
    auto& e = *engines;
    auto& c = e.controls;
    const auto type = boundTypeIndex;
    c.sampleRate = sampleRateHz;
    c.decaySeconds = decaySeconds(type, smoothed[kDecay]);
    c.sizeScale = sizeScale(type, smoothed[kSize]);
    c.lowMultiplier = lowMultiplier(smoothed[kLow]);
    c.damping = smoothed[kDamping];
    c.diffusion = smoothed[kDiffusion];
    c.modulation = smoothed[kModulation];
    c.early = smoothed[kEarly];
    c.shimmer = smoothed[kShimmer];
    c.drip = smoothed[kDrip];
    c.shape = smoothed[kShape];
    switch (type)
    {
        case room:   e.room.control(c); break;
        case plate:  e.plate.control(c); break;
        case hall:   e.hall.control(c); break;
        case cloud:  e.cloud.control(c); break;
        case spring: e.spring.control(c); break;
        default:     e.gated.control(c); break;
    }
    wetGainTarget = kTypeLevel[static_cast<std::size_t>(type)] * decayLoudness(type, c.decaySeconds);
}

void Reverb::processEngine(float inL, float inR, float& outL, float& outR) noexcept
{
    auto& e = *engines;
    switch (boundTypeIndex)
    {
        case room:   e.room.process(inL, inR, outL, outR); break;
        case plate:  e.plate.process(inL, inR, outL, outR); break;
        case hall:   e.hall.process(inL, inR, outL, outR); break;
        case cloud:  e.cloud.process(inL, inR, outL, outR); break;
        case spring: e.spring.process(inL, inR, outL, outR); break;
        case gated:  e.gated.process(inL, inR, outL, outR); break;
        default:     outL = outR = 0.0f; break;
    }
}

void Reverb::processSampleFrame(float inL, float inR, float& outL, float& outR)
{
    if (arena.empty()) { outL = inL; outR = inR; return; }

    const auto amountTarget = target.enabled ? target.amount : 0.0f;
    amountSmoothed += amountCoefficient * (amountTarget - amountSmoothed);
    if (amountTarget <= 0.0f && amountSmoothed < 1.0e-6f) { amountSmoothed = 0.0f; }

    if (--tickCounter <= 0)
    {
        tickCounter = kControlInterval;
        controlTick();
    }

    // Exactly the input while the reverb is silent: an untouched MIX knob must
    // not colour the FX bus by a single bit.
    if (amountSmoothed <= 0.0f && phase != Phase::running && phase != Phase::fadingOut)
    {
        outL = inL;
        outR = inR;
        return;
    }

    const auto cleanL = std::isfinite(inL) ? inL : 0.0f;
    const auto cleanR = std::isfinite(inR) ? inR : 0.0f;

    // 28 Hz DC block on what enters the reverb (not on the dry path).
    const auto hpL = cleanL - dcX[0] + dcCoefficient * dcY[0];
    const auto hpR = cleanR - dcX[1] + dcCoefficient * dcY[1];
    dcX = { cleanL, cleanR };
    dcY = { hpL, hpR };

    // PRE-DELAY. A change crossfades (equal power, 20 ms) from the old tap to
    // the new one instead of sliding the read pointer, which would pitch-bend
    // everything already in flight.
    auto* buf = preDelayBuffer.data();
    buf[2 * preDelayWrite] = hpL;
    buf[2 * preDelayWrite + 1] = hpR;
    const auto wanted = std::clamp(static_cast<int>(std::lround(preDelayMs(target.preDelay) * 0.001 * sampleRateHz)), 0, preDelaySize - 2);
    if (preDelayFade >= 1.0f && wanted != preDelayCurrent)
    {
        preDelayNext = wanted;
        preDelayFade = 0.0f;
    }
    const auto readAt = [&](int delay, int channel)
    {
        auto p = preDelayWrite - delay;
        if (p < 0) { p += preDelaySize; }
        return buf[2 * p + channel];
    };
    float pdL = readAt(preDelayCurrent, 0), pdR = readAt(preDelayCurrent, 1);
    if (preDelayFade < 1.0f)
    {
        preDelayFade = std::min(1.0f, preDelayFade + preDelayFadeStep);
        float s, c;
        sinCosSmall(preDelayFade * 1.5707963f, s, c);
        pdL = c * pdL + s * readAt(preDelayNext, 0);
        pdR = c * pdR + s * readAt(preDelayNext, 1);
        if (preDelayFade >= 1.0f) { preDelayCurrent = preDelayNext; }
    }
    if (++preDelayWrite >= preDelaySize) { preDelayWrite = 0; }
    tickInputPeak = std::max(tickInputPeak, std::max(std::abs(pdL), std::abs(pdR)));

    // Wake on signal, on this very sample. Asleep, the engine costs nothing
    // and holds nothing (its lines are zero), so it starts at full level: its
    // response builds from silence, which is its fade-in.
    if (phase == Phase::idle && amountTarget > 0.0f && boundTypeIndex == target.algorithmIndex
        && std::max(std::abs(pdL), std::abs(pdR)) >= 1.0e-7f)
    {
        phase = Phase::running;
        fade = 1.0f;
        silentTicks = 0;
    }

    float wetL = 0.0f, wetR = 0.0f;
    if (phase == Phase::running || phase == Phase::fadingOut)
    {
        if (inputFade < 1.0f && phase == Phase::running)
        {
            inputFade = std::min(1.0f, inputFade + inputFadeStep);
            const auto g = smoothstep01(inputFade);
            pdL *= g;
            pdR *= g;
        }
        processEngine(pdL, pdR, wetL, wetR);
        if (phase == Phase::running) { fade = std::min(1.0f, fade + fadeInStep); }
        else { fade = std::max(0.0f, fade - fadeOutStep); }
        const auto f = smoothstep01(fade);
        wetL *= f;
        wetR *= f;
        tickWetPeak = std::max(tickWetPeak, std::max(std::abs(wetL), std::abs(wetR)));
    }

    // WIDTH on mid/side: the mid is never touched, so the mono sum of the
    // wet does not change with WIDTH. 1 is the algorithm's own image; there is
    // no exaggeration past it.
    widthSmoothed += widthCoefficient * (target.width - widthSmoothed);
    const auto mid = 0.5f * (wetL + wetR);
    const auto side = 0.5f * (wetL - wetR) * widthSmoothed;
    wetGain += wetGainCoefficient * (wetGainTarget - wetGain);
    wetL = (mid + side) * wetGain;
    wetR = (mid - side) * wetGain;

    // Equal-power MIX: 0 is dry, 1 is fully wet (exactly: the gains are
    // recomputed only while the knob moves, and land on 0 and 1).
    if (amountSmoothed != mixFor)
    {
        if (std::abs(amountSmoothed - amountTarget) < 1.0e-4f) { amountSmoothed = amountTarget; }
        mixFor = amountSmoothed;
        const auto angle = 1.5707963267948966 * static_cast<double>(amountSmoothed);
        mixWet = amountSmoothed >= 1.0f ? 1.0f : static_cast<float>(std::sin(angle));
        mixDry = amountSmoothed >= 1.0f ? 0.0f : static_cast<float>(std::cos(angle));
    }
    const auto wetMix = mixWet;
    const auto dryMix = mixLaw == px3::FxMixLaw::additive ? 1.0f : mixDry;
    outL = sanitize(cleanL * dryMix + wetL * wetMix);
    outR = sanitize(cleanR * dryMix + wetR * wetMix);
}
