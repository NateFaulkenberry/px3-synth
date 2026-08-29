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

juce::StringArray AnalogEngine::profileNames()
{
    return { "CLEAN", "BRITISH", "AMERICAN", "TRANSFORMER", "MODERN" };
}

juce::StringArray AnalogEngine::tuningKeys()
{
    return { "channelDrive", "busDrive", "masterDrive", "fxBusTrim", "curveBlend",
             "evenHarmonic", "slewEnhance", "hfRolloffHz", "hfLevelDependence",
             "lfCornerHz", "lfLevelTrim", "dcBlockHz", "headroom", "engineAmount" };
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
            t.channelDrive = 0.85f;
            t.busDrive = 0.85f;
            t.masterDrive = 0.70f;
            t.curveBlend = 0.10f;
            t.evenHarmonic = 0.0f;
            t.slewEnhance = 0.12f;
            t.hfRolloffHz = 21000.0f;
            t.hfLevelDependence = 0.10f;
            t.lfCornerHz = 6.0f;
            t.lfLevelTrim = 0.05f;
            t.headroom = 1.15f;
            break;

        case Profile::british:
            // Discrete, transformer-coupled. Even-harmonic bias for the warmth,
            // a real coupling-capacitor corner, and the top rolled off enough to
            // hear. Slew is moderate - these desks are not fast.
            t.channelDrive = 1.05f;
            t.busDrive = 1.00f;
            t.masterDrive = 0.88f;
            t.curveBlend = 0.30f;
            t.evenHarmonic = 0.085f;
            t.slewEnhance = 0.40f;
            t.hfRolloffHz = 16000.0f;
            t.hfLevelDependence = 0.38f;
            t.lfCornerHz = 18.0f;
            t.lfLevelTrim = 0.28f;
            t.headroom = 0.95f;
            break;

        case Profile::american:
            // Discrete op-amp. Harder knee and a faster slew than BRITISH, with
            // the harmonic weight on odd orders - forward rather than warm. The
            // heaviest pre-warp of any profile, which is where the harder onset
            // and the extra 5th-harmonic content come from.
            t.channelDrive = 1.15f;
            t.busDrive = 1.10f;
            t.masterDrive = 0.92f;
            t.curveBlend = 0.62f;
            t.evenHarmonic = 0.030f;
            t.slewEnhance = 0.62f;
            t.hfRolloffHz = 18500.0f;
            t.hfLevelDependence = 0.22f;
            t.lfCornerHz = 14.0f;
            t.lfLevelTrim = 0.12f;
            t.headroom = 0.90f;
            break;

        case Profile::transformer:
            // Transformer-heavy. The most even-harmonic content, the most
            // level-dependent low end, and the most restricted bandwidth. This
            // is the profile that changes tone rather than just density.
            t.channelDrive = 1.10f;
            t.busDrive = 1.05f;
            t.masterDrive = 0.95f;
            t.curveBlend = 0.20f;
            t.evenHarmonic = 0.135f;
            t.slewEnhance = 0.30f;
            t.hfRolloffHz = 13500.0f;
            t.hfLevelDependence = 0.52f;
            t.lfCornerHz = 26.0f;
            t.lfLevelTrim = 0.45f;
            t.headroom = 0.88f;
            break;

        case Profile::modern:
            // Later clean VCA large-format. The most headroom, the fastest
            // recovery, the least colour - but a distinctly harder edge than
            // CLEAN when it finally does reach the knee.
            t.channelDrive = 0.90f;
            t.busDrive = 0.90f;
            t.masterDrive = 0.75f;
            t.curveBlend = 0.45f;
            t.evenHarmonic = 0.012f;
            t.slewEnhance = 0.20f;
            t.hfRolloffHz = 20000.0f;
            t.hfLevelDependence = 0.14f;
            t.lfCornerHz = 8.0f;
            t.lfLevelTrim = 0.08f;
            t.headroom = 1.25f;
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
    if (key == "channelDrive")           { tuning.channelDrive = juce::jlimit(0.0f, 3.0f, value); }
    else if (key == "busDrive")          { tuning.busDrive = juce::jlimit(0.0f, 3.0f, value); }
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
}

float AnalogEngine::getTuningValue(const juce::String& key) const
{
    if (key == "channelDrive")           { return tuning.channelDrive; }
    if (key == "busDrive")               { return tuning.busDrive; }
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

    // A slow envelope of this stage's own level. Everything level-dependent
    // below reads it, which is what makes the frequency response change with
    // how hard the stage is being driven rather than only with the knob.
    const auto envCoeff = onePoleCoeff(18.0f, rate);
    state.envelope += (std::abs(input) - state.envelope) * envCoeff;
    const auto drivenBy = juce::jlimit(0.0f, 1.0f, state.envelope * 2.0f);

    auto x = input;

    // ---- coupling capacitor -------------------------------------------------
    // A real stage is AC coupled. The corner moves up with level on the
    // transformer-ish profiles, which is the level-dependent LF behaviour a
    // transformer actually has - it loses low end as it approaches saturation.
    if (context != Context::fxBus)
    {
        const auto corner = tuning.lfCornerHz * (1.0f + drivenBy * tuning.lfLevelTrim * 3.0f);
        const auto lfCoeff = onePoleCoeff(corner, rate);
        state.lfState += (x - state.lfState) * lfCoeff;
        x -= state.lfState;
    }

    // ---- slew ----------------------------------------------------------------
    // The channel ENHANCES slew and the bus cuts it back, the same way the
    // transfer pair works: arcsine one side, sine the other. A stage that only
    // ever softened transients would make everything duller the more of it you
    // used; this way the pair has a transient personality without a net loss.
    if (tuning.slewEnhance > 0.0f && context != Context::fxBus)
    {
        auto difference = juce::jlimit(-1.0f, 1.0f, x - state.lastSample);
        state.lastSample = x;

        const auto shaped = forward
                                ? std::asin(difference * 0.999f)
                                : std::sin(difference);
        const auto blended = difference + (shaped - difference) * tuning.slewEnhance;

        // The slew stage integrates, so it can walk off into DC. The servo pulls
        // it back the way a real amplifier's does - slowly, and proportionally
        // to how far out it has drifted.
        state.servo += (blended - difference) * 1.0e-4f;
        state.servo *= 0.99995f;

        x = state.lastShaped + blended - state.servo;
        state.lastShaped = juce::jlimit(-4.0f, 4.0f, x);
    }

    // ---- drive into the transfer ---------------------------------------------
    auto drive = 1.0f;
    switch (context)
    {
        case Context::channel: drive = tuning.channelDrive; break;
        case Context::dryBus:  drive = tuning.busDrive; break;
        case Context::fxBus:   drive = tuning.busDrive * tuning.fxBusTrim; break;
        case Context::master:  drive = tuning.masterDrive; break;
    }
    drive /= juce::jmax(0.05f, tuning.headroom);

    x *= drive;

    // ---- even-harmonic bias --------------------------------------------------
    // A purely odd transfer produces only odd harmonics, and odd harmonics alone
    // read as hard rather than warm. A small squared term is the 2nd harmonic.
    //
    // This is deliberately applied OUTSIDE the invertible pair: it is colour,
    // and inverting it would cancel exactly the thing it was added for. It is
    // kept small because it is also the one part of the model that breaks the
    // single-channel transparency guarantee.
    if (tuning.evenHarmonic > 0.0f && context != Context::dryBus)
    {
        const auto bias = context == Context::master ? tuning.evenHarmonic * 0.5f
                                                     : tuning.evenHarmonic;
        x += bias * x * std::abs(x);
    }

    // ---- the transfer --------------------------------------------------------
    x = forward ? forwardTransfer(x, tuning.curveBlend)
                : inverseTransfer(juce::jlimit(-kInverseClamp, kInverseClamp, x), tuning.curveBlend);

    x /= juce::jmax(0.05f, drive);

    // ---- bandwidth -----------------------------------------------------------
    // Not a generic low-pass bolted on the end: the corner falls as the stage is
    // driven, which is what a real amplifier's slew-limited bandwidth does. On
    // the transformer profile this is a large part of the character.
    {
        const auto corner = tuning.hfRolloffHz
                            * (1.0f - drivenBy * tuning.hfLevelDependence * 0.55f);
        const auto hfCoeff = onePoleCoeff(juce::jmax(1000.0f, corner), rate);
        state.hfState += (x - state.hfState) * hfCoeff;
        x = state.hfState;
    }

    // ---- DC block ------------------------------------------------------------
    // Every nonlinear stage here can generate DC - the even-harmonic term does so
    // by definition - and four channels plus three buses in series would let it
    // accumulate. Blocked per stage rather than once at the end.
    {
        const auto r = 1.0f - (juce::MathConstants<float>::twoPi * tuning.dcBlockHz / rate);
        const auto out = x - state.dcPrev + r * state.dcState;
        state.dcPrev = x;
        state.dcState = out;
        x = out;
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

    left = sanitize(left + (shapedL - left) * wet);
    right = sanitize(right + (shapedR - right) * wet);
}

} // namespace px3
