#include "FetCompressor.h"

#include <cmath>

namespace px3
{
namespace
{
float sanitize(float v)
{
    return std::isfinite(v) ? v : 0.0f;
}

float onePoleCoeff(float seconds, double sampleRate)
{
    const auto t = juce::jmax(1.0e-6f, seconds);
    return juce::jlimit(1.0e-6f,
                        1.0f,
                        static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * t))));
}
} // namespace

// The ratio buttons apply a bias to the detector diodes, so each one moves the
// threshold as well as the slope. The thresholds fall as the ratio rises, which
// is why a higher ratio on an 1176 also starts working earlier - it is one
// control in the hardware and it is one here.
//
// ALL BUTTONS is not in this table as "20:1 but more". It is a separate bias
// state: a threshold below any single button's, a hard knee, and a slope that
// is only the STARTING point - processSample raises it after the transient.
FetCompressor::RatioPoint FetCompressor::ratioPointFor(CompRatio ratio)
{
    switch (ratio)
    {
        case CompRatio::fourToOne:    return {  4.0f, -22.0f, 10.0f };
        case CompRatio::eightToOne:   return {  8.0f, -24.0f,  7.0f };
        case CompRatio::twelveToOne:  return { 12.0f, -26.0f,  5.0f };
        case CompRatio::twentyToOne:  return { 20.0f, -28.0f,  3.0f };
        case CompRatio::allButtons:   return { 12.0f, -32.0f,  1.5f };
    }
    return { 4.0f, -22.0f, 10.0f };
}

void FetCompressor::prepare(double sampleRate)
{
    sampleRateHz = juce::jmax(1000.0, sampleRate);

    // VU ballistics: about 300 ms to full deflection. A meter that follows gain
    // reduction sample-accurately looks wrong and reads worse.
    meterCoeff = onePoleCoeff(0.30f, sampleRateHz);

    // The output transformer's low-frequency corner. Below it the core starts
    // to saturate, which is audible on bass-heavy material - which this bus
    // will see.
    transformerCoeff = onePoleCoeff(1.0f / (juce::MathConstants<float>::twoPi * 30.0f), sampleRateHz);

    // The detector does not hear the deepest lows the way it hears the mids.
    // Without this a bass note pumps the whole mix.
    detectorHpCoeff = onePoleCoeff(1.0f / (juce::MathConstants<float>::twoPi * 60.0f), sampleRateHz);

    // The transformer's DC block, at 8 Hz - below anything musical and well
    // above where a float integrator would drift.
    transformerDcCoeff = 1.0f - (juce::MathConstants<float>::twoPi * 8.0f / static_cast<float>(sampleRateHz));

    // How fast the all-buttons ratio climbs after a transient. Slow enough to
    // be heard as a separate event from the initial grab.
    creepCoeff = onePoleCoeff(0.120f, sampleRateHz);

    reset();
}

void FetCompressor::reset()
{
    envelopeDb = { { 0.0f, 0.0f } };
    slowEnvelopeDb = { { 0.0f, 0.0f } };
    feedbackGain = { { 1.0f, 1.0f } };
    transformerState = { { 0.0f, 0.0f } };
    transformerDcX1 = { { 0.0f, 0.0f } };
    transformerDcY1 = { { 0.0f, 0.0f } };
    detectorHpState = { { 0.0f, 0.0f } };
    fetPrevInput = { { 0.0f, 0.0f } };
    fetPrevIntegral = { { 0.0f, 0.0f } };
    fetPrevDrive = { { 1.0f, 1.0f } };
    fetPrevBias = { { 0.0f, 0.0f } };
    ratioCreep = 0.0f;
    meterSmoothed = 0.0f;
    inputMeterSmoothed = -60.0f;
    outputMeterSmoothed = -60.0f;
    meterDb.store(0.0f, std::memory_order_relaxed);
    inputMeterDb.store(-60.0f, std::memory_order_relaxed);
    outputMeterDb.store(-60.0f, std::memory_order_relaxed);
}

void FetCompressor::setSettings(const CompressorSettings& newSettings)
{
    settings = newSettings;
    settings.inputDb = juce::jlimit(-12.0f, 36.0f, settings.inputDb);
    settings.outputDb = juce::jlimit(-24.0f, 24.0f, settings.outputDb);
    settings.attack = juce::jlimit(0.0f, 1.0f, settings.attack);
    settings.release = juce::jlimit(0.0f, 1.0f, settings.release);
    settings.mix = juce::jlimit(0.0f, 1.0f, settings.mix);

    // Both panel controls are reversed on the hardware - fully clockwise is
    // fastest - and the parameter is stored that way, so 1 is fast here.
    const auto attackSeconds = juce::jmap(1.0f - settings.attack,
                                          kAttackFastUs, kAttackSlowUs) * 1.0e-6f;
    const auto releaseSeconds = juce::jmap(1.0f - settings.release,
                                           kReleaseFastMs, kReleaseSlowMs) * 1.0e-3f;

    attackCoeff = onePoleCoeff(attackSeconds, sampleRateHz);
    releaseCoeff = onePoleCoeff(releaseSeconds, sampleRateHz);
    // The second, slower release. Program dependency comes from the two running
    // together rather than from a rule about how loud the signal is.
    slowReleaseCoeff = onePoleCoeff(releaseSeconds * 4.0f, sampleRateHz);
}

// The rectifier's diodes do not switch at exactly zero. Their forward knee
// softens detection near the threshold, which IS the compressor's knee - it is
// not a curve chosen for taste, it is where the hardware's knee comes from.
// Above the knee the response is linear in dB.
float FetCompressor::detectorKnee(float overDb, float knee) const
{
    if (knee <= 0.001f)
    {
        return juce::jmax(0.0f, overDb);
    }

    if (overDb <= -knee * 0.5f)
    {
        return 0.0f;
    }
    if (overDb >= knee * 0.5f)
    {
        return overDb;
    }

    const auto t = overDb + knee * 0.5f;
    return t * t / (2.0f * knee);
}

// The FET is a voltage-controlled resistor and its control law is not linear.
// The circuit linearises it by feeding a fraction of the drain signal back to
// the gate; what that correction leaves behind is a large part of the audible
// signature, and it grows as the device works harder - so the distortion is
// program dependent for free rather than being a fixed "analog" flavour.
//
// Asymmetry is a GATE BIAS rather than a squared term. Both put second harmonic
// in, but a bias is what the device actually has, and - decisively - it keeps
// the curve integrable in closed form, which is what the antialiasing below
// needs. The bias's own DC offset is subtracted so the stage still passes zero
// to zero.
float FetCompressor::fetCurve(float x, float drive, float bias)
{
    return (std::tanh(drive * (x + bias)) - std::tanh(drive * bias)) / drive;
}

// The antiderivative of the curve above. log(cosh(u)) overflows for large u if
// written literally, so it is evaluated as |u| + log1p(e^-2|u|) - log 2, which
// is exact for small u and asymptotically |u| - log 2 for large.
float FetCompressor::fetIntegral(float x, float drive, float bias)
{
    const auto u = drive * (x + bias);
    const auto a = std::abs(u);
    const auto logCosh = a + std::log1p(std::exp(-2.0f * a)) - 0.6931472f;
    return logCosh / (drive * drive) - std::tanh(drive * bias) * x / drive;
}

// The gain element, antialiased.
//
// Measured: a slammed 11 kHz tone folded images back into the band at -15.2 dB
// relative to the fundamental. Linearising this stage and the transformer sent
// that to -77.6 dB, which located all of it here rather than in the detector
// loop, so this is the stage that needed treating.
//
// The treatment is first-order antiderivative antialiasing rather than 2x
// oversampling. Oversampling was measured as the alternative and rejected on
// LATENCY, not on cost: a halfband pair adds around 15 samples of group delay,
// and this insert is per-bus - so enabling it on the dry bus alone would slide
// the dry path against the FX return and comb the two together at the master
// sum. ADAA has none: no filters, no delay, nothing to align.
//
// It works by integrating the curve across the segment between this sample and
// the last one instead of point-sampling it, which is exactly the average the
// band-limited version would have taken. When two consecutive inputs are too
// close together the difference quotient loses its precision, so the midpoint
// value is used instead - that is the standard fallback, not a fudge.
float FetCompressor::fetGainElement(int channel, float x, float reductionDb)
{
    const auto c = static_cast<std::size_t>(channel);
    const auto work = juce::jlimit(0.0f, 1.0f, reductionDb / 20.0f);
    const auto drive = 1.0f + 0.55f * work;
    // Matched to the previous squared term at typical level, so the second
    // harmonic content the stage was tuned for is preserved.
    const auto bias = 0.03f * work;

    const auto integral = fetIntegral(x, drive, bias);
    const auto prev = fetPrevInput[c];
    const auto delta = x - prev;

    float y;
    if (std::abs(delta) < 1.0e-5f)
    {
        y = fetCurve(0.5f * (x + prev), drive, bias);
    }
    else
    {
        // The stored integral belongs to the drive and bias in force last
        // sample. Those move with the envelope, so it has to be re-evaluated
        // under the current pair or the quotient mixes two different curves.
        const auto prevIntegral = (drive == fetPrevDrive[c] && bias == fetPrevBias[c])
                                      ? fetPrevIntegral[c]
                                      : fetIntegral(prev, drive, bias);
        y = (integral - prevIntegral) / delta;
    }

    fetPrevInput[c] = x;
    fetPrevIntegral[c] = integral;
    fetPrevDrive[c] = drive;
    fetPrevBias[c] = bias;
    return y;
}

// A transformer cannot pass DC and saturates at low frequencies first, because
// flux is the integral of voltage. Modelled as a soft limit applied to the
// low-frequency content only, so the top end stays open.
float FetCompressor::outputTransformer(float x, float& state, float coeff, float& dcX1, float& dcY1, float dcCoeff)
{
    state += (x - state) * coeff;
    const auto lows = state;
    const auto rest = x - lows;
    const auto saturatedLows = std::tanh(lows * 1.6f) / 1.6f;
    const auto shaped = saturatedLows + rest;

    // A transformer cannot pass DC - flux is the integral of voltage, so a
    // constant would saturate the core. That is not decoration: the FET stage
    // above is deliberately ASYMMETRIC, which is where its second harmonic
    // comes from, and an asymmetric shaper generates DC by construction.
    // Without this the output drifted to a measured mean of 0.0054.
    const auto y = shaped - dcX1 + dcCoeff * dcY1;
    dcX1 = shaped;
    dcY1 = y;
    return y;
}

void FetCompressor::processSample(float& left, float& right)
{
    if (! settings.enabled)
    {
        return;
    }

    const auto dryL = left;
    const auto dryR = right;

    const auto inputGain = juce::Decibels::decibelsToGain(settings.inputDb);
    const auto outputGain = juce::Decibels::decibelsToGain(settings.outputDb);

    const auto point = ratioPointFor(settings.ratio);
    const auto allButtons = settings.ratio == CompRatio::allButtons;

    auto inL = dryL * inputGain;
    auto inR = dryR * inputGain;

    // ---- detector -------------------------------------------------------
    // FEEDBACK: what the detector sees is the signal AFTER last sample's gain
    // reduction, not the input. This is the whole topology - it is what makes
    // the threshold program dependent and the effective ratio softer than the
    // nominal one until the loop settles.
    auto detect = [&](int channel, float x)
    {
        const auto c = static_cast<std::size_t>(channel);
        const auto afterGain = x * feedbackGain[c];

        // Full-wave rectification, as CR2/CR3 do. A peak detector, not RMS: an
        // RMS detector would ignore exactly the transients the 20 us attack
        // exists to catch.
        auto rectified = std::abs(afterGain);

        // The detector is deliberately deaf to the deepest lows.
        detectorHpState[c] += (rectified - detectorHpState[c]) * detectorHpCoeff;
        rectified = juce::jmax(0.0f, rectified - detectorHpState[c] * 0.6f);

        return rectified;
    };

    auto rectL = detect(0, inL);
    auto rectR = detect(1, inR);

    if (settings.stereoLink)
    {
        // Unlinked, a loud transient on one side pulls only that side down and
        // the image shifts. The louder side drives both.
        const auto both = juce::jmax(rectL, rectR);
        rectL = both;
        rectR = both;
    }

    auto reductionForChannel = [&](int channel, float rectified)
    {
        const auto c = static_cast<std::size_t>(channel);
        const auto levelDb = juce::Decibels::gainToDecibels(juce::jmax(1.0e-6f, rectified), -120.0f);
        // All-buttons drops the bias further the longer the detector works, so
        // a sustained passage is squeezed harder than the transient that
        // started it.
        const auto bias = point.thresholdDb - (allButtons ? ratioCreep * 6.0f : 0.0f);
        const auto overDb = levelDb - bias;
        const auto over = detectorKnee(overDb, point.knee);

        // Attack when the envelope has to rise, release when it falls. Two
        // release constants running together give the program dependency.
        auto& fast = envelopeDb[c];
        auto& slow = slowEnvelopeDb[c];

        if (over > fast)
        {
            fast += (over - fast) * attackCoeff;
        }
        else
        {
            fast += (over - fast) * releaseCoeff;
        }

        if (over > slow)
        {
            slow += (over - slow) * attackCoeff;
        }
        else
        {
            slow += (over - slow) * slowReleaseCoeff;
        }

        // The slower envelope only ever holds the release back, never the
        // attack: the grab stays fast and the recovery is what becomes program
        // dependent.
        const auto envelope = juce::jmax(fast, slow * 0.7f);

        auto slope = point.slope;
        if (allButtons)
        {
            // The documented behaviour: the selected ratio ON the transient,
            // then the ratio RISES afterwards.
            //
            // The creep moves the BIAS as well as the slope, because that is
            // what the parallel bias networks do - and because in a feedback
            // loop a slope change alone barely registers: the loop absorbs it.
            // Measured, slope-only creep produced 0.61 dB of extra settling
            // against a fixed 20:1's 0.62, which is nothing.
            const auto want = juce::jlimit(0.0f, 1.0f, envelope / 10.0f);
            ratioCreep += (want - ratioCreep) * creepCoeff;
            slope = juce::jmap(ratioCreep, 12.0f, 20.0f);
        }

        // The gain computer, in dB. A feedback loop means this is applied to a
        // signal the detector has already seen reduced, so the loop converges
        // to a softer effective ratio than this slope alone implies.
        return envelope * (1.0f - 1.0f / slope);
    };

    const auto reduceL = reductionForChannel(0, rectL);
    const auto reduceR = reductionForChannel(1, rectR);

    const auto gainL = juce::Decibels::decibelsToGain(-reduceL);
    const auto gainR = juce::Decibels::decibelsToGain(-reduceR);

    // Closing the loop for the next sample.
    feedbackGain[0] = gainL;
    feedbackGain[1] = gainR;

    // ---- gain element ---------------------------------------------------
    auto outL = fetGainElement(0, inL * gainL, reduceL);
    auto outR = fetGainElement(1, inR * gainR, reduceR);

    outL = outputTransformer(outL, transformerState[0], transformerCoeff,
                             transformerDcX1[0], transformerDcY1[0], transformerDcCoeff);
    outR = outputTransformer(outR, transformerState[1], transformerCoeff,
                             transformerDcX1[1], transformerDcY1[1], transformerDcCoeff);

    outL *= outputGain;
    outR *= outputGain;

    // ---- parallel blend --------------------------------------------------
    // 0 is the untouched bus, 1 is fully compressed. The dry side is the signal
    // as it arrived, before input gain, so the blend is between "this bus" and
    // "this bus compressed" rather than between two different levels.
    const auto wet = settings.mix;
    const auto dry = 1.0f - wet;
    left = sanitize(dryL * dry + outL * wet);
    right = sanitize(dryR * dry + outR * wet);

    // ---- meter -----------------------------------------------------------
    const auto reduction = juce::jmax(reduceL, reduceR);
    meterSmoothed += (reduction - meterSmoothed) * meterCoeff;
    meterDb.store(sanitize(meterSmoothed), std::memory_order_relaxed);

    // Level either side, on the same ballistics. Measured on the louder
    // channel: a stereo pair driven from one meter reads the programme, and
    // averaging would hide a one-sided transient.
    const auto inLevel = juce::Decibels::gainToDecibels(
        juce::jmax(std::abs(inL), std::abs(inR)), -60.0f);
    const auto outLevel = juce::Decibels::gainToDecibels(
        juce::jmax(std::abs(left), std::abs(right)), -60.0f);

    inputMeterSmoothed += (inLevel - inputMeterSmoothed) * meterCoeff;
    outputMeterSmoothed += (outLevel - outputMeterSmoothed) * meterCoeff;
    inputMeterDb.store(sanitize(inputMeterSmoothed), std::memory_order_relaxed);
    outputMeterDb.store(sanitize(outputMeterSmoothed), std::memory_order_relaxed);
}

} // namespace px3
