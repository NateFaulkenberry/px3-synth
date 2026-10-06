#include "UniVibe.h"

#include <cmath>
#include <complex>

namespace px3
{
namespace
{
// ---- the phase network: Shin-ei / Univox Uni-Vibe ---------------------------
//
// Sources (docs/VIBE_DSP_DESIGN.md):
//   [K]  R.G. Keen, "The Technology of the Univibe", geofex.com
//   [D]  C. Darabundit, R. Wedelich, P. Bischoff, "Digital Grey Box Model of the
//        Uni-Vibe Effects Pedal", DAFx-19 (Table 1 and eq. 9, 16-21)
//   [P]  PerkinElmer, "Photoconductive Cell Application Notes"
// Values marked UNVERIFIED are not stated by a source and were chosen here.

// Phase capacitors, in circuit order. [K], [D] Table 1. Verified.
constexpr std::array<double, 4> kPhaseCapF { { 15.0e-9, 220.0e-9, 470.0e-12, 4.7e-9 } };
// Coupling capacitor in series with each LDR. [K] (the four 1 uF caps). Verified.
constexpr double kCouplingCapF = 1.0e-6;
// Fixed resistor in series with each LDR (R6 in [D]). UNVERIFIED: 4.7 k is
// inferred from clone BOMs (three 4.7 k per stage); [D] gives no value.
constexpr double kSeriesOhms = 4.7e3;
// Non-inverting (emitter) and inverting (collector) gains, measured per stage.
// [D] Table 1 (one unit; units vary).
constexpr std::array<double, 4> kAlpha { { 1.01, 0.98, 0.97, 0.95 } };
constexpr std::array<double, 4> kBeta { { 1.11, 1.09, 1.10, 1.09 } };
// Each cell's measured resistance at full brightness and in the dark. [D]
// Table 1 minimum and maximum (the dark value is the maximum seen over a sweep).
constexpr std::array<double, 4> kCellBrightOhms { { 12.7e3, 6.86e3, 7.69e3, 6.22e3 } };
constexpr std::array<double, 4> kCellDarkOhms { { 2.79e6, 2.59e6, 3.32e6, 4.16e6 } };

// ---- the transistor stages ---------------------------------------------------
// Input: two jacks mixed by 22 k / 47 k to ground [K]: x 47/69.
constexpr float kInputPad = 47.0f / 69.0f;
// Preamp: a discrete op-amp with a 3.9 k / 1.2 k feedback divider [K]: x 4.25.
constexpr float kPreampGain = 1.0f + 3.9f / 1.2f;
// Volts at the input for a full-scale sample. UNVERIFIED: chosen as a hot
// pickup (0.5 V peak); it sets how hard the stages are driven at 0 dBFS.
constexpr float kInputVolts = 0.5f;
// Each splitter swings about 3.4 V peak before it clips on a 15 V rail [K].
constexpr float kStageHeadroomVolts = 3.4f;
// Bias of the asymmetric clip (the "u" of [D] 3.2.1, which gives no value).
// UNVERIFIED: chosen for a 2nd-harmonic-led curve at a few % THD at full scale.
constexpr float kClipBias = 0.15f;

// ---- lamp and photocells -----------------------------------------------------
// All normalised (lamp current 1 = the driver's full swing). The structure is
// from [K] and [D]; every number here is UNVERIFIED and was calibrated so the
// cell resistances land on [D] Table 1 at full intensity (bright ~10-13 k,
// dark ~2.8 M, swept over ~2.4 decades at 2 Hz) and so the sweep keeps more
// than a decade at 8 Hz - the job [K] says the LFO's speed-rising amplitude does.
constexpr float kLampIdle = 0.55f;          // "a dim orange, about halfway" [K]
constexpr float kLampSwing = 0.80f;         // full-INTENSITY swing
constexpr float kSwingSlowFactor = 0.70f;   // swing at the slowest speed...
constexpr float kSwingSpeedRise = 0.60f;    // ...rising this much by the fastest [K]
constexpr float kDiodeLimit = 1.5f;         // LFO amplitude limited by its diodes [K]
constexpr float kHeatSeconds = 0.008f;      // filament heats fast...
constexpr float kCoolSeconds = 0.022f;      // ...and cools slower [K]: "thermal time constant"
constexpr float kLightExponent = 3.0f;      // light rises steeply with filament temperature
constexpr float kLdrGamma = 0.9f;           // CdS conductance ~ light^gamma, gamma < 1 [P]
constexpr float kLdrBrightenSeconds = 0.004f;   // rise faster than decay [P], [D]
constexpr float kLdrDarkenSeconds = 0.012f;     // decay, plus...
constexpr float kLdrDarkenDimSeconds = 0.040f;  // ...slower again at low light [P]
constexpr float kLdrSaturation = 1.5f;      // conductance flattens out when very bright
constexpr float kLightFloor = 1.0e-7f;
constexpr float kIntensitySmoothSeconds = 0.030f;

constexpr float kSpeedMinHz = 0.5f;
constexpr float kSpeedMaxHz = 8.0f;

// ---- mixer ---------------------------------------------------------------
// CHORUS is the pedal's 100 k / 100 k equal sum of dry and phased [K]; VIBRATO
// the phased signal alone. The makeups only level-match the two modes against
// the input (pink noise, measured: Vibe_ModesAreLevelMatched); they do not
// change what is mixed.
constexpr float kChorusMakeup = 0.50f * 1.25f;
constexpr float kVibratoMakeup = 0.86f;
constexpr float kDcBlockHz = 5.0f;
constexpr double kFadeSeconds = 0.020;

// A C1 tanh: x(27 + x^2) / (27 + 9x^2), which reaches +-1 at +-3 with zero
// slope, so clamping beyond that leaves no corner.
constexpr float softTanh(float x) noexcept
{
    x = x > 3.0f ? 3.0f : (x < -3.0f ? -3.0f : x);
    const auto x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Its slope at a point, for normalising the biased clip to unit gain at zero.
constexpr float softTanhSlope(float t) noexcept
{
    const auto t2 = t * t;
    const auto d = 27.0f + 9.0f * t2;
    return ((27.0f + 3.0f * t2) * d - t * (27.0f + t2) * 18.0f * t) / (d * d);
}

constexpr float kClipOffset = softTanh(kClipBias);
constexpr float kClipSlope = softTanhSlope(kClipBias);

// The asymmetric clip of one transistor stage, in volts. Normalised to unit
// slope at zero, so small signals pass at the circuit's own gain.
inline float stageClip(float volts) noexcept
{
    return kStageHeadroomVolts * (softTanh(volts * (1.0f / kStageHeadroomVolts) + kClipBias) - kClipOffset)
           * (1.0f / kClipSlope);
}

inline float onePole(double seconds, double rate) noexcept
{
    return static_cast<float>(1.0 - std::exp(-1.0 / (seconds * rate)));
}

inline double effectiveCap(int stage) noexcept
{
    const auto cp = kPhaseCapF[static_cast<std::size_t>(stage)];
    return cp * kCouplingCapF / (cp + kCouplingCapF);
}
} // namespace

float UniVibe::speedToHz(float speed) noexcept
{
    return kSpeedMinHz * std::pow(kSpeedMaxHz / kSpeedMinHz, juce::jlimit(0.0f, 1.0f, speed));
}

double UniVibe::cellResistance(int stage, double conductance) noexcept
{
    const auto s = static_cast<std::size_t>(juce::jlimit(0, kStageCount - 1, stage));
    return 1.0 / (juce::jmax(0.0, conductance) / kCellBrightOhms[s] + 1.0 / kCellDarkOhms[s]);
}

double UniVibe::stageCentreHz(int stage, double ldrOhms) noexcept
{
    // [D] eq. 18: w0 = (C_DC + Cp) / (R' Cp C_DC), R' = LDR + R6.
    return 1.0 / (juce::MathConstants<double>::twoPi * (ldrOhms + kSeriesOhms) * effectiveCap(stage));
}

std::complex<double> UniVibe::stageResponse(int stage, double ldrOhms, double hz, double sampleRate) noexcept
{
    // The discrete stage exactly as it runs (bilinear, pre-warped at w0).
    const auto s = static_cast<std::size_t>(juce::jlimit(0, kStageCount - 1, stage));
    const auto cp = kPhaseCapF[s];
    const auto kc = cp / (cp + kCouplingCapF);
    const auto ke = kCouplingCapF / (cp + kCouplingCapF);
    const auto f0 = juce::jmin(stageCentreHz(stage, ldrOhms), 0.45 * sampleRate);
    const auto t = std::tan(juce::MathConstants<double>::pi * f0 / sampleRate);
    const auto b0 = kAlpha[s] * ke * t - kBeta[s] * (kc * t + 1.0);
    const auto b1 = kAlpha[s] * ke * t - kBeta[s] * (kc * t - 1.0);
    const auto a0 = t + 1.0;
    const auto a1 = t - 1.0;
    const auto z1 = std::polar(1.0, -juce::MathConstants<double>::twoPi * hz / sampleRate);
    return (b0 + b1 * z1) / (a0 + a1 * z1);
}

void UniVibe::prepare(double sampleRate)
{
    sampleRateHz = sampleRate > 0.0 ? sampleRate : 48000.0;
    const auto controlRate = sampleRateHz / static_cast<double>(kControlInterval);
    controlDt = static_cast<float>(1.0 / controlRate);
    heatCoeff = onePole(kHeatSeconds, controlRate);
    coolCoeff = onePole(kCoolSeconds, controlRate);
    ampCoeff = onePole(kIntensitySmoothSeconds, controlRate);
    dcCoeff = static_cast<float>(std::exp(-juce::MathConstants<double>::twoPi * kDcBlockHz / sampleRateHz));

    enabledGate.prepare(sampleRateHz, kFadeSeconds);
    vibratoGate.prepare(sampleRateHz, kFadeSeconds);
    invertGate.prepare(sampleRateHz, kFadeSeconds);
    levelGain.prepare(sampleRateHz, 0.015);

    enabledGate.setCurrent(current.enabled);
    vibratoGate.setCurrent(current.mode == 1);
    invertGate.setCurrent(current.stereo == 1);
    levelTarget = juce::Decibels::decibelsToGain(juce::jlimit(-12.0f, 12.0f, current.levelDb));
    levelGain.setCurrent(levelTarget);
    reset();
}

void UniVibe::settleLampAtIdle() noexcept
{
    driveAmplitude = driveAmplitudeTarget;
    filament = kLampIdle * kLampIdle;
    const auto light = std::pow(filament, kLightExponent);
    ldrLogConductance = kLdrGamma * std::log(juce::jmax(light, kLightFloor));
}

void UniVibe::reset()
{
    for (auto& c : channels) { c = Channel {}; }
    for (auto& c : secondChannels) { c = Channel {}; }
    lfoPhase = 0.0f;
    settleLampAtIdle();
    computeStageCoefficients(coeff);
    for (auto& s : coeffStep) { s = Coefficients {}; }
    controlCountdown = 0;
}

void UniVibe::updateForBlock(const UniVibeSettings& settings)
{
    current = settings;
    enabledGate.setTarget(settings.enabled);
    vibratoGate.setTarget(settings.mode == 1);
    invertGate.setTarget(settings.stereo == 1);
    levelTarget = juce::Decibels::decibelsToGain(juce::jlimit(-12.0f, 12.0f, settings.levelDb));

    const auto speed = juce::jlimit(0.0f, 1.0f, settings.speed);
    lfoHz = speedToHz(speed);
    // [K]: the oscillator's amplitude rises with its speed, a first-order
    // compensation for the lamp averaging more the faster it is driven.
    driveAmplitudeTarget = kLampSwing * juce::jlimit(0.0f, 1.0f, settings.intensity)
                           * (kSwingSlowFactor + kSwingSpeedRise * speed);
}

std::array<double, UniVibe::kStageCount> UniVibe::debugCellResistances() const noexcept
{
    std::array<double, kStageCount> r {};
    auto g = static_cast<double>(std::exp(ldrLogConductance));
    g = g / (1.0 + g / kLdrSaturation);
    for (int s = 0; s < kStageCount; ++s) { r[static_cast<std::size_t>(s)] = cellResistance(s, g); }
    return r;
}

void UniVibe::computeStageCoefficients(std::array<Coefficients, kStageCount>& out) const noexcept
{
    auto g = static_cast<double>(std::exp(ldrLogConductance));
    g = g / (1.0 + g / kLdrSaturation);
    for (int stage = 0; stage < kStageCount; ++stage)
    {
        const auto s = static_cast<std::size_t>(stage);
        const auto cp = kPhaseCapF[s];
        const auto kc = cp / (cp + kCouplingCapF);
        const auto ke = kCouplingCapF / (cp + kCouplingCapF);
        const auto f0 = juce::jmin(stageCentreHz(stage, cellResistance(stage, g)), 0.45 * sampleRateHz);
        // [D] eq. 19-21, with the bilinear transform pre-warped at w0. The
        // non-inverting leg's numerator carries tan(w0/2) as well: the paper's
        // eq. 20 drops it, but the continuous form (eq. 15) requires it.
        const auto t = std::tan(juce::MathConstants<double>::pi * f0 / sampleRateHz);
        const auto a0 = t + 1.0;
        out[s].b0 = static_cast<float>((kAlpha[s] * ke * t - kBeta[s] * (kc * t + 1.0)) / a0);
        out[s].b1 = static_cast<float>((kAlpha[s] * ke * t - kBeta[s] * (kc * t - 1.0)) / a0);
        out[s].a1 = static_cast<float>((t - 1.0) / a0);
    }
}

void UniVibe::controlTick() noexcept
{
    driveAmplitude += (driveAmplitudeTarget - driveAmplitude) * ampCoeff;

    lfoPhase += lfoHz * controlDt;
    lfoPhase -= std::floor(lfoPhase);
    // A near-sine whose peaks the oscillator's diodes round off [K].
    const auto lfo = std::tanh(kDiodeLimit * std::sin(juce::MathConstants<float>::twoPi * lfoPhase))
                     / std::tanh(kDiodeLimit);

    // Lamp driver: an idle current plus the LFO; it cannot drive negative.
    const auto lampCurrent = juce::jmax(0.0f, kLampIdle + driveAmplitude * lfo);
    const auto power = lampCurrent * lampCurrent;
    filament += (power - filament) * (power > filament ? heatCoeff : coolCoeff);

    // Light, then the photocells. Conductance moves in the log domain - in
    // octaves, which is how the stage frequencies move - brightening faster
    // than it darkens, and darkening slower still when the light is low.
    const auto light = filament * filament * filament; // kLightExponent = 3
    const auto targetLog = kLdrGamma * std::log(juce::jmax(light, kLightFloor));
    const auto dim = 1.0f - juce::jmin(light, 1.0f);
    const auto tau = targetLog > ldrLogConductance ? kLdrBrightenSeconds
                                                   : kLdrDarkenSeconds + kLdrDarkenDimSeconds * dim;
    // 1 - exp(-dt/tau) to second order: tau >= 4 ms against dt = 0.33 ms.
    const auto x = controlDt / tau;
    ldrLogConductance += (targetLog - ldrLogConductance) * (x - 0.5f * x * x + x * x * x / 6.0f);

    std::array<Coefficients, kStageCount> next {};
    computeStageCoefficients(next);
    constexpr auto inv = 1.0f / static_cast<float>(kControlInterval);
    for (std::size_t s = 0; s < coeff.size(); ++s)
    {
        coeffStep[s].b0 = (next[s].b0 - coeff[s].b0) * inv;
        coeffStep[s].b1 = (next[s].b1 - coeff[s].b1) * inv;
        coeffStep[s].a1 = (next[s].a1 - coeff[s].a1) * inv;
    }
}

float UniVibe::processChannel(Channel& c, float input, float& dryOut) noexcept
{
    // Input pad and preamp, in volts. The preamp's output is both the dry
    // signal the mixer receives and the drive to the first phase network.
    auto v = stageClip(input * (kInputVolts * kInputPad * kPreampGain));

    auto dry = v - c.dryDcX1 + dcCoeff * c.dryDcY1;
    c.dryDcX1 = v;
    c.dryDcY1 = dry;
    dryOut = dry;

    for (std::size_t s = 0; s < static_cast<std::size_t>(kStageCount); ++s)
    {
        // Stages 2-4 are driven by a Darlington splitter that clips on its own.
        if (s > 0) { v = stageClip(v); }
        const auto y = coeff[s].b0 * v + coeff[s].b1 * c.x1[s] - coeff[s].a1 * c.y1[s];
        c.x1[s] = v;
        c.y1[s] = y;
        v = y;
    }

    auto wet = v - c.wetDcX1 + dcCoeff * c.wetDcY1;
    c.wetDcX1 = v;
    c.wetDcY1 = wet;
    return wet;
}

void UniVibe::processChannelPair(Channel& a, Channel& b, float inA, float inB,
                                 float& dryA, float& dryB, float& wetA, float& wetB) noexcept
{
    // The clip's secant gain on the sum, applied to each part: the parts then
    // sum to clip(a + b) exactly. Unit slope at zero, so a silent sum is 1.
    const auto sharedClip = [](float& va, float& vb) noexcept
    {
        // One part silent: the other IS the sum, clipped directly (so a pair
        // with one silent signal is the single pedal bit for bit).
        if (vb == 0.0f) { va = stageClip(va); return; }
        if (va == 0.0f) { vb = stageClip(vb); return; }
        const auto sum = va + vb;
        const auto gain = std::abs(sum) > 1.0e-12f ? stageClip(sum) / sum : 1.0f;
        va *= gain;
        vb *= gain;
    };

    constexpr auto kDrive = kInputVolts * kInputPad * kPreampGain;
    auto va = inA * kDrive;
    auto vb = inB * kDrive;
    sharedClip(va, vb);

    const auto dcBlock = [this](float v, float& x1, float& y1) noexcept
    {
        const auto y = v - x1 + dcCoeff * y1;
        x1 = v;
        y1 = y;
        return y;
    };
    dryA = dcBlock(va, a.dryDcX1, a.dryDcY1);
    dryB = dcBlock(vb, b.dryDcX1, b.dryDcY1);

    for (std::size_t s = 0; s < static_cast<std::size_t>(kStageCount); ++s)
    {
        if (s > 0) { sharedClip(va, vb); }
        const auto ya = coeff[s].b0 * va + coeff[s].b1 * a.x1[s] - coeff[s].a1 * a.y1[s];
        const auto yb = coeff[s].b0 * vb + coeff[s].b1 * b.x1[s] - coeff[s].a1 * b.y1[s];
        a.x1[s] = va;
        a.y1[s] = ya;
        b.x1[s] = vb;
        b.y1[s] = yb;
        va = ya;
        vb = yb;
    }

    wetA = dcBlock(va, a.wetDcX1, a.wetDcY1);
    wetB = dcBlock(vb, b.wetDcX1, b.wetDcY1);
}

void UniVibe::processSampleFrame(float inL, float inR, float& outL, float& outR) noexcept
{
    const auto enabled = enabledGate.next();
    if (enabled <= 0.0f && ! current.enabled)
    {
        // Fully off: the input untouched, and the state cleared so switching
        // back on starts from a settled lamp rather than whatever was left.
        if (! idle) { reset(); idle = true; }
        outL = inL;
        outR = inR;
        return;
    }
    idle = false;

    if (--controlCountdown < 0)
    {
        controlTick();
        controlCountdown = kControlInterval - 1;
    }
    for (std::size_t s = 0; s < coeff.size(); ++s)
    {
        coeff[s].b0 += coeffStep[s].b0;
        coeff[s].b1 += coeffStep[s].b1;
        coeff[s].a1 += coeffStep[s].a1;
    }

    float dryL = 0.0f, dryR = 0.0f;
    const auto wetL = processChannel(channels[0], inL, dryL);
    // INVERTED takes the right side from the last stage's collector [K]: the
    // same phased signal, opposite polarity.
    const auto wetR = processChannel(channels[1], inR, dryR) * (1.0f - 2.0f * invertGate.next());

    const auto vibrato = vibratoGate.next();
    constexpr auto toUnits = 1.0f / (kInputVolts * kInputPad * kPreampGain);
    const auto level = levelGain.next(levelTarget) * toUnits;
    const auto mixL = ((dryL + wetL) * kChorusMakeup * (1.0f - vibrato) + wetL * kVibratoMakeup * vibrato) * level;
    const auto mixR = ((dryR + wetR) * kChorusMakeup * (1.0f - vibrato) + wetR * kVibratoMakeup * vibrato) * level;

    outL = inL + (mixL - inL) * enabled;
    outR = inR + (mixR - inR) * enabled;

    if (! std::isfinite(outL) || ! std::isfinite(outR))
    {
        reset();
        outL = inL;
        outR = inR;
    }
}

void UniVibe::processSampleFramePair(float aL, float aR, float bL, float bR,
                                     float& outAL, float& outAR, float& outBL, float& outBR) noexcept
{
    const auto enabled = enabledGate.next();
    if (enabled <= 0.0f && ! current.enabled)
    {
        if (! idle) { reset(); idle = true; }
        outAL = aL;
        outAR = aR;
        outBL = bL;
        outBR = bR;
        return;
    }
    idle = false;

    // The shared part, once: lamp, cells, coefficients, fades.
    if (--controlCountdown < 0)
    {
        controlTick();
        controlCountdown = kControlInterval - 1;
    }
    for (std::size_t s = 0; s < coeff.size(); ++s)
    {
        coeff[s].b0 += coeffStep[s].b0;
        coeff[s].b1 += coeffStep[s].b1;
        coeff[s].a1 += coeffStep[s].a1;
    }
    const auto invert = 1.0f - 2.0f * invertGate.next();
    const auto vibrato = vibratoGate.next();
    constexpr auto toUnits = 1.0f / (kInputVolts * kInputPad * kPreampGain);
    const auto level = levelGain.next(levelTarget) * toUnits;

    // The audio: both signals through the same coefficients, each clip on
    // their sum, then the pedal's output switch on each.
    float dryAL, dryBL, wetAL, wetBL, dryAR, dryBR, wetAR, wetBR;
    processChannelPair(channels[0], secondChannels[0], aL, bL, dryAL, dryBL, wetAL, wetBL);
    processChannelPair(channels[1], secondChannels[1], aR, bR, dryAR, dryBR, wetAR, wetBR);
    const auto mix = [&](float in, float dry, float wet) noexcept
    {
        const auto m = ((dry + wet) * kChorusMakeup * (1.0f - vibrato) + wet * kVibratoMakeup * vibrato) * level;
        return in + (m - in) * enabled;
    };
    outAL = mix(aL, dryAL, wetAL);
    outAR = mix(aR, dryAR, wetAR * invert);
    outBL = mix(bL, dryBL, wetBL);
    outBR = mix(bR, dryBR, wetBR * invert);

    if (! std::isfinite(outAL) || ! std::isfinite(outAR) || ! std::isfinite(outBL) || ! std::isfinite(outBR))
    {
        reset();
        outAL = aL;
        outAR = aR;
        outBL = bL;
        outBR = bR;
    }
}
} // namespace px3
