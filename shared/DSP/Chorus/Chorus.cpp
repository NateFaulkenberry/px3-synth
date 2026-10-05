#include "Chorus.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace px3
{
namespace
{
using Family = Chorus::Family;

// ----------------------------------------------------------------------------
// The modes. See docs/CHORUS_DSP_DESIGN.md section 4 for the source of each
// figure and which ones are unverified.
//
// family, lines, rateHz, baseMs, excursionMs, skew, offset, corner, sine,
// fastHz, fastMs, compander, dryGain, wetGain
// ----------------------------------------------------------------------------
constexpr float kInvSqrt3 = 0.57735026919f;

constexpr std::array<Chorus::ModeSpec, 12> kModes { {
    // SDD-320. Two speeds (~4 s and ~2 s cycles) by two depths across buttons
    // 1-4; mode 1 runs longer delays (8-12 ms); the other buttons sit at about
    // 5 and 5.5 ms on the two sides (a ~10% mismatch).
    { Family::dimension, 2, 0.25f, 10.00f, 2.0f, 0.05f, 0.5f, 0.02f, false, 0.0f, 0.0f, true, 1.0f, 0.5f },  // DIM 1
    { Family::dimension, 2, 0.25f,  5.25f, 2.5f, 0.05f, 0.5f, 0.02f, false, 0.0f, 0.0f, true, 1.0f, 0.5f },  // DIM 2
    { Family::dimension, 2, 0.50f,  5.25f, 1.5f, 0.05f, 0.5f, 0.02f, false, 0.0f, 0.0f, true, 1.0f, 0.5f },  // DIM 3
    { Family::dimension, 2, 0.50f,  5.25f, 2.5f, 0.05f, 0.5f, 0.02f, false, 0.0f, 0.0f, true, 1.0f, 0.5f },  // DIM 4
    // Combination buttons: both switch states on the SAME pair. The timing
    // resistors of the two buttons sit in parallel, so the rates add; the
    // depth is the deeper of the two; the centre delay is the lower button's.
    // (Modelling assumption - see the design doc.)
    { Family::dimension, 2, 0.75f, 10.00f, 2.5f, 0.05f, 0.5f, 0.02f, false, 0.0f, 0.0f, true, 1.0f, 0.5f },  // DIM 1+4
    { Family::dimension, 2, 0.75f,  5.25f, 2.5f, 0.05f, 0.5f, 0.02f, false, 0.0f, 0.0f, true, 1.0f, 0.5f },  // DIM 2+4
    { Family::dimension, 2, 1.00f,  5.25f, 2.5f, 0.05f, 0.5f, 0.02f, false, 0.0f, 0.0f, true, 1.0f, 0.5f },  // DIM 3+4
    // Solina-style ensemble: slow "chorus" and fast "vibrato" 3-phase
    // generators summed on each of three lines.
    { Family::ensemble, 3, 0.70f, 6.00f, 1.6f, 0.0f, 1.0f / 3.0f, 0.0f, true, 6.3f, 0.12f, false, 1.0f, kInvSqrt3 },
    // CE-1: one 512-stage MN3002, sine-ish LFO, no compander.
    { Family::ce1, 1, 0.70f, 2.80f, 0.75f, 0.0f, 0.0f, 0.0f, true, 0.0f, 0.0f, false, 1.0f, 1.0f },
    // Juno-60 (pendragon-andyh measurements): I and II share one delay range
    // (1.66-5.35 ms) and differ only in rate; I+II is 9.75 Hz over 3.3-3.7 ms
    // with both lines in phase. Dry 0.83 against a unity BBD path (jpcima).
    { Family::juno, 2, 0.513f, 3.505f, 1.845f, 0.0f, 0.5f, 0.01f, false, 0.0f, 0.0f, false, 0.83f, 1.0f },   // JUNO-60 I
    { Family::juno, 2, 0.863f, 3.505f, 1.845f, 0.0f, 0.5f, 0.01f, false, 0.0f, 0.0f, false, 0.83f, 1.0f },   // JUNO-60 II
    { Family::juno, 2, 9.75f,  3.500f, 0.200f, 0.0f, 0.0f, 0.08f, false, 0.0f, 0.0f, false, 0.83f, 1.0f },   // JUNO-60 I+II
} };

static_assert(kModes.size() == 12, "modeFilters is sized for twelve modes");

constexpr float kMaxDelayMs = 24.0f;
constexpr float kMinDelayMs = 0.5f;
constexpr float kFadeSeconds = 0.04f;
constexpr float kDefaultRate = 0.35f;
constexpr float kRateOctaves = 3.0f;
constexpr float kDefaultWidth = 0.75f;

// The compander: an NE570-style 2:1 pair. The compressor's rectifier sits on
// its OUTPUT (gain = ref / env(out), so out ~ sqrt(ref * in)); the expander's
// on its INPUT (gain = env(in) / ref). With matched detectors the pair is an
// exact inverse across any pure delay, and departs from one only where the
// filters and the saturation in between change the signal.
constexpr float kCompanderRef = 0.25f;
constexpr float kCompanderFloor = 0.025f;   // 20 dB maximum gain
constexpr float kCompanderAttackSeconds = 0.002f;
constexpr float kCompanderReleaseSeconds = 0.02f;

// The BBD's own clipping, scaled by CHARACTER: tanh(k x)/k with k = 1.2 *
// character, so 0 is exactly linear and the default 0.5 a gentle ~1% at
// nominal level.
constexpr float kDriveScale = 1.2f;

// ---- Juno-60 BBD input and output filters ----------------------------------
// Holters & Parker, "A Combined Model for a Bucket Brigade Device and its Input
// and Output Filters", DAFx-18 - fitted to a Juno-60. Pole/residue form,
// H(s) = sum R_m / (s - P_m), as published in jpcima's rc-effect-playground
// (bbd_filter.cpp, ISC licence; see THIRD_PARTY_NOTICES.md).
constexpr int kJunoPoles = 5;
const std::array<std::complex<double>, kJunoPoles> kJunoInR { {
    { 251589.0, 0.0 }, { -130428.0, -4165.0 }, { -130428.0, 4165.0 }, { 4634.0, -22873.0 }, { 4634.0, 22873.0 } } };
const std::array<std::complex<double>, kJunoPoles> kJunoInP { {
    { -46580.0, 0.0 }, { -55482.0, 25082.0 }, { -55482.0, -25082.0 }, { -26292.0, -59437.0 }, { -26292.0, 59437.0 } } };
const std::array<std::complex<double>, kJunoPoles> kJunoOutR { {
    { 5092.0, 0.0 }, { 11256.0, -99566.0 }, { 11256.0, 99566.0 }, { -13802.0, -24606.0 }, { -13802.0, 24606.0 } } };
const std::array<std::complex<double>, kJunoPoles> kJunoOutP { {
    { -176261.0, 0.0 }, { -51468.0, 21437.0 }, { -51468.0, -21437.0 }, { -26276.0, -59699.0 }, { -26276.0, 59699.0 } } };

float smoothstep(float t)
{
    const auto x = juce::jlimit(0.0f, 1.0f, t);
    return x * x * (3.0f - 2.0f * x);
}

float onePoleCoeff(double hz, double rate)
{
    if (rate <= 0.0 || hz <= 0.0)
    {
        return 1.0f;
    }
    return static_cast<float>(juce::jlimit(0.0, 1.0, 1.0 - std::exp(-juce::MathConstants<double>::twoPi * hz / rate)));
}
} // namespace

// ============================================================================
// filter design - prepare() only
// ============================================================================
namespace
{
using Section = std::array<double, 5>;   // b0 b1 b2 a1 a2

// Impulse-invariant discretisation of a pole/residue analogue filter as a
// PARALLEL sum of real sections (one per real pole, one per conjugate pair),
// DC-normalised to the analogue gain. Impulse invariance rather than bilinear:
// the poles sit at 7-10 kHz, where bilinear warping would darken the response
// by 4-5 dB at 10 kHz (44.1 kHz). Measured error of this form against the
// analogue response: < 0.1 dB to 12 kHz at 44.1 kHz.
std::vector<Section> designPoleResidue(const std::array<std::complex<double>, kJunoPoles>& residues,
                                       const std::array<std::complex<double>, kJunoPoles>& poles,
                                       double sampleRate)
{
    const auto t = 1.0 / sampleRate;
    std::vector<Section> sections;
    std::complex<double> analogueDc { 0.0, 0.0 };

    for (std::size_t m = 0; m < poles.size(); ++m)
    {
        const auto p = poles[m];
        const auto r = residues[m];
        analogueDc -= r / p;

        const auto z = std::exp(p * t);
        if (std::abs(p.imag()) < 1.0e-9)
        {
            sections.push_back({ t * r.real(), 0.0, 0.0, -z.real(), 0.0 });
        }
        else if (p.imag() > 0.0)
        {
            // r/(1 - z q) + conj: (2Re r - 2Re(r conj z) q) / (1 - 2Re z q + |z|^2 q^2)
            sections.push_back({ 2.0 * t * r.real(), -2.0 * t * (r * std::conj(z)).real(), 0.0,
                                 -2.0 * z.real(), std::norm(z) });
        }
    }

    auto digitalDc = 0.0;
    for (const auto& s : sections)
    {
        digitalDc += (s[0] + s[1] + s[2]) / (1.0 + s[3] + s[4]);
    }

    const auto scale = std::abs(digitalDc) > 1.0e-12 ? analogueDc.real() / digitalDc : 1.0;
    for (auto& s : sections)
    {
        s[0] *= scale;
        s[1] *= scale;
        s[2] *= scale;
    }
    return sections;
}

// RBJ low-pass biquad (bilinear, prewarped at the corner).
Section lowPassBiquad(double hz, double q, double sampleRate)
{
    const auto w = juce::MathConstants<double>::twoPi * juce::jmin(hz, sampleRate * 0.45) / sampleRate;
    const auto alpha = std::sin(w) / (2.0 * q);
    const auto cosw = std::cos(w);
    const auto a0 = 1.0 + alpha;
    return { (1.0 - cosw) * 0.5 / a0, (1.0 - cosw) / a0, (1.0 - cosw) * 0.5 / a0,
             -2.0 * cosw / a0, (1.0 - alpha) / a0 };
}

// First-order sections by bilinear transform.
Section highPassFirstOrder(double hz, double sampleRate)
{
    const auto k = std::tan(juce::MathConstants<double>::pi * hz / sampleRate);
    const auto a0 = 1.0 + k;
    return { 1.0 / a0, -1.0 / a0, 0.0, (k - 1.0) / a0, 0.0 };
}

// Shelf H(s) = (1 + s/wz) / (1 + s/wp): unity at DC, wp/wz at HF.
Section shelfFirstOrder(double zeroHz, double poleHz, double sampleRate)
{
    const auto kk = 2.0 * sampleRate;
    const auto wz = juce::MathConstants<double>::twoPi * zeroHz;
    const auto wp = juce::MathConstants<double>::twoPi * poleHz;
    // (s + wz)/(s + wp) * wp/wz, s = kk (1 - q)/(1 + q)
    const auto g = wp / wz;
    const auto b0 = (kk + wz) * g;
    const auto b1 = (wz - kk) * g;
    const auto a0 = kk + wp;
    const auto a1 = wp - kk;
    return { b0 / a0, b1 / a0, 0.0, a1 / a0, 0.0 };
}

Section inverseOf(const Section& s)
{
    // Swap numerator and denominator (first-order, minimum phase).
    const auto b0 = s[0];
    return { 1.0 / b0, s[3] / b0, 0.0, s[1] / b0, 0.0 };
}
} // namespace

int Chorus::modeCount()
{
    return static_cast<int>(kModes.size());
}

Chorus::ModeSpec Chorus::specFor(int modeIndex)
{
    return kModes[static_cast<std::size_t>(juce::jlimit(0, static_cast<int>(kModes.size()) - 1, modeIndex))];
}

float Chorus::roundedTriangle(float phase01, float cornerFraction)
{
    const auto p = phase01 - std::floor(phase01);
    const auto h = juce::jlimit(0.0f, 0.2f, cornerFraction);

    // q: distance from the peak at phase 0 (0 .. 0.5). Linear part 1 - 4q.
    const auto q = std::abs(p > 0.5f ? 1.0f - p : p);
    auto value = 1.0f - 4.0f * q;

    if (h > 0.0f)
    {
        if (q < h)
        {
            value = 1.0f - 4.0f * (q * q + h * h) / (2.0f * h);
        }
        else if (0.5f - q < h)
        {
            const auto r = 0.5f - q;
            value = -1.0f + 4.0f * (r * r + h * h) / (2.0f * h);
        }
        value /= 1.0f - 2.0f * h;
    }
    return value;
}

// ============================================================================
// helpers
// ============================================================================

float Chorus::sanitize(float v)
{
    if (! std::isfinite(v))
    {
        return 0.0f;
    }
    return juce::jlimit(-8.0f, 8.0f, v);
}

float Chorus::runSection(const Section& s, std::array<float, 2>& state, float x)
{
    // Transposed direct form II.
    const auto y = s.b0 * x + state[0];
    state[0] = s.b1 * x - s.a1 * y + state[1];
    state[1] = s.b2 * x - s.a2 * y;
    return y;
}

float Chorus::runFilter(const FilterDesign& design, FilterState& state, float x)
{
    if (design.parallel)
    {
        auto y = 0.0f;
        for (int i = 0; i < design.count; ++i)
        {
            const auto idx = static_cast<std::size_t>(i);
            y += runSection(design.sections[idx], state[idx], x);
        }
        return y;
    }

    auto v = x;
    for (int i = 0; i < design.count; ++i)
    {
        const auto idx = static_cast<std::size_t>(i);
        v = runSection(design.sections[idx], state[idx], v);
    }
    return v;
}

// ============================================================================
// lifecycle
// ============================================================================

void Chorus::prepare(double sampleRate)
{
    sampleRateHz = sampleRate > 0.0 ? sampleRate : 44100.0;
    msToSamples = static_cast<float>(sampleRateHz * 0.001);
    lineSize = juce::jmax(256, static_cast<int>(sampleRateHz * kMaxDelayMs * 0.001) + 8);

    for (auto& slot : slots)
    {
        for (auto& line : slot.lines)
        {
            line.buffer.assign(static_cast<std::size_t>(lineSize), 0.0f);
        }
    }

    auto toSection = [](const std::array<double, 5>& s)
    {
        Section out;
        out.b0 = static_cast<float>(s[0]);
        out.b1 = static_cast<float>(s[1]);
        out.b2 = static_cast<float>(s[2]);
        out.a1 = static_cast<float>(s[3]);
        out.a2 = static_cast<float>(s[4]);
        return out;
    };

    auto butterworth4 = [&](double hz)
    {
        FilterDesign d;
        d.count = 2;
        d.parallel = false;
        d.sections[0] = toSection(lowPassBiquad(hz, 0.54119610, sampleRateHz));
        d.sections[1] = toSection(lowPassBiquad(hz, 1.30656296, sampleRateHz));
        return d;
    };

    auto poleResidue = [&](const std::array<std::complex<double>, kJunoPoles>& r,
                           const std::array<std::complex<double>, kJunoPoles>& p)
    {
        FilterDesign d;
        const auto sections = designPoleResidue(r, p, sampleRateHz);
        d.parallel = true;
        d.count = static_cast<int>(juce::jmin(sections.size(), static_cast<std::size_t>(kMaxSections)));
        for (std::size_t i = 0; i < static_cast<std::size_t>(d.count); ++i)
        {
            d.sections[i] = toSection(sections[i]);
        }
        return d;
    };

    for (int mode = 0; mode < kModeCount; ++mode)
    {
        const auto spec = specFor(mode);
        auto& f = modeFilters[static_cast<std::size_t>(mode)];
        f = ModeFilters {};

        switch (spec.family)
        {
            case Family::juno:
                f.pre = poleResidue(kJunoInR, kJunoInP);
                f.post = poleResidue(kJunoOutR, kJunoOutP);
                break;
            case Family::dimension:
                // ~8 kHz steep filtering after the lines (measured as a
                // "brickwall"); the order is not documented - 4th-order
                // Butterworth either side of the BBD.
                f.pre = butterworth4(8000.0);
                f.post = butterworth4(8000.0);
                f.highPass = toSection(highPassFirstOrder(150.0, sampleRateHz));
                f.emphasis = toSection(shelfFirstOrder(2000.0, 6000.0, sampleRateHz));
                f.deEmphasis = toSection(inverseOf(shelfFirstOrder(2000.0, 6000.0, sampleRateHz)));
                break;
            case Family::ensemble:
                f.pre = butterworth4(7000.0);
                f.post = butterworth4(7000.0);
                break;
            case Family::ce1:
                f.pre = butterworth4(6500.0);
                f.post = butterworth4(6500.0);
                break;
        }
    }

    envAttack = onePoleCoeff(1.0 / (juce::MathConstants<double>::twoPi * kCompanderAttackSeconds), sampleRateHz);
    envRelease = onePoleCoeff(1.0 / (juce::MathConstants<double>::twoPi * kCompanderReleaseSeconds), sampleRateHz);
    toneCoeff = onePoleCoeff(1400.0, sampleRateHz);
    fadeIncrement = static_cast<float>(1.0 / (kFadeSeconds * sampleRateHz));

    const auto rampSeconds = 0.03;
    for (auto* smoother : { &enabledSmoothed, &amountSmoothed, &rateTrimSmoothed, &depthSmoothed,
                            &widthSmoothed, &spreadSmoothed, &toneSmoothed, &lowCutCoeffSmoothed,
                            &feedbackSmoothed, &characterSmoothed, &mixSmoothed })
    {
        smoother->reset(sampleRateHz, rampSeconds);
    }

    updateForBlock(settings);
    reset();
}

void Chorus::activateSlot(int slotIndex, int mode)
{
    auto& slot = slots[static_cast<std::size_t>(slotIndex)];
    slot.mode = juce::jlimit(0, kModeCount - 1, mode);
    slot.phase = 0.0;
    slot.fastPhase = 0.0;

    for (auto& line : slot.lines)
    {
        std::fill(line.buffer.begin(), line.buffer.end(), 0.0f);
        line.write = 0;
        for (auto& s : line.pre) { s.fill(0.0f); }
        for (auto& s : line.post) { s.fill(0.0f); }
        line.highPass.fill(0.0f);
        line.emphasis.fill(0.0f);
        line.deEmphasis.fill(0.0f);
        line.compressorEnv = kCompanderRef;
        line.expanderEnv = kCompanderRef;
        line.lastPost = 0.0f;
    }
}

void Chorus::reset()
{
    activeSlot = 0;
    activateSlot(0, pendingMode);
    activateSlot(1, pendingMode);
    fadePosition = 1.0f;

    lowCutState = { { 0.0f, 0.0f } };
    toneState = { { 0.0f, 0.0f } };

    for (auto* smoother : { &enabledSmoothed, &amountSmoothed, &rateTrimSmoothed, &depthSmoothed,
                            &widthSmoothed, &spreadSmoothed, &toneSmoothed, &lowCutCoeffSmoothed,
                            &feedbackSmoothed, &characterSmoothed, &mixSmoothed })
    {
        smoother->setCurrentAndTargetValue(smoother->getTargetValue());
    }
}

void Chorus::updateForBlock(const ChorusSettings& next)
{
    settings = next;
    pendingMode = juce::jlimit(0, kModeCount - 1, settings.modeIndex);

    // An inaudible chorus has nothing to crossfade: change mode outright.
    if (idle && slots[static_cast<std::size_t>(activeSlot)].mode != pendingMode)
    {
        activateSlot(activeSlot, pendingMode);
        fadePosition = 1.0f;
    }

    enabledSmoothed.setTargetValue(settings.enabled ? 1.0f : 0.0f);
    amountSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.amount));
    rateTrimSmoothed.setTargetValue(std::exp2((juce::jlimit(0.0f, 1.0f, settings.rate) - kDefaultRate) * kRateOctaves));
    depthSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.depth));
    widthSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.width));
    spreadSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.spread));
    toneSmoothed.setTargetValue(juce::jlimit(-1.0f, 1.0f, settings.tone));
    lowCutCoeffSmoothed.setTargetValue(
        onePoleCoeff(static_cast<double>(juce::jmap(juce::jlimit(0.0f, 1.0f, settings.lowCut), 20.0f, 420.0f)), sampleRateHz));
    feedbackSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.feedback));
    characterSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.character));
    mixSmoothed.setTargetValue(juce::jlimit(0.0f, 1.0f, settings.mix));
}

// ============================================================================
// one line: the BBD and everything around it
// ============================================================================

float Chorus::readLine(const Line& line, float delaySamples) const
{
    const auto size = static_cast<float>(lineSize);
    auto pos = static_cast<float>(line.write) - juce::jlimit(2.0f, size - 4.0f, delaySamples);
    if (pos < 0.0f)
    {
        pos += size;
    }

    const auto i1 = static_cast<int>(pos) % lineSize;
    const auto frac = pos - std::floor(pos);

    // Cubic (Catmull-Rom). Linear interpolation of a moving read is a
    // level-dependent low-pass that moves with the modulation.
    const auto i0 = (i1 - 1 + lineSize) % lineSize;
    const auto i2 = (i1 + 1) % lineSize;
    const auto i3 = (i1 + 2) % lineSize;

    const auto y0 = line.buffer[static_cast<std::size_t>(i0)];
    const auto y1 = line.buffer[static_cast<std::size_t>(i1)];
    const auto y2 = line.buffer[static_cast<std::size_t>(i2)];
    const auto y3 = line.buffer[static_cast<std::size_t>(i3)];

    const auto a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const auto b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto c = -0.5f * y0 + 0.5f * y2;

    return ((a * frac + b) * frac + c) * frac + y1;
}

float Chorus::processLine(Line& line, const ModeFilters& filters, bool compander, float x,
                          float delaySamples, float drive, float feedback)
{
    auto v = x;

    if (compander)
    {
        // Bass roll-off and pre-emphasis ahead of the compressor.
        v = runSection(filters.highPass, line.highPass, v);
        v = runSection(filters.emphasis, line.emphasis, v);

        // Compressor: gain from the envelope of its own output.
        v *= kCompanderRef / juce::jmax(kCompanderFloor, line.compressorEnv);
        const auto level = std::abs(v);
        line.compressorEnv += (level - line.compressorEnv) * (level > line.compressorEnv ? envAttack : envRelease);
    }

    // Input (anti-alias) filter.
    v = runFilter(filters.pre, line.pre, v);

    // User colour: the line's own output fed back in. The post filter's gain
    // never exceeds one and this is capped at 0.55, so the loop cannot run.
    v += line.lastPost * feedback;

    // The BBD's own clipping, at the point the charge is sampled.
    if (drive > 1.0e-3f)
    {
        v = std::tanh(v * drive) / drive;
    }

    line.buffer[static_cast<std::size_t>(line.write)] = sanitize(v);
    auto y = readLine(line, delaySamples);
    line.write = (line.write + 1) % lineSize;

    // Output (reconstruction) filter.
    y = runFilter(filters.post, line.post, y);
    line.lastPost = y;

    if (compander)
    {
        // Expander: gain from the envelope of its own input, the detector
        // matched to the compressor's.
        const auto expanded = y * juce::jmax(kCompanderFloor, line.expanderEnv) / kCompanderRef;
        const auto level = std::abs(y);
        line.expanderEnv += (level - line.expanderEnv) * (level > line.expanderEnv ? envAttack : envRelease);
        y = runSection(filters.deEmphasis, line.deEmphasis, expanded);
    }

    return sanitize(y);
}

// ============================================================================
// one mode
// ============================================================================

Chorus::Output Chorus::renderSlot(Slot& slot, float inL, float inR, const Controls& c)
{
    const auto spec = specFor(slot.mode);
    const auto& filters = modeFilters[static_cast<std::size_t>(slot.mode)];

    slot.phase += static_cast<double>(spec.rateHz * c.rateTrim) / sampleRateHz;
    slot.phase -= std::floor(slot.phase);
    if (spec.fastRateHz > 0.0f)
    {
        slot.fastPhase += static_cast<double>(spec.fastRateHz * c.rateTrim) / sampleRateHz;
        slot.fastPhase -= std::floor(slot.fastPhase);
    }

    // DEPTH 0.5 is the hardware's swing; 0 is none, 1 twice it - never closer
    // than kMinDelayMs to zero delay.
    const auto depthScale = 2.0f * c.depth;
    const auto basePhase = static_cast<float>(slot.phase);
    const auto fastPhase = static_cast<float>(slot.fastPhase);

    auto lfoAt = [&spec](float phase)
    {
        return spec.sineLfo ? std::sin(juce::MathConstants<float>::twoPi * phase)
                            : roundedTriangle(phase, spec.cornerFraction);
    };

    auto delayFor = [&](int lineIndex, float lineOffset)
    {
        auto centre = spec.baseDelayMs;
        if (spec.lines == 2)
        {
            centre *= lineIndex == 0 ? 1.0f - spec.lineSkew : 1.0f + spec.lineSkew;
        }
        const auto room = juce::jmax(0.0f, centre - kMinDelayMs);
        auto swing = spec.excursionMs * depthScale * lfoAt(basePhase + lineOffset);
        if (spec.fastRateHz > 0.0f)
        {
            swing += spec.fastExcursionMs * depthScale
                     * std::sin(juce::MathConstants<float>::twoPi * (fastPhase + lineOffset));
        }
        return (centre + juce::jlimit(-room, room, swing)) * msToSamples;
    };

    Output out;
    const auto amountDry = 1.0f + c.amount * (spec.dryGain - 1.0f);
    const auto sideScale = juce::jlimit(0.0f, 4.0f / 3.0f, c.width / kDefaultWidth);

    switch (spec.family)
    {
        case Family::juno:
        {
            // One LFO; line B at the mode's phase (0.5 = inverted for I/II,
            // 0 = in phase for I+II), trimmed by SPREAD around it.
            const auto offsetB = spec.lineOffsetCycles + (c.spread - 0.5f);
            const auto a = processLine(slot.lines[0], filters, false, inL, delayFor(0, 0.0f), c.drive, c.feedback) * spec.wetGain;
            const auto b = processLine(slot.lines[1], filters, false, inR, delayFor(1, offsetB), c.drive, c.feedback) * spec.wetGain;
            // L = dry + A, R = dry + B, both positive. WIDTH scales the
            // difference between them; 0.75 is the hardware.
            const auto mid = 0.5f * (a + b);
            const auto side = 0.5f * (a - b) * sideScale;
            out = { inL * amountDry, inR * amountDry, mid + side, mid - side };
            break;
        }

        case Family::dimension:
        {
            const auto offsetB = spec.lineOffsetCycles + (c.spread - 0.5f);
            const auto a = processLine(slot.lines[0], filters, true, inL, delayFor(0, 0.0f), c.drive, c.feedback) * spec.wetGain;
            const auto b = processLine(slot.lines[1], filters, true, inR, delayFor(1, offsetB), c.drive, c.feedback) * spec.wetGain;
            // Each line to its own side, and inverted to the other. The wet
            // is pure side by construction, so the mono sum is the dry.
            const auto side = (a - b) * sideScale;
            out = { inL * amountDry, inR * amountDry, side, -side };
            break;
        }

        case Family::ensemble:
        {
            // Three lines from the mono sum, 120 degrees apart at SPREAD 0.5.
            // WIDTH pans them L / centre / R with a sum-preserving law, so the
            // mono sum is always the original's mono output.
            const auto mono = 0.5f * (inL + inR);
            const auto spacing = spec.lineOffsetCycles * 2.0f * c.spread;
            const auto pan = juce::jlimit(0.0f, 1.0f, c.width);
            auto wetL = 0.0f;
            auto wetR = 0.0f;
            for (int i = 0; i < 3; ++i)
            {
                const auto y = processLine(slot.lines[static_cast<std::size_t>(i)], filters, false, mono,
                                           delayFor(i, static_cast<float>(i) * spacing), c.drive, c.feedback)
                               * spec.wetGain;
                const auto p = static_cast<float>(i - 1) * pan;
                wetL += (1.0f - p) * y;
                wetR += (1.0f + p) * y;
            }
            out = { inL * amountDry, inR * amountDry, wetL, wetR };
            break;
        }

        case Family::ce1:
        {
            // One line from the mono sum. Mono: A = direct + chorus. Stereo
            // (WIDTH 0.75 and up): A = chorus only, B = direct only.
            const auto mono = 0.5f * (inL + inR);
            const auto y = processLine(slot.lines[0], filters, false, mono, delayFor(0, 0.0f), c.drive, c.feedback)
                           * spec.wetGain;
            const auto spatial = juce::jlimit(0.0f, 1.0f, c.width / kDefaultWidth) * c.amount;
            out = { inL * (1.0f - spatial),
                    inR * (1.0f - spatial) + mono * spatial,
                    y,
                    y * (1.0f - spatial) };
            break;
        }
    }

    return out;
}

// ============================================================================
// host-rate entry point
// ============================================================================

void Chorus::processSampleFrame(float inL, float inR, float& outL, float& outR)
{
    const auto enabled = enabledSmoothed.getNextValue();
    const auto amountRaw = amountSmoothed.getNextValue();
    const auto mix = mixSmoothed.getNextValue();

    if (amountRaw * enabled * mix <= 1.0e-6f
        && ! amountSmoothed.isSmoothing() && ! enabledSmoothed.isSmoothing()
        && ! mixSmoothed.isSmoothing())
    {
        if (! idle)
        {
            idle = true;
            reset();
        }
        if (slots[static_cast<std::size_t>(activeSlot)].mode != pendingMode)
        {
            activateSlot(activeSlot, pendingMode);
        }
        outL = inL;
        outR = inR;
        return;
    }
    idle = false;

    Controls c {};
    c.amount = amountRaw * enabled;
    c.rateTrim = rateTrimSmoothed.getNextValue();
    c.depth = depthSmoothed.getNextValue();
    c.width = widthSmoothed.getNextValue();
    c.spread = spreadSmoothed.getNextValue();
    c.drive = characterSmoothed.getNextValue() * kDriveScale;
    c.feedback = feedbackSmoothed.getNextValue() * 0.55f;
    const auto tone = toneSmoothed.getNextValue();
    const auto lowCutCoeff = lowCutCoeffSmoothed.getNextValue();

    // A mode change crossfades between two complete engines, so no delay,
    // filter or routing ever switches in one sample.
    if (fadePosition >= 1.0f && slots[static_cast<std::size_t>(activeSlot)].mode != pendingMode)
    {
        activateSlot(1 - activeSlot, pendingMode);
        fadePosition = 0.0f;
    }

    auto result = renderSlot(slots[static_cast<std::size_t>(activeSlot)], inL, inR, c);
    if (fadePosition < 1.0f)
    {
        const auto incoming = renderSlot(slots[static_cast<std::size_t>(1 - activeSlot)], inL, inR, c);
        fadePosition = juce::jmin(1.0f, fadePosition + fadeIncrement);
        const auto f = smoothstep(fadePosition);
        result.dryL += (incoming.dryL - result.dryL) * f;
        result.dryR += (incoming.dryR - result.dryR) * f;
        result.wetL += (incoming.wetL - result.wetL) * f;
        result.wetR += (incoming.wetR - result.wetR) * f;
        if (fadePosition >= 1.0f)
        {
            activeSlot = 1 - activeSlot;
        }
    }

    // ---- wet-path user filters. The dry path is NEVER filtered -------------
    lowCutState[0] += (result.wetL - lowCutState[0]) * lowCutCoeff;
    lowCutState[1] += (result.wetR - lowCutState[1]) * lowCutCoeff;
    auto wetL = result.wetL - lowCutState[0];
    auto wetR = result.wetR - lowCutState[1];

    toneState[0] += (wetL - toneState[0]) * toneCoeff;
    toneState[1] += (wetR - toneState[1]) * toneCoeff;
    if (std::abs(tone) > 1.0e-4f)
    {
        const auto lowGain = 1.0f - juce::jmax(0.0f, tone) * 0.55f;
        const auto highGain = 1.0f - juce::jmax(0.0f, -tone) * 0.8f;
        wetL = toneState[0] * lowGain + (wetL - toneState[0]) * highGain;
        wetR = toneState[1] * lowGain + (wetR - toneState[1]) * highGain;
    }

    const auto processedL = result.dryL + wetL * c.amount;
    const auto processedR = result.dryR + wetR * c.amount;

    outL = sanitize(inL + (processedL - inL) * mix);
    outR = sanitize(inR + (processedR - inR) * mix);
}

} // namespace px3
