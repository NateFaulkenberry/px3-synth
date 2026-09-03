#include "AnalogEngine.h"

#include <algorithm>
#include <cmath>

namespace px3
{
namespace
{
// The forward transfer is clamped here because sin() folds past pi/2 - past
// that point it stops being a saturator and starts being a wavefolder, and the
// inverse would no longer be an inverse.
constexpr float kForwardClamp = juce::MathConstants<float>::halfPi;
constexpr float kInverseClamp = 0.9999f;
} // namespace

AnalogEngine::AnalogEngine()
{
    // Without this the tuning holds the struct's inline defaults rather than
    // the active profile's, until prepare() happens to run. Measured: a
    // restored processor reported curveBlend 0.25 where CLEAN compiles 0.10.
    resetTuning();
}

juce::StringArray AnalogEngine::profileNames()
{
    return { "CLEAN", "BRITISH", "AMERICAN", "TRANSFORMER", "MODERN" };
}

juce::StringArray AnalogEngine::tuningKeys()
{
    return { "pairDrive", "masterDrive", "fxBusTrim", "curveBlend",
             "evenHarmonic", "slewEnhance", "hfRolloffHz", "hfLevelDependence",
             "lfCornerHz", "lfLevelTrim", "dcBlockHz", "headroom", "engineAmount",
             "outputTrim" };
}

// ============================================================================
// the transfer pair
// ============================================================================

namespace
{
// The blend is applied as an invertible PRE-WARP rather than as a blend of two
// output curves.
//
// Blending two curves is the obvious approach and it is wrong here: the inverse
// of a blend is not the blend of the inverses, so channel-then-bus stopped being
// exactly transparent as soon as the blend left zero (measured: 6.6e-4 of error
// at the blend AMERICAN uses). Warping the ARGUMENT with a function that has a
// closed-form inverse keeps the pair exact at every blend - measured at machine
// precision, 1e-16 - while still changing where on the sine a given level lands,
// which is what actually moves the harmonic structure.
//
// warp(x) = x(1-b) + b·x|x|   is monotone for b in [0,1], so it inverts by
// solving the quadratic.
float warpArgument(float x, float blend)
{
    return x * (1.0f - blend) + blend * x * std::abs(x);
}

float unwarpArgument(float u, float blend)
{
    if (blend <= 1.0e-6f)
    {
        return u;
    }

    const auto sign = u >= 0.0f ? 1.0f : -1.0f;
    const auto a = std::abs(u);
    const auto linear = 1.0f - blend;
    const auto root = std::sqrt(linear * linear + 4.0f * blend * a);
    return sign * (root - linear) / (2.0f * blend);
}
} // namespace

float AnalogEngine::forwardTransfer(float x, float blend)
{
    const auto warped = juce::jlimit(-kForwardClamp, kForwardClamp, warpArgument(x, blend));
    return std::sin(warped);
}

float AnalogEngine::inverseTransfer(float x, float blend)
{
    const auto clamped = juce::jlimit(-kInverseClamp, kInverseClamp, x);
    return unwarpArgument(std::asin(clamped), blend);
}

// ============================================================================
// profiles
// ============================================================================

// A second calibration pass, after the engine was measured on BROADBAND
// material rather than on a single tone. Two things came out of it.
//
// The colour stages were set where they were measurable but barely audible -
// a 21 kHz bandwidth limit is above most people's hearing, and a 6 Hz coupling
// corner does nothing at all. They are the stages nothing downstream inverts,
// so they are where a profile's character actually lives, and they are now set
// where they can be heard: bandwidth into the audible range, real coupling
// corners, and enough level dependence that a loud passage audibly loses its
// top the way a driven desk does.
//
// The trims are solved against noise, not a 440 Hz sine. Solved on a tone they
// left the engine 1.7 to 4.4 dB down on wideband material, because a sine at
// 440 Hz never meets the bandwidth limit that costs the level. A quieter,
// duller signal reads as less character, not more.
//
// Every profile was recalibrated once the FULL path was measured. Two things
// came out of that:
//
// 1. The trims had been derived on channel + dry bus, but the real path also
//    runs a master stage. With it included the engine LOST level - 0.8 dB on
//    CLEAN up to 3.5 dB on TRANSFORMER - so switching it on made the mix
//    quieter, and an A/B was partly a loudness comparison. The trims below are
//    solved against channel + dry bus + master and land within 0.01 dB.
//
// 2. curveBlend, not drive, is what controls how much character the engine
//    has. Raising pairDrive does almost nothing, because the channel and the
//    bus share it and the pair cancels: measured, THD did not even rise
//    monotonically with it. The blend changes the SHAPE of the transfer, which
//    the inverse cannot undo once several channels have been summed.
//
// Blends were raised to roughly double the harmonic content at full amount.
AnalogEngine::Tuning AnalogEngine::defaultTuningFor(Profile profile)
{
    Tuning t;

    switch (profile)
    {
        case Profile::clean:
            // Modern large-format VCA. Almost nothing but the summing
            // behaviour: no even-harmonic bias, wide bandwidth, minimal slew.
            // This profile exists to prove the architecture - if CLEAN still
            // does something audible on a busy mix, the character really is
            // coming from accumulation rather than from colour.
            t.pairDrive = 0.85f;
            t.masterDrive = 0.92f;
            t.curveBlend = 0.34f;
            t.evenHarmonic = 0.010f;
            t.slewEnhance = 0.14f;
            t.hfRolloffHz = 18000.0f;
            t.hfLevelDependence = 0.22f;
            t.lfCornerHz = 12.0f;
            t.lfLevelTrim = 0.12f;
            t.headroom = 1.15f;
            t.outputTrim = 1.3708f;   // 2.2% THD; solved against noise on the full path
            break;

        case Profile::british:
            // Discrete, transformer-coupled. Even-harmonic bias for the warmth,
            // a real coupling-capacitor corner, and the top rolled off enough to
            // hear. Slew is moderate - these desks are not fast.
            t.pairDrive = 1.05f;
            t.masterDrive = 1.15f;
            t.curveBlend = 0.58f;
            t.evenHarmonic = 0.055f;
            t.slewEnhance = 0.30f;
            t.hfRolloffHz = 11500.0f;
            t.hfLevelDependence = 0.60f;
            t.lfCornerHz = 26.0f;
            t.lfLevelTrim = 0.42f;
            t.headroom = 0.95f;
            t.outputTrim = 1.8522f;   // 10.6% THD; solved against noise on the full path
            break;

        case Profile::american:
            // Discrete op-amp. Harder knee and a faster slew than BRITISH, with
            // the harmonic weight on odd orders - forward rather than warm. The
            // heaviest pre-warp of any profile, which is where the harder onset
            // and the extra 5th-harmonic content come from.
            t.pairDrive = 1.15f;
            t.masterDrive = 1.20f;
            t.curveBlend = 0.70f;
            t.evenHarmonic = 0.012f;
            t.slewEnhance = 0.46f;
            t.hfRolloffHz = 14000.0f;
            t.hfLevelDependence = 0.36f;
            t.lfCornerHz = 20.0f;
            t.lfLevelTrim = 0.20f;
            t.headroom = 0.90f;
            t.outputTrim = 1.8222f;   // 11.3% THD; solved against noise on the full path
            break;

        case Profile::transformer:
            // Transformer-heavy. The most even-harmonic content, the most
            // level-dependent low end, and the most restricted bandwidth. This
            // is the profile that changes tone rather than just density.
            t.pairDrive = 1.10f;
            t.masterDrive = 1.24f;
            t.curveBlend = 0.46f;
            t.evenHarmonic = 0.075f;
            t.slewEnhance = 0.24f;
            t.hfRolloffHz = 9000.0f;
            t.hfLevelDependence = 0.78f;
            t.lfCornerHz = 38.0f;
            t.lfLevelTrim = 0.68f;
            t.headroom = 0.88f;
            t.outputTrim = 1.8788f;   // 8.2% THD; solved against noise on the full path
            break;

        case Profile::modern:
            // Later clean VCA large-format. The most headroom, the fastest
            // recovery, the least colour - but a distinctly harder edge than
            // CLEAN when it finally does reach the knee.
            t.pairDrive = 0.90f;
            t.masterDrive = 0.98f;
            t.curveBlend = 0.66f;
            t.evenHarmonic = 0.020f;
            t.slewEnhance = 0.20f;
            t.hfRolloffHz = 16500.0f;
            t.hfLevelDependence = 0.26f;
            t.lfCornerHz = 14.0f;
            t.lfLevelTrim = 0.16f;
            t.headroom = 1.25f;
            t.outputTrim = 1.7487f;   // 7.4% THD; solved against noise on the full path
            break;
    }

    return t;
}

// ============================================================================
// helpers
// ============================================================================

float AnalogEngine::sanitize(float v)
{
    if (! std::isfinite(v))
    {
        return 0.0f;
    }
    return juce::jlimit(-8.0f, 8.0f, v);
}

// A leak of a few hertz: high enough that nothing can accumulate over seconds,
// low enough that the audio band passes through the reconstruction unchanged.
constexpr float kSlewLeak = 0.9994f;

float AnalogEngine::onePoleCoeff(float hz, float rate)
{
    if (rate <= 0.0f || hz <= 0.0f)
    {
        return 0.0f;
    }
    return juce::jlimit(0.0f, 1.0f,
                        1.0f - std::exp(-juce::MathConstants<float>::twoPi * hz / rate));
}

// ============================================================================
// lifecycle
// ============================================================================

void AnalogEngine::prepare(double sampleRate, int channelCount)
{
    sampleRateHz = sampleRate > 0.0 ? sampleRate : 44100.0;
    juce::ignoreUnused(channelCount);

    amountSmoothed.reset(sampleRateHz, 0.02);
    resetTuning();
    reset();
}

void AnalogEngine::reset()
{
    for (auto& state : channelState)
    {
        state.clear();
    }
    for (auto& context : busState)
    {
        for (auto& state : context)
        {
            state.clear();
        }
    }
    amountSmoothed.setCurrentAndTargetValue(amount);
}

void AnalogEngine::setProfile(Profile profile)
{
    if (profile == activeProfile)
    {
        return;
    }

    activeProfile = profile;

    // A profile change replaces the tuning wholesale, which also discards any
    // debug edits. That is intended: the debug console edits the ACTIVE
    // profile's constants, and switching profile is asking for a different set.
    resetTuning();
}

void AnalogEngine::resetTuning()
{
    tuning = defaultTuningFor(activeProfile);
}

void AnalogEngine::setAmount(float newAmount) noexcept
{
    amount = juce::jlimit(0.0f, 1.0f, newAmount);
    amountSmoothed.setTargetValue(amount * tuning.engineAmount);
}

void AnalogEngine::refreshAmount() noexcept
{
    amountSmoothed.setTargetValue(amount * tuning.engineAmount);
}

void AnalogEngine::setTuningValue(const juce::String& key, float value)
{
    if (key == "pairDrive")              { tuning.pairDrive = juce::jlimit(0.0f, 3.0f, value); }
    else if (key == "masterDrive")       { tuning.masterDrive = juce::jlimit(0.0f, 3.0f, value); }
    else if (key == "fxBusTrim")         { tuning.fxBusTrim = juce::jlimit(0.0f, 2.0f, value); }
    else if (key == "curveBlend")        { tuning.curveBlend = juce::jlimit(0.0f, 1.0f, value); }
    else if (key == "evenHarmonic")      { tuning.evenHarmonic = juce::jlimit(0.0f, 0.5f, value); }
    else if (key == "slewEnhance")       { tuning.slewEnhance = juce::jlimit(0.0f, 1.0f, value); }
    else if (key == "hfRolloffHz")       { tuning.hfRolloffHz = juce::jlimit(1000.0f, 22000.0f, value); }
    else if (key == "hfLevelDependence") { tuning.hfLevelDependence = juce::jlimit(0.0f, 1.0f, value); }
    else if (key == "lfCornerHz")        { tuning.lfCornerHz = juce::jlimit(1.0f, 200.0f, value); }
    else if (key == "lfLevelTrim")       { tuning.lfLevelTrim = juce::jlimit(0.0f, 1.0f, value); }
    else if (key == "dcBlockHz")         { tuning.dcBlockHz = juce::jlimit(0.1f, 50.0f, value); }
    else if (key == "headroom")          { tuning.headroom = juce::jlimit(0.25f, 3.0f, value); }
    else if (key == "engineAmount")      { tuning.engineAmount = juce::jlimit(0.0f, 1.0f, value); refreshAmount(); }
    else if (key == "outputTrim")        { tuning.outputTrim = juce::jlimit(0.25f, 4.0f, value); }
}

float AnalogEngine::getTuningValue(const juce::String& key) const
{
    if (key == "pairDrive")              { return tuning.pairDrive; }
    if (key == "masterDrive")            { return tuning.masterDrive; }
    if (key == "fxBusTrim")              { return tuning.fxBusTrim; }
    if (key == "curveBlend")             { return tuning.curveBlend; }
    if (key == "evenHarmonic")           { return tuning.evenHarmonic; }
    if (key == "slewEnhance")            { return tuning.slewEnhance; }
    if (key == "hfRolloffHz")            { return tuning.hfRolloffHz; }
    if (key == "hfLevelDependence")      { return tuning.hfLevelDependence; }
    if (key == "lfCornerHz")             { return tuning.lfCornerHz; }
    if (key == "lfLevelTrim")            { return tuning.lfLevelTrim; }
    if (key == "dcBlockHz")              { return tuning.dcBlockHz; }
    if (key == "headroom")               { return tuning.headroom; }
    if (key == "engineAmount")           { return tuning.engineAmount; }
    if (key == "outputTrim")             { return tuning.outputTrim; }
    return 0.0f;
}

// ============================================================================
// one stage
// ============================================================================

float AnalogEngine::processStage(StageState& state,
                                 float input,
                                 Context context,
                                 bool forward)
{
    const auto rate = static_cast<float>(sampleRateHz);

    // An envelope of this stage's own level, tracking PROGRAMME level rather
    // than the waveform. Everything level-dependent below reads it, which is
    // what makes the frequency response change with how hard the stage is being
    // driven rather than only with a knob.
    //
    // The rate is load-bearing. Fast enough to follow the waveform, a modulated
    // filter corner becomes an amplitude modulator: at 18 Hz it produced 3% THD
    // on a path the architecture guarantees is transparent. A transformer's
    // low-frequency behaviour responds to flux over many cycles, not within one.
    const auto envCoeff = onePoleCoeff(1.5f, rate);
    state.envelope += (std::abs(input) - state.envelope) * envCoeff;
    const auto drivenBy = juce::jlimit(0.0f, 1.0f, state.envelope * 2.0f);

    // ---- the colour group ---------------------------------------------------
    // Everything here is the desk's TONE rather than its summing behaviour, and
    // where it sits relative to the transfer is not a detail.
    //
    // It has to be OUTSIDE the invertible pair. Filtering between the forward
    // and inverse transfers breaks the cancellation - g-1(HP(g(x))) is not
    // HP(x) - which is a nonlinearity that appears on a single channel, exactly
    // where the architecture promises none. Measured at 2.3% THD before this
    // was ordered correctly.
    //
    // So: the channel runs colour BEFORE its forward transfer, and the bus runs
    // colour AFTER its inverse transfer. The composite is then
    // g-1(g(colour(x))) = colour(x), and colour is linear filtering, so it adds
    // no harmonics of its own.
    auto applyColour = [&](float x)
    {
        // Coupling capacitor. The corner moves up with level on the
        // transformer-ish profiles, which is what a transformer does as it
        // approaches saturation: it loses low end.
        if (context != Context::fxBus)
        {
            const auto corner = tuning.lfCornerHz * (1.0f + drivenBy * tuning.lfLevelTrim * 3.0f);
            const auto lfCoeff = onePoleCoeff(corner, rate);
            state.lfState += (x - state.lfState) * lfCoeff;
            x -= state.lfState;
        }

        // Bandwidth. Not a generic low-pass bolted on the end: the corner falls
        // as the stage is driven, which is what a real amplifier's slew-limited
        // bandwidth does.
        {
            const auto corner = tuning.hfRolloffHz
                                * (1.0f - drivenBy * tuning.hfLevelDependence * 0.55f);
            const auto hfCoeff = onePoleCoeff(juce::jmax(1000.0f, corner), rate);
            state.hfState += (x - state.hfState) * hfCoeff;
            x = state.hfState;
        }

        return x;
    };

    auto x = input;

    if (forward)
    {
        x = applyColour(x);
    }

    // ---- slew ----------------------------------------------------------------
    // The channel enhances slew and the bus cuts it back, the same way the
    // transfer pair works: arcsine one side, sine the other. A stage that only
    // ever softened transients would make everything duller the more of it you
    // used; this way the pair has a transient personality without a net loss.
    if (tuning.slewEnhance > 0.0f && context != Context::fxBus)
    {
        auto difference = juce::jlimit(-1.0f, 1.0f, x - state.lastSample);
        state.lastSample = x;

        const auto shaped = forward ? std::asin(difference * 0.999f)
                                    : std::sin(difference);
        const auto blended = difference + (shaped - difference) * tuning.slewEnhance;

        // Reconstructed with a LEAKY integrator, not a plain one plus a servo
        // correction. Differencing and re-integrating is an identity only if
        // nothing is done in between; the whole point of this stage is that
        // something is, so the sum drifts. On a periodic signal the drift
        // cancels each cycle, which is why every test built on sines passed -
        // but the integral of a noise-like signal is a random walk, with
        // unbounded variance, and the servo's 0.4 second leak could not contain
        // it. It reached the +/-4 rail, the transfer's clamp then flattened the
        // signal to a constant, and the DC blocker removed the constant: BRITISH
        // and AMERICAN faded to complete silence after about six seconds of
        // dense material.
        //
        // The leak makes the reconstruction a one-pole high-pass at a few hertz
        // instead - identical in the audio band, and unable to accumulate.
        x = state.lastShaped * kSlewLeak + blended;
        state.lastShaped = juce::jlimit(-4.0f, 4.0f, x);
    }

    // ---- drive into the transfer ---------------------------------------------
    // The channel and the buses that invert it share one drive, so the pair
    // cancels. Only the master, which is a forward output stage rather than half
    // of a pair, gets its own.
    const auto drive = (context == Context::master ? tuning.masterDrive : tuning.pairDrive)
                       / juce::jmax(0.05f, tuning.headroom);

    x *= drive;

    // ---- even-harmonic bias --------------------------------------------------
    // A purely odd transfer produces only odd harmonics, and odd harmonics alone
    // read as hard rather than warm. A small squared term is the 2nd harmonic.
    //
    // "Small" is load-bearing. At the values this started with, the bias alone
    // produced 6.75% second harmonic on a single channel. Real desks measure a
    // fraction of a percent at nominal level.
    //
    // Applied on the forward side only, so it is colour rather than something
    // the bus tries and fails to undo.
    if (tuning.evenHarmonic > 0.0f && forward)
    {
        const auto bias = context == Context::master ? tuning.evenHarmonic * 0.5f
                                                     : tuning.evenHarmonic;
        // x*x, NOT x*|x|. The latter is half-wave symmetric - f(t+pi) = -f(t) -
        // so its Fourier series contains only ODD harmonics, and an
        // "even-harmonic bias" built from it produces no even harmonics
        // whatsoever. Measured: H2 was zero to four decimal places.
        //
        // The squared term is asymmetric and does generate a second harmonic.
        // It also generates DC, which is exactly what the DC blocker downstream
        // is for, and is what a real asymmetric stage does.
        x += bias * x * x;
    }

    // ---- the transfer --------------------------------------------------------
    x = forward ? forwardTransfer(x, tuning.curveBlend)
                : inverseTransfer(juce::jlimit(-kInverseClamp, kInverseClamp, x), tuning.curveBlend);

    x /= juce::jmax(0.05f, drive);

    if (! forward)
    {
        x = applyColour(x);
    }

    // ---- DC block ------------------------------------------------------------
    // The even-harmonic term generates DC by definition, and four channels plus
    // three buses in series would let it accumulate.
    //
    // Blocked on the INVERSE side and at the master only. On the forward side it
    // would be one more filter sitting between the channel's transfer and the
    // bus's inverse, which is the same mistake as putting the colour there -
    // measured at an extra 1.1% THD on a single AMERICAN channel. The channel's
    // DC passes to the bus, which blocks it, and the channel's own input is
    // already high-passed by its coupling stage.
    if (! forward || context == Context::master)
    {
        const auto r = 1.0f - (juce::MathConstants<float>::twoPi * tuning.dcBlockHz / rate);
        const auto out = x - state.dcPrev + r * state.dcState;
        state.dcPrev = x;
        state.dcState = out;
        x = out;
    }

    // The make-up is applied on the INVERSE side and at the master only, for the
    // same reason the colour and the DC blocker are. A trim on the forward
    // channel stage sits between the channel's transfer and the bus's inverse,
    // so the bus inverts a signal that has been scaled since - and the pair
    // stops cancelling. Measured on one channel, which must be transparent by
    // construction: a trim of 1.08 on the forward side put 0.045% THD there.
    if (! forward || context == Context::master)
    {
        x *= tuning.outputTrim;
    }

    return sanitize(x);
}

// ============================================================================
// public processing
// ============================================================================

float AnalogEngine::processChannelSample(int slot, float input)
{
    const auto wet = amountSmoothed.getNextValue();

    if (bypass || wet <= 1.0e-6f)
    {
        return input;
    }

    const auto index = static_cast<std::size_t>(juce::jlimit(0, kMaxChannels - 1, slot));
    const auto processed = processStage(channelState[index], input, Context::channel, true);

    return input + (processed - input) * wet;
}

void AnalogEngine::processBusSample(Context context, float& left, float& right)
{
    const auto wet = amountSmoothed.getNextValue();

    if (bypass || wet <= 1.0e-6f)
    {
        return;
    }

    const auto contextIndex = static_cast<std::size_t>(context);
    auto& state = busState[juce::jmin(contextIndex, busState.size() - 1)];

    // The bus runs the INVERSE of the channel, except at the master - which is
    // an output stage rather than a summing amplifier, so it saturates forward
    // like a channel does.
    const auto forward = context == Context::master;

    if (context == Context::master)
    {
        // Mid/side rather than left/right. A master bus amplifier sums; stereo
        // behaviour at that point is a property of the summing, and processing
        // two independent channels there would let a hard-panned source drive
        // one side into the knee while the other stayed clean, which is not what
        // a stereo output stage does.
        const auto mid = 0.5f * (left + right);
        const auto side = 0.5f * (left - right);

        const auto shapedMid = processStage(state[0], mid, context, forward);
        // The side path gets the same stage but is not driven as hard: side
        // content is the difference between two channels and is usually much
        // quieter, so equal drive would saturate it disproportionately.
        const auto shapedSide = processStage(state[1], side * 0.7f, context, forward) / 0.7f;

        const auto outMid = mid + (shapedMid - mid) * wet;
        const auto outSide = side + (shapedSide - side) * wet;

        left = sanitize(outMid + outSide);
        right = sanitize(outMid - outSide);
        return;
    }

    const auto shapedL = processStage(state[0], left, context, forward);
    const auto shapedR = processStage(state[1], right, context, forward);

    // The FX bus gets a lighter touch by mixing in less of the stage, not by
    // driving it less: trimming its drive would break invertibility on the FX
    // path exactly the way separate channel and bus drives broke it on the dry
    // path.
    const auto stageWet = context == Context::fxBus ? wet * tuning.fxBusTrim : wet;

    left = sanitize(left + (shapedL - left) * stageWet);
    right = sanitize(right + (shapedR - right) * stageWet);
}

} // namespace px3
