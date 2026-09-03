#include "OscillatorUnit.h"

#include <JuceHeader.h>

#include <algorithm>
#include <cmath>

namespace
{
inline float clamp01(float v)
{
    return juce::jlimit(0.0f, 1.0f, v);
}

inline float softClip(float x)
{
    return std::tanh(x);
}
}

void OscillatorUnit::setSettings(const OscillatorSettings& settings)
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

    // The table and the scan position are assigned BEFORE the guard below.
    // They are not derived state: the position moves under modulation on almost
    // every block, so including it in the comparison would rebuild the derived
    // curves constantly, and leaving it out of both would mean it never
    // arrived at all.
    oscillatorSettings.table = clamped.table;
    oscillatorSettings.wtPosition = juce::jlimit(0.0f, 1.0f, clamped.wtPosition);

    // The processor pushes settings to every voice on every block, including
    // the ones playing nothing, so this runs 192 times per block at full
    // polyphony. Rebuilding the derived curves unconditionally would move the
    // per-sample pow cost to a per-block cost rather than removing it; almost
    // always the settings are simply unchanged.
    if (derivedValid
        && clamped.modeIndex == oscillatorSettings.modeIndex
        && clamped.macroA == oscillatorSettings.macroA
        && clamped.macroB == oscillatorSettings.macroB
        && clamped.macroC == oscillatorSettings.macroC
        && clamped.vowelIndex == oscillatorSettings.vowelIndex
        && clamped.harmonics == oscillatorSettings.harmonics)
    {
        return;
    }

    oscillatorSettings = clamped;
    updateDerivedCurves();
    derivedValid = true;
}

// Reproduces the original per-sample expressions exactly, in the same order and
// at the same precision, so the rendered signal is unchanged bit for bit.
// Sines at different frequencies do not line up at their peaks, so the sum of
// the magnitudes is a bound that never occurs. Normalising by it made a rich
// registration quieter than a sparse one - the opposite of what adding partials
// should do. Root-sum-square is the level the sum actually has, with a little
// headroom left for the peaks that do coincide.
float OscillatorUnit::normaliseHarmonicSet(float energy)
{
    return std::sqrt(juce::jmax(1.0e-8f, energy)) * 1.35f;
}

OscillatorUnit::HarmonicSet OscillatorUnit::buildHarmonicSet(const std::array<float, 8>& harmonics,
                                                            float rolloffBias,
                                                            float oddEvenBias,
                                                            float inharmonicity)
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

        set.amplitude[static_cast<std::size_t>(i)] = amp;
        set.ratio[static_cast<std::size_t>(i)] = h * (1.0f + inharmonicity * 0.03f * h);
        energy += amp * amp;
    }

    set.norm = normaliseHarmonicSet(energy);
    return set;
}

float OscillatorUnit::readHarmonicSum(double currentAngle, const HarmonicSet& set)
{
    float sum = 0.0f;

    for (int i = 0; i < kHarmonicCount; ++i)
    {
        const auto v = std::sin(currentAngle * static_cast<double>(set.ratio[static_cast<std::size_t>(i)]));
        sum += set.amplitude[static_cast<std::size_t>(i)] * static_cast<float>(v);
    }

    // `norm` is an RMS-style figure, not the sum of the magnitudes. Dividing by
    // the sum was what made every harmonic mode collapse towards a sine: sines
    // at unrelated phases do not add to their peaks, so the sum is a worst case
    // that never happens, and the more partials a registration had the quieter
    // and flatter it got. Measured, ORGAN and ADDITIVE both sat within 10 dB of
    // a pure sine.
    return set.norm > 0.0001f ? sum / set.norm : 0.0f;
}

void OscillatorUnit::updateDerivedCurves()
{
    const auto a = oscillatorSettings.macroA;
    const auto b = oscillatorSettings.macroB;
    const auto c = oscillatorSettings.macroC;

    // SPREAD is the whole supersaw control: it sets how far apart the stacked
    // saws sit AND how much they drift. There used to be a separate DETUNE
    // macro for the spacing, which is what "spread" already means. Both read
    // macro A, because the UI shows a mode's macros in slot order and SPREAD is
    // now supersaw's first and only one.
    derived.superSawSpread = std::pow(a, 1.65f);
    derived.superSawWidth = std::pow(a, 1.2f);
    derived.superSawEdgeSoft = 0.58f + 0.42f * (1.0f - derived.superSawWidth);
    for (std::size_t i = 0; i < superSawOffsets.size(); ++i)
    {
        const auto spreadSemitones = superSawOffsets[i] * (0.04f + 16.0f * derived.superSawSpread);
        derived.superSawRatios[i] = std::pow(2.0, static_cast<double>(spreadSemitones) / 12.0);
    }

    derived.pwmWidthCurve = std::pow(a, 1.15f);

    // The top of this range used to be 2.55, which puts the 8th harmonic 45 dB
    // down: opening macro A all the way turned ADDITIVE - and ISAAC, which
    // shares the curve - into a sine. Measured at -23.9 dB of overtones against
    // a sine's -24.6. The knob still runs bright to mellow, it just no longer
    // runs all the way to nothing.
    derived.additiveRolloff = juce::jmap(std::pow(a, 1.15f), 0.25f, 1.15f);
    derived.additiveOddEven = juce::jmap(std::pow(b, 1.1f), -0.65f, 0.65f);
    const auto inharmonicity = juce::jmap(std::pow(c, 1.2f), 0.0f, 1.1f);
    derived.additiveStatic = buildHarmonicSet(oscillatorSettings.harmonics,
                                              derived.additiveRolloff,
                                              derived.additiveOddEven,
                                              0.0f);
    derived.additiveDynamic = buildHarmonicSet(oscillatorSettings.harmonics,
                                               derived.additiveRolloff,
                                               derived.additiveOddEven,
                                               inharmonicity);

    {
        // Measured formant frequencies for the five cardinal vowels, in hertz,
        // with the bandwidth of each resonance and its relative level. These are
        // the standard values the speech-synthesis literature uses for an adult
        // male tract (Peterson & Barney / Klatt); the bandwidths widen with
        // frequency the way real ones do.
        //
        // The previous implementation stored fixed amplitudes for harmonics
        // 1..8 instead. That cannot be a vowel: a formant is a resonance of the
        // tract, so it stays at the same frequency whatever note is played,
        // which is what makes an "ah" still an "ah" an octave up. Weighting
        // harmonics instead pinned the spectral peak to the note, so it moved
        // with pitch and read as a dull static timbre. It measured 2 to 3
        // audible partials, within a few dB of a sine.
        struct Vowel { float f1, f2, f3, b1, b2, b3, a1, a2, a3; };
        static constexpr std::array<Vowel, 5> kVowels { {
            //  F1     F2     F3     B1     B2     B3    A1    A2     A3
            {  730.f, 1090.f, 2440.f,  70.f, 110.f, 170.f, 1.0f, 0.50f, 0.28f },  // AH
            {  530.f, 1840.f, 2480.f,  60.f, 100.f, 160.f, 1.0f, 0.45f, 0.30f },  // EH
            {  270.f, 2290.f, 3010.f,  55.f, 100.f, 180.f, 1.0f, 0.35f, 0.25f },  // EE
            {  570.f,  840.f, 2410.f,  70.f,  95.f, 160.f, 1.0f, 0.55f, 0.18f },  // OH
            {  300.f,  870.f, 2240.f,  55.f,  90.f, 160.f, 1.0f, 0.40f, 0.14f },  // OO
        } };

        // Macro A glides between vowels. Interpolating the FREQUENCIES is what
        // makes that a vowel glide rather than a crossfade of two timbres.
        const auto morph = juce::jlimit(0.0f, 1.0f, a) * 4.0f;
        const auto lower = juce::jlimit(0, 4, static_cast<int>(std::floor(morph)));
        const auto upper = juce::jlimit(0, 4, lower + 1);
        const auto frac = morph - static_cast<float>(lower);

        const auto& v0 = kVowels[static_cast<std::size_t>(
            juce::jlimit(0, 4, oscillatorSettings.vowelIndex))];
        const auto& v1 = kVowels[static_cast<std::size_t>(upper)];
        const auto& vLow = kVowels[static_cast<std::size_t>(lower)];
        juce::ignoreUnused(vLow);

        const auto mix = [frac](float from, float to) { return from + (to - from) * frac; };

        // Macro B is the tract LENGTH: shifting every formant together is what
        // takes a voice from large to small, and it is the one control that
        // stays musical across the whole range.
        const auto shift = juce::jmap(juce::jlimit(0.0f, 1.0f, b), 0.72f, 1.55f);

        const std::array<float, 3> freq {
            mix(v0.f1, v1.f1) * shift, mix(v0.f2, v1.f2) * shift, mix(v0.f3, v1.f3) * shift };
        const std::array<float, 3> bw {
            mix(v0.b1, v1.b1), mix(v0.b2, v1.b2), mix(v0.b3, v1.b3) };
        const std::array<float, 3> amp {
            mix(v0.a1, v1.a1), mix(v0.a2, v1.a2), mix(v0.a3, v1.a3) };

        const auto rate = static_cast<float>(juce::jmax(1000.0, preparedSampleRate));

        // One-pole tilt on the excitation, fixed in hertz so it does not move
        // with the note.
        const auto tiltHz = 260.0f;
        derived.formantSourceCoeff =
            juce::jlimit(0.0005f, 0.9f, 1.0f - std::exp(-juce::MathConstants<float>::twoPi * tiltHz / rate));
        derived.formantTrim = 2.2f;

        for (int i = 0; i < 3; ++i)
        {
            const auto idx = static_cast<std::size_t>(i);
            // The standard two-pole resonator: a pole pair at the formant
            // frequency whose radius sets the bandwidth. One biquad per
            // formant, run in parallel and summed.
            const auto f = juce::jlimit(20.0f, rate * 0.45f, freq[idx]);
            const auto r = std::exp(-juce::MathConstants<float>::pi * bw[idx] / rate);
            const auto cosw = std::cos(juce::MathConstants<float>::twoPi * f / rate);

            derived.formant.b[idx] = 2.0f * r * cosw;
            derived.formant.c[idx] = -r * r;
            // Normalised so each resonator peaks at unity rather than at a gain
            // that swings with its bandwidth - otherwise the vowel's balance
            // changes as the tract is resized.
            derived.formant.a[idx] = (1.0f - r) * std::sqrt(1.0f - 2.0f * r * cosw + r * r);
            // Alternating sign, as parallel formant synthesisers have always
            // done: in phase, the skirts of neighbouring resonators cancel in
            // the valleys between them and hollow the vowel out.
            derived.formant.gain[idx] = (i == 1 ? -amp[idx] : amp[idx]);
        }

    }

    {
        // The nine Hammond drawbar footages, as pitch ratios against the note:
        // 16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1'. The sub at 0.5
        // and the quint at 1.5 are not harmonics of the note, and they are a
        // large part of why the instrument sounds like an organ and not like a
        // stack of sines. The old table used 1..8 - plain integer harmonics -
        // and then applied pow(1/h, up to 1.8) on top, which put the 8th
        // partial 41 dB down and left the fundamental alone: measured, it was
        // within 10 dB of a sine and got THINNER as the macro opened.
        static constexpr std::array<float, kHarmonicCount> kDrawbarRatios {
            0.5f, 1.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f };

        // Two registrations, crossfaded by macro A - which is what an organist
        // actually changes. Mellow is the classic 88 8000 000 flutes; bright
        // pulls the upper drawbars out for the full 88 8888 888.
        static constexpr std::array<float, kHarmonicCount> kMellow {
            0.85f, 0.30f, 1.00f, 0.55f, 0.16f, 0.10f, 0.05f, 0.04f, 0.03f };
        static constexpr std::array<float, kHarmonicCount> kBright {
            0.80f, 0.70f, 1.00f, 0.85f, 0.72f, 0.66f, 0.55f, 0.50f, 0.45f };

        const auto tone = juce::jlimit(0.0f, 1.0f, a);
        derived.organClick = std::pow(b, 1.2f);
        derived.organClickDecay = 0.0006f + 0.003f * derived.organClick;

        derived.organ = HarmonicSet {};
        auto energy = 0.0f;
        for (int i = 0; i < kHarmonicCount; ++i)
        {
            const auto idx = static_cast<std::size_t>(i);
            const auto drawbar = kMellow[idx] + (kBright[idx] - kMellow[idx]) * tone;
            // The per-oscillator harmonic sliders still trim the registration,
            // but they can no longer silence it: a drawbar at rest is a real
            // registration, not an empty one.
            const auto trim = i < 8 ? (0.55f + oscillatorSettings.harmonics[idx] * 0.75f) : 1.0f;
            const auto amp = drawbar * trim;
            derived.organ.amplitude[idx] = amp;
            derived.organ.ratio[idx] = kDrawbarRatios[idx];
            energy += amp * amp;
        }
        derived.organ.norm = normaliseHarmonicSet(energy);
    }

    derived.fmRatio = std::pow(2.0f, juce::jmap(std::pow(a, 1.1f), -1.6f, 2.2f));
    derived.fmIndex = juce::jmap(std::pow(b, 1.35f), 0.0f, 10.0f);
    derived.fmOutputScale = 0.66f + 0.05f * (1.0f - b);

    derived.hardSyncRatio = juce::jmap(std::pow(a, 1.3f), 1.0f, 11.0f);
    derived.hardSyncDrive = 1.0f + std::pow(b, 1.15f) * 2.3f;

    derived.karplusDecay = juce::jmap(std::pow(a, 1.85f), 0.90f, 0.99945f);
    derived.karplusBrightness = juce::jmap(std::pow(b, 1.2f), 0.03f, 0.94f);

    {
        const auto bitsCurve = std::pow(a, 1.25f);
        const auto rateCurve = std::pow(b, 1.15f);
        derived.digitalBitDepth = juce::jlimit(2, 16, static_cast<int>(std::round(juce::jmap(bitsCurve, 2.0f, 16.0f))));
        derived.digitalHoldSamples = juce::jlimit(1, 64, static_cast<int>(std::round(juce::jmap(rateCurve, 1.0f, 52.0f))));
        derived.digitalAliasFold = 1.0 + static_cast<double>(juce::jmap(rateCurve, 0.4f, 6.8f));
        derived.digitalSteps = static_cast<float>(1 << juce::jlimit(1, 20, derived.digitalBitDepth));
        derived.digitalCrushSteps = static_cast<float>(1 << juce::jlimit(1, 14, derived.digitalBitDepth - 1));
    }

    derived.physicalDamping = juce::jmap(std::pow(a, 1.6f), 0.9995f, 0.9957f);
    derived.physicalMaterial = juce::jmap(std::pow(b, 1.2f), 0.85f, 2.7f);

    derived.robTrans = std::pow(a, 0.55f);
    derived.robBody = std::pow(b, 0.72f);
    derived.robChaos = std::pow(c, 0.80f);

    derived.px3Morph = std::pow(a, 1.1f);
    derived.px3Character = std::pow(b, 1.2f);
    derived.px3Movement = std::pow(c, 1.1f);

    derived.wavetablePos = std::pow(a, 1.1f);

    // Per-mode output trim, previously recomputed after every rendered sample.
    const auto mode = static_cast<px3::OscillatorMode>(oscillatorSettings.modeIndex);
    derived.modeGainTrim = 1.0f;
    if (mode == px3::OscillatorMode::superSaw)
    {
        derived.modeGainTrim = juce::jmap(std::pow(a, 1.35f), 1.0f, 0.84f);
    }
    else if (mode == px3::OscillatorMode::fm)
    {
        derived.modeGainTrim = juce::jmap(std::pow(b, 1.2f), 1.0f, 0.82f);
    }
    else if (mode == px3::OscillatorMode::hardSync)
    {
        derived.modeGainTrim = juce::jmap(std::pow(b, 1.18f), 1.0f, 0.78f);
    }
    else if (mode == px3::OscillatorMode::digital)
    {
        derived.modeGainTrim = juce::jmap(std::pow(b, 1.1f), 1.0f, 0.86f);
    }
    else if (mode == px3::OscillatorMode::rob || mode == px3::OscillatorMode::px3)
    {
        derived.modeGainTrim = juce::jmap(std::pow(c, 1.12f), 1.0f, 0.84f);
    }
}

void OscillatorUnit::prepare(double sampleRate)
{
    // One-pole toward the target. Expressed as a time so it means the same
    // thing at every sample rate.
    wtPositionCoeff = sampleRate > 0.0
                        ? static_cast<float>(1.0 - std::exp(-1.0 / (kWtPositionSmoothingSeconds * sampleRate)))
                        : 1.0f;

    const auto safeRate = juce::jmax(1.0, sampleRate);
    if (! juce::approximatelyEqual(preparedSampleRate, safeRate))
    {
        preparedSampleRate = safeRate;
        derivedValid = false;   // the formant resonators are designed in hertz
    }
    const auto required = static_cast<int>(std::ceil(safeRate / kKarplusLowestFrequencyHz)) + 4;
    if (static_cast<int>(karplusBuffer.size()) != required)
    {
        karplusBuffer.assign(static_cast<std::size_t>(required), 0.0f);
    }
    karplusWriteIndex = 0;
    karplusLastSample = 0.0f;
}

void OscillatorUnit::resetForNote(double sampleRate, double currentFrequencyHz)
{
    // Start AT the position rather than gliding to it from wherever the last
    // note left off, which would make the first few milliseconds of every note
    // depend on the one before it.
    smoothedWtPosition = oscillatorSettings.wtPosition;
    wavetableReader.reset();

    for (std::size_t i = 0; i < superSawAngles.size(); ++i)
    {
        const auto r = juce::Random::getSystemRandom().nextDouble();
        superSawAngles[i] = r * juce::MathConstants<double>::twoPi;
        superSawDrift[i] = juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f;
    }

    fmModAngle = 0.0;
    syncMasterAngle = 0.0;
    syncSlaveAngle = 0.0;
    digitalHoldCounter = 0;
    digitalHeldSample = 0.0f;

    for (auto& s : pinkState)
    {
        s = 0.0f;
    }
    noiseColorState = 0.0f;
    pinkColorState = 0.0f;

    const auto safeRate = juce::jmax(1.0, sampleRate);
    const auto frequency = juce::jmax(kKarplusLowestFrequencyHz, currentFrequencyHz);
    const auto bufferSamples = static_cast<int>(karplusBuffer.size());
    karplusDelaySamples = bufferSamples > 16
                              ? juce::jlimit(8,
                                             bufferSamples - 2,
                                             static_cast<int>(std::round(safeRate / frequency)))
                              : 8;
    karplusLastSample = 0.0f;

    // Clear the whole delay line before seeding the excitation. Without this a
    // reused voice starts its pluck on top of the previous note's residue: the
    // read tap wraps into never-rewritten samples during the first `delay`
    // samples, so the attack depended on both the buffer length and whatever
    // played on that voice before. Clearing makes each note deterministic.
    std::fill(karplusBuffer.begin(), karplusBuffer.end(), 0.0f);

    for (int i = 0; i < karplusDelaySamples && i < static_cast<int>(karplusBuffer.size()); ++i)
    {
        const auto n = juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f;
        karplusBuffer[static_cast<std::size_t>(i)] = n * 0.5f;
    }

    // Start writing one delay period in, so the read tap - which trails the
    // write tap by exactly that much - begins on the excitation just seeded.
    //
    // Starting both taps at 0 meant the write tap marched across indices
    // 0..delay-1 overwriting the burst during the very samples the read tap was
    // still traversing the untouched far end of the buffer. By the time the
    // read tap reached index 0 the burst was gone, and the only excitation left
    // was the handful of note-age noise samples renderKarplus adds - roughly a
    // twentieth of the intended amplitude, which is why KARPLUS was audible
    // only as a faint click. The buffer is sized for the lowest supported note,
    // so it is longer than the delay and there is room for both taps.
    karplusWriteIndex = bufferSamples > 0 ? karplusDelaySamples % bufferSamples : 0;

    for (std::size_t i = 0; i < physicalState.size(); ++i)
    {
        physicalState[i] = juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f;
        physicalPhase[i] = juce::Random::getSystemRandom().nextDouble() * juce::MathConstants<double>::twoPi;
    }
    // A resonator carrying the tail of the previous note would start the new
    // one mid-vowel.
    formantY1.fill(0.0f);
    formantY2.fill(0.0f);
    formantSourceState = 0.0f;
}

float OscillatorUnit::nextDeterministicNoise()
{
    noiseSeed = noiseSeed * 1664525u + 1013904223u;
    const auto bits = static_cast<int32_t>((noiseSeed >> 9) & 0x007FFFFFu);
    return (static_cast<float>(bits) / 4194303.5f) * 2.0f - 1.0f;
}

float OscillatorUnit::renderPinkNoise(float white)
{
    pinkState[0] = 0.99886f * pinkState[0] + white * 0.0555179f;
    pinkState[1] = 0.99332f * pinkState[1] + white * 0.0750759f;
    pinkState[2] = 0.96900f * pinkState[2] + white * 0.1538520f;
    pinkState[3] = 0.86650f * pinkState[3] + white * 0.3104856f;
    pinkState[4] = 0.55000f * pinkState[4] + white * 0.5329522f;
    pinkState[5] = -0.7616f * pinkState[5] - white * 0.0168980f;
    const auto pink = pinkState[0] + pinkState[1] + pinkState[2] + pinkState[3] + pinkState[4] + pinkState[5] + pinkState[6] + white * 0.5362f;
    pinkState[6] = white * 0.115926f;
    return pink * 0.11f;
}

float OscillatorUnit::renderSuperSaw(double sampleRate, const RenderContext& context)
{
    const auto width = derived.superSawWidth;
    float sum = 0.0f;

    for (std::size_t i = 0; i < superSawAngles.size(); ++i)
    {
        const auto driftHz = superSawDrift[i] * (0.03 + 0.95 * width);
        const auto freq = juce::jmax(8.0, context.currentFrequencyHz * derived.superSawRatios[i] + driftHz);
        const auto delta = juce::MathConstants<double>::twoPi * freq / sampleRate;

        superSawAngles[i] += delta;
        if (superSawAngles[i] >= juce::MathConstants<double>::twoPi)
        {
            superSawAngles[i] -= juce::MathConstants<double>::twoPi;
        }

        const auto phase = static_cast<float>(superSawAngles[i] / juce::MathConstants<double>::twoPi);
        const auto saw = phase * 2.0f - 1.0f;
        sum += softClip(saw * derived.superSawEdgeSoft * 1.35f);
    }

    // Left as two multiplies in the original order: folding them into one
    // cached scale changes the association and with it the last bit.
    return sum * (1.0f / 7.0f) * (0.84f + 0.10f * width);
}

float OscillatorUnit::renderPwm(const RenderContext& context) const
{
    const auto widthCurve = derived.pwmWidthCurve;
    const auto width = juce::jlimit(0.08f,
                                    0.92f,
                                    0.1f + widthCurve * 0.8f + (context.pwmModWheelNorm - 0.5f) * 0.14f);
    const auto phase = static_cast<float>(context.currentAngle / juce::MathConstants<double>::twoPi);
    return phase < width ? 1.0f : -1.0f;
}

float OscillatorUnit::renderAdditive(const RenderContext& context, bool dynamic)
{
    const auto base = readHarmonicSum(context.currentAngle,
                                      dynamic ? derived.additiveDynamic : derived.additiveStatic);

    if (!dynamic)
    {
        return base;
    }

    const auto shimmer = std::sin(context.currentAngle * 0.5 + static_cast<double>(context.noteAgeSamples) * 0.0007) * 0.15f;
    return softClip(static_cast<float>(base + shimmer * (0.18f + oscillatorSettings.macroC * 0.42f)));
}

float OscillatorUnit::renderWavetable(double sampleRate, const RenderContext& context)
{
    const auto* table = oscillatorSettings.table;
    if (table == nullptr || sampleRate <= 0.0)
    {
        // No table loaded yet. Silence rather than a fallback waveform: a mode
        // that quietly plays something else is harder to diagnose than one that
        // plays nothing.
        return 0.0f;
    }

    smoothedWtPosition += (oscillatorSettings.wtPosition - smoothedWtPosition) * wtPositionCoeff;

    // Phase comes from the voice, which already owns it and already wraps it.
    // Deriving it here rather than keeping a second accumulator is what makes
    // phase continuous through a scan for free - there is nothing to reset.
    const auto phase = context.currentAngle / juce::MathConstants<double>::twoPi;
    const auto increment = context.currentFrequencyHz / sampleRate;

    return wavetableReader.read(*table, phase, smoothedWtPosition, increment);
}

float OscillatorUnit::renderFm(double sampleRate, const RenderContext& context)
{
    const auto ratio = derived.fmRatio;
    const auto index = derived.fmIndex;

    fmModAngle += juce::MathConstants<double>::twoPi * (context.currentFrequencyHz * static_cast<double>(ratio)) / sampleRate;
    if (fmModAngle >= juce::MathConstants<double>::twoPi)
    {
        fmModAngle -= juce::MathConstants<double>::twoPi;
    }

    const auto mod = std::sin(fmModAngle) * index;
    const auto sample = std::sin(context.currentAngle + mod);
    return static_cast<float>(sample) * derived.fmOutputScale;
}

float OscillatorUnit::renderHardSync(double sampleRate, const RenderContext& context)
{
    const auto ratio = derived.hardSyncRatio;
    syncMasterAngle += juce::MathConstants<double>::twoPi * context.currentFrequencyHz / sampleRate;

    if (syncMasterAngle >= juce::MathConstants<double>::twoPi)
    {
        syncMasterAngle -= juce::MathConstants<double>::twoPi;
        syncSlaveAngle = 0.0;
    }

    syncSlaveAngle += juce::MathConstants<double>::twoPi * context.currentFrequencyHz * ratio / sampleRate;
    if (syncSlaveAngle >= juce::MathConstants<double>::twoPi)
    {
        syncSlaveAngle -= juce::MathConstants<double>::twoPi;
    }

    const auto slavePhase = static_cast<float>(syncSlaveAngle / juce::MathConstants<double>::twoPi);
    const auto synced = slavePhase * 2.0f - 1.0f;
    const auto drive = derived.hardSyncDrive;
    return softClip(synced * drive) * 0.82f;
}

float OscillatorUnit::renderKarplus(const RenderContext& context)
{
    const auto decay = derived.karplusDecay;
    const auto brightness = derived.karplusBrightness;

    const auto bufferSamples = static_cast<int>(karplusBuffer.size());
    if (bufferSamples <= 16)
    {
        return 0.0f;
    }
    const auto readIndex = (karplusWriteIndex - karplusDelaySamples + bufferSamples) % bufferSamples;
    const auto delayed = karplusBuffer[static_cast<std::size_t>(readIndex)];
    const auto filtered = brightness * delayed + (1.0f - brightness) * karplusLastSample;
    karplusLastSample = filtered;

    const auto excite = context.noteAgeSamples < 8 ? nextDeterministicNoise() * 0.18f : 0.0f;
    const auto writeSample = (filtered + excite) * decay;
    karplusBuffer[static_cast<std::size_t>(karplusWriteIndex)] = writeSample;
    karplusWriteIndex = (karplusWriteIndex + 1) % bufferSamples;

    return delayed * (1.28f + 0.08f * brightness);
}

float OscillatorUnit::renderFormant(double sampleRate, const RenderContext& context)
{
    // The excitation is a band-limited impulse train: flat up to its highest
    // harmonic and silent above it, so it can drive a 3 kHz formant without
    // aliasing. A harmonic set could not - eight harmonics of a low note do not
    // reach F2, let alone F3, so there was nothing at those frequencies for a
    // resonance to find even in principle.
    //
    // Closed form rather than a summed series: sin(N*x/2) / (N*sin(x/2)) is the
    // Dirichlet kernel, N equal-amplitude harmonics for the price of two sines.
    const auto rate = juce::jmax(1000.0, sampleRate);
    const auto f0 = juce::jmax(20.0, context.currentFrequencyHz);
    auto harmonics = static_cast<int>(rate * 0.45 / f0);
    harmonics = juce::jlimit(1, 400, harmonics);
    // Odd, so the kernel is symmetric about the pulse and carries no DC step.
    if ((harmonics % 2) == 0) --harmonics;
    const auto n = static_cast<double>(juce::jmax(1, harmonics));

    const auto half = context.currentAngle * 0.5;
    const auto denominator = std::sin(half);
    // Deliberately NOT divided by n. The kernel with the 1/n in it has each
    // harmonic at about 2/n, so a low note - which needs the most harmonics to
    // reach F3 without aliasing - excited the resonators with almost nothing
    // and the mode rendered at an RMS of 0.003. Dropping the 1/n gives every
    // harmonic unit amplitude whatever the pitch, which is also what keeps the
    // vowel at a steady level across the keyboard.
    const auto raw = std::abs(denominator) < 1.0e-7
                         ? n
                         : std::sin(n * half) / denominator;

    // A real glottal source is not flat - it falls steeply with frequency, and
    // a flat one makes F2 and F3 shout over F1. This is the -12 dB/octave tilt
    // the speech literature models, applied as a one-pole on the excitation.
    formantSourceState += (static_cast<float>(raw) - formantSourceState) * derived.formantSourceCoeff;
    const auto pulse = formantSourceState;

    auto out = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        const auto idx = static_cast<std::size_t>(i);
        const auto y = derived.formant.a[idx] * pulse
                       + derived.formant.b[idx] * formantY1[idx]
                       + derived.formant.c[idx] * formantY2[idx];
        formantY2[idx] = formantY1[idx];
        formantY1[idx] = std::isfinite(y) ? y : 0.0f;
        out += derived.formant.gain[idx] * formantY1[idx];
    }

    // The resonators are individually normalised, so the sum needs a single
    // trim rather than a per-vowel one.
    return softClip(out * derived.formantTrim);
}

float OscillatorUnit::renderOrgan(const RenderContext& context)
{
    const auto click = derived.organClick;
    const auto organ = readHarmonicSum(context.currentAngle, derived.organ);

    const auto clickEnv = std::exp(-static_cast<float>(context.noteAgeSamples) * derived.organClickDecay);
    const auto keyClick = (nextDeterministicNoise() * 0.08f + std::sin(context.currentAngle * 9.0) * 0.05f) * clickEnv * click;
    return softClip(static_cast<float>((organ + keyClick) * (1.05f + 0.07f * (1.0f - click))));
}

float OscillatorUnit::renderDigital(double sampleRate, const RenderContext& context)
{
    digitalHoldSamples = derived.digitalHoldSamples;

    if (++digitalHoldCounter >= digitalHoldSamples)
    {
        digitalHoldCounter = 0;
        const auto phase = static_cast<float>(context.currentAngle / juce::MathConstants<double>::twoPi);
        const auto steps = derived.digitalSteps;
        const auto quantizedPhase = std::floor(phase * steps) / juce::jmax(2.0f, steps - 1.0f);
        const auto aliasFold = derived.digitalAliasFold;
        const auto aliased = std::sin(static_cast<double>(quantizedPhase) * juce::MathConstants<double>::twoPi * aliasFold);
        const auto crushSteps = derived.digitalCrushSteps;
        digitalHeldSample = std::floor(static_cast<float>(aliased) * crushSteps) / juce::jmax(2.0f, crushSteps);
    }

    juce::ignoreUnused(sampleRate);
    return softClip(digitalHeldSample * 1.08f);
}

float OscillatorUnit::renderPhysical(double sampleRate, const RenderContext& context)
{
    const auto damping = derived.physicalDamping;
    const auto material = derived.physicalMaterial;

    const std::array<double, 4> ratios { 1.0, 2.32, 3.91, 5.48 };
    float sum = 0.0f;

    for (std::size_t i = 0; i < ratios.size(); ++i)
    {
        const auto freq = context.currentFrequencyHz * ratios[i] * material;
        physicalPhase[i] += juce::MathConstants<double>::twoPi * freq / sampleRate;
        if (physicalPhase[i] >= juce::MathConstants<double>::twoPi)
        {
            physicalPhase[i] -= juce::MathConstants<double>::twoPi;
        }

        const auto excite = context.noteAgeSamples < 10 ? nextDeterministicNoise() * 0.22f : 0.0f;
        physicalState[i] = physicalState[i] * damping + static_cast<float>(std::sin(physicalPhase[i])) * 0.012f + excite * (0.02f / static_cast<float>(i + 1));
        sum += physicalState[i] * (0.72f / static_cast<float>(i + 1));
    }

    return softClip(sum * 1.95f);
}

float OscillatorUnit::renderRobOsc(double sampleRate, const RenderContext& context)
{
    const auto transCurve = derived.robTrans;
    const auto bodyCurve = derived.robBody;
    const auto chaosCurve = derived.robChaos;
    const auto transientDecay = juce::jmap(transCurve, 0.085f, 0.012f);
    const auto transient = std::exp(-static_cast<float>(context.noteAgeSamples) * transientDecay);
    const auto bodyPhase = context.currentAngle * (1.0 + chaosCurve * 1.05 + bodyCurve * 0.45)
                           + std::sin(context.currentAngle * (3.5 + chaosCurve * 10.0)) * (0.02 + chaosCurve * 0.38);

    const auto bodyFund = std::sin(bodyPhase);
    const auto bodySub = std::sin(bodyPhase * 0.5) * (0.12f + bodyCurve * 0.42f);
    const auto bodySecond = std::sin(bodyPhase * (1.34 + bodyCurve * 1.10)) * (0.08f + bodyCurve * 0.34f);
    const auto bodyThird = std::sin(bodyPhase * (2.00 + bodyCurve * 2.05)) * (0.03f + bodyCurve * 0.22f);
    auto body = bodyFund * (0.42f + bodyCurve * 0.52f) + bodySub + bodySecond + bodyThird;
    body = std::tanh(body * (1.12f + bodyCurve * 2.40f));

    const auto clickTone = std::sin(bodyPhase * (9.0f + transCurve * 46.0f));
    const auto clickNoise = nextDeterministicNoise();
    const auto clickMix = juce::jmap(transCurve, 0.25f, 0.80f);
    const auto clickCore = clickTone * (1.0f - clickMix) + clickNoise * clickMix;
    const auto transientGain = juce::jmap(transCurve, 0.04f, 2.30f);
    const auto smack = clickCore * transient * transientGain;

    const auto attackSamples = juce::jlimit(10,
                                            96,
                                            static_cast<int>(10 + transCurve * 86.0f));
    float onsetEnv = 0.0f;
    if (context.noteAgeSamples < attackSamples)
    {
        onsetEnv = 1.0f - static_cast<float>(context.noteAgeSamples) / static_cast<float>(attackSamples);
        onsetEnv = onsetEnv * onsetEnv;
    }
    const auto onset = nextDeterministicNoise() * onsetEnv * juce::jmap(transCurve, 0.0f, 1.25f);

    const auto edgeShaper = std::tanh(body * (1.0f + transCurve * 3.8f));
    const auto edgeCarrier = std::sin(bodyPhase * (5.0f + transCurve * 22.0f + chaosCurve * 24.0f));
    const auto edge = (edgeShaper - body) * (0.08f + transCurve * 0.60f)
                      + edgeCarrier * (0.01f + transCurve * 0.22f);

    const auto chaosRate = 6.0 + chaosCurve * 32.0;
    const auto chaosWarp = std::sin(bodyPhase * (3.0 + chaosCurve * 9.0) + std::sin(context.currentAngle * (11.0 + chaosCurve * 27.0)));
    const auto chaosNoise = nextDeterministicNoise() * (0.02f + chaosCurve * 0.22f);
    const auto chaos = std::sin(bodyPhase * chaosRate + chaosWarp * (0.6f + chaosCurve * 2.4f)) * (0.05f + chaosCurve * 0.34f)
                       + chaosNoise;

    juce::ignoreUnused(sampleRate);
    return softClip(static_cast<float>((body + smack + onset + edge + chaos) * 0.86f));
}

float OscillatorUnit::renderPx3(double sampleRate, const RenderContext& context)
{
    const auto morph = derived.px3Morph;
    const auto character = derived.px3Character;
    const auto movement = derived.px3Movement;

    const auto fmPart = renderFm(sampleRate, context);
    const auto additivePart = renderAdditive(context, true);
    const auto foldedSaw = softClip(static_cast<float>((context.currentAngle / juce::MathConstants<double>::pi) - 1.0) * (1.0f + 4.8f * character));
    const auto ext = 0.0f;

    const auto blendA = fmPart * (1.0f - morph) + additivePart * morph;
    const auto blendB = foldedSaw * (0.45f + 0.45f * character) + ext;
    const auto movingPhase = std::sin(static_cast<double>(context.noteAgeSamples) * (0.0008 + movement * 0.0022));
    const auto px3 = softClip((blendA * 0.74f + blendB * 0.66f) + static_cast<float>(movingPhase) * 0.25f * movement);
    return px3 * 0.9f;
}

float OscillatorUnit::renderSample(double sampleRate, const RenderContext& context)
{
    const auto mode = static_cast<px3::OscillatorMode>(px3::clampOscillatorModeIndex(oscillatorSettings.modeIndex));
    const auto modeIndex = px3::clampOscillatorModeIndex(oscillatorSettings.modeIndex);
    const auto phase = static_cast<float>(context.currentAngle / juce::MathConstants<double>::twoPi);
    const auto external = 0.0f;

    float sample = 0.0f;

    // The four basic waveforms are computed inside their own cases rather than
    // up front. Hoisted out, every mode paid for the sine - a libm call per
    // sample, per oscillator, per voice - including the sixteen modes that
    // never read it.
    switch (mode)
    {
        case px3::OscillatorMode::sine:
            sample = static_cast<float>(std::sin(context.currentAngle));
            break;
        case px3::OscillatorMode::saw:
            sample = phase * 2.0f - 1.0f;
            break;
        case px3::OscillatorMode::square:
            sample = phase < 0.5f ? 1.0f : -1.0f;
            break;
        case px3::OscillatorMode::triangle:
            sample = 1.0f - 4.0f * std::abs(phase - 0.5f);
            break;
        case px3::OscillatorMode::noise:
        {
            const auto white = nextDeterministicNoise();
            const auto color = oscillatorSettings.macroA;
            const auto lpCoeff = juce::jmap(color, 0.02f, 0.48f);
            noiseColorState += (white - noiseColorState) * lpCoeff;
            sample = (noiseColorState * (1.0f - color) + white * color) * 0.78f;
            break;
        }
        case px3::OscillatorMode::pinkNoise:
        {
            const auto white = nextDeterministicNoise();
            auto pink = renderPinkNoise(white);
            const auto color = oscillatorSettings.macroA;
            const auto lpCoeff = juce::jmap(color, 0.01f, 0.30f);
            pinkColorState += (pink - pinkColorState) * lpCoeff;
            pink = pinkColorState * (1.0f - color) + pink * color;
            sample = pink * 1.45f;
            break;
        }
        case px3::OscillatorMode::superSaw:
            sample = renderSuperSaw(sampleRate, context);
            break;
        case px3::OscillatorMode::pwm:
            sample = renderPwm(context);
            break;
        case px3::OscillatorMode::wavetable:
            sample = renderWavetable(sampleRate, context);
            break;
        case px3::OscillatorMode::additive:
            sample = renderAdditive(context, false);
            break;
        case px3::OscillatorMode::formant:
            sample = renderFormant(sampleRate, context);
            break;
        case px3::OscillatorMode::fm:
            sample = renderFm(sampleRate, context);
            break;
        case px3::OscillatorMode::hardSync:
            sample = renderHardSync(sampleRate, context);
            break;
        case px3::OscillatorMode::karplus:
            sample = renderKarplus(context);
            break;
        case px3::OscillatorMode::organ:
            sample = renderOrgan(context);
            break;
        case px3::OscillatorMode::digital:
            sample = renderDigital(sampleRate, context);
            break;
        case px3::OscillatorMode::physical:
            sample = renderPhysical(sampleRate, context);
            break;
        case px3::OscillatorMode::rob:
            sample = renderRobOsc(sampleRate, context);
            break;
        case px3::OscillatorMode::isaac:
            sample = renderAdditive(context, true);
            break;
        case px3::OscillatorMode::px3:
            sample = renderPx3(sampleRate, context);
            break;
        default:
            break;
    }

    static constexpr std::array<float, 20> kModeTrim {
        0.82f, 0.74f, 0.72f, 0.78f, 0.64f,
        0.67f, 0.62f, 0.70f, 0.76f, 0.80f,
        0.73f, 0.64f, 0.60f, 0.78f, 0.76f,
        0.66f, 0.70f, 0.62f, 0.74f, 0.60f
    };

    auto modeGain = kModeTrim[static_cast<std::size_t>(modeIndex)];

    const auto macroEnergy = [this, mode]()
    {
        const auto a = oscillatorSettings.macroA;
        const auto b = oscillatorSettings.macroB;
        const auto c = oscillatorSettings.macroC;

        switch (mode)
        {
            case px3::OscillatorMode::sine:
            case px3::OscillatorMode::saw:
            case px3::OscillatorMode::square:
            case px3::OscillatorMode::triangle:
                return 0.5f;
            case px3::OscillatorMode::noise:
            case px3::OscillatorMode::pinkNoise:
            case px3::OscillatorMode::pwm:
            case px3::OscillatorMode::wavetable:
                return a;
            case px3::OscillatorMode::superSaw:
            case px3::OscillatorMode::karplus:
            case px3::OscillatorMode::organ:
            case px3::OscillatorMode::digital:
            case px3::OscillatorMode::physical:
            case px3::OscillatorMode::fm:
            case px3::OscillatorMode::hardSync:
            case px3::OscillatorMode::formant:
                return 0.5f * (a + b);
            case px3::OscillatorMode::additive:
            case px3::OscillatorMode::isaac:
            case px3::OscillatorMode::rob:
            case px3::OscillatorMode::px3:
                return (a + b + c) * (1.0f / 3.0f);
            default:
                return 0.5f;
        }
    }();

    static constexpr std::array<float, 20> kModeTravelSlope {
        0.00f, 0.08f, 0.10f, 0.06f, 0.16f,
        0.14f, 0.42f, 0.18f, 0.20f, 0.14f,
        0.20f, 0.38f, 0.46f, 0.16f, 0.14f,
        0.34f, 0.24f, 0.34f, 0.20f, 0.40f
    };

    const auto slope = kModeTravelSlope[static_cast<std::size_t>(modeIndex)];
    const auto centered = macroEnergy - 0.5f;
    modeGain *= 1.0f - slope * centered;

    // Mode-dependent macro trim, precomputed. It is 1.0 for every mode that had
    // no trim branch, so this multiply is the same value as before.
    modeGain *= derived.modeGainTrim;

    modeGain = juce::jlimit(0.45f, 1.08f, modeGain);
    sample *= modeGain;

    const auto wheelBlend = 0.15f + context.modWheelNorm * 0.22f;
    sample = sample * (1.0f - wheelBlend) + external * wheelBlend;
    juce::ignoreUnused(context.pitchRatio);
    return softClip(sample);
}
