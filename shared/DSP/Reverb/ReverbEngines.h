#pragma once

// The six reverb types. Each is a small class with the same shape:
//
//   floatsNeeded(sr)   storage it needs at the largest SIZE, at rate sr
//   bind(arena, sr)    carve that storage (no allocation; the Reverb has
//                      zeroed it first)
//   control(c)         per control tick (32 samples): coefficients
//   process(l, r, ...) per sample
//
// The architecture of each is documented above its class and in
// docs/REVERB_DSP_DESIGN.md. Constants are in seconds at SIZE scale 1.

#include "ReverbMapping.h"
#include "ReverbPrimitives.h"

#include <array>
#include <cstddef>

namespace px3::reverb
{
// Real-unit controls, already smoothed, handed to a type every control tick.
struct Controls
{
    double sampleRate { 48000.0 };
    float decaySeconds { 2.0f };
    float sizeScale { 1.0f };
    float lowMultiplier { 1.0f };
    float damping { 0.35f };
    float diffusion { 0.7f };
    float modulation { 0.35f };
    float early { 0.5f };
    float shimmer { 0.0f };
    float drip { 0.5f };
    float shape { 0.5f };
};

inline int samples(double seconds, double sr) noexcept { return static_cast<int>(std::ceil(seconds * sr)); }

// Per-sample glide toward a target length: SIZE moves are smooth Doppler
// sweeps of every delay, never steps. ~120 ms time constant.
inline float glideCoefficient(double sr) noexcept { return static_cast<float>(1.0 - std::exp(-1.0 / (0.12 * sr))); }

inline void setDecayFilter(DecayFilter& f, double loopSeconds, const Controls& c, double lowHz, double highHzAtZero)
{
    const auto rt = static_cast<double>(c.decaySeconds);
    f.set(decayGain(loopSeconds, rt * c.lowMultiplier),
          decayGain(loopSeconds, rt),
          decayGain(loopSeconds, rt * highDecayRatio(c.damping)),
          onePoleCoefficient(lowHz, c.sampleRate),
          onePoleCoefficient(highCornerHz(c.damping, static_cast<float>(highHzAtZero)), c.sampleRate));
}

//==============================================================================
// A 16-line feedback delay network (Jot; Adriaensen's zita-rev1 for the
// allpass in each loop). Shared by ROOM, HALL and CLOUD, which give it very
// different delay sets, diffusion and modulation:
//
//   in L -> lines 0..7, in R -> lines 8..15 (balanced random signs)
//   line i: Lagrange read (random-modulated, gliding with SIZE)
//           -> DecayFilter (Jot gain per band for THIS line's length)
//   [optional slow Givens rotations: a time-varying lossless matrix]
//   -> 16x16 Hadamard -> + input -> allpass (in-loop diffusion) -> line i
//   out L / out R: two orthogonal balanced +-1 pickups over all 16 lines
//
// Loop gain < 1 at every frequency (DecayFilter bound; Hadamard, allpasses,
// rotations and the interpolator are all |H| <= 1), so it is stable at any
// decay.
struct Fdn16
{
    static constexpr std::size_t N = 16;
    std::array<Line, N> lines;
    std::array<Allpass, N> allpasses;
    // The sixteen DecayFilters, stored as arrays (structure of arrays) so the
    // per-sample filter loop vectorises.
    std::array<float, N> dGMid {}, dKLow {}, dKHigh {}, dPLow {}, dPHigh {}, dSLow {}, dSHigh {};
    std::array<SmoothRandom, N> lfo;
    std::array<float, N> lineSeconds {}, allpassSeconds {};
    std::array<float, N> length {}, lengthTarget {}, apLength {}, apTarget {};
    std::array<float, N> inject {}, pickLeft {}, pickRight {};
    std::array<float, N> out {};
    std::array<float, N> modValue {};
    float modDepth { 0.0f };     // samples
    float apModDepth { 0.0f };   // samples
    float glide { 0.001f };
    float inputGain { 0.35355339f };
    float sideWeight { 0.0f };   // set before bind()
    // Time-varying matrix: 8 Givens rotations on pairs (k, k + 8) before the
    // Hadamard. Lossless at every angle; moving the angles keeps the modes
    // from settling (Schlecht & Habets 2015).
    static constexpr int kTickSamples = 32;   // Reverb::kControlInterval
    float rotationDepth { 0.0f };
    std::array<float, N / 2> rotationPhase {};
    std::array<float, N / 2> rotCos {}, rotSin {}, rotCosStep {}, rotSinStep {};
    float lastScale { -1.0f }, lastDecay { -1.0f }, lastLow { -1.0f }, lastDamping { -1.0f };
    double lowCornerHz { 250.0 }, highCornerAtZero { 10000.0 };

    void setDelays(const std::array<float, N>& lineSec, const std::array<float, N>& apSec)
    {
        lineSeconds = lineSec;
        allpassSeconds = apSec;
    }

    std::size_t floatsNeeded(double sr, float maxScale, double maxModSeconds) const noexcept
    {
        std::size_t total = 0;
        for (std::size_t i = 0; i < N; ++i)
        {
            total += Line::floatsFor(samples(lineSeconds[i] * maxScale + maxModSeconds, sr) + 4);
            total += Line::floatsFor(samples(allpassSeconds[i] * maxScale + maxModSeconds, sr) + 4);
        }
        return total;
    }

    bool bind(Arena& arena, double sr, float maxScale, double maxModSeconds, float initialScale, std::uint32_t seed)
    {
        bool ok = true;
        for (std::size_t i = 0; i < N; ++i)
        {
            ok = lines[i].bind(arena, samples(lineSeconds[i] * maxScale + maxModSeconds, sr) + 4) && ok;
            ok = allpasses[i].line.bind(arena, samples(allpassSeconds[i] * maxScale + maxModSeconds, sr) + 4) && ok;
            lfo[i].seed(seed * 31u + static_cast<std::uint32_t>(i), static_cast<float>(i) * 0.137f);
            dSLow[i] = dSHigh[i] = 0.0f;
            dGMid[i] = dKLow[i] = dKHigh[i] = 1.0f;
            dPLow[i] = dPHigh[i] = 0.0f;
            modValue[i] = 0.0f;
            length[i] = lengthTarget[i] = static_cast<float>(lineSeconds[i] * initialScale * sr);
            apLength[i] = apTarget[i] = std::round(static_cast<float>(allpassSeconds[i] * initialScale * sr));
            out[i] = 0.0f;
        }
        inject = balancedSigns<N>(seed + 101u);
        pickLeft = balancedSigns<N>(seed + 202u);
        pickRight = orthogonalSigns<N>(pickLeft, seed + 303u);
        // Optional side weighting: each output leans on the lines its own
        // input feeds, so a panned source keeps its side. Mirrored weights
        // keep the two pickups orthogonal: the dot product only scales by
        // (1 - w^2).
        for (std::size_t i = 0; i < N; ++i)
        {
            const auto own = i < N / 2 ? 1.0f + sideWeight : 1.0f - sideWeight;
            const auto other = i < N / 2 ? 1.0f - sideWeight : 1.0f + sideWeight;
            pickLeft[i] *= own;
            pickRight[i] *= other;
        }
        for (std::size_t k = 0; k < N / 2; ++k)
        {
            rotationPhase[k] = static_cast<float>(k) * 0.71f;
            rotCos[k] = 1.0f;
            rotSin[k] = rotCosStep[k] = rotSinStep[k] = 0.0f;
        }
        glide = glideCoefficient(sr);
        lastScale = lastDecay = lastLow = lastDamping = -1.0f;
        return ok;
    }

    // modHz: mean random-modulation rate; lines spread +-30 % around it.
    void control(const Controls& c, float modSeconds, float modHz, float apModSeconds, float allpassGain,
                 float rotationDepthIn = 0.0f, float rotationHz = 1.0f)
    {
        const auto sr = c.sampleRate;
        modDepth = static_cast<float>(modSeconds * sr);
        apModDepth = static_cast<float>(apModSeconds * sr);
        rotationDepth = rotationDepthIn;
        for (std::size_t i = 0; i < N; ++i)
        {
            lfo[i].setRate(modHz * (0.7 + 0.6 * static_cast<double>(i) / (N - 1)), sr);
            allpasses[i].gain = (i & 1) ? -allpassGain : allpassGain;
        }
        // The rotation angles move at 1-2 Hz, so they are evaluated once per
        // control tick and their sine and cosine ramped linearly between
        // ticks (the orthogonality error of the ramp is ~1e-8).
        for (std::size_t k = 0; k < N / 2; ++k)
        {
            const auto increment = kTwoPi * rotationHz * (0.7 + 0.6 * static_cast<double>(k) / 7.0) / sr * kTickSamples;
            rotationPhase[k] = static_cast<float>(std::fmod(rotationPhase[k] + increment, kTwoPi));
            const auto angle = rotationDepth * std::sin(static_cast<double>(rotationPhase[k]));
            const auto targetCos = static_cast<float>(std::cos(angle)), targetSin = static_cast<float>(std::sin(angle));
            rotCosStep[k] = (targetCos - rotCos[k]) / static_cast<float>(kTickSamples);
            rotSinStep[k] = (targetSin - rotSin[k]) / static_cast<float>(kTickSamples);
        }

        if (c.sizeScale != lastScale)
        {
            for (std::size_t i = 0; i < N; ++i)
            {
                lengthTarget[i] = static_cast<float>(lineSeconds[i] * c.sizeScale * sr);
                apTarget[i] = std::round(static_cast<float>(allpassSeconds[i] * c.sizeScale * sr));
            }
        }
        if (c.sizeScale != lastScale || c.decaySeconds != lastDecay || c.lowMultiplier != lastLow || c.damping != lastDamping)
        {
            for (std::size_t i = 0; i < N; ++i)
            {
                DecayFilter f;
                setDecayFilter(f, (lineSeconds[i] + allpassSeconds[i]) * c.sizeScale, c, lowCornerHz, highCornerAtZero);
                dGMid[i] = f.gMid;
                dKLow[i] = f.kLow;
                dKHigh[i] = f.kHigh;
                dPLow[i] = f.pLow;
                dPHigh[i] = f.pHigh;
            }
            lastScale = c.sizeScale;
            lastDecay = c.decaySeconds;
            lastLow = c.lowMultiplier;
            lastDamping = c.damping;
        }
    }

    // feed(i, y) may replace line i's recirculating sample (CLOUD's shimmer);
    // for every other type it returns y untouched and compiles away.
    template <typename Feed>
    void process(float inL, float inR, float& outL, float& outR, Feed&& feed) noexcept
    {
        std::array<float, N> s;
        for (std::size_t i = 0; i < N; ++i)
        {
            length[i] += glide * (lengthTarget[i] - length[i]);
            apLength[i] = glideTo(apLength[i], apTarget[i], glide);
        }
        if (modDepth > 0.0f || apModDepth > 0.0f)
            for (std::size_t i = 0; i < N; ++i) { modValue[i] = lfo[i].process(); }
        for (std::size_t i = 0; i < N; ++i) { s[i] = lines[i].lagrange(length[i] + modValue[i] * modDepth); }
        // DecayFilter (see ReverbPrimitives.h), all sixteen at once.
        for (std::size_t i = 0; i < N; ++i)
        {
            dSLow[i] = (1.0f - dPLow[i]) * s[i] + dPLow[i] * dSLow[i];
            const auto y = s[i] + (dKLow[i] - 1.0f) * dSLow[i];
            dSHigh[i] = (1.0f - dPHigh[i]) * y + dPHigh[i] * dSHigh[i];
            s[i] = dGMid[i] * (dSHigh[i] + dKHigh[i] * (y - dSHigh[i]));
        }
        out = s;
        if (rotationDepth > 0.0f)
        {
            for (std::size_t k = 0; k < N / 2; ++k)
            {
                rotCos[k] += rotCosStep[k];
                rotSin[k] += rotSinStep[k];
                const auto a = s[k], b = s[k + N / 2];
                s[k] = rotCos[k] * a - rotSin[k] * b;
                s[k + N / 2] = rotSin[k] * a + rotCos[k] * b;
            }
        }
        hadamard<N>(s);
        const auto gl = inL * inputGain, gr = inR * inputGain;
        for (std::size_t i = 0; i < N; ++i)
        {
            auto y = s[i] + inject[i] * (i < N / 2 ? gl : gr);
            y = feed(i, y);
            // The allpass takes another line's modulator, so the two delays in
            // one loop do not move together.
            if (apModDepth > 0.0f) { y = allpasses[i].processModulated(y, apLength[i] + modValue[(i + 5) % N] * apModDepth); }
            else { y = allpasses[i].processGliding(y, apLength[i]); }
            lines[i].push(std::fmin(std::fmax(y, -8.0f), 8.0f));   // also turns a NaN into a bound
        }
        float l = 0.0f, r = 0.0f;
        for (std::size_t i = 0; i < N; ++i)
        {
            l += pickLeft[i] * out[i];
            r += pickRight[i] * out[i];
        }
        outL = l * 0.25f;
        outR = r * 0.25f;
    }

    void process(float inL, float inR, float& outL, float& outR) noexcept
    {
        process(inL, inR, outL, outR, [](std::size_t, float y) noexcept { return y; });
    }
};

//==============================================================================
// Early reflections: an image-source pattern for a shoebox (Allen & Berkley),
// computed once per ear, scaled by SIZE. Every reflection is same-sign (rigid
// walls; it also survives a mono sum) and passes through a short scattering
// diffuser, because real walls smear what they return - a bare tap pattern is
// heard as a flam, not a room.
struct ReflectionTap { float seconds; float gain; };

template <std::size_t Taps>
struct ReflectionPattern
{
    std::array<ReflectionTap, Taps> ear[2] {};
    float longestSeconds { 0.0f };
};

template <std::size_t Taps>
inline ReflectionPattern<Taps> imageSourcePattern(const std::array<double, 3>& room, const std::array<double, 3>& source,
                                                  const std::array<double, 3>& listener, double beta)
{
    ReflectionPattern<Taps> pattern;
    constexpr double c = 343.0, earOffset = 0.09;
    for (int e = 0; e < 2; ++e)
    {
        const std::array<double, 3> ear { listener[0] + (e == 0 ? -earOffset : earOffset), listener[1], listener[2] };
        const auto dist = [](const std::array<double, 3>& a, const std::array<double, 3>& b)
        { return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2])); };
        const auto direct = dist(source, ear);
        // Orders 1 and 2: 6 + 18 + 12 = 36 images, the earliest Taps kept.
        std::array<ReflectionTap, 64> all {};
        std::size_t count = 0;
        for (int nx = -2; nx <= 2; ++nx)
            for (int ny = -2; ny <= 2; ++ny)
                for (int nz = -2; nz <= 2; ++nz)
                {
                    const auto order = std::abs(nx) + std::abs(ny) + std::abs(nz);
                    if (order == 0 || order > 2 || count >= all.size()) { continue; }
                    const auto image = [](int n, double s, double d) { return n * d + ((n & 1) == 0 ? s : d - s); };
                    const std::array<double, 3> p { image(nx, source[0], room[0]), image(ny, source[1], room[1]),
                                                    image(nz, source[2], room[2]) };
                    const auto r = dist(p, ear);
                    all[count++] = { static_cast<float>((r - direct) / c), static_cast<float>(direct / r * std::pow(beta, order)) };
                }
        std::sort(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(count),
                  [](const ReflectionTap& a, const ReflectionTap& b) { return a.seconds < b.seconds; });
        // Images that arrive within 0.6 ms of each other are merged: two equal
        // taps a fraction of a millisecond apart are a comb filter with teeth
        // kilohertz apart, heard as a metallic colour on everything.
        std::size_t merged = 0;
        for (std::size_t k = 0; k < count; ++k)
        {
            if (merged > 0 && all[k].seconds - all[merged - 1].seconds < 0.0006f)
            {
                const auto g = all[merged - 1].gain + all[k].gain;
                all[merged - 1].seconds = (all[merged - 1].seconds * all[merged - 1].gain + all[k].seconds * all[k].gain) / g;
                all[merged - 1].gain = g;
            }
            else { all[merged++] = all[k]; }
        }
        count = merged;
        double energy = 0.0;
        for (std::size_t k = 0; k < Taps && k < count; ++k) { energy += static_cast<double>(all[k].gain) * all[k].gain; }
        const auto norm = static_cast<float>(1.0 / std::sqrt(std::max(1.0e-9, energy)));
        for (std::size_t k = 0; k < Taps; ++k)
        {
            if (k >= count) { pattern.ear[e][k] = { 0.001f, 0.0f }; continue; }
            pattern.ear[e][k] = { std::max(0.0003f, all[k].seconds), all[k].gain * norm };
            pattern.longestSeconds = std::max(pattern.longestSeconds, all[k].seconds);
        }
    }
    return pattern;
}

template <std::size_t Taps>
struct EarlyReflections
{
    // Reflections are scattered in four groups, each through its own short
    // allpass pair, and only then summed. Summing first would leave every
    // reflection an exact copy of every other - a comb - however much the sum
    // were smeared afterwards.
    static constexpr std::size_t kGroups = 4;
    static constexpr double kScatter[2][kGroups][2] = {
        { { 0.00071, 0.00173 }, { 0.00083, 0.00211 }, { 0.00097, 0.00157 }, { 0.00113, 0.00293 } },
        { { 0.00077, 0.00181 }, { 0.00089, 0.00223 }, { 0.00103, 0.00149 }, { 0.00119, 0.00271 } } };

    ReflectionPattern<Taps> pattern;
    float timeScale { 1.0f };          // pattern stretch at SIZE scale 1
    std::array<Line, 2> lines;
    std::array<std::array<std::array<Allpass, 2>, kGroups>, 2> scatter;
    std::array<OnePoleLowpass, 2> tone;
    float scale { 1.0f }, scaleTarget { 1.0f }, glide { 0.001f };
    double sampleRate { 48000.0 };

    std::size_t floatsNeeded(double sr, float maxScale) const noexcept
    {
        std::size_t total = 2 * Line::floatsFor(samples(pattern.longestSeconds * timeScale * maxScale + 0.002, sr));
        for (const auto& ear : kScatter)
            for (const auto& group : ear)
                for (const auto t : group) total += Line::floatsFor(samples(t + 0.0001, sr));
        return total;
    }

    bool bind(Arena& arena, double sr, float maxScale, float initialScale)
    {
        sampleRate = sr;
        bool ok = true;
        for (std::size_t e = 0; e < 2; ++e)
        {
            ok = lines[e].bind(arena, samples(pattern.longestSeconds * timeScale * maxScale + 0.002, sr)) && ok;
            for (std::size_t g = 0; g < kGroups; ++g)
                for (std::size_t k = 0; k < 2; ++k)
                {
                    auto& ap = scatter[e][g][k];
                    ok = ap.line.bind(arena, samples(kScatter[e][g][k] + 0.0001, sr)) && ok;
                    ap.delay = static_cast<float>(kScatter[e][g][k] * sr);
                }
            tone[e].clear();
        }
        scale = scaleTarget = initialScale;
        glide = glideCoefficient(sr);
        return ok;
    }

    void control(const Controls& c, float cornerAtZero)
    {
        scaleTarget = c.sizeScale;
        const auto g = 0.3f + 0.4f * c.diffusion;
        for (auto& ear : scatter)
            for (auto& group : ear)
                for (std::size_t k = 0; k < group.size(); ++k) { group[k].gain = (k & 1) ? -g : g; }
        for (auto& t : tone) { t.setCutoff(highCornerHz(c.damping, cornerAtZero) * 1.5, c.sampleRate); }
    }

    // Each ear hears mostly its own side of the source plus some of the
    // other, so a panned input keeps its place in the room.
    void process(float inL, float inR, float& outL, float& outR) noexcept
    {
        scale += glide * (scaleTarget - scale);
        const auto stretch = static_cast<float>(sampleRate) * timeScale * scale;
        float result[2];
        const float in[2] = { 0.75f * inL + 0.25f * inR, 0.75f * inR + 0.25f * inL };
        for (std::size_t e = 0; e < 2; ++e)
        {
            auto& line = lines[e];
            line.push(in[e]);
            std::array<float, kGroups> group {};
            for (std::size_t k = 0; k < Taps; ++k)
            {
                const auto& tap = pattern.ear[e][k];
                // Linear interpolation is enough here: no feedback, and the
                // scattering allpasses smear each tap anyway.
                const auto d = std::max(1.0f, tap.seconds * stretch);
                const auto di = static_cast<int>(d);
                const auto f = d - static_cast<float>(di);
                const auto p0 = line.tap(di);
                group[k % kGroups] += tap.gain * (p0 + f * (line.tap(di + 1) - p0));
            }
            float y = 0.0f;
            for (std::size_t g = 0; g < kGroups; ++g)
            {
                auto v = group[g];
                for (auto& ap : scatter[e][g]) { v = ap.process(v); }
                y += v;
            }
            result[e] = tone[e].process(y);
        }
        outL = result[0];
        outR = result[1];
    }
};

//==============================================================================
// ROOM: "put this inside a space". The early reflections ARE the room: a
// 5.3 x 4.1 x 2.9 m shoebox scaled by SIZE (~1.8 m .. 8.5 m), 20 image
// sources per ear, scattered. They feed a short, dense 16-line late field so
// the tail grows out of the reflections instead of arriving separately. Short
// lines ring, so the late field is random-modulated AND its matrix rotates
// slowly. EARLY trades reflections against tail.
class RoomEngine
{
public:
    RoomEngine()
    {
        er.pattern = imageSourcePattern<20>({ 5.3, 4.1, 2.9 }, { 1.7, 2.6, 1.4 }, { 3.6, 1.5, 1.3 }, 0.8);
        er.timeScale = 1.0f;
        // 14 .. 62 ms at scale 1, mutually incommensurate (no common grid).
        constexpr std::array<float, 16> base { 0.0071f, 0.0083f, 0.0097f, 0.0109f, 0.0121f, 0.0137f, 0.0149f, 0.0163f,
                                               0.0179f, 0.0191f, 0.0211f, 0.0227f, 0.0241f, 0.0263f, 0.0281f, 0.0307f };
        constexpr std::array<float, 16> ap { 0.0021f, 0.0027f, 0.0019f, 0.0031f, 0.0023f, 0.0029f, 0.0017f, 0.0033f,
                                             0.0025f, 0.0037f, 0.0022f, 0.0028f, 0.0035f, 0.0018f, 0.0026f, 0.0032f };
        std::array<float, 16> lineSec {}, apSec {};
        for (std::size_t i = 0; i < 16; ++i)
        {
            lineSec[i] = base[i] * 2.0f * (1.0f + 0.013f * static_cast<float>(i));
            apSec[i] = ap[i] * 1.5f;
        }
        late.setDelays(lineSec, apSec);
        late.lowCornerHz = 200.0;
        late.highCornerAtZero = 12000.0;
    }

    static constexpr float kMaxModSeconds = 0.0008f;

    std::size_t floatsNeeded(double sr) const noexcept
    {
        const auto maxScale = kSizeRange[room].hi;
        return er.floatsNeeded(sr, maxScale) + late.floatsNeeded(sr, maxScale, kMaxModSeconds) + 2 * 4 * Line::floatsFor(samples(0.0084 * maxScale, sr));
    }

    bool bind(Arena& arena, double sr, float initialScale)
    {
        const auto maxScale = kSizeRange[room].hi;
        bool ok = er.bind(arena, sr, maxScale, initialScale);
        ok = late.bind(arena, sr, maxScale, kMaxModSeconds, initialScale, 3u) && ok;
        static constexpr double kDiff[2][4] = { { 0.0013, 0.0029, 0.0047, 0.0083 }, { 0.0017, 0.0023, 0.0053, 0.0079 } };
        for (int ch = 0; ch < 2; ++ch)
            for (int k = 0; k < 4; ++k)
            {
                auto& a = diffuser[static_cast<std::size_t>(ch)][static_cast<std::size_t>(k)];
                ok = a.line.bind(arena, samples(kDiff[ch][k] * maxScale + 0.0001, sr)) && ok;
                diffuserSeconds[static_cast<std::size_t>(ch)][static_cast<std::size_t>(k)] = static_cast<float>(kDiff[ch][k]);
                a.delay = diffuserTarget[static_cast<std::size_t>(ch)][static_cast<std::size_t>(k)] = std::round(static_cast<float>(kDiff[ch][k] * initialScale * sr));
            }
        sampleRate = sr;
        glide = glideCoefficient(sr);
        return ok;
    }

    void control(const Controls& c)
    {
        er.control(c, 12000.0f);
        late.control(c, (0.1f + 0.5f * c.modulation) * 0.001f, 1.2f + 1.2f * c.modulation, 0.0f, 0.2f + 0.25f * c.diffusion,
                     0.3f + 0.9f * c.modulation, 1.0f + 1.0f * c.modulation);
        const auto g = 0.5f + 0.2f * c.diffusion;
        for (std::size_t ch = 0; ch < 2; ++ch)
            for (std::size_t k = 0; k < 4; ++k)
            {
                diffuser[ch][k].gain = (k & 1) ? -g : g;
                diffuserTarget[ch][k] = std::round(static_cast<float>(diffuserSeconds[ch][k] * c.sizeScale * sampleRate));
            }
        earlyGain = 1.6f * c.early;
        lateGain = 1.0f - 0.5f * c.early;
    }

    void process(float inL, float inR, float& outL, float& outR) noexcept
    {
        float el, er2;
        er.process(inL, inR, el, er2);
        // The late field grows out of the reflections, but mostly out of the
        // diffused direct sound: fed only from the reflections, every tail
        // carries the reflection pattern's comb as a fixed metallic colour
        // (measured: ringing 9 dB over the noise reference, 2 dB at this mix).
        auto a = 0.25f * el + 0.8f * inL, b = 0.25f * er2 + 0.8f * inR;
        for (std::size_t k = 0; k < 4; ++k)
        {
            diffuser[0][k].delay = glideTo(diffuser[0][k].delay, diffuserTarget[0][k], glide);
            diffuser[1][k].delay = glideTo(diffuser[1][k].delay, diffuserTarget[1][k], glide);
            a = diffuser[0][k].processGliding(a, diffuser[0][k].delay);
            b = diffuser[1][k].processGliding(b, diffuser[1][k].delay);
        }
        float l, r;
        late.process(a, b, l, r);
        outL = earlyGain * el + lateGain * l;
        outR = earlyGain * er2 + lateGain * r;
    }

private:
    EarlyReflections<20> er;
    Fdn16 late;
    std::array<std::array<Allpass, 4>, 2> diffuser;
    std::array<std::array<float, 4>, 2> diffuserSeconds {}, diffuserTarget {};
    double sampleRate { 48000.0 };
    float glide { 0.001f };
    float earlyGain { 0.8f }, lateGain { 0.75f };
};

//==============================================================================
// HALL: a large, smooth late field. Six-stage stereo input diffusion (so the
// network is never fed a click), then a 16-line FDN of 32-97 ms lines with
// short in-loop allpasses, gentle random modulation and Jot three-band decay.
// True stereo in (L feeds half the lines, R the other half). A sparse
// early-reflection cluster from a larger shoebox gives the front of the hall;
// EARLY sets its level.
class HallEngine
{
public:
    HallEngine()
    {
        er.pattern = imageSourcePattern<14>({ 5.3, 4.1, 2.9 }, { 1.4, 2.9, 1.5 }, { 3.9, 1.3, 1.2 }, 0.8);
        er.timeScale = 2.2f;
        late.setDelays({ 0.0317f, 0.0361f, 0.0389f, 0.0431f, 0.0467f, 0.0503f, 0.0547f, 0.0593f,
                         0.0631f, 0.0677f, 0.0719f, 0.0761f, 0.0811f, 0.0853f, 0.0907f, 0.0971f },
                       { 0.0047f, 0.0061f, 0.0053f, 0.0071f, 0.0083f, 0.0059f, 0.0067f, 0.0089f,
                         0.0043f, 0.0077f, 0.0091f, 0.0057f, 0.0069f, 0.0081f, 0.0049f, 0.0073f });
        late.lowCornerHz = 250.0;
        late.highCornerAtZero = 10000.0;
    }

    static constexpr float kMaxModSeconds = 0.0007f;
    static constexpr double kDiff[2][6] = { { 0.0013, 0.0029, 0.0047, 0.0083, 0.0119, 0.0157 },
                                            { 0.0017, 0.0023, 0.0053, 0.0079, 0.0127, 0.0149 } };

    std::size_t floatsNeeded(double sr) const noexcept
    {
        const auto maxScale = kSizeRange[hall].hi;
        std::size_t diff = 0;
        for (const auto& ch : kDiff)
            for (const auto t : ch) diff += Line::floatsFor(samples(t * std::sqrt(maxScale) + 0.0001, sr));
        return er.floatsNeeded(sr, maxScale) + late.floatsNeeded(sr, maxScale, kMaxModSeconds) + diff;
    }

    bool bind(Arena& arena, double sr, float initialScale)
    {
        const auto maxScale = kSizeRange[hall].hi;
        bool ok = er.bind(arena, sr, maxScale, initialScale);
        ok = late.bind(arena, sr, maxScale, kMaxModSeconds, initialScale, 7u) && ok;
        for (std::size_t ch = 0; ch < 2; ++ch)
            for (std::size_t k = 0; k < 6; ++k)
            {
                ok = diffuser[ch][k].line.bind(arena, samples(kDiff[ch][k] * std::sqrt(maxScale) + 0.0001, sr)) && ok;
                diffuserLength[ch][k] = diffuserTarget[ch][k] = std::round(static_cast<float>(kDiff[ch][k] * std::sqrt(initialScale) * sr));
            }
        sampleRate = sr;
        glide = glideCoefficient(sr);
        return ok;
    }

    void control(const Controls& c)
    {
        er.control(c, 10000.0f);
        late.control(c, (0.05f + 0.55f * c.modulation) * 0.001f, 0.35f + 0.6f * c.modulation, 0.0f, 0.15f + 0.45f * c.diffusion);
        const auto g = 0.4f + 0.35f * c.diffusion;
        const auto stretch = std::sqrt(c.sizeScale);
        for (std::size_t ch = 0; ch < 2; ++ch)
            for (std::size_t k = 0; k < 6; ++k)
            {
                diffuser[ch][k].gain = (k & 1) ? -g : g;
                diffuserTarget[ch][k] = std::round(static_cast<float>(kDiff[ch][k] * stretch * sampleRate));
            }
        earlyGain = 1.2f * c.early;
    }

    void process(float inL, float inR, float& outL, float& outR) noexcept
    {
        float el, er2;
        er.process(inL, inR, el, er2);
        auto a = inL, b = inR;
        for (std::size_t k = 0; k < 6; ++k)
        {
            diffuserLength[0][k] = glideTo(diffuserLength[0][k], diffuserTarget[0][k], glide);
            diffuserLength[1][k] = glideTo(diffuserLength[1][k], diffuserTarget[1][k], glide);
            a = diffuser[0][k].processGliding(a, diffuserLength[0][k]);
            b = diffuser[1][k].processGliding(b, diffuserLength[1][k]);
        }
        float l, r;
        late.process(a, b, l, r);
        outL = l + earlyGain * el;
        outR = r + earlyGain * er2;
    }

private:
    EarlyReflections<14> er;
    Fdn16 late;
    std::array<std::array<Allpass, 6>, 2> diffuser;
    std::array<std::array<float, 6>, 2> diffuserLength {}, diffuserTarget {};
    double sampleRate { 48000.0 };
    float glide { 0.001f };
    float earlyGain { 0.4f };
};

//==============================================================================
// PLATE: Dattorro's figure-of-eight tank (JAES 45(9), 1997; Griesinger's
// Lexicon topology) with the fixes the audit found. A plate is mono in, so
// the input is summed; the 7 + 7 output pickups make it stereo.
//
//   in -> bandwidth LP (13 kHz) -> 4 short dense diffusers (0.4-1.7 ms; a
//   real plate is dense within milliseconds - the paper's four alone take
//   ~300 ms to mix) -> the paper's 4 input diffusers -> tank:
//   each half: modulated allpass (-0.70) -> delay -> DecayFilter -> allpass
//   (0.50) -> delay -> the other half.
//
// Decay is in SECONDS: the per-half gain comes from the half's own length,
// so SIZE no longer changes the decay time (it did: the paper's single
// "decay" coefficient is per pass). The damping filter has Dattorro's sense
// (the paper's 0.0005 is the coefficient on the FEEDBACK term - the old port
// put it on the input and turned the tank into a 4 Hz lowpass).
class PlateEngine
{
public:
    static constexpr double kRef = 29761.0;
    static constexpr std::array<int, 12> kLen { 142, 107, 379, 277, 672, 4453, 1800, 3720, 908, 4217, 2656, 3163 };
    struct Tap { int node; int position; float sign; };   // node: 0 d1L,1 ap2L,2 d2L,3 d1R,4 ap2R,5 d2R
    static constexpr std::array<Tap, 7> kLeft { { { 3, 266, 1 }, { 3, 2974, 1 }, { 4, 1913, -1 }, { 5, 1996, 1 },
                                                  { 0, 1990, -1 }, { 1, 187, -1 }, { 2, 1066, -1 } } };
    static constexpr std::array<Tap, 7> kRight { { { 0, 353, 1 }, { 0, 3627, 1 }, { 1, 1228, -1 }, { 2, 2673, 1 },
                                                   { 3, 2111, -1 }, { 4, 335, -1 }, { 5, 121, -1 } } };
    static constexpr std::array<double, 4> kDense { 0.00043, 0.00079, 0.00113, 0.00167 };
    static constexpr float kMaxExcursionSeconds = 0.0011f;

    std::size_t floatsNeeded(double sr) const noexcept
    {
        const auto k = sr / kRef * kSizeRange[plate].hi;
        std::size_t total = 0;
        for (const auto t : kDense) total += Line::floatsFor(samples(t, sr) + 2);
        for (std::size_t i = 0; i < kLen.size(); ++i)
            total += Line::floatsFor(static_cast<int>(std::ceil(kLen[i] * k)) + samples(kMaxExcursionSeconds, sr) + 4);
        return total;
    }

    bool bind(Arena& arena, double sr, float initialScale)
    {
        sampleRate = sr;
        const auto k = sr / kRef * kSizeRange[plate].hi;
        bool ok = true;
        for (std::size_t i = 0; i < 4; ++i)
        {
            ok = dense[i].line.bind(arena, samples(kDense[i], sr) + 2) && ok;
            dense[i].delay = static_cast<float>(kDense[i] * sr);
            dense[i].gain = (i & 1) ? -0.6f : 0.6f;
        }
        const auto lineFor = [&](std::size_t i) { return static_cast<int>(std::ceil(kLen[i] * k)) + samples(kMaxExcursionSeconds, sr) + 4; };
        for (std::size_t i = 0; i < 4; ++i) ok = input[i].line.bind(arena, lineFor(i)) && ok;
        for (std::size_t h = 0; h < 2; ++h)
        {
            ok = ap1[h].line.bind(arena, lineFor(4 + 4 * h)) && ok;
            ok = d1[h].bind(arena, lineFor(5 + 4 * h)) && ok;
            ok = ap2[h].line.bind(arena, lineFor(6 + 4 * h)) && ok;
            ok = d2[h].bind(arena, lineFor(7 + 4 * h)) && ok;
            decay[h].clear();
            tank[h] = 0.0f;
            lfo[h].seed(41u + static_cast<std::uint32_t>(h), 0.3f * static_cast<float>(h));
            ap1[h].gain = -0.70f;
            ap2[h].gain = 0.50f;
        }
        bandwidth.setCutoff(13000.0, sr);
        bandwidth.clear();
        scale = scaleTarget = static_cast<float>(sr / kRef) * initialScale;
        for (std::size_t i = 0; i < 4; ++i) { inputLength[i] = inputTarget[i] = std::round(static_cast<float>(kLen[i]) * scale); }
        for (std::size_t h = 0; h < 2; ++h) { ap2Length[h] = ap2Target[h] = std::round(static_cast<float>(kLen[6 + 4 * h]) * scale); }
        glide = glideCoefficient(sr);
        phase = 0.0f;
        lastScale = lastDecay = lastLow = lastDamping = -1.0f;
        return ok;
    }

    void control(const Controls& c)
    {
        scaleTarget = static_cast<float>(c.sampleRate / kRef) * c.sizeScale;
        for (std::size_t i = 0; i < 4; ++i) { inputTarget[i] = std::round(static_cast<float>(kLen[i]) * scaleTarget); }
        for (std::size_t h = 0; h < 2; ++h) { ap2Target[h] = std::round(static_cast<float>(kLen[6 + 4 * h]) * scaleTarget); }
        const auto g1 = 0.5f + 0.35f * c.diffusion, g2 = 0.4f + 0.3f * c.diffusion;
        input[0].gain = input[1].gain = g1;
        input[2].gain = input[3].gain = g2;
        excursion = static_cast<float>(c.modulation * 0.001 * c.sampleRate);
        phaseIncrement = static_cast<float>(kTwoPi * (0.6 + 0.8 * c.modulation) / c.sampleRate);
        for (auto& l : lfo) { l.setRate(0.9 + 0.6 * c.modulation, c.sampleRate); }
        if (c.sizeScale != lastScale || c.decaySeconds != lastDecay || c.lowMultiplier != lastLow || c.damping != lastDamping)
        {
            for (std::size_t h = 0; h < 2; ++h)
            {
                const auto halfSeconds = (kLen[4 + 4 * h] + kLen[5 + 4 * h] + kLen[6 + 4 * h] + kLen[7 + 4 * h]) / kRef * c.sizeScale;
                setDecayFilter(decay[h], halfSeconds, c, 250.0, 11000.0);
            }
            lastScale = c.sizeScale;
            lastDecay = c.decaySeconds;
            lastLow = c.lowMultiplier;
            lastDamping = c.damping;
        }
    }

    void process(float inL, float inR, float& outL, float& outR) noexcept
    {
        scale += glide * (scaleTarget - scale);
        auto x = bandwidth.process(0.5f * (inL + inR));
        for (auto& a : dense) { x = a.process(x); }
        for (std::size_t i = 0; i < 4; ++i)
        {
            inputLength[i] = glideTo(inputLength[i], inputTarget[i], glide);
            x = input[i].processGliding(x, inputLength[i]);
        }

        phase += phaseIncrement;
        if (phase > 6.28318531f) { phase -= 6.28318531f; }
        float sp, cp;
        // sin over the whole circle from the small-angle series, by symmetry.
        const auto folded = phase < 3.14159265f ? phase : phase - 6.28318531f;
        sinCosSmall(folded > 1.5707963f ? 3.14159265f - folded : (folded < -1.5707963f ? -3.14159265f - folded : folded), sp, cp);
        for (std::size_t h = 0; h < 2; ++h)
        {
            const auto m = (h == 0 ? sp : -sp) * 0.7f + 0.3f * lfo[h].process();
            auto a = x + tank[1 - h];
            a = ap1[h].processModulated(a, static_cast<float>(kLen[4 + 4 * h]) * scale + m * excursion);
            d1[h].push(a);
            a = decay[h].process(d1[h].lagrange(static_cast<float>(kLen[5 + 4 * h]) * scale));
            ap2Length[h] = glideTo(ap2Length[h], ap2Target[h], glide);
            a = ap2[h].processGliding(a, ap2Length[h]);
            d2[h].push(a);
            tank[h] = std::fmin(std::fmax(d2[h].lagrange(static_cast<float>(kLen[7 + 4 * h]) * scale), -8.0f), 8.0f);
        }
        const Line* nodes[6] = { &d1[0], &ap2[0].line, &d2[0], &d1[1], &ap2[1].line, &d2[1] };
        // Interpolated pickups (Lagrange: a linear one would dull the plate
        // by a different amount on every tap): SIZE glides every tap, and a
        // whole-sample tap would step as it moves.
        const auto pick = [this, &nodes](const Tap& t)
        {
            return t.sign * nodes[t.node]->lagrange(std::max(2.0f, static_cast<float>(t.position) * scale));
        };
        float l = 0.0f, r = 0.0f;
        for (const auto& t : kLeft) l += pick(t);
        for (const auto& t : kRight) r += pick(t);
        outL = l * 0.6f;
        outR = r * 0.6f;
    }

private:
    double sampleRate { 48000.0 };
    OnePoleLowpass bandwidth;
    std::array<Allpass, 4> dense, input;
    std::array<Allpass, 2> ap1, ap2;
    std::array<Line, 2> d1, d2;
    std::array<DecayFilter, 2> decay;
    std::array<SmoothRandom, 2> lfo;
    std::array<float, 2> tank {};
    float scale { 1.0f }, scaleTarget { 1.0f }, glide { 0.001f };
    std::array<float, 4> inputLength {}, inputTarget {};
    std::array<float, 2> ap2Length {}, ap2Target {};
    float excursion { 0.0f }, phase { 0.0f }, phaseIncrement { 0.0f };
    float lastScale { -1.0f }, lastDecay { -1.0f }, lastLow { -1.0f }, lastDamping { -1.0f };
};

//==============================================================================
// +12 semitone delay-line pitch shifter for CLOUD's shimmer. Two read heads
// half a window apart move through the buffer at twice the write speed; their
// sin^2 / cos^2 crossfade sums to exactly one. The window length is re-drawn
// at random (+-15 %) every cycle, so the crossfade's AM and the comb it leaves
// are not periodic (Costello: randomisation against comb artifacts).
struct OctaveShifter
{
    Line line;
    float window { 4800.0f }, phase { 0.0f };
    // Each head's read offset is re-drawn at random whenever that head is
    // silent (its weight is zero at its own wrap), so the delay between the
    // two heads - the comb a two-head shifter leaves - keeps changing,
    // without a single step in either head's read position.
    float jitterA { 0.0f }, jitterB { 0.0f };
    SmoothRandom random;

    std::size_t floatsNeeded(double windowSeconds, double sr) const noexcept { return Line::floatsFor(samples(windowSeconds * 1.4, sr) + 8); }

    bool bind(Arena& arena, double windowSeconds, double sr, std::uint32_t seed)
    {
        window = static_cast<float>(windowSeconds * sr);
        phase = 0.0f;
        random.seed(seed, 0.0f);
        jitterA = jitterB = 0.0f;
        return line.bind(arena, samples(windowSeconds * 1.4, sr) + 8);
    }

    float process(float x) noexcept
    {
        line.push(x);
        phase += 1.0f / window;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            jitterA = 0.3f * window * random.next01();   // head A is silent here
        }
        auto pb = phase + 0.5f;
        if (pb >= 1.0f) { pb -= 1.0f; }
        if (pb < 1.0f / window) { jitterB = 0.3f * window * random.next01(); }   // and B here
        // Head A at phase p, head B at p + 1/2: weights sin^2(pi p) and
        // cos^2(pi p), from one small-angle evaluation at pi (p - 1/2).
        float s, c;
        sinCosSmall(3.14159265f * (phase - 0.5f), s, c);
        const auto a = line.lagrange((1.0f - phase) * window + 3.0f + jitterA);
        const auto b = line.lagrange((1.0f - pb) * window + 3.0f + jitterB);
        return c * c * a + s * s * b;
    }
};

//==============================================================================
// CLOUD: an ambient wash, a different machine from HALL rather than a long
// hall. A "bloom" stage of four long, slowly modulated allpasses per channel
// (23-89 ms) turns every attack into a swell; a 16-line FDN of 80-234 ms
// lines with long modulated in-loop allpasses holds it for up to a minute.
//
// SHIMMER is pitch-shifted regeneration INSIDE the loop: four lines per side
// are summed, high-passed at 160 Hz and band-limited by an 8th-order
// Butterworth at min(6 kHz, 0.2 fs) - so the doubled signal cannot alias -
// shifted up an octave, and crossfaded into four lines' feedback as
// sqrt(1 - a^2) y + a k shifted. Equal-power, not additive: shimmer replaces
// part of the circulation instead of adding loop gain (k = 1.15 restores the
// crossfade's -1.25 dB). A slow governor backs a off if the loop's energy
// ever grows with no input. Each pass climbs another octave and the
// band-limit takes the top away, so the climb fades upward.
class CloudEngine
{
public:
    CloudEngine()
    {
        late.setDelays({ 0.0797f, 0.0883f, 0.0971f, 0.1063f, 0.1151f, 0.1249f, 0.1327f, 0.1433f,
                         0.1531f, 0.1627f, 0.1741f, 0.1849f, 0.1951f, 0.2069f, 0.2203f, 0.2339f },
                       { 0.0193f, 0.0241f, 0.0167f, 0.0283f, 0.0211f, 0.0317f, 0.0149f, 0.0263f,
                         0.0229f, 0.0337f, 0.0181f, 0.0251f, 0.0301f, 0.0157f, 0.0271f, 0.0223f });
        late.lowCornerHz = 250.0;
        late.highCornerAtZero = 12000.0;
        late.sideWeight = 0.45f;
    }

    static constexpr float kMaxModSeconds = 0.0025f;
    static constexpr double kBloom[2][4] = { { 0.0231, 0.0419, 0.0613, 0.0887 }, { 0.0263, 0.0389, 0.0659, 0.0821 } };
    static constexpr double kWindow[2] = { 0.095, 0.113 };

    std::size_t floatsNeeded(double sr) const noexcept
    {
        const auto maxScale = kSizeRange[cloud].hi;
        std::size_t total = late.floatsNeeded(sr, maxScale, kMaxModSeconds);
        for (const auto& ch : kBloom)
            for (const auto t : ch) total += Line::floatsFor(samples(t * maxScale + 0.003, sr));
        for (int k = 0; k < 2; ++k) total += shifter[static_cast<std::size_t>(k)].floatsNeeded(kWindow[k], sr);
        return total;
    }

    bool bind(Arena& arena, double sr, float initialScale)
    {
        sampleRate = sr;
        const auto maxScale = kSizeRange[cloud].hi;
        bool ok = late.bind(arena, sr, maxScale, kMaxModSeconds, initialScale, 21u);
        for (std::size_t ch = 0; ch < 2; ++ch)
            for (std::size_t k = 0; k < 4; ++k)
            {
                ok = bloom[ch][k].line.bind(arena, samples(kBloom[ch][k] * maxScale + 0.003, sr)) && ok;
                bloomLfo[ch][k].seed(50u + static_cast<std::uint32_t>(ch * 4 + k), 0.21f * static_cast<float>(k));
                bloomLength[ch][k] = bloomTarget[ch][k] = static_cast<float>(kBloom[ch][k] * initialScale * sr);
            }
        for (std::size_t k = 0; k < 2; ++k)
        {
            ok = shifter[k].bind(arena, kWindow[k], sr, 77u + static_cast<std::uint32_t>(k)) && ok;
            const auto limit = std::min(6000.0, 0.2 * sr);
            highpass[k].setHighpass(160.0, 0.7071, sr);
            static constexpr double kQ[4] = { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };
            for (std::size_t q = 0; q < 4; ++q) { lowpass[k][q].setLowpass(limit, kQ[q], sr); lowpass[k][q].clear(); }
            highpass[k].clear();
            shifted[k] = 0.0f;
        }
        glide = glideCoefficient(sr);
        alpha = alphaTarget = 0.0f;
        governor = 1.0f;
        tickEnergy = previousEnergy = inputEnergy = 0.0;
        return ok;
    }

    void control(const Controls& c)
    {
        late.control(c, 2.0f * c.modulation * 0.001f, 0.18f + 0.15f * c.modulation, 0.6f * c.modulation * 0.001f, 0.5f + 0.2f * c.diffusion);
        const auto g = 0.45f + 0.25f * c.diffusion;
        for (std::size_t ch = 0; ch < 2; ++ch)
            for (std::size_t k = 0; k < 4; ++k)
            {
                bloom[ch][k].gain = g;
                bloomLfo[ch][k].setRate(0.13 + 0.05 * static_cast<double>(k), c.sampleRate);
                bloomTarget[ch][k] = static_cast<float>(kBloom[ch][k] * c.sizeScale * c.sampleRate);
            }
        bloomDepth = static_cast<float>(0.0025 * c.modulation * c.sampleRate);

        // Governor: if the network's energy grows from one tick to the next
        // while almost nothing comes in, back the shimmer off; recover slowly.
        if (alpha > 0.0f && inputEnergy < 1.0e-3 * tickEnergy && tickEnergy > previousEnergy * 1.002 && tickEnergy > 1.0e-10)
            governor = std::max(0.2f, governor * 0.97f);
        else
            governor += 0.002f * (1.0f - governor);
        previousEnergy = tickEnergy;
        tickEnergy = inputEnergy = 0.0;
        alphaTarget = shimmerAlpha(c.shimmer) * governor;
    }

    void process(float inL, float inR, float& outL, float& outR) noexcept
    {
        auto a = inL, b = inR;
        for (std::size_t k = 0; k < 4; ++k)
        {
            bloomLength[0][k] += glide * (bloomTarget[0][k] - bloomLength[0][k]);
            bloomLength[1][k] += glide * (bloomTarget[1][k] - bloomLength[1][k]);
            a = bloom[0][k].processModulated(a, bloomLength[0][k] + bloomLfo[0][k].process() * bloomDepth);
            b = bloom[1][k].processModulated(b, bloomLength[1][k] + bloomLfo[1][k].process() * bloomDepth);
        }
        inputEnergy += static_cast<double>(a) * a + static_cast<double>(b) * b;

        // alpha glides per sample (~30 ms), so SHIMMER moves without a step.
        alpha += 0.0007f * (alphaTarget - alpha);
        // The shifters (and their band-limit) keep listening while SHIMMER
        // is off, so turning it up starts from audio already in their
        // buffers, not from silence and a filter start-up transient.
        const auto& s = late.out;
        const float source[2] = { 0.5f * (s[0] + s[2] + s[4] + s[6]), 0.5f * (s[9] + s[11] + s[13] + s[15]) };
        float band[2];
        for (std::size_t k = 0; k < 2; ++k)
        {
            auto v = highpass[k].process(source[k]);
            for (auto& f : lowpass[k]) { v = f.process(v); }
            band[k] = v;
        }
        if (alpha > 1.0e-5f)
        {
            for (std::size_t k = 0; k < 2; ++k) { shifted[k] = shifter[k].process(band[k]); }
            const auto keep = std::sqrt(std::max(0.0f, 1.0f - alpha * alpha));
            const auto add = alpha * 1.15f;
            late.process(a, b, outL, outR, [&](std::size_t i, float y) noexcept
            {
                if (i < 4) { return keep * y + add * shifted[0]; }
                if (i >= 8 && i < 12) { return keep * y + add * shifted[1]; }
                return y;
            });
        }
        else
        {
            shifter[0].line.push(band[0]);
            shifter[1].line.push(band[1]);
            late.process(a, b, outL, outR);
        }
        for (const auto v : late.out) { tickEnergy += static_cast<double>(v) * v; }
    }

private:
    double sampleRate { 48000.0 };
    Fdn16 late;
    std::array<std::array<Allpass, 4>, 2> bloom;
    std::array<std::array<SmoothRandom, 4>, 2> bloomLfo;
    std::array<std::array<float, 4>, 2> bloomLength {}, bloomTarget {};
    float bloomDepth { 0.0f }, glide { 0.001f };
    std::array<OctaveShifter, 2> shifter;
    std::array<Biquad, 2> highpass;
    std::array<std::array<Biquad, 4>, 2> lowpass;
    std::array<float, 2> shifted {};
    float alpha { 0.0f }, alphaTarget { 0.0f }, governor { 1.0f };
    double tickEnergy { 0.0 }, previousEnergy { 0.0 }, inputEnergy { 0.0 };
};

//==============================================================================
// SPRING: after Valimaki, Parker & Abel, "Parametric spring reverberation
// effect" (JAES 58(7/8), 2010). Two springs of different length (a tank's
// left and right). Each is a feedback loop around a cascade of identical
// "stretched" allpasses H(z^K) = (a + z^-K) / (1 + a z^-K), K = fs / (2 fc):
// their group delay rises with frequency up to fc (~4.3 kHz), so every
// round trip arrives as an upward chirp - the drip - and the high end of each
// echo trails behind the low end further every time round. A randomly
// modulated delay holds the transit time; a lowpass at fc is the spring's
// bandwidth. A short first-order allpass chain on the input adds the high
// "splash". DRIP sets the chirp (the allpass coefficient), DIFFUSION the
// splash, SIZE the spring length.
class SpringEngine
{
public:
    static constexpr int kSections = 128;
    static constexpr double kTransitionHz = 2800.0;
    static constexpr double kTransit[2] = { 0.0360, 0.0423 };
    static constexpr int kSplash = 10;

    static int stretchFor(double sr) noexcept { return std::max(1, static_cast<int>(std::lround(sr / (2.0 * kTransitionHz)))); }

    std::size_t floatsNeeded(double sr) const noexcept
    {
        const auto maxScale = kSizeRange[spring].hi;
        std::size_t total = 0;
        for (const auto t : kTransit)
            total += Line::floatsFor(samples(t * maxScale + 0.001, sr)) + static_cast<std::size_t>(kSections * stretchFor(sr));
        return total;
    }

    bool bind(Arena& arena, double sr, float initialScale)
    {
        sampleRate = sr;
        stretch = stretchFor(sr);
        bool ok = true;
        for (std::size_t k = 0; k < 2; ++k)
        {
            auto& s = springs[k];
            ok = s.line.bind(arena, samples(kTransit[k] * kSizeRange[spring].hi + 0.001, sr)) && ok;
            s.chirp = arena.take(static_cast<std::size_t>(kSections * stretch));
            ok = s.chirp != nullptr && ok;
            s.index = 0;
            s.lfo.seed(90u + static_cast<std::uint32_t>(k), 0.4f * static_cast<float>(k));
            s.lfo.setRate(2.3, sr);
            s.length = s.target = static_cast<float>(kTransit[k] * initialScale * sr);
            s.splash.fill(0.0f);
            for (auto& f : s.lowpass) { f.clear(); }
            for (auto& f : s.inputLowpass) { f.clear(); }
            s.damp.clear();
        }
        glide = glideCoefficient(sr);
        lastDamping = -1.0f;
        return ok;
    }

    void control(const Controls& c)
    {
        coefficient = 0.4f + 0.3f * c.drip;
        splashGain = 0.3f * c.diffusion;
        for (std::size_t k = 0; k < 2; ++k)
        {
            auto& s = springs[k];
            s.target = static_cast<float>(kTransit[k] * c.sizeScale * c.sampleRate);
            // Gain per round trip, from the round trip's actual length (the
            // chirp adds its own group delay at mid frequencies).
            const auto roundTrip = kTransit[k] * c.sizeScale + kSections * stretch * (1.0 - coefficient) / (1.0 + coefficient) / c.sampleRate;
            s.feedback = decayGain(roundTrip, c.decaySeconds);
            s.modDepth = static_cast<float>((0.00008 + 0.0003 * c.modulation) * c.sampleRate);
        }
        if (c.damping != lastDamping)
        {
            for (auto& s : springs)
            {
                s.lowpass[0].setLowpass(kTransitionHz * (1.25 - 0.45 * c.damping), 0.5412, c.sampleRate);
                s.lowpass[1].setLowpass(kTransitionHz * (1.25 - 0.45 * c.damping), 1.3066, c.sampleRate);
                // The stretched allpass repeats every fs / K: its chirp has
                // images at 3 fc, 5 fc... The input is band-limited before
                // the loop as well as inside it, so no image can ring.
                s.inputLowpass[0].setLowpass(kTransitionHz * 1.2, 0.5412, c.sampleRate);
                s.inputLowpass[1].setLowpass(kTransitionHz * 1.2, 1.3066, c.sampleRate);
                s.damp.setCutoff(highCornerHz(c.damping, 9000.0f), c.sampleRate);
            }
            lastDamping = c.damping;
        }
    }

    void process(float inL, float inR, float& outL, float& outR) noexcept
    {
        const auto x = 0.5f * (inL + inR);
        float y[2];
        for (std::size_t k = 0; k < 2; ++k)
        {
            auto& s = springs[k];
            s.length += glide * (s.target - s.length);
            const auto back = s.line.lagrange(s.length + s.lfo.process() * s.modDepth) * s.feedback;
            y[k] = s.inputLowpass[1].process(s.inputLowpass[0].process(x)) + back;
        }
        // The two springs' stretched-allpass cascades, interleaved: each
        // section depends on the one before it, so one chain alone waits on
        // its own arithmetic; two side by side keep the core busy.
        {
            auto* z0 = springs[0].chirp + springs[0].index;
            auto* z1 = springs[1].chirp + springs[1].index;
            const auto a = coefficient;
            auto y0 = y[0], y1 = y[1];
            for (int m = 0; m < kSections; ++m, z0 += stretch, z1 += stretch)
            {
                const auto v0 = *z0, v1 = *z1;
                const auto u0 = y0 - a * v0, u1 = y1 - a * v1;
                *z0 = u0;
                *z1 = u1;
                y0 = a * u0 + v0;
                y1 = a * u1 + v1;
            }
            y[0] = y0;
            y[1] = y1;
            for (auto& s : springs)
                if (++s.index == stretch) { s.index = 0; }
        }
        float result[2];
        for (std::size_t k = 0; k < 2; ++k)
        {
            auto& s = springs[k];
            auto v = y[k];
            for (auto& f : s.lowpass) { v = f.process(v); }
            v = s.damp.process(v);
            s.line.push(std::fmin(std::fmax(v, -8.0f), 8.0f));

            auto h = x;
            for (auto& state : s.splash)
            {
                const auto u = h + 0.6f * state;
                const auto o = state - 0.6f * u;
                state = u;
                h = o;
            }
            result[k] = v + splashGain * h;
        }
        outL = result[0];
        outR = result[1];
    }

private:
    struct Spring
    {
        Line line;
        float* chirp { nullptr };
        int index { 0 };
        SmoothRandom lfo;
        float length { 0.0f }, target { 0.0f }, feedback { 0.0f }, modDepth { 0.0f };
        std::array<float, kSplash> splash {};
        std::array<Biquad, 2> lowpass;
        std::array<Biquad, 2> inputLowpass;
        OnePoleLowpass damp;
    };
    double sampleRate { 48000.0 };
    int stretch { 6 };
    std::array<Spring, 2> springs;
    float coefficient { 0.66f }, splashGain { 0.15f }, glide { 0.001f }, lastDamping { -1.0f };
};

//==============================================================================
// GATED: the shaped-envelope reverb of the AMS RMX16 "nonlin" programs and
// the gated drum rooms they imitate. No feedback at all, so it cannot ring or
// run away: each channel's input is diffused (4 allpasses), read by 24
// irregularly spaced taps spread over the burst LENGTH whose gains draw the
// envelope SHAPE (0 = reverse swell, 0.5 = flat gate, 1 = falling), smeared
// again (3 allpasses) and darkened by DAMPING. The last 12 % of the envelope
// is a smoothstep, so the gate closes without a click.
class GatedEngine
{
public:
    static constexpr std::size_t kTaps = 48;
    // Two diffusion chains per channel; the taps alternate between them. All
    // taps reading ONE diffused signal would be an FIR of identical copies -
    // a fixed comb whose ripple is heard as a metallic colour. Two chains
    // halve it.
    // Short diffusers only: a long allpass rings on past the gate, and the
    // point of this type is that it stops.
    static constexpr double kDiff[2][2][4] = { { { 0.00071, 0.00137, 0.00229, 0.00347 }, { 0.00089, 0.00163, 0.00251, 0.00317 } },
                                               { { 0.00079, 0.00149, 0.00211, 0.00331 }, { 0.00097, 0.00157, 0.00263, 0.00359 } } };
    static constexpr double kPost[2][3] = { { 0.00113, 0.00191, 0.00283 }, { 0.00127, 0.00179, 0.00271 } };

    GatedEngine()
    {
        // Fixed irregular tap positions (fraction of the length), per channel.
        SmoothRandom r;
        for (std::size_t ch = 0; ch < 2; ++ch)
        {
            r.seed(5u + static_cast<std::uint32_t>(ch), 0.0f);
            for (std::size_t k = 0; k < kTaps; ++k)
                position[ch][k] = (static_cast<float>(k) + 0.15f + 0.7f * r.next01()) / static_cast<float>(kTaps);
        }
    }

    std::size_t floatsNeeded(double sr) const noexcept
    {
        const auto maxScale = kSizeRange[gated].hi;
        std::size_t total = 4 * Line::floatsFor(samples(kDecayRange[gated].hi + 0.002, sr));
        for (std::size_t ch = 0; ch < 2; ++ch)
        {
            for (const auto& chain : kDiff[ch])
                for (const auto t : chain) total += Line::floatsFor(samples(t * maxScale + 0.0001, sr));
            for (const auto t : kPost[ch]) total += Line::floatsFor(samples(t * maxScale + 0.0001, sr));
        }
        return total;
    }

    bool bind(Arena& arena, double sr, float initialScale, float initialLength)
    {
        sampleRate = sr;
        const auto maxScale = kSizeRange[gated].hi;
        bool ok = true;
        for (std::size_t ch = 0; ch < 2; ++ch)
        {
            for (std::size_t c = 0; c < 2; ++c)
            {
                ok = lines[ch][c].bind(arena, samples(kDecayRange[gated].hi + 0.002, sr)) && ok;
                for (std::size_t k = 0; k < 4; ++k)
                {
                    ok = pre[ch][c][k].line.bind(arena, samples(kDiff[ch][c][k] * maxScale + 0.0001, sr)) && ok;
                    pre[ch][c][k].delay = preTarget[ch][c][k] = std::round(static_cast<float>(kDiff[ch][c][k] * initialScale * sr));
                }
            }
            for (std::size_t k = 0; k < 3; ++k)
            {
                ok = post[ch][k].line.bind(arena, samples(kPost[ch][k] * maxScale + 0.0001, sr)) && ok;
                post[ch][k].delay = postTarget[ch][k] = std::round(static_cast<float>(kPost[ch][k] * initialScale * sr));
            }
            tone[ch].clear();
        }
        length = lengthTarget = static_cast<float>(initialLength * sr);
        glide = glideCoefficient(sr);
        return ok;
    }

    void control(const Controls& c)
    {
        lengthTarget = static_cast<float>(c.decaySeconds * c.sampleRate);
        const auto g = 0.45f + 0.2f * c.diffusion;
        for (std::size_t ch = 0; ch < 2; ++ch)
        {
            for (std::size_t chain = 0; chain < 2; ++chain)
                for (std::size_t k = 0; k < 4; ++k)
                {
                    auto& a = pre[ch][chain][k];
                    a.gain = ((k + chain) & 1) ? -g : g;
                    preTarget[ch][chain][k] = std::round(static_cast<float>(kDiff[ch][chain][k] * c.sizeScale * c.sampleRate));
                }
            for (std::size_t k = 0; k < 3; ++k)
            {
                post[ch][k].gain = (k & 1) ? 0.5f : -0.5f;
                postTarget[ch][k] = std::round(static_cast<float>(kPost[ch][k] * c.sizeScale * c.sampleRate));
            }
            tone[ch].setCutoff(highCornerHz(c.damping, 16000.0f), c.sampleRate);
        }
        // Envelope from SHAPE, normalised to unit energy so SHAPE changes the
        // contour, not the level.
        const auto shape = std::clamp(c.shape, 0.0f, 1.0f);
        double energy = 0.0;
        for (std::size_t ch = 0; ch < 2; ++ch)
            for (std::size_t k = 0; k < kTaps; ++k)
            {
                const auto u = position[ch][k];
                float env;
                if (shape < 0.5f)
                {
                    const auto s = 1.0f - 2.0f * shape;
                    env = (1.0f - s) + s * std::pow(u, 1.5f);
                }
                else
                {
                    const auto s = 2.0f * shape - 1.0f;
                    env = (1.0f - s) + s * std::pow(1.0f - u, 1.5f);
                }
                if (u > 0.85f) { env *= smoothstep01((1.0f - u) / 0.15f); }
                gain[ch][k] = env;
                energy += static_cast<double>(env) * env;
            }
        const auto norm = static_cast<float>(1.0 / std::sqrt(std::max(1.0e-9, energy * 0.5)));
        for (auto& ch : gain)
            for (auto& v : ch) { v *= norm; }
    }

    void process(float inL, float inR, float& outL, float& outR) noexcept
    {
        length += glide * (lengthTarget - length);
        const float in[2] = { inL, inR };
        float result[2];
        for (std::size_t ch = 0; ch < 2; ++ch)
        {
            for (std::size_t c = 0; c < 2; ++c)
            {
                auto x = in[ch];
                for (std::size_t k = 0; k < 4; ++k)
                {
                    auto& a = pre[ch][c][k];
                    a.delay = glideTo(a.delay, preTarget[ch][c][k], glide);
                    x = a.processGliding(x, a.delay);
                }
                lines[ch][c].push(x);
            }
            float y = 0.0f;
            for (std::size_t k = 0; k < kTaps; ++k)
            {
                const auto& line = lines[ch][k & 1];
                const auto d = std::max(1.0f, position[ch][k] * length);
                const auto di = static_cast<int>(d);
                const auto f = d - static_cast<float>(di);
                const auto p0 = line.tap(di), p1 = line.tap(di + 1);
                y += gain[ch][k] * (p0 + f * (p1 - p0));
            }
            for (std::size_t k = 0; k < 3; ++k)
            {
                auto& a = post[ch][k];
                a.delay = glideTo(a.delay, postTarget[ch][k], glide);
                y = a.processGliding(y, a.delay);
            }
            result[ch] = tone[ch].process(y);
        }
        outL = result[0];
        outR = result[1];
    }

private:
    double sampleRate { 48000.0 };
    std::array<std::array<Line, 2>, 2> lines;
    std::array<std::array<std::array<Allpass, 4>, 2>, 2> pre;
    std::array<std::array<Allpass, 3>, 2> post;
    std::array<OnePoleLowpass, 2> tone;
    std::array<std::array<float, kTaps>, 2> position {}, gain {};
    std::array<std::array<std::array<float, 4>, 2>, 2> preTarget {};
    std::array<std::array<float, 3>, 2> postTarget {};
    float length { 0.0f }, lengthTarget { 0.0f }, glide { 0.001f };
};
} // namespace px3::reverb
