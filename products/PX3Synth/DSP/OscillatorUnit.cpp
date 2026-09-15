#include "OscillatorUnit.h"

#include <JuceHeader.h>

#include <algorithm>
#include <cmath>

namespace
{
using px3::dsp::fastSine;
using px3::dsp::kInverseTwoPi;
using px3::dsp::kPi;
using px3::dsp::kTanhAdaa;
using px3::dsp::kTwoPi;
using Mode = px3::OscillatorMode;

inline float clamp01(float v)
{
    return juce::jlimit(0.0f, 1.0f, v);
}

inline double wrap(double phase) noexcept
{
    return phase - std::floor(phase);
}

// Each mode's level before the voice's soft clip.
constexpr std::array<float, px3::oscillatorModeCount> kModeTrim {
    0.82f, 0.74f, 0.72f, 0.78f, 0.64f,
    0.67f, 0.62f, 0.70f, 0.76f, 0.80f,
    0.73f, 0.64f, 0.60f, 0.76f,
    0.66f, 0.70f, 0.62f, 0.74f, 0.60f
};

// How much a mode's level follows its macros, so opening them does not make it
// louder as well as brighter.
constexpr std::array<float, px3::oscillatorModeCount> kModeTravelSlope {
    0.00f, 0.08f, 0.10f, 0.06f, 0.16f,
    0.14f, 0.42f, 0.18f, 0.20f, 0.14f,
    0.20f, 0.38f, 0.46f, 0.14f,
    0.34f, 0.24f, 0.34f, 0.20f, 0.40f
};

// Every mode used to be blended 15% toward a silent input that was never wired
// up - more as the mod wheel rose - so the wheel was a hidden volume control and
// every oscillator sat 15% down even at rest. The wheel no longer touches level;
// the 15% it took at rest is kept here as a plain trim, so no patch changes
// loudness with the wheel down.
constexpr float kLevelAtRest = 0.85f;

// Modes whose output still carries DC once its causes are removed, and so take
// a 5 Hz blocker on their own output. Measured by PX3Diag oscdc.
//
// HARD SYNC: its slave restarts partway up a ramp at every reset, so the
// waveform is not symmetric and its mean depends on the ratio - measured at 1.6%
// of RMS, and 10% at ratios like 2.5. Real analog sync has the same offset.
//
// FORMANT: once its excitation's DC term is removed at the source, the soft
// clip still makes 2-4% - tanh is odd, but a resonator's ringing is skewed, so
// the clipped signal's mean is not zero.
constexpr std::array<bool, px3::oscillatorModeCount> kModeUsesDcBlocker { {
    false, false, false, false, false,
    false, false, false, false, false,
    true,  false, true,  false,
    false, false, false, false, false
} };

constexpr std::array<float, 7> kSuperSawOffsets { { -0.22f, -0.14f, -0.07f, 0.0f, 0.07f, 0.14f, 0.22f } };

// The nine Hammond drawbar footages as ratios to the note: 16', 5 1/3', 8', 4',
// 2 2/3', 2', 1 3/5', 1 1/3', 1'.
constexpr std::array<float, 9> kDrawbarRatios { { 0.5f, 1.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f } };

// PHYSICAL's modes: a struck bar's partials, stretched by MATERIAL about the
// fundamental - which stays at the played pitch.
constexpr std::array<double, 4> kPhysicalRatios { { 1.0, 2.32, 3.91, 5.48 } };
constexpr std::array<double, 4> kPhysicalWeights { { 0.72, 0.36, 0.24, 0.18 } };
constexpr std::array<double, 4> kPhysicalSustain { { 0.30, 0.15, 0.10, 0.075 } };
constexpr double kPhysicalDrive = 2.2;

// ISAAC's shimmer partial: half the note, detuned by the 0.0007 rad/sample the
// old code drifted it at 48 kHz - as hertz, so it beats at the same rate at any
// sample rate.
constexpr double kShimmerOffsetHz = 0.0007 * 48000.0 / (2.0 * 3.14159265358979323846);

float rollGain(int harmonic, float roll)
{
    // ROLL slides a window along the series. At 0.5 every partial passes; below
    // it the upper partials roll off from the top, above it the lower ones roll
    // off from the bottom.
    const auto h = static_cast<float>(harmonic);
    if (roll <= 0.5f)
    {
        const auto corner = 1.0f + 14.0f * roll;
        return h <= corner ? 1.0f : std::exp(-(h - corner) * 1.2f);
    }
    const auto floorHarmonic = 1.0f + 14.0f * (roll - 0.5f);
    return h >= floorHarmonic ? 1.0f : std::exp(-(floorHarmonic - h) * 1.2f);
}
} // namespace

// ---- controls -------------------------------------------------------------------

void OscillatorUnit::prepare(double newSampleRate)
{
    sampleRate = juce::jmax(1.0, newSampleRate);
    inverseSampleRate = 1.0 / sampleRate;
    wtPositionCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (kWtPositionSmoothingSeconds * sampleRate)));
    fadeLength = juce::jmax(1, static_cast<int>(std::lround(kModeCrossfadeSeconds * sampleRate)));

    // Super saw drift: white noise through a 0.5 Hz one-pole, scaled back to the
    // spread of the fixed offsets it replaces.
    driftCoeff = 1.0 - std::exp(-kTwoPi * 0.5 / sampleRate);
    driftNorm = std::sqrt((2.0 - driftCoeff) / driftCoeff);

    // White noise keeps its density per hertz: at a higher rate the same
    // variance is spread over a wider band.
    whiteScale = std::sqrt(sampleRate / px3::dsp::kReferenceSampleRate);

    for (auto& blocker : dcBlockers)
    {
        blocker.prepare(sampleRate);
    }

    if (derivedValid)
    {
        // Resonators, decays and colour filters are designed in hertz.
        updateDerivedCurves();
        previous = target;
    }
}

void OscillatorUnit::setSettings(const OscillatorSettings& settings, int rampSamples)
{
    auto clamped = settings;
    clamped.modeIndex = px3::clampOscillatorModeIndex(clamped.modeIndex);
    clamped.macroA = clamp01(clamped.macroA);
    clamped.macroB = clamp01(clamped.macroB);
    clamped.macroC = clamp01(clamped.macroC);
    clamped.vowelIndex = juce::jlimit(0, 4, clamped.vowelIndex);
    for (auto& h : clamped.harmonics)
    {
        h = clamp01(h);
    }
    clamped.wtPosition = juce::jlimit(0.0f, 1.0f, clamped.wtPosition);

    const auto first = !derivedValid;
    const auto controlsChanged = first
                                 || clamped.macroA != oscillatorSettings.macroA
                                 || clamped.macroB != oscillatorSettings.macroB
                                 || clamped.macroC != oscillatorSettings.macroC
                                 || clamped.vowelIndex != oscillatorSettings.vowelIndex
                                 || clamped.harmonics != oscillatorSettings.harmonics;

    // The table and the scan position are not derived state: the position
    // moves on almost every block, and is smoothed per sample on its own.
    oscillatorSettings.table = clamped.table;
    oscillatorSettings.wtPosition = clamped.wtPosition;

    if (controlsChanged)
    {
        previous = target;
        const auto mode = oscillatorSettings.modeIndex;
        oscillatorSettings = clamped;
        oscillatorSettings.modeIndex = mode;
        updateDerivedCurves();
        if (first)
        {
            previous = target;
        }
        rampLength = juce::jmax(1, rampSamples);
        rampPosition = rampSamples > 0 ? 0 : rampLength;
        currentRamp = rampSamples > 0 ? 0.0f : 1.0f;
        if (rampSamples <= 0)
        {
            previous = target;
        }
    }

    oscillatorSettings.modeIndex = clamped.modeIndex;

    if (first)
    {
        derivedValid = true;
        activeMode = clamped.modeIndex;
        activateMode(activeMode, false);
    }
    else
    {
        requestMode(clamped.modeIndex);
    }
}

void OscillatorUnit::resetForNote(double startPhase, std::uint32_t seed)
{
    phase = wrap(startPhase);
    noise.seed(seed);

    previous = target;
    rampPosition = rampLength;
    currentRamp = 1.0f;

    // A new note starts in the mode that was asked for, not partway through a
    // crossfade left over from the last one.
    if (pendingMode >= 0)
    {
        activeMode = pendingMode;
    }
    fadingMode = -1;
    pendingMode = -1;
    fadeRemaining = 0;

    smoothedWtPosition = oscillatorSettings.wtPosition;
    activateMode(activeMode, true);
}

void OscillatorUnit::requestMode(int mode)
{
    if (fadeRemaining > 0)
    {
        // Structural: a change that arrives mid-fade waits for the fade.
        pendingMode = mode == activeMode ? -1 : mode;
        return;
    }
    if (mode != activeMode)
    {
        beginModeChange(mode);
    }
}

void OscillatorUnit::beginModeChange(int mode)
{
    fadingMode = activeMode;
    activeMode = mode;
    fadeRemaining = fadeLength;
    activateMode(mode, false);
}

void OscillatorUnit::activateMode(int modeIndex, bool strike)
{
    const auto idx = static_cast<std::size_t>(px3::clampOscillatorModeIndex(modeIndex));
    plainDelays[idx].reset();
    dcBlockers[idx].reset();

    switch (static_cast<Mode>(idx))
    {
        case Mode::saw:
            sawLine.reset();
            break;
        case Mode::square:
            squareLine.reset();
            break;
        case Mode::triangle:
            triangleLine.reset();
            break;
        case Mode::pwm:
            pwmLine.reset();
            break;
        case Mode::noise:
            noiseColorState = 0.0f;
            break;
        case Mode::pinkNoise:
            pinkFilter.reset();
            pinkColorState = 0.0f;
            break;
        case Mode::superSaw:
            // Random start phases are the super saw's sound; the randomness is
            // this oscillator's own seeded stream, so a render repeats exactly.
            for (std::size_t i = 0; i < superSawPhases.size(); ++i)
            {
                superSawPhases[i] = noise.unit();
                superSawDrift[i] = 0.0;
                superSawLines[i].reset();
                superSawClips[i].reset();
            }
            break;
        case Mode::wavetable:
            smoothedWtPosition = oscillatorSettings.wtPosition;
            wavetableReader.reset();
            break;
        case Mode::additive:
            for (std::size_t i = 0; i < additivePhases.size(); ++i)
            {
                additivePhases[i] = wrap(phase * target.additive.ratio[i]);
            }
            break;
        case Mode::isaac:
            for (std::size_t i = 0; i < isaacPhases.size(); ++i)
            {
                isaacPhases[i] = wrap(phase * target.isaac.ratio[i]);
            }
            isaacShimmerPhase = wrap(0.5 * phase);
            isaacClip.reset();
            break;
        case Mode::formant:
            formantY1.fill(0.0f);
            formantY2.fill(0.0f);
            formantSourceState = 0.0f;
            formantClip.reset();
            break;
        case Mode::fm:
            fmModulatorPhase = wrap(phase * target.fmRatio);
            fmDecimator.reset();
            break;
        case Mode::hardSync:
            syncSlavePhase = wrap(phase * target.hardSyncRatio);
            syncLine.reset();
            syncClip.reset();
            break;
        case Mode::organ:
            for (std::size_t i = 0; i < organPhases.size(); ++i)
            {
                organPhases[i] = wrap(phase * kDrawbarRatios[i]);
            }
            organClickPhase = wrap(9.0 * phase);
            organClip.reset();
            break;
        case Mode::digital:
            digitalHoldCounter = 0;
            digitalHeld = 0.0;
            digitalClip.reset();
            break;
        case Mode::physical:
            for (std::size_t i = 0; i < physicalPhases.size(); ++i)
            {
                // Every mode starts from zero phase: the strike is the same on
                // every note, and it starts at silence rather than mid-cycle.
                physicalPhases[i] = 0.0;
                physicalEnvelopes[i] = strike ? 1.0 : kPhysicalSustain[i];
            }
            physicalClip.reset();
            break;
        case Mode::rob:
            robPhases.fill(0.0);
            robPhases[0] = wrap(phase * (1.0 + target.robChaos * 1.05 + target.robBody * 0.45));
            robTransient = strike ? 1.0 : 0.0;
            robBodyClip.reset();
            robEdgeClip.reset();
            robOutClip.reset();
            break;
        case Mode::px3:
            px3ModulatorPhase = wrap(2.0 * phase);
            px3Decimator.reset();
            px3SawLine.reset();
            px3SawClip.reset();
            px3IsaacClip.reset();
            px3OutClip.reset();
            for (std::size_t i = 0; i < px3IsaacPhases.size(); ++i)
            {
                px3IsaacPhases[i] = wrap(phase * target.px3Isaac.ratio[i]);
            }
            px3ShimmerPhase = wrap(0.5 * phase);
            px3MovePhase = 0.0;
            px3IsaacDelay.reset();
            break;
        case Mode::sine:
        default:
            break;
    }
}

OscillatorUnit::HarmonicSet OscillatorUnit::buildHarmonicSet(const std::array<float, 8>& harmonics,
                                                            float rolloffBias,
                                                            float oddEvenBias,
                                                            float inharmonicity,
                                                            float roll)
{
    HarmonicSet set;
    auto energy = 0.0f;

    for (int i = 0; i < 8; ++i)
    {
        const auto h = static_cast<float>(i + 1);
        auto amp = harmonics[static_cast<std::size_t>(i)];
        amp *= std::pow(1.0f / h, rolloffBias);

        const auto isOdd = (i % 2) == 0;
        const auto oddEven = isOdd ? (1.0f + oddEvenBias) : (1.0f - oddEvenBias * 0.82f);
        amp *= juce::jmax(0.0f, oddEven);
        amp *= rollGain(i + 1, roll);

        set.amplitude[static_cast<std::size_t>(i)] = amp;
        set.ratio[static_cast<std::size_t>(i)] = h * (1.0f + inharmonicity * 0.03f * h);
        energy += amp * amp;
    }
    set.amplitude[8] = 0.0f;
    set.ratio[8] = 9.0f;

    // Root-sum-square: sines at different frequencies do not line up at their
    // peaks, so this is the level the sum actually has, with a little headroom
    // for the peaks that do coincide.
    set.norm = std::sqrt(juce::jmax(1.0e-8f, energy)) * 1.35f;
    return set;
}

void OscillatorUnit::updateDerivedCurves()
{
    const auto a = oscillatorSettings.macroA;
    const auto b = oscillatorSettings.macroB;
    const auto c = oscillatorSettings.macroC;
    auto& d = target;

    // SUPER SAW: SPREAD sets both how far apart the saws sit and how far they drift.
    {
        const auto spread = std::pow(a, 1.65f);
        d.superSawWidth = std::pow(a, 1.2f);
        d.superSawEdgeSoft = 0.58f + 0.42f * (1.0f - d.superSawWidth);
        for (std::size_t i = 0; i < d.superSawRatios.size(); ++i)
        {
            const auto semitones = kSuperSawOffsets[i] * (0.04f + 16.0f * spread);
            d.superSawRatios[i] = std::pow(2.0, static_cast<double>(semitones) / 12.0);
        }
    }

    d.pwmWidthCurve = std::pow(a, 1.15f);

    // ADDITIVE and ISAAC share TILT (A) and ODD/EVEN (B). ADDITIVE's C is ROLL;
    // ISAAC's is STRETCH, the inharmonic spread of its partials, and its shimmer.
    {
        const auto rolloff = juce::jmap(std::pow(a, 1.15f), 0.25f, 1.15f);
        const auto oddEven = juce::jmap(std::pow(b, 1.1f), -0.65f, 0.65f);
        const auto inharmonicity = juce::jmap(std::pow(c, 1.2f), 0.0f, 1.1f);
        d.additive = buildHarmonicSet(oscillatorSettings.harmonics, rolloff, oddEven, 0.0f, c);
        d.isaac = buildHarmonicSet(oscillatorSettings.harmonics, rolloff, oddEven, inharmonicity, 0.5f);
        d.isaacShimmer = 0.15f * (0.18f + c * 0.42f);
    }

    // FORMANT
    {
        // Formant frequencies, bandwidths and levels for the five cardinal
        // vowels, adult male tract (Peterson & Barney / Klatt).
        struct Vowel { float f1, f2, f3, b1, b2, b3, a1, a2, a3; };
        static constexpr std::array<Vowel, 5> kVowels { {
            {  730.f, 1090.f, 2440.f,  70.f, 110.f, 170.f, 1.0f, 0.50f, 0.28f },  // AH
            {  530.f, 1840.f, 2480.f,  60.f, 100.f, 160.f, 1.0f, 0.45f, 0.30f },  // EH
            {  270.f, 2290.f, 3010.f,  55.f, 100.f, 180.f, 1.0f, 0.35f, 0.25f },  // EE
            {  570.f,  840.f, 2410.f,  70.f,  95.f, 160.f, 1.0f, 0.55f, 0.18f },  // OH
            {  300.f,  870.f, 2240.f,  55.f,  90.f, 160.f, 1.0f, 0.40f, 0.14f },  // OO
        } };

        // MORPH glides onward FROM the selected vowel, through its neighbours in
        // turn: at 0 it is the vowel on the menu, and each quarter of the knob
        // is one more vowel along. It used to interpolate from the selected
        // vowel to the vowel at MORPH's own position, which jumped at every
        // quarter and could never reach the neighbours between.
        const auto position = static_cast<float>(oscillatorSettings.vowelIndex) + a * 4.0f;
        const auto lowerIndex = static_cast<int>(std::floor(position));
        const auto frac = position - static_cast<float>(lowerIndex);
        const auto& v0 = kVowels[static_cast<std::size_t>(lowerIndex % 5)];
        const auto& v1 = kVowels[static_cast<std::size_t>((lowerIndex + 1) % 5)];
        const auto mix = [frac](float from, float to) { return from + (to - from) * frac; };

        // SHIFT (B) is the tract length: every formant moves together.
        const auto shift = juce::jmap(b, 0.72f, 1.55f);

        const std::array<float, 3> freq { mix(v0.f1, v1.f1) * shift, mix(v0.f2, v1.f2) * shift, mix(v0.f3, v1.f3) * shift };
        const std::array<float, 3> bw { mix(v0.b1, v1.b1), mix(v0.b2, v1.b2), mix(v0.b3, v1.b3) };
        const std::array<float, 3> amp { mix(v0.a1, v1.a1), mix(v0.a2, v1.a2), mix(v0.a3, v1.a3) };

        const auto rate = static_cast<float>(juce::jmax(1000.0, sampleRate));
        // The glottal tilt, fixed in hertz so it does not move with the note.
        constexpr float tiltHz = 260.0f;
        d.formantSourceCoeff = juce::jlimit(0.0005f, 0.9f, 1.0f - std::exp(-juce::MathConstants<float>::twoPi * tiltHz / rate));
        d.formantTrim = 2.2f;

        for (int i = 0; i < 3; ++i)
        {
            const auto idx = static_cast<std::size_t>(i);
            const auto f = juce::jlimit(20.0f, rate * 0.45f, freq[idx]);
            const auto r = std::exp(-juce::MathConstants<float>::pi * bw[idx] / rate);
            const auto cosw = std::cos(juce::MathConstants<float>::twoPi * f / rate);
            d.formant.b[idx] = 2.0f * r * cosw;
            d.formant.c[idx] = -r * r;
            // Each resonator peaks at unity, so the vowel's balance holds as the
            // tract is resized.
            d.formant.a[idx] = (1.0f - r) * std::sqrt(1.0f - 2.0f * r * cosw + r * r);
            // Alternating sign, as parallel formant synthesisers do: in phase,
            // neighbouring skirts cancel in the valleys and hollow the vowel.
            d.formant.gain[idx] = (i == 1 ? -amp[idx] : amp[idx]);
        }
    }

    // ORGAN: two registrations crossfaded by TONE, trimmed by the harmonic sliders.
    {
        static constexpr std::array<float, kHarmonicCount> kMellow { 0.85f, 0.30f, 1.00f, 0.55f, 0.16f, 0.10f, 0.05f, 0.04f, 0.03f };
        static constexpr std::array<float, kHarmonicCount> kBright { 0.80f, 0.70f, 1.00f, 0.85f, 0.72f, 0.66f, 0.55f, 0.50f, 0.45f };

        d.organClick = std::pow(b, 1.2f);
        // 0.0006 to 0.0036 per sample at 48 kHz, as a rate per second.
        d.organClickDecayPerSecond = (0.0006f + 0.003f * d.organClick) * 48000.0f;

        d.organ = HarmonicSet {};
        auto energy = 0.0f;
        for (int i = 0; i < kHarmonicCount; ++i)
        {
            const auto idx = static_cast<std::size_t>(i);
            const auto drawbar = kMellow[idx] + (kBright[idx] - kMellow[idx]) * a;
            const auto trim = i < 8 ? (0.55f + oscillatorSettings.harmonics[idx] * 0.75f) : 1.0f;
            const auto level = drawbar * trim;
            d.organ.amplitude[idx] = level;
            d.organ.ratio[idx] = kDrawbarRatios[idx];
            energy += level * level;
        }
        d.organ.norm = std::sqrt(juce::jmax(1.0e-8f, energy)) * 1.35f;
    }

    d.fmRatio = std::pow(2.0f, juce::jmap(std::pow(a, 1.1f), -1.6f, 2.2f));
    d.fmIndex = juce::jmap(std::pow(b, 1.35f), 0.0f, 10.0f);
    d.fmOutputScale = 0.66f + 0.05f * (1.0f - b);

    d.hardSyncRatio = juce::jmap(std::pow(a, 1.3f), 1.0f, 11.0f);
    d.hardSyncDrive = 1.0f + std::pow(b, 1.15f) * 2.3f;

    // DIGITAL
    {
        const auto bitsCurve = std::pow(a, 1.25f);
        const auto rateCurve = std::pow(b, 1.15f);
        const auto bitDepth = juce::jlimit(2, 16, static_cast<int>(std::round(juce::jmap(bitsCurve, 2.0f, 16.0f))));
        // A duration: the number of samples it was at 48 kHz.
        d.digitalHoldAt48k = std::round(juce::jmap(rateCurve, 1.0f, 52.0f));
        d.digitalFold = 1.0f + juce::jmap(rateCurve, 0.4f, 6.8f);
        d.digitalSteps = static_cast<float>(1 << bitDepth);
        d.digitalCrushSteps = static_cast<float>(1 << juce::jmax(1, bitDepth - 1));
    }

    // PHYSICAL: DECAY is the ring time, MATERIAL the spread of the upper modes.
    {
        const auto decaySeconds = 0.12 * std::pow(40.0, static_cast<double>(a));
        for (std::size_t i = 0; i < d.physicalDecayCoeff.size(); ++i)
        {
            const auto seconds = decaySeconds / (1.0 + 0.8 * static_cast<double>(i));
            d.physicalDecayCoeff[i] = static_cast<float>(std::exp(-1.0 / (seconds * sampleRate)));
        }
        d.physicalSpread = 0.7f + 0.8f * std::pow(b, 1.2f);
    }

    d.robTrans = std::pow(a, 0.55f);
    d.robBody = std::pow(b, 0.72f);
    d.robChaos = std::pow(c, 0.80f);
    d.robTransientCoeff = static_cast<float>(std::exp(-px3::dsp::perSampleRate(juce::jmap(d.robTrans, 0.085f, 0.012f), sampleRate)));

    d.px3Morph = std::pow(a, 1.1f);
    d.px3Character = std::pow(b, 1.2f);
    d.px3Movement = std::pow(c, 1.1f);
    d.px3Isaac = buildHarmonicSet(oscillatorSettings.harmonics, 0.55f, 0.0f, 0.3f * d.px3Movement, 0.5f);

    d.noiseColor = a;
    d.noiseCoeff = static_cast<float>(px3::dsp::onePoleCoefficient(juce::jmap(a, 0.02f, 0.48f), sampleRate));
    d.pinkCoeff = static_cast<float>(px3::dsp::onePoleCoefficient(juce::jmap(a, 0.01f, 0.30f), sampleRate));

    // Level per mode. The macro average reads exactly the knobs the mode shows.
    for (int mode = 0; mode < px3::oscillatorModeCount; ++mode)
    {
        const auto count = px3::oscillatorModeMacroCount(mode);
        const auto energy = count == 0 ? 0.5f : count == 1 ? a : count == 2 ? 0.5f * (a + b) : (a + b + c) / 3.0f;

        auto trim = 1.0f;
        switch (static_cast<Mode>(mode))
        {
            case Mode::superSaw: trim = juce::jmap(std::pow(a, 1.35f), 1.0f, 0.84f); break;
            case Mode::fm:       trim = juce::jmap(std::pow(b, 1.2f), 1.0f, 0.82f); break;
            case Mode::hardSync: trim = juce::jmap(std::pow(b, 1.18f), 1.0f, 0.78f); break;
            case Mode::digital:  trim = juce::jmap(std::pow(b, 1.1f), 1.0f, 0.86f); break;
            case Mode::rob:
            case Mode::px3:      trim = juce::jmap(std::pow(c, 1.12f), 1.0f, 0.84f); break;
            default: break;
        }

        const auto idx = static_cast<std::size_t>(mode);
        auto gain = kModeTrim[idx] * (1.0f - kModeTravelSlope[idx] * (energy - 0.5f)) * trim;
        d.modeGain[idx] = juce::jlimit(0.45f, 1.08f, gain) * kLevelAtRest;
    }
}

// ---- rendering --------------------------------------------------------------------

double OscillatorUnit::renderSample(const RenderContext& context)
{
    if (rampPosition < rampLength)
    {
        ++rampPosition;
        currentRamp = static_cast<float>(rampPosition) / static_cast<float>(rampLength);
        if (rampPosition == rampLength)
        {
            // Settled: from here every ramp reads its target exactly.
            previous = target;
            currentRamp = 1.0f;
        }
    }

    MainPhase main;
    // Multiplied by the inverse rather than divided: a division per sample per
    // accumulator was a measurable share of the saw-heavy modes.
    main.increment = juce::jlimit(0.0, 0.499, context.frequencyHz * inverseSampleRate);
    main.wrapped = px3::dsp::advancePhase(phase, main.increment);
    main.phase = phase;
    main.tau = main.wrapped && main.increment > 0.0 ? phase / main.increment : 0.0;

    auto out = renderMode(activeMode, context, main);

    if (fadeRemaining > 0)
    {
        const auto outgoing = renderMode(fadingMode, context, main);
        const auto weight = static_cast<double>(fadeRemaining) / static_cast<double>(fadeLength);
        out += (outgoing - out) * weight;
        if (--fadeRemaining == 0)
        {
            fadingMode = -1;
            if (pendingMode >= 0)
            {
                const auto next = pendingMode;
                pendingMode = -1;
                if (next != activeMode)
                {
                    beginModeChange(next);
                }
            }
        }
    }

    return out;
}

double OscillatorUnit::renderMode(int modeIndex, const RenderContext& context, const MainPhase& main)
{
    const auto idx = static_cast<std::size_t>(px3::clampOscillatorModeIndex(modeIndex));
    double sample = 0.0;

    switch (static_cast<Mode>(idx))
    {
        case Mode::sine:
            sample = plainDelays[idx].push(fastSine(main.phase));
            break;

        case Mode::saw:
            if (main.wrapped)
            {
                sawLine.step(main.tau, -2.0);
            }
            sample = sawLine.push(2.0 * main.phase - 1.0);
            break;

        case Mode::square:
            sample = renderPulse(squareLine, main, 0.5);
            break;

        case Mode::triangle:
            sample = renderTriangle(main);
            break;

        case Mode::noise:
        {
            const auto color = ramped(&DerivedCurves::noiseColor);
            const auto white = noise.white() * static_cast<float>(whiteScale);
            noiseColorState += (white - noiseColorState) * ramped(&DerivedCurves::noiseCoeff);
            sample = plainDelays[idx].push((noiseColorState * (1.0f - color) + white * color) * 0.78f);
            break;
        }

        case Mode::pinkNoise:
        {
            const auto color = ramped(&DerivedCurves::noiseColor);
            auto pink = pinkFilter.process(noise.white());
            pinkColorState += (pink - pinkColorState) * ramped(&DerivedCurves::pinkCoeff);
            pink = pinkColorState * (1.0f - color) + pink * color;
            sample = plainDelays[idx].push(pink * 1.45f);
            break;
        }

        case Mode::superSaw:
            sample = renderSuperSaw(context);
            break;

        case Mode::pwm:
        {
            // The mod wheel moves the width; it no longer moves anything else.
            const auto width = juce::jlimit(0.08, 0.92,
                                            0.1 + static_cast<double>(ramped(&DerivedCurves::pwmWidthCurve)) * 0.8
                                                + (static_cast<double>(context.modWheelNorm) - 0.5) * 0.14);
            sample = renderPulse(pwmLine, main, width);
            break;
        }

        case Mode::wavetable:
        {
            smoothedWtPosition += (oscillatorSettings.wtPosition - smoothedWtPosition) * wtPositionCoeff;
            const auto* table = oscillatorSettings.table;
            // No table loaded yet: silence rather than a fallback waveform.
            sample = plainDelays[idx].push(table != nullptr
                                               ? wavetableReader.read(*table, main.phase, smoothedWtPosition, main.increment)
                                               : 0.0f);
            break;
        }

        case Mode::additive:
            sample = plainDelays[idx].push(renderHarmonicSet(additivePhases, previous.additive, target.additive, main.increment));
            break;

        case Mode::isaac:
        {
            const auto partials = renderHarmonicSet(isaacPhases, previous.isaac, target.isaac, main.increment);
            isaacShimmerPhase = wrap(isaacShimmerPhase
                                     + juce::jlimit(0.0, 0.499, (0.5 * context.frequencyHz + kShimmerOffsetHz) * inverseSampleRate));
            const auto shimmer = fastSine(isaacShimmerPhase) * ramped(&DerivedCurves::isaacShimmer);
            sample = plainDelays[idx].push(isaacClip.process(kTanhAdaa, partials + shimmer));
            break;
        }

        case Mode::formant:
            sample = renderFormant(context, main);
            break;

        case Mode::fm:
            sample = renderFmCore(fmModulatorPhase, fmDecimator, main,
                                  ramped(&DerivedCurves::fmRatio), ramped(&DerivedCurves::fmIndex))
                     * ramped(&DerivedCurves::fmOutputScale);
            break;

        case Mode::hardSync:
            sample = renderHardSync(main);
            break;

        case Mode::organ:
            sample = renderOrgan(context, main);
            break;

        case Mode::digital:
            sample = renderDigital(main);
            break;

        case Mode::physical:
            sample = renderPhysical(main);
            break;

        case Mode::rob:
            sample = renderRob(context, main);
            break;

        case Mode::px3:
            sample = renderPx3(main);
            break;

        default:
            break;
    }

    if (kModeUsesDcBlocker[idx])
    {
        sample = dcBlockers[idx].process(sample);
    }

    const auto gain = previous.modeGain[idx] + (target.modeGain[idx] - previous.modeGain[idx]) * currentRamp;
    return sample * gain;
}

double OscillatorUnit::renderPulse(px3::dsp::BlepLine& line, const MainPhase& main, double width)
{
    // Rising edge at the wrap, falling edge where the phase crosses the width,
    // each corrected at its own fractional time.
    if (main.increment > 0.0)
    {
        if (main.wrapped)
        {
            const auto before = main.phase + 1.0 - main.increment;
            if (before < width)
            {
                line.step((main.phase + 1.0 - width) / main.increment, -2.0);
            }
            line.step(main.tau, 2.0);
            if (main.phase >= width)
            {
                line.step((main.phase - width) / main.increment, -2.0);
            }
        }
        else
        {
            const auto before = main.phase - main.increment;
            if (before < width && main.phase >= width)
            {
                line.step((main.phase - width) / main.increment, -2.0);
            }
        }
    }

    // The pulse's mean is 2w - 1. Taken out, so the width knob is not also an
    // offset knob: the pulse is zero-mean at every width.
    const auto naive = (main.phase < width ? 1.0 : -1.0) - (2.0 * width - 1.0);
    return line.push(naive);
}

double OscillatorUnit::renderTriangle(const MainPhase& main)
{
    if (main.increment > 0.0)
    {
        const auto slope = 8.0 * main.increment;
        if (main.wrapped)
        {
            const auto before = main.phase + 1.0 - main.increment;
            if (before < 0.5)
            {
                triangleLine.ramp((main.phase + 0.5) / main.increment, -slope);
            }
            triangleLine.ramp(main.tau, slope);
            if (main.phase >= 0.5)
            {
                triangleLine.ramp((main.phase - 0.5) / main.increment, -slope);
            }
        }
        else if (main.phase - main.increment < 0.5 && main.phase >= 0.5)
        {
            triangleLine.ramp((main.phase - 0.5) / main.increment, -slope);
        }
    }
    return triangleLine.push(1.0 - 4.0 * std::abs(main.phase - 0.5));
}

double OscillatorUnit::renderSuperSaw(const RenderContext& context)
{
    const auto width = ramped(&DerivedCurves::superSawWidth);
    const auto edge = static_cast<double>(ramped(&DerivedCurves::superSawEdgeSoft)) * 1.35;
    const auto driftDepth = 0.03 + 0.95 * static_cast<double>(width);
    const auto t = static_cast<double>(currentRamp);

    double sum = 0.0;
    for (std::size_t i = 0; i < superSawPhases.size(); ++i)
    {
        // Each saw wanders on its own slow random walk rather than sitting on a
        // fixed offset picked at note-on.
        auto& drift = superSawDrift[i];
        drift += (static_cast<double>(noise.white()) - drift) * driftCoeff;
        const auto driftHz = drift * driftNorm * driftDepth;

        const auto ratio = previous.superSawRatios[i] + (target.superSawRatios[i] - previous.superSawRatios[i]) * t;
        const auto increment = juce::jlimit(0.0, 0.499, juce::jmax(8.0, context.frequencyHz * ratio + driftHz) * inverseSampleRate);
        auto& p = superSawPhases[i];
        if (px3::dsp::advancePhase(p, increment))
        {
            superSawLines[i].step(p / increment, -2.0);
        }
        const auto band = superSawLines[i].push(2.0 * p - 1.0);
        sum += superSawClips[i].process(kTanhAdaa, band * edge);
    }

    // Independent phases and drift keep the saws uncorrelated at any detune, so
    // one fixed scale holds the level steady across SPREAD.
    return sum * (1.0 / 7.0) * (0.84 + 0.10 * static_cast<double>(width));
}

double OscillatorUnit::renderHarmonicSet(std::array<double, kHarmonicCount>& phases,
                                         const HarmonicSet& from,
                                         const HarmonicSet& to,
                                         double increment)
{
    const auto t = currentRamp;
    const auto settled = rampPosition >= rampLength;
    double sum = 0.0;
    for (std::size_t i = 0; i < phases.size(); ++i)
    {
        const auto amp = settled ? to.amplitude[i] : from.amplitude[i] + (to.amplitude[i] - from.amplitude[i]) * t;
        const auto ratio = static_cast<double>(settled ? to.ratio[i] : from.ratio[i] + (to.ratio[i] - from.ratio[i]) * t);
        // Its own accumulator at its own ratio: a fractional ratio read off a
        // wrapped master phase jumps a part-cycle at every wrap.
        const auto partialIncrement = increment * ratio;
        auto& p = phases[i];
        p += partialIncrement;
        if (p >= 1.0)
        {
            p = p < 2.0 ? p - 1.0 : wrap(p);
        }
        if (amp != 0.0f)
        {
            const auto fade = px3::dsp::nyquistFade(partialIncrement);
            if (fade > 0.0f)
            {
                sum += static_cast<double>(amp * fade) * fastSine(p);
            }
        }
    }
    const auto norm = settled ? to.norm : from.norm + (to.norm - from.norm) * t;
    return norm > 1.0e-4f ? sum / static_cast<double>(norm) : 0.0;
}

double OscillatorUnit::renderFormant(const RenderContext& context, const MainPhase& main)
{
    // A band-limited impulse train excites the resonators: the Dirichlet kernel,
    // sin(N x/2) / sin(x/2), N equal harmonics for the price of two sines.
    // Deliberately not divided by N, so every harmonic has unit amplitude and
    // the vowel holds its level across the keyboard.
    //
    // N follows the pitch, and used to change by two harmonics in a single
    // sample as the pitch crossed a threshold - a pop. The top pair now fades
    // in and out continuously: D(N+2) - D(N) is exactly 2 cos((N+1) x/2).
    const auto f0 = juce::jmax(20.0, context.frequencyHz);
    const auto limit = 0.45 * sampleRate / f0;
    auto harmonics = juce::jlimit(1, 399, static_cast<int>(limit));
    if ((harmonics % 2) == 0)
    {
        --harmonics;
    }
    const auto topPair = juce::jlimit(0.0, 1.0, (limit - harmonics) * 0.5);

    const auto half = kPi * main.phase;
    const auto denominator = std::sin(half);
    auto raw = std::abs(denominator) < 1.0e-7 ? static_cast<double>(harmonics)
                                              : std::sin(harmonics * half) / denominator;
    raw += topPair * 2.0 * std::cos((harmonics + 1) * half);
    // The kernel is 1 + 2 * sum(cos k x): its DC term is exactly 1. The
    // resonators pass a few percent of DC, which measured 12-18% of the mode's
    // RMS, so it is taken out here, where it is exact, rather than filtered later.
    raw -= 1.0;

    formantSourceState += (static_cast<float>(raw) - formantSourceState) * ramped(&DerivedCurves::formantSourceCoeff);
    const auto pulse = formantSourceState;
    const auto t = currentRamp;

    auto out = 0.0f;
    for (std::size_t i = 0; i < 3; ++i)
    {
        const auto a = previous.formant.a[i] + (target.formant.a[i] - previous.formant.a[i]) * t;
        const auto b = previous.formant.b[i] + (target.formant.b[i] - previous.formant.b[i]) * t;
        const auto c = previous.formant.c[i] + (target.formant.c[i] - previous.formant.c[i]) * t;
        const auto gain = previous.formant.gain[i] + (target.formant.gain[i] - previous.formant.gain[i]) * t;
        const auto y = a * pulse + b * formantY1[i] + c * formantY2[i];
        formantY2[i] = formantY1[i];
        formantY1[i] = std::isfinite(y) ? y : 0.0f;
        out += gain * formantY1[i];
    }

    return plainDelays[static_cast<std::size_t>(Mode::formant)].push(
        formantClip.process(kTanhAdaa, static_cast<double>(out * ramped(&DerivedCurves::formantTrim))));
}

double OscillatorUnit::renderFmCore(double& modulatorPhase,
                                    px3::dsp::HalfbandDecimator& decimator,
                                    const MainPhase& main,
                                    double ratio,
                                    double index)
{
    // Two points per sample, at t = n and n + 1/2, decimated: the sidebands that
    // would fold at 1x land below the doubled Nyquist, where the decimator
    // removes them. The index eases off only where even that would not be
    // enough (docs/OSCILLATOR_DSP_DESIGN.md, FM).
    const auto modulatorIncrement = main.increment * ratio;
    modulatorPhase = wrap(modulatorPhase + modulatorIncrement);
    const auto depth = px3::dsp::carsonLimitedIndex(index, main.increment, modulatorIncrement);

    const auto first = fastSine(main.phase + depth * kInverseTwoPi * fastSine(modulatorPhase));
    const auto second = fastSine(main.phase + 0.5 * main.increment
                                 + depth * kInverseTwoPi * fastSine(modulatorPhase + 0.5 * modulatorIncrement));
    return decimator.process(first, second);
}

double OscillatorUnit::renderHardSync(const MainPhase& main)
{
    const auto ratio = static_cast<double>(ramped(&DerivedCurves::hardSyncRatio));
    const auto slaveIncrement = main.increment * ratio;
    auto& slave = syncSlavePhase;

    if (main.wrapped && slaveIncrement > 0.0)
    {
        // The slave runs on up to the instant the master wraps - wrapping itself
        // if it gets there first - and restarts from zero AT that instant, not at
        // the next sample.
        auto atReset = slave + slaveIncrement * (1.0 - main.tau);
        while (atReset >= 1.0)
        {
            atReset -= 1.0;
            syncLine.step(main.tau + atReset / slaveIncrement, -2.0);
        }
        syncLine.step(main.tau, -2.0 * atReset);
        slave = slaveIncrement * main.tau;
        while (slave >= 1.0)
        {
            slave -= 1.0;
            syncLine.step(slave / slaveIncrement, -2.0);
        }
    }
    else
    {
        slave += slaveIncrement;
        while (slave >= 1.0)
        {
            slave -= 1.0;
            syncLine.step(slave / slaveIncrement, -2.0);
        }
    }

    const auto band = syncLine.push(2.0 * slave - 1.0);
    return syncClip.process(kTanhAdaa, band * static_cast<double>(ramped(&DerivedCurves::hardSyncDrive))) * 0.82;
}

double OscillatorUnit::renderOrgan(const RenderContext& context, const MainPhase& main)
{
    const auto drawbars = renderHarmonicSet(organPhases, previous.organ, target.organ, main.increment);

    const auto clickIncrement = main.increment * 9.0;
    organClickPhase = wrap(organClickPhase + clickIncrement);

    const auto click = static_cast<double>(ramped(&DerivedCurves::organClick));
    auto keyClick = 0.0;
    if (click > 0.0)
    {
        const auto exponent = static_cast<double>(context.noteAgeSamples) * inverseSampleRate
                              * static_cast<double>(ramped(&DerivedCurves::organClickDecayPerSecond));
        // Once the click is below -100 dB it is left uncomputed: exp() every
        // sample for the rest of the note was most of the cost of doing nothing.
        if (exponent < 11.512925464970229)   // exp(-11.51) = 1e-5
        {
            const auto envelope = std::exp(-exponent);
            keyClick = (static_cast<double>(noise.white()) * 0.08
                        + fastSine(organClickPhase) * px3::dsp::nyquistFade(clickIncrement) * 0.05)
                       * envelope * click;
        }
    }

    return plainDelays[static_cast<std::size_t>(Mode::organ)].push(
        organClip.process(kTanhAdaa, (drawbars + keyClick) * (1.05 + 0.07 * (1.0 - click))));
}

double OscillatorUnit::renderDigital(const MainPhase& main)
{
    // Intentional, and not band-limited: the hold, the phase quantisation, the
    // bit crush and the aliasing they make ARE the mode. What is no longer part
    // of it: a hold measured in samples (it changed character with the sample
    // rate), a floor() quantiser (half a step of DC), and a fold that read a
    // non-integer multiple of the wrapped phase (a jump every cycle).
    const auto hold = px3::dsp::sampleCount(static_cast<double>(ramped(&DerivedCurves::digitalHoldAt48k)), sampleRate);
    if (++digitalHoldCounter >= hold)
    {
        digitalHoldCounter = 0;
        const auto steps = static_cast<double>(target.digitalSteps);
        const auto quantised = std::round(main.phase * steps) / steps;

        // FOLD between two whole multiples of the cycle, crossfaded, so it is
        // continuous at the wrap for any fold amount.
        const auto fold = static_cast<double>(ramped(&DerivedCurves::digitalFold));
        const auto lower = std::floor(fold);
        const auto blend = fold - lower;
        const auto shaped = (1.0 - blend) * fastSine(lower * quantised)
                            + blend * fastSine((lower + 1.0) * quantised);

        const auto crush = static_cast<double>(target.digitalCrushSteps);
        digitalHeld = std::round(shaped * crush) / crush;
    }

    return plainDelays[static_cast<std::size_t>(Mode::digital)].push(digitalClip.process(kTanhAdaa, digitalHeld * 1.08));
}

double OscillatorUnit::renderPhysical(const MainPhase& main)
{
    // A synthetic modal resonator - not a physical model. Four sine modes at a
    // struck bar's partial ratios, each with a strike that decays to a held
    // level. The fundamental is always at the played pitch; MATERIAL spreads
    // only the modes above it.
    const auto spread = static_cast<double>(ramped(&DerivedCurves::physicalSpread));
    const auto t = currentRamp;

    double sum = 0.0;
    for (std::size_t i = 0; i < physicalPhases.size(); ++i)
    {
        const auto ratio = 1.0 + (kPhysicalRatios[i] - 1.0) * spread;
        const auto increment = main.increment * ratio;
        physicalPhases[i] = wrap(physicalPhases[i] + increment);

        const auto coeff = static_cast<double>(previous.physicalDecayCoeff[i]
                                               + (target.physicalDecayCoeff[i] - previous.physicalDecayCoeff[i]) * t);
        auto& envelope = physicalEnvelopes[i];
        envelope = kPhysicalSustain[i] + (envelope - kPhysicalSustain[i]) * coeff;

        sum += envelope * kPhysicalWeights[i] * px3::dsp::nyquistFade(increment) * fastSine(physicalPhases[i]);
    }

    return plainDelays[static_cast<std::size_t>(Mode::physical)].push(physicalClip.process(kTanhAdaa, sum * kPhysicalDrive));
}

double OscillatorUnit::renderRob(const RenderContext& context, const MainPhase& main)
{
    const auto trans = static_cast<double>(ramped(&DerivedCurves::robTrans));
    const auto body = static_cast<double>(ramped(&DerivedCurves::robBody));
    const auto chaos = static_cast<double>(ramped(&DerivedCurves::robChaos));
    const auto increment = main.increment;

    const auto advance = [this](std::size_t i, double by)
    {
        auto& p = robPhases[i];
        p = wrap(p + by);
        return p;
    };

    // The body runs at a stretched multiple of the note, and a slow wobble
    // phase-modulates everything built on it. Each multiple of the body is its
    // own accumulator - they used to be multiples of a wrapped angle, which
    // jumped once per cycle - and each fades out as its sidebands near Nyquist.
    const auto bodyIncrement = increment * (1.0 + chaos * 1.05 + body * 0.45);
    const auto wobbleIncrement = increment * (3.5 + chaos * 10.0);
    const auto wobbleDepth = chaos * 0.40;   // radians: nothing at all at CHAOS 0
    const auto wobble = fastSine(advance(1, wobbleIncrement)) * wobbleDepth;

    const auto component = [&](std::size_t i, double multiple, double extraBandwidth = 0.0)
    {
        const auto p = advance(i, bodyIncrement * multiple);
        const auto top = bodyIncrement * multiple + (multiple * wobbleDepth + 1.0) * wobbleIncrement + extraBandwidth;
        return fastSine(p + multiple * wobble * kInverseTwoPi) * static_cast<double>(px3::dsp::nyquistFade(top));
    };

    const auto fundamental = component(0, 1.0);
    const auto sub = component(2, 0.5) * (0.12 + body * 0.42);
    const auto second = component(3, 1.34 + body * 1.10) * (0.08 + body * 0.34);
    const auto third = component(4, 2.00 + body * 2.05) * (0.03 + body * 0.22);
    auto bodySignal = fundamental * (0.42 + body * 0.52) + sub + second + third;
    bodySignal = robBodyClip.process(kTanhAdaa, bodySignal * (1.12 + body * 2.40));

    // TRANS: the smack at the front of the note, decaying in seconds.
    robTransient *= static_cast<double>(previous.robTransientCoeff
                                        + (target.robTransientCoeff - previous.robTransientCoeff) * currentRamp);
    const auto clickTone = component(5, 9.0 + trans * 46.0);
    const auto clickMix = 0.25 + (0.80 - 0.25) * trans;
    const auto clickCore = clickTone * (1.0 - clickMix) + static_cast<double>(noise.white()) * clickMix;
    const auto smack = clickCore * robTransient * (0.04 + (2.30 - 0.04) * trans);

    const auto attackSamples = px3::dsp::sampleCount(10.0 + trans * 86.0, sampleRate);
    auto onset = 0.0;
    if (context.noteAgeSamples < attackSamples)
    {
        auto envelope = 1.0 - static_cast<double>(context.noteAgeSamples) / static_cast<double>(attackSamples);
        envelope *= envelope;
        onset = static_cast<double>(noise.white()) * envelope * (1.25 * trans);
    }

    const auto edgeShaper = robEdgeClip.process(kTanhAdaa, bodySignal * (1.0 + trans * 3.8));
    const auto edgeCarrier = component(6, 5.0 + trans * 22.0 + chaos * 24.0);
    const auto edge = (edgeShaper - bodySignal) * (0.08 + trans * 0.60) + edgeCarrier * (0.01 + trans * 0.22);

    // CHAOS: every part of it scales with the knob, so at zero there is none.
    auto chaosSignal = 0.0;
    if (chaos > 0.0)
    {
        const auto warpInner = fastSine(advance(8, increment * (11.0 + chaos * 27.0)));
        const auto warpMultiple = 3.0 + chaos * 9.0;
        const auto warp = fastSine(advance(7, bodyIncrement * warpMultiple) + (warpMultiple * wobble + warpInner) * kInverseTwoPi);
        const auto warpDepth = 0.6 + chaos * 2.4;
        const auto rate = 6.0 + chaos * 32.0;
        const auto tone = component(9, rate, (warpDepth + 1.0) * bodyIncrement * warpMultiple);
        const auto warped = std::sin(std::asin(juce::jlimit(-1.0, 1.0, tone)) + warp * warpDepth);
        chaosSignal = warped * (0.39 * chaos) + static_cast<double>(noise.white()) * (0.24 * chaos);
    }

    return plainDelays[static_cast<std::size_t>(Mode::rob)].push(
        robOutClip.process(kTanhAdaa, (bodySignal + smack + onset + edge + chaosSignal) * 0.86));
}

double OscillatorUnit::renderPx3(const MainPhase& main)
{
    // A hybrid of three engines, each with one job per knob:
    //   MORPH (A)  the balance between the FM engine and the ISAAC partials
    //   CHAR  (B)  how hard the whole voice is pushed: the saw's drive and the
    //              FM index together
    //   MOVE  (C)  a slow movement of both, at 0.3 to 6 Hz
    const auto character = static_cast<double>(ramped(&DerivedCurves::px3Character));
    const auto movement = static_cast<double>(ramped(&DerivedCurves::px3Movement));

    px3MovePhase = wrap(px3MovePhase + (0.3 + 5.7 * movement) * inverseSampleRate);
    const auto lfo = fastSine(px3MovePhase);
    const auto morph = juce::jlimit(0.0, 1.0, static_cast<double>(ramped(&DerivedCurves::px3Morph)) + lfo * 0.25 * movement);

    // All three arrive at the common latency before they are mixed.
    const auto fmPart = renderFmCore(px3ModulatorPhase, px3Decimator, main, 2.0, 1.0 + 6.0 * character) * 0.70;

    const auto partials = renderHarmonicSet(px3IsaacPhases, previous.px3Isaac, target.px3Isaac, main.increment);
    px3ShimmerPhase = wrap(px3ShimmerPhase + juce::jlimit(0.0, 0.499, 0.5 * main.increment + kShimmerOffsetHz * inverseSampleRate));
    const auto shimmer = fastSine(px3ShimmerPhase) * 0.15 * (0.18 + 0.42 * movement);
    const auto isaacPart = px3IsaacDelay.push(px3IsaacClip.process(kTanhAdaa, partials + shimmer));

    if (main.wrapped)
    {
        px3SawLine.step(main.tau, -2.0);
    }
    const auto saw = px3SawLine.push(2.0 * main.phase - 1.0);
    const auto drive = (1.0 + 4.8 * character) * (1.0 + 0.3 * movement * lfo);
    const auto driven = px3SawClip.process(kTanhAdaa, saw * drive);

    const auto blendA = fmPart * (1.0 - morph) + isaacPart * morph;
    const auto blendB = driven * (0.45 + 0.45 * character);
    return px3OutClip.process(kTanhAdaa, blendA * 0.74 + blendB * 0.66) * 0.9;
}
