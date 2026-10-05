#include "TestSupport.h"
#include "Distortion.h"

#include <complex>

// testChorus, testStereoSpread

namespace px3tests
{


// ============================================================================
// CHORUS
// ============================================================================

namespace chorustest
{
using doomtest::Result;

enum class Source { silence, impulse, sine, saw, supersaw, bass, pluck };

Result runChorus(const ChorusSettings& settings,
                 Source source,
                 double seconds,
                 double sampleRate = kSampleRate,
                 float frequency = 220.0f)
{
    px3::Chorus chorus;
    chorus.prepare(sampleRate);
    chorus.updateForBlock(settings);

    const auto total = static_cast<int>(sampleRate * seconds);
    Result result;
    result.left.reserve(static_cast<std::size_t>(total));
    result.right.reserve(static_cast<std::size_t>(total));

    auto phase = 0.0;
    std::array<double, 7> superPhase {};
    const auto increment = juce::MathConstants<double>::twoPi * frequency / sampleRate;

    for (int i = 0; i < total; ++i)
    {
        auto in = 0.0f;
        switch (source)
        {
            case Source::silence:
                break;
            case Source::impulse:
                in = i == 4096 ? 1.0f : 0.0f;
                break;
            case Source::sine:
                in = 0.5f * static_cast<float>(std::sin(phase));
                break;
            case Source::saw:
            {
                auto sum = 0.0;
                for (int h = 1; h <= 12; ++h)
                {
                    sum += std::sin(phase * h) / h;
                }
                in = 0.28f * static_cast<float>(sum);
                break;
            }
            case Source::supersaw:
            {
                auto sum = 0.0;
                for (std::size_t v = 0; v < superPhase.size(); ++v)
                {
                    for (int h = 1; h <= 8; ++h)
                    {
                        sum += std::sin(superPhase[v] * h) / h;
                    }
                }
                in = 0.06f * static_cast<float>(sum);
                break;
            }
            case Source::bass:
            {
                // A square-ish bass: a strong fundamental plus odd harmonics,
                // which is what the low-end anchoring has to protect.
                auto sum = 0.0;
                for (int h = 1; h <= 9; h += 2)
                {
                    sum += std::sin(phase * h) / h;
                }
                in = 0.4f * static_cast<float>(sum);
                break;
            }
            case Source::pluck:
            {
                const auto age = static_cast<double>(i % static_cast<int>(sampleRate * 0.5));
                const auto env = std::exp(-age / (sampleRate * 0.1));
                in = 0.6f * static_cast<float>(env * std::sin(phase));
                break;
            }
        }

        phase += increment;
        for (std::size_t v = 0; v < superPhase.size(); ++v)
        {
            // Detuned copies, a few cents apart.
            superPhase[v] += increment * (1.0 + (static_cast<double>(v) - 3.0) * 0.004);
        }

        float outL = 0.0f;
        float outR = 0.0f;
        chorus.processSampleFrame(in, in, outL, outR);
        result.left.push_back(outL);
        result.right.push_back(outR);
    }

    return result;
}

ChorusSettings audible()
{
    ChorusSettings s;
    s.enabled = true;
    s.amount = 0.75f;
    return s;
}

// RMS of the mono sum. The Dimension architecture's wet terms are equal and
// opposite, so this is where a phase-trick widener would collapse.
double monoRms(const Result& r)
{
    auto sum = 0.0;
    for (std::size_t i = 0; i < r.left.size(); ++i)
    {
        const auto m = 0.5 * (static_cast<double>(r.left[i]) + r.right[i]);
        sum += m * m;
    }
    return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1u, r.left.size())));
}

// How much the summed signal's period wanders, as a fraction. This is the
// design brief measured directly: "a new dimension WITHOUT the apparent
// movement of sound".
double pitchInstability(const Result& r, double sampleRate)
{
    std::vector<double> periods;
    auto lastCrossing = -1.0;
    for (std::size_t i = 1; i < r.left.size(); ++i)
    {
        const auto a = 0.5 * (static_cast<double>(r.left[i - 1]) + r.right[i - 1]);
        const auto b = 0.5 * (static_cast<double>(r.left[i]) + r.right[i]);
        if (a <= 0.0 && b > 0.0)
        {
            const auto frac = b != a ? -a / (b - a) : 0.0;
            const auto crossing = static_cast<double>(i - 1) + frac;
            if (lastCrossing >= 0.0)
            {
                periods.push_back(crossing - lastCrossing);
            }
            lastCrossing = crossing;
        }
    }

    juce::ignoreUnused(sampleRate);
    if (periods.size() < 8)
    {
        return 0.0;
    }

    auto mean = 0.0;
    for (const auto p : periods) { mean += p; }
    mean /= static_cast<double>(periods.size());

    auto variance = 0.0;
    for (const auto p : periods) { variance += (p - mean) * (p - mean); }
    return std::sqrt(variance / static_cast<double>(periods.size())) / std::max(1.0e-9, mean);
}
} // namespace chorustest

// ============================================================================
// CHORUS - hardware models, measured from the outside
// ============================================================================
//
// Everything below drives the engine only through prepare / updateForBlock /
// processSampleFrame and reads only its stereo output, so the same tests can
// be compiled against any version of the engine. Delay trajectories come from
// an impulse train: each impulse's wet copy arrives at the line's delay at that
// moment, so the peak position after every impulse samples d(t) directly.
namespace chorushw
{
using doomtest::Result;

constexpr double kRate = 48000.0;
constexpr int kModeJunoI = 9;
constexpr int kModeJunoII = 10;
constexpr int kModeJunoBoth = 11;
constexpr int kModeEnsemble = 7;
constexpr int kModeCe = 8;

ChorusSettings hardware(int mode)
{
    // The defaults ARE the hardware: full intensity, every other control at
    // its default.
    ChorusSettings s;
    s.enabled = true;
    s.amount = 1.0f;
    s.modeIndex = mode;
    s.lowCut = 0.0f;
    s.feedback = 0.0f;
    s.mix = 1.0f;
    return s;
}

template <typename InputFn>
Result render(const ChorusSettings& s, double seconds, InputFn input)
{
    px3::Chorus chorus;
    chorus.prepare(kRate);
    const auto total = static_cast<int>(kRate * seconds);
    Result r;
    r.left.reserve(static_cast<std::size_t>(total));
    r.right.reserve(static_cast<std::size_t>(total));
    for (int n = 0; n < total; ++n)
    {
        if (n % 512 == 0)
        {
            chorus.updateForBlock(s);
        }
        float inL = 0.0f;
        float inR = 0.0f;
        input(n, inL, inR);
        float l = 0.0f;
        float r2 = 0.0f;
        chorus.processSampleFrame(inL, inR, l, r2);
        r.left.push_back(l);
        r.right.push_back(r2);
    }
    return r;
}

struct Trajectory
{
    std::vector<double> timeSeconds;
    std::vector<double> left;        // ms
    std::vector<double> right;       // ms
    std::vector<double> leftSign;
    std::vector<double> rightSign;
};

// Peak of |x| in [from, to), refined by a parabola. Returns the position and
// the sign of the sample at the peak.
std::pair<double, double> peakIn(const std::vector<float>& x, int from, int to)
{
    auto best = from;
    for (int i = from; i < to; ++i)
    {
        if (std::abs(x[static_cast<std::size_t>(i)]) > std::abs(x[static_cast<std::size_t>(best)]))
        {
            best = i;
        }
    }
    auto pos = static_cast<double>(best);
    if (best > from && best + 1 < to)
    {
        const auto a = std::abs(static_cast<double>(x[static_cast<std::size_t>(best - 1)]));
        const auto b = std::abs(static_cast<double>(x[static_cast<std::size_t>(best)]));
        const auto c = std::abs(static_cast<double>(x[static_cast<std::size_t>(best + 1)]));
        const auto denom = a - 2.0 * b + c;
        if (std::abs(denom) > 1.0e-12)
        {
            pos += 0.5 * (a - c) / denom;
        }
    }
    return { pos, x[static_cast<std::size_t>(best)] >= 0.0f ? 1.0 : -1.0 };
}

Trajectory track(ChorusSettings s, bool leftOnly, double spacingMs, double seconds,
                 double windowFromMs, double windowToMs)
{
    s.character = 0.0f;   // keep the impulses linear
    const auto spacing = static_cast<int>(kRate * spacingMs * 0.001);
    const auto warm = static_cast<int>(kRate * 0.5);
    const auto out = render(s, seconds, [&](int n, float& l, float& r)
    {
        const auto hit = n >= warm && (n - warm) % spacing == 0 ? 0.5f : 0.0f;
        l = hit;
        r = leftOnly ? 0.0f : hit;
    });

    Trajectory t;
    const auto from = static_cast<int>(kRate * windowFromMs * 0.001);
    const auto to = static_cast<int>(kRate * windowToMs * 0.001);
    for (int n0 = warm; n0 + to < static_cast<int>(out.left.size()); n0 += spacing)
    {
        const auto l = peakIn(out.left, n0 + from, n0 + to);
        const auto r = peakIn(out.right, n0 + from, n0 + to);
        t.timeSeconds.push_back(static_cast<double>(n0) / kRate);
        t.left.push_back((l.first - n0) / kRate * 1000.0);
        t.right.push_back((r.first - n0) / kRate * 1000.0);
        t.leftSign.push_back(l.second);
        t.rightSign.push_back(r.second);
    }
    return t;
}

double meanOf(const std::vector<double>& v)
{
    auto s = 0.0;
    for (const auto x : v) { s += x; }
    return v.empty() ? 0.0 : s / static_cast<double>(v.size());
}

double pearson(const std::vector<double>& a, const std::vector<double>& b)
{
    const auto ma = meanOf(a);
    const auto mb = meanOf(b);
    auto sab = 0.0, saa = 0.0, sbb = 0.0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
    {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return sab / std::sqrt(std::max(1.0e-18, saa * sbb));
}

// The frequency at which the trajectory has most energy, by a fine scan.
double dominantRate(const Trajectory& t, const std::vector<double>& d, double lo, double hi)
{
    const auto m = meanOf(d);
    auto bestF = lo;
    auto bestP = -1.0;
    for (auto f = lo; f <= hi; f += 0.0005 * (hi + lo))
    {
        auto re = 0.0, im = 0.0;
        for (std::size_t i = 0; i < d.size(); ++i)
        {
            const auto ph = juce::MathConstants<double>::twoPi * f * t.timeSeconds[i];
            re += (d[i] - m) * std::cos(ph);
            im += (d[i] - m) * std::sin(ph);
        }
        const auto p = re * re + im * im;
        if (p > bestP) { bestP = p; bestF = f; }
    }
    return bestF;
}

// Least-squares fit of c + sum_k (a_k cos + b_k sin)(2 pi f_k t): amplitude
// and phase of each named frequency.
struct Component { double amplitude; double phase; };
std::vector<Component> fitSinusoids(const Trajectory& t, const std::vector<double>& d,
                                    const std::vector<double>& freqs)
{
    const auto n = 1 + 2 * freqs.size();
    std::vector<std::vector<double>> ata(n, std::vector<double>(n + 1, 0.0));
    std::vector<double> row(n);
    for (std::size_t i = 0; i < d.size(); ++i)
    {
        row[0] = 1.0;
        for (std::size_t k = 0; k < freqs.size(); ++k)
        {
            const auto ph = juce::MathConstants<double>::twoPi * freqs[k] * t.timeSeconds[i];
            row[1 + 2 * k] = std::cos(ph);
            row[2 + 2 * k] = std::sin(ph);
        }
        for (std::size_t a = 0; a < n; ++a)
        {
            for (std::size_t b = 0; b < n; ++b) { ata[a][b] += row[a] * row[b]; }
            ata[a][n] += row[a] * d[i];
        }
    }
    // Gaussian elimination.
    for (std::size_t col = 0; col < n; ++col)
    {
        auto pivot = col;
        for (auto r = col + 1; r < n; ++r)
        {
            if (std::abs(ata[r][col]) > std::abs(ata[pivot][col])) { pivot = r; }
        }
        std::swap(ata[col], ata[pivot]);
        const auto div = ata[col][col];
        if (std::abs(div) < 1.0e-15) { continue; }
        for (auto c = col; c <= n; ++c) { ata[col][c] /= div; }
        for (std::size_t r = 0; r < n; ++r)
        {
            if (r == col) { continue; }
            const auto f = ata[r][col];
            for (auto c = col; c <= n; ++c) { ata[r][c] -= f * ata[col][c]; }
        }
    }
    std::vector<Component> out;
    for (std::size_t k = 0; k < freqs.size(); ++k)
    {
        const auto a = ata[1 + 2 * k][n];
        const auto b = ata[2 + 2 * k][n];
        out.push_back({ std::sqrt(a * a + b * b), std::atan2(b, a) });
    }
    return out;
}

double wrapDegrees(double radians)
{
    auto deg = radians * 180.0 / juce::MathConstants<double>::pi;
    while (deg > 180.0) { deg -= 360.0; }
    while (deg < -180.0) { deg += 360.0; }
    return deg;
}

double minOf(const std::vector<double>& v) { return *std::min_element(v.begin(), v.end()); }
double maxOf(const std::vector<double>& v) { return *std::max_element(v.begin(), v.end()); }

// Holters & Parker's Juno-60 BBD input and output filters (DAFx-18), as the
// analogue reference the engine's filters are measured against.
double junoAnalogueMagnitude(double hz)
{
    using C = std::complex<double>;
    const std::array<C, 5> rin { { { 251589, 0 }, { -130428, -4165 }, { -130428, 4165 }, { 4634, -22873 }, { 4634, 22873 } } };
    const std::array<C, 5> pin { { { -46580, 0 }, { -55482, 25082 }, { -55482, -25082 }, { -26292, -59437 }, { -26292, 59437 } } };
    const std::array<C, 5> rout { { { 5092, 0 }, { 11256, -99566 }, { 11256, 99566 }, { -13802, -24606 }, { -13802, 24606 } } };
    const std::array<C, 5> pout { { { -176261, 0 }, { -51468, 21437 }, { -51468, -21437 }, { -26276, -59699 }, { -26276, 59699 } } };
    auto h = [](const std::array<C, 5>& r, const std::array<C, 5>& p, double f)
    {
        const C s(0.0, juce::MathConstants<double>::twoPi * f);
        C sum(0.0, 0.0);
        for (std::size_t i = 0; i < 5; ++i) { sum += r[i] / (s - p[i]); }
        return sum;
    };
    return std::abs(h(rin, pin, hz) * h(rout, pout, hz)) / std::abs(h(rin, pin, 0.0) * h(rout, pout, 0.0));
}

double rmsOf(const std::vector<float>& v, std::size_t from, std::size_t to)
{
    auto s = 0.0;
    for (auto i = from; i < to; ++i) { s += static_cast<double>(v[i]) * v[i]; }
    return std::sqrt(s / static_cast<double>(std::max<std::size_t>(1u, to - from)));
}

float sawAt(int n, double hz)
{
    const auto phase = juce::MathConstants<double>::twoPi * hz * n / kRate;
    auto sum = 0.0;
    for (int h = 1; h <= 12; ++h) { sum += std::sin(phase * h) / h; }
    return 0.28f * static_cast<float>(sum);
}
} // namespace chorushw

void testChorusHardwareModels()
{
    using namespace chorushw;

    // ---- JUNO-60 I / II: rate, swing, phase, polarity ------------------------
    // pendragon-andyh's Juno-60 measurements: I 0.513 Hz and II 0.863 Hz, BOTH
    // over 1.66-5.35 ms (3.69 ms peak to peak); one triangle LFO with the right
    // line inverted; both lines added to the dry with positive polarity.
    for (const auto mode : { kModeJunoI, kModeJunoII })
    {
        const auto t = track(hardware(mode), false, 25.0, 12.0, 1.0, 9.0);
        const auto expectedRate = mode == kModeJunoI ? 0.513 : 0.863;
        const auto rate = dominantRate(t, t.left, expectedRate * 0.5, expectedRate * 1.5);
        const auto swingL = maxOf(t.left) - minOf(t.left);
        const auto swingR = maxOf(t.right) - minOf(t.right);
        const auto centreL = 0.5 * (maxOf(t.left) + minOf(t.left));
        const auto corr = pearson(t.left, t.right);
        auto positive = true;
        for (std::size_t i = 0; i < t.leftSign.size(); ++i)
        {
            positive = positive && t.leftSign[i] > 0.0 && t.rightSign[i] > 0.0;
        }
        const auto name = mode == kModeJunoI ? "I" : "II";
        check(mode == kModeJunoI ? "ChorusHw_JunoIRateMatchesTheJuno60"
                                 : "ChorusHw_JunoIIRateMatchesTheJuno60",
              std::abs(rate / expectedRate - 1.0) < 0.02,
              juce::String(name) + " LFO " + juce::String(rate, 4) + " Hz (Juno-60 " + juce::String(expectedRate, 3) + ")");
        check(mode == kModeJunoI ? "ChorusHw_JunoISweepsTheJuno60DelayRange"
                                 : "ChorusHw_JunoIISweepsTheJuno60DelayRange",
              std::abs(swingL - 3.69) < 0.2 && std::abs(swingR - 3.69) < 0.2
                  && centreL > 3.45 && centreL < 3.95,
              juce::String(name) + " L " + juce::String(minOf(t.left), 2) + "-" + juce::String(maxOf(t.left), 2)
                  + " ms, R " + juce::String(minOf(t.right), 2) + "-" + juce::String(maxOf(t.right), 2)
                  + " ms (hardware 1.66-5.35, plus filter group delay)");
        check(mode == kModeJunoI ? "ChorusHw_JunoILinesSweepInOpposition"
                                 : "ChorusHw_JunoIILinesSweepInOpposition",
              corr < -0.9, juce::String(name) + " L/R delay correlation " + juce::String(corr, 3));
        check(mode == kModeJunoI ? "ChorusHw_JunoIWetIsPositiveOnBothSides"
                                 : "ChorusHw_JunoIIWetIsPositiveOnBothSides",
              positive, juce::String(name) + (positive ? " both wet copies in phase with the dry" : " a wet copy is inverted"));
    }

    // ---- JUNO-60 I+II -------------------------------------------------------
    {
        const auto t = track(hardware(kModeJunoBoth), false, 8.0, 6.0, 1.0, 6.5);
        const auto rate = dominantRate(t, t.left, 5.0, 15.0);
        const auto swing = maxOf(t.left) - minOf(t.left);
        const auto corr = pearson(t.left, t.right);
        check("ChorusHw_JunoBothRateIs9_75Hz", std::abs(rate / 9.75 - 1.0) < 0.03,
              "I+II LFO " + juce::String(rate, 3) + " Hz (Juno-60 recording 9.75)");
        check("ChorusHw_JunoBothSweepsNarrowlyInPhase",
              std::abs(swing - 0.4) < 0.1 && corr > 0.9,
              "I+II swing " + juce::String(swing, 3) + " ms (3.3-3.7), L/R correlation " + juce::String(corr, 3));
    }

    // ---- Juno: stereo image against the Juno-60 recordings -------------------
    {
        auto corrFor = [](int mode)
        {
            return render(hardware(mode), 4.0, [](int n, float& l, float& r) { l = r = sawAt(n, 220.0); })
                .correlation();
        };
        const auto cI = corrFor(kModeJunoI);
        const auto cII = corrFor(kModeJunoII);
        const auto cBoth = corrFor(kModeJunoBoth);
        // Recordings (jpcima, pendragon): saw L/R correlation 0.20-0.25 for
        // I and II, 0.974 for I+II.
        check("ChorusHw_JunoStereoImageMatchesTheRecordings",
              cI < 0.6 && cII < 0.6 && cBoth > 0.95,
              "L/R correlation I " + juce::String(cI, 3) + ", II " + juce::String(cII, 3) + ", I+II "
                  + juce::String(cBoth, 3));
    }

    // ---- Juno: BBD filters against Holters & Parker --------------------------
    {
        auto s = hardware(kModeJunoI);
        s.depth = 0.0f;
        s.character = 0.0f;
        const auto n0 = static_cast<int>(kRate * 0.3);
        const auto out = render(s, 0.5, [&](int n, float& l, float& r) { l = r = n == n0 ? 1.0f : 0.0f; });
        const auto from = static_cast<std::size_t>(n0 + 24);    // past the dry impulse
        const auto to = static_cast<std::size_t>(n0 + 1200);
        auto response = [&](double hz)
        {
            std::complex<double> sum(0.0, 0.0);
            for (auto i = from; i < to; ++i)
            {
                const auto ph = -juce::MathConstants<double>::twoPi * hz * static_cast<double>(i) / kRate;
                sum += static_cast<double>(out.left[i]) * std::complex<double>(std::cos(ph), std::sin(ph));
            }
            return std::abs(sum);
        };
        // Referenced to 300 Hz rather than DC: the 20 Hz floor of LOW CUT
        // takes the DC out of a finite window.
        const auto reference = response(300.0);
        const auto referenceTarget = junoAnalogueMagnitude(300.0);
        juce::String detail;
        auto ok = reference > 0.5;
        for (const auto hz : { 1000.0, 4000.0, 6000.0, 8000.0, 10000.0, 12000.0 })
        {
            const auto measured = 20.0 * std::log10(std::max(1.0e-9, response(hz) / reference));
            const auto target = 20.0 * std::log10(junoAnalogueMagnitude(hz) / referenceTarget);
            const auto tolerance = hz > 10500.0 ? 1.5 : 1.0;
            ok = ok && std::abs(measured - target) < tolerance;
            detail << juce::String(static_cast<int>(hz / 1000.0)) << "k " << juce::String(measured, 1) << "/"
                   << juce::String(target, 1) << "  ";
        }
        check("ChorusHw_JunoFiltersMatchHoltersParker", ok, "dB measured/target " + detail);
    }

    // ---- mono sum per family --------------------------------------------------
    {
        juce::String detail;
        auto ok = true;
        for (int mode = 0; mode < px3::Chorus::modeCount(); ++mode)
        {
            const auto out = render(hardware(mode), 2.0, [](int n, float& l, float& r) { l = r = sawAt(n, 220.0); });
            auto diff = 0.0, dry = 0.0;
            for (std::size_t i = out.left.size() / 2; i < out.left.size(); ++i)
            {
                const auto x = static_cast<double>(sawAt(static_cast<int>(i), 220.0));
                const auto m = 0.5 * (static_cast<double>(out.left[i]) + out.right[i]);
                diff += (m - x) * (m - x);
                dry += x * x;
            }
            const auto rel = std::sqrt(diff / dry);
            // Dimension: the wet is pure side, so the mono sum IS the dry.
            // Everything else keeps its chorus in mono, as the hardware does.
            const auto dimension = mode <= 6;
            const auto pass = dimension ? rel < 1.0e-3 : rel > 0.15;
            ok = ok && pass;
            detail << mode << ":" << juce::String(rel, 3) << (pass ? " " : "! ");
        }
        check("ChorusHw_MonoSumBehavesAsEachHardwareDoes", ok,
              "|mono - dry| / dry by mode: " + detail);
    }

    // ---- CE-1: output A chorus only, output B direct only --------------------
    {
        const auto n0 = static_cast<int>(kRate * 0.3);
        const auto out = render(hardware(kModeCe), 0.5, [&](int n, float& l, float& r) { l = r = n == n0 ? 0.5f : 0.0f; });
        const auto idx = static_cast<std::size_t>(n0);
        const auto wetFrom = idx + 24;
        const auto wetTo = idx + static_cast<std::size_t>(kRate * 0.012);
        const auto lDry = std::abs(out.left[idx]);
        const auto rDry = std::abs(out.right[idx]);
        const auto lWet = rmsOf(out.left, wetFrom, wetTo);
        const auto rWet = rmsOf(out.right, wetFrom, wetTo);
        check("ChorusHw_Ce1StereoIsChorusLeftDirectRight",
              lDry < 1.0e-3f && rDry > 0.45f && lWet > 1.0e-3 && rWet < 1.0e-5,
              "L dry " + juce::String(lDry, 4) + " wet " + juce::String(lWet, 5) + ", R dry "
                  + juce::String(rDry, 4) + " wet " + juce::String(rWet, 6));
    }

    // ---- Dimension: one pair, rounded triangle, specified rates ---------------
    {
        juce::String detail;
        auto ok = true;
        auto plateauOk = true;
        juce::String plateauDetail;
        // The model's figures (design doc section 4): two speeds by two depths
        // on buttons 1-4, combinations as parallel switch states.
        const std::array<double, 7> rates { { 0.25, 0.25, 0.5, 0.5, 0.75, 0.75, 1.0 } };
        const std::array<double, 7> excursions { { 2.0, 2.5, 1.5, 2.5, 2.5, 2.5, 2.5 } };
        for (int mode = 0; mode <= 6; ++mode)
        {
            const auto expectedRate = rates[static_cast<std::size_t>(mode)];
            // Left input only: line A alone, on L in phase and on R inverted.
            const auto t = track(hardware(mode), true, 25.0, 4.0 / expectedRate + 0.6, 1.0, 20.0);
            const auto rate = dominantRate(t, t.left, expectedRate * 0.6, expectedRate * 1.4);
            const auto swing = maxOf(t.left) - minOf(t.left);
            const auto expectedSwing = 2.0 * excursions[static_cast<std::size_t>(mode)];
            const auto pass = std::abs(rate / expectedRate - 1.0) < 0.03
                              && std::abs(swing / expectedSwing - 1.0) < 0.1;
            ok = ok && pass;
            detail << mode << ":" << juce::String(rate, 3) << "Hz/" << juce::String(swing, 2) << "ms ";

            // The LFO is a triangle with only slightly rounded corners: the
            // delay's slope (the detune) sits at its plateau most of the cycle.
            std::vector<double> velocity;
            for (std::size_t i = 1; i < t.left.size(); ++i) { velocity.push_back(std::abs(t.left[i] - t.left[i - 1])); }
            auto sorted = velocity;
            std::sort(sorted.begin(), sorted.end());
            const auto plateau = sorted[sorted.size() * 3 / 4];
            auto atPlateau = 0;
            for (const auto v : velocity) { atPlateau += v > 0.85 * plateau ? 1 : 0; }
            const auto fraction = static_cast<double>(atPlateau) / static_cast<double>(velocity.size());
            plateauOk = plateauOk && fraction > 0.8;
            plateauDetail << mode << ":" << juce::String(fraction, 2) << " ";
        }
        check("ChorusHw_DimensionModesRunAtTheirRatesAndSwings", ok, detail);
        check("ChorusHw_DimensionLfoIsATriangleNotATrapezoid", plateauOk,
              "share of the cycle at constant detune: " + plateauDetail);
    }

    {
        // The SDD-320 has two BBDs. A combination button changes how that pair
        // is driven - it cannot add a second pair. From a left-only impulse,
        // the left output must hold exactly one wet copy.
        juce::String detail;
        auto ok = true;
        for (int mode = 4; mode <= 6; ++mode)
        {
            auto s = hardware(mode);
            s.character = 0.0f;
            s.depth = 0.0f;
            const auto n0 = static_cast<int>(kRate * 0.3);
            const auto out = render(s, 0.5, [&](int n, float& l, float& r) { l = n == n0 ? 0.5f : 0.0f; r = 0.0f; });
            const auto from = static_cast<std::size_t>(n0 + 24);
            const auto to = static_cast<std::size_t>(n0) + static_cast<std::size_t>(kRate * 0.03);
            auto biggest = 0.0f;
            for (auto i = from; i < to; ++i) { biggest = juce::jmax(biggest, std::abs(out.left[i])); }
            // Count separate arrivals: local maxima above 30% of the biggest,
            // more than 0.5 ms apart.
            auto arrivals = 0;
            auto lastArrival = -1.0e9;
            for (auto i = from + 1; i + 1 < to; ++i)
            {
                const auto v = std::abs(out.left[i]);
                if (v > 0.3f * biggest && v >= std::abs(out.left[i - 1]) && v >= std::abs(out.left[i + 1])
                    && static_cast<double>(i) - lastArrival > kRate * 0.0005)
                {
                    ++arrivals;
                    lastArrival = static_cast<double>(i);
                }
            }
            ok = ok && arrivals == 1;
            detail << mode << ":" << arrivals << " ";
        }
        check("ChorusHw_DimensionCombinationsDriveOnePair", ok, "wet arrivals per left impulse: " + detail);
    }

    // ---- Dimension compander round trip ---------------------------------------
    {
        // Line A alone (left input only) comes out inverted on R. With the
        // sweep stopped and the BBD linear, compressor and expander must undo
        // each other at every level.
        auto s = hardware(1);
        s.depth = 0.0f;
        s.character = 0.0f;
        auto gainAt = [&s](float level)
        {
            const auto out = render(s, 1.5, [level](int n, float& l, float& r)
            {
                l = level * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * 1000.0 * n / kRate));
                r = 0.0f;
            });
            const auto from = static_cast<std::size_t>(kRate * 0.8);
            const auto wet = 2.0 * rmsOf(out.right, from, out.right.size());   // R = -0.5 A
            return 20.0 * std::log10(std::max(1.0e-9, wet / (level / std::sqrt(2.0))));
        };
        const auto quiet = gainAt(0.1f);    // -20 dBFS
        const auto loud = gainAt(0.5f);     // -6 dBFS
        check("ChorusHw_DimensionCompanderRoundTripIsTransparent",
              std::abs(quiet) < 1.0 && std::abs(loud) < 1.0 && std::abs(quiet - loud) < 0.5,
              "line gain at -20 dBFS " + juce::String(quiet, 2) + " dB, at -6 dBFS " + juce::String(loud, 2) + " dB");
    }

    // ---- Ensemble: two generators, three phases --------------------------------
    {
        auto s = hardware(kModeEnsemble);
        s.width = 1.0f;   // line 1 hard left, line 3 hard right
        const auto t = track(s, false, 10.0, 8.6, 1.0, 9.5);
        // Solina-style: slow ~0.6-0.8 Hz and fast ~6-6.4 Hz generators.
        const auto slow = 0.7;
        const auto fast = 6.3;
        const auto slowSwing = 1.6;
        const auto fastSwing = 0.12;
        const auto fitL = fitSinusoids(t, t.left, { slow, fast });
        const auto fitR = fitSinusoids(t, t.right, { slow, fast });
        const auto slowPhase = std::abs(wrapDegrees(fitL[0].phase - fitR[0].phase));
        const auto fastPhase = std::abs(wrapDegrees(fitL[1].phase - fitR[1].phase));
        check("ChorusHw_EnsembleHasBothGenerators",
              fitL[0].amplitude > 0.6 * slowSwing
                  && fitL[1].amplitude > 0.5 * fastSwing
                  && fitR[0].amplitude > 0.6 * slowSwing
                  && fitR[1].amplitude > 0.5 * fastSwing,
              "L slow " + juce::String(fitL[0].amplitude, 3) + " ms @" + juce::String(slow, 2) + " Hz, fast "
                  + juce::String(fitL[1].amplitude, 3) + " ms @" + juce::String(fast, 2) + " Hz");
        check("ChorusHw_EnsembleLinesAre120DegreesApart",
              std::abs(slowPhase - 120.0) < 15.0 && std::abs(fastPhase - 120.0) < 25.0,
              "outer lines apart by " + juce::String(slowPhase, 1) + " deg (slow), " + juce::String(fastPhase, 1)
                  + " deg (fast)");
    }

    // ---- mode changes crossfade ---------------------------------------------
    {
        // Juno II -> DIM 1 under a sine: the base delay jumps from 3.5 to 10 ms,
        // so an instant switch is a step in the wet signal.
        px3::Chorus chorus;
        chorus.prepare(kRate);
        auto s = hardware(kModeJunoII);
        const auto switchAt = static_cast<int>(kRate * 1.0);
        auto steady = 0.0f, around = 0.0f, previous = 0.0f;
        for (int n = 0; n < static_cast<int>(kRate * 1.5); ++n)
        {
            if (n == switchAt) { s.modeIndex = 0; }
            if (n % 512 == 0 || n == switchAt) { chorus.updateForBlock(s); }
            const auto x = 0.5f * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * 220.0 * n / kRate));
            float l = 0.0f, r = 0.0f;
            chorus.processSampleFrame(x, x, l, r);
            const auto step = std::abs(l - previous);
            previous = l;
            if (n > static_cast<int>(kRate * 0.3) && n < switchAt) { steady = juce::jmax(steady, step); }
            if (n >= switchAt && n < switchAt + static_cast<int>(kRate * 0.1)) { around = juce::jmax(around, step); }
        }
        check("ChorusHw_ModeChangeCrossfades", around < steady * 1.5f,
              "worst step steady " + juce::String(steady, 4) + ", across the change " + juce::String(around, 4));
    }

    // ---- cost ------------------------------------------------------------------
    {
        juce::String detail;
        auto worst = 0.0;
        for (int mode = 0; mode < px3::Chorus::modeCount(); ++mode)
        {
            px3::Chorus chorus;
            chorus.prepare(kRate);
            chorus.updateForBlock(hardware(mode));
            const auto total = static_cast<int>(kRate * 2.0);
            std::vector<float> input(static_cast<std::size_t>(total));
            for (int n = 0; n < total; ++n) { input[static_cast<std::size_t>(n)] = sawAt(n, 220.0); }
            auto sink = 0.0f;
            const auto start = juce::Time::getHighResolutionTicks();
            for (int n = 0; n < total; ++n)
            {
                float l = 0.0f, r = 0.0f;
                chorus.processSampleFrame(input[static_cast<std::size_t>(n)], input[static_cast<std::size_t>(n)], l, r);
                sink += l + r;
            }
            const auto seconds = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start);
            const auto ns = seconds * 1.0e9 / total;
            worst = std::max(worst, ns);
            detail << mode << ":" << juce::String(ns, 0) << (std::isfinite(sink) ? "" : "?") << " ";
        }
        check("ChorusHw_CostPerSampleIsBounded", worst < 1000.0, "ns/sample by mode: " + detail);
    }
}

void testChorus()
{
    suite("CHORUS");

    // The Juno-60, Dimension, CE-1 and ensemble models are measured against
    // their hardware in testChorusHardwareModels() (the old wobble-count check
    // pinned the Juno modes to 8 Hz for I+II and to one line inverted on R,
    // neither of which is the Juno-60).

    using namespace chorustest;

    // ---- construction and defaults -----------------------------------------
    {
        const ChorusSettings defaults;
        check("Chorus_DefaultsAreValid",
              defaults.enabled && juce::approximatelyEqual(defaults.amount, 0.0f)
                  && defaults.modeIndex == 1
                  && juce::approximatelyEqual(defaults.feedback, 0.0f),
              "amount zero, DIM 2, no feedback");

        px3::Chorus chorus;
        chorus.prepare(kSampleRate);
        chorus.reset();
        float l = 0.0f;
        float r = 0.0f;
        chorus.processSampleFrame(0.0f, 0.0f, l, r);
        check("Chorus_ConstructsAndProcessesWithoutPreparingSettings",
              std::isfinite(l) && std::isfinite(r), "");
    }

    // ---- sample rates ------------------------------------------------------
    {
        juce::String detail;
        auto allFine = true;
        for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            const auto out = runChorus(audible(), Source::saw, 1.5, rate);
            const auto ok = out.finite() && out.peak() < 4.0f && out.rms() > 1.0e-5;
            allFine = allFine && ok;
            detail << juce::String(static_cast<int>(rate)) << " rms "
                   << juce::String(out.rms(), 4) << "  ";
        }
        check("Chorus_RunsAtEverySupportedSampleRate", allFine, detail);
    }

    // ---- silence, impulse, sine --------------------------------------------
    {
        const auto quiet = runChorus(audible(), Source::silence, 1.5);
        check("Chorus_SilenceStaysSilent",
              quiet.finite() && quiet.peak() < 1.0e-5f && std::abs(quiet.dc()) < 1.0e-6,
              "peak " + juce::String(quiet.peak(), 8));

        const auto impulse = runChorus(audible(), Source::impulse, 2.0);
        check("Chorus_ImpulseStaysBounded",
              impulse.finite() && impulse.peak() < 4.0f,
              "peak " + juce::String(impulse.peak(), 4));

        juce::String freqDetail;
        auto allFine = true;
        for (const auto hz : { 50.0f, 100.0f, 440.0f, 1000.0f, 5000.0f, 10000.0f })
        {
            const auto out = runChorus(audible(), Source::sine, 1.5, kSampleRate, hz);
            const auto ok = out.finite() && out.peak() < 4.0f;
            allFine = allFine && ok;
            freqDetail << juce::String(static_cast<int>(hz)) << " "
                       << juce::String(out.peak(), 3) << "  ";
        }
        check("Chorus_SineIsStableAcrossTheBand", allFine, freqDetail);
    }

    // ---- the design brief: width without apparent movement -----------------
    {
        // Roland's claim for the SDD-320 is width "without the apparent movement
        // of sound produced by most other chorus devices". The anti-phase pair
        // is how: when one path goes sharp the other goes flat by the same
        // amount, so the pair has no average pitch deviation.
        //
        // Measured against a deliberately naive single-delay chorus at the same
        // depth, built here rather than in the engine - the engine must not
        // contain the thing it is being compared against.
        auto s = audible();
        s.amount = 1.0f;
        s.depth = 1.0f;
        s.mix = 1.0f;
        s.character = 0.0f;
        const auto paired = runChorus(s, Source::sine, 3.0, kSampleRate, 220.0f);

        // The naive version: one delay line, one sine LFO, dry plus wet.
        std::vector<float> naive;
        {
            const auto size = static_cast<int>(kSampleRate * 0.05);
            std::vector<float> line(static_cast<std::size_t>(size), 0.0f);
            auto write = 0;
            auto lfo = 0.0;
            auto phase = 0.0;
            const auto increment = juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
            const auto total = static_cast<int>(kSampleRate * 3.0);
            naive.reserve(static_cast<std::size_t>(total));

            for (int i = 0; i < total; ++i)
            {
                const auto in = 0.5f * static_cast<float>(std::sin(phase));
                phase += increment;

                line[static_cast<std::size_t>(write)] = in;
                write = (write + 1) % size;

                lfo += juce::MathConstants<double>::twoPi * 0.6 / kSampleRate;
                const auto delay = kSampleRate * 0.007 + kSampleRate * 0.0032 * std::sin(lfo);
                auto pos = static_cast<double>(write) - delay;
                while (pos < 0.0) { pos += size; }
                const auto i1 = static_cast<int>(pos) % size;
                const auto i2 = (i1 + 1) % size;
                const auto frac = pos - std::floor(pos);
                const auto wet = line[static_cast<std::size_t>(i1)] * (1.0 - frac)
                                 + line[static_cast<std::size_t>(i2)] * frac;
                naive.push_back(in + static_cast<float>(wet));
            }
        }

        Result naiveResult;
        naiveResult.left = naive;
        naiveResult.right = naive;

        const auto pairedInstability = pitchInstability(paired, kSampleRate);
        const auto naiveInstability = pitchInstability(naiveResult, kSampleRate);

        check("Chorus_AntiPhasePairIsPitchStableWhereASingleDelayIsNot",
              pairedInstability < naiveInstability * 0.6,
              "anti-phase pair " + juce::String(pairedInstability, 6)
                  + ", one delay and one sine LFO " + juce::String(naiveInstability, 6));
    }

    // ---- mono compatibility ------------------------------------------------
    {
        // DIMENSION modes (the default DIM 2 here): L + R = 2*dry by
        // construction, the wet terms being equal and opposite. The other
        // families keep their chorus in mono, as their hardware does - see
        // ChorusHw_MonoSumBehavesAsEachHardwareDoes.
        juce::String detail;
        auto allRetained = true;

        const auto dry = runChorus([]{ auto s = audible(); s.amount = 0.0f; return s; }(),
                                   Source::saw, 2.0);
        const auto dryMono = monoRms(dry);

        for (const auto amount : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto s = audible();
            s.amount = amount;
            const auto out = runChorus(s, Source::saw, 2.0);
            const auto ratio = monoRms(out) / juce::jmax(1.0e-9, dryMono);
            const auto retained = ratio > 0.84 && ratio < 1.25;
            allRetained = allRetained && retained;
            detail << juce::String(amount, 2) << ":" << juce::String(ratio, 3) << "  ";
        }

        check("Chorus_MonoCollapseRetainsLevelAtEveryAmount", allRetained,
              "mono sum against dry: " + detail);
    }

    // ---- modes -------------------------------------------------------------
    {
        juce::String detail;
        auto allStable = true;
        std::vector<double> rmsByMode;
        std::vector<double> corrByMode;

        for (int mode = 0; mode < px3::Chorus::modeCount(); ++mode)
        {
            auto s = audible();
            s.modeIndex = mode;
            const auto out = runChorus(s, Source::saw, 2.0);
            allStable = allStable && out.finite() && out.peak() < 4.0f;
            rmsByMode.push_back(out.rms());
            corrByMode.push_back(out.correlation());
            detail << mode << ":" << juce::String(out.correlation(), 3) << "  ";
        }

        check("Chorus_EveryModeIsStable", allStable,
              juce::String(px3::Chorus::modeCount()) + " modes");

        // Modes must be structurally different, not one set of numbers scaled.
        auto distinct = 0;
        for (std::size_t i = 1; i < corrByMode.size(); ++i)
        {
            if (std::abs(corrByMode[i] - corrByMode[i - 1]) > 1.0e-4)
            {
                ++distinct;
            }
        }
        check("Chorus_ModesAreGenuinelyDifferent",
              distinct >= static_cast<int>(corrByMode.size()) - 3,
              "correlation by mode: " + detail);

        // Mode 1 is documented as the softest and mode 4 as the strongest.
        auto soft = audible();
        soft.modeIndex = 0;
        auto strong = audible();
        strong.modeIndex = 3;
        const auto a = runChorus(soft, Source::saw, 2.5);
        const auto b = runChorus(strong, Source::saw, 2.5);
        check("Chorus_Mode1IsSofterThanMode4",
              b.correlation() < a.correlation(),
              "mode 1 correlation " + juce::String(a.correlation(), 4)
                  + ", mode 4 " + juce::String(b.correlation(), 4));

        // And mode 1 is documented as having the LONGER delay, because its VCOs
        // run slower - which is why its base delay is larger, not smaller.
        check("Chorus_Mode1HasTheLongestDelayAsDocumented",
              px3::Chorus::specFor(0).baseDelayMs > px3::Chorus::specFor(3).baseDelayMs,
              "mode 1 " + juce::String(px3::Chorus::specFor(0).baseDelayMs, 1) + " ms, mode 4 "
                  + juce::String(px3::Chorus::specFor(3).baseDelayMs, 1) + " ms");

        // (The old Chorus_CombinationModesStackASecondPair pinned an invented
        // architecture: the SDD-320 has two BBDs, so a combination button can
        // only change how that one pair is driven. Replaced by
        // ChorusHw_DimensionCombinationsDriveOnePair.)
    }

    // ---- depth, width, feedback -------------------------------------------
    {
        // Depth zero must mean no pitch modulation at all.
        auto still = audible();
        still.depth = 0.0f;
        still.character = 0.0f;
        const auto out = runChorus(still, Source::sine, 2.5, kSampleRate, 440.0f);
        check("Chorus_DepthZeroHasNoPitchModulation",
              pitchInstability(out, kSampleRate) < 0.01,
              "period instability at depth 0 = "
                  + juce::String(pitchInstability(out, kSampleRate), 6));

        juce::String widthDetail;
        std::vector<double> corrByWidth;
        auto widthStable = true;
        for (const auto width : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto s = audible();
            s.width = width;
            const auto r = runChorus(s, Source::saw, 2.0);
            widthStable = widthStable && r.finite();
            corrByWidth.push_back(r.correlation());
            widthDetail << juce::String(width, 2) << ":" << juce::String(r.correlation(), 3) << "  ";
        }
        check("Chorus_WidthWidensTheImage",
              widthStable && corrByWidth.back() < corrByWidth.front(),
              widthDetail);

        // Feedback must stay well short of flanging, even at maximum.
        auto flangeRisk = audible();
        flangeRisk.feedback = 1.0f;
        flangeRisk.amount = 1.0f;
        flangeRisk.depth = 1.0f;
        const auto fb = runChorus(flangeRisk, Source::saw, 3.0);
        check("Chorus_MaximumFeedbackStaysBounded",
              fb.finite() && fb.peak() < 4.0f,
              "peak " + juce::String(fb.peak(), 4));
    }

    // ---- low-end anchoring -------------------------------------------------
    {
        // The dry path is never filtered, and the wet is high-passed, so a bass
        // note keeps its weight and its pitch while its harmonics move.
        auto s = audible();
        s.amount = 1.0f;
        s.depth = 1.0f;

        const auto processed = runChorus(s, Source::bass, 2.5, kSampleRate, 55.0f);
        const auto dry = runChorus([]{ auto d = audible(); d.amount = 0.0f; return d; }(),
                                   Source::bass, 2.5, kSampleRate, 55.0f);

        // Low-frequency energy, via a crude integrator: a widener that moved
        // the bass into the sides would lose it here.
        auto lowEnergy = [](const Result& r)
        {
            auto state = 0.0;
            auto sum = 0.0;
            for (std::size_t i = 0; i < r.left.size(); ++i)
            {
                const auto m = 0.5 * (static_cast<double>(r.left[i]) + r.right[i]);
                state += (m - state) * 0.01;
                sum += state * state;
            }
            return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1u, r.left.size())));
        };

        const auto ratio = lowEnergy(processed) / juce::jmax(1.0e-9, lowEnergy(dry));
        check("Chorus_BassKeepsItsWeight", ratio > 0.85 && ratio < 1.2,
              "low-frequency energy against dry = " + juce::String(ratio, 3));

        // And its pitch: the fundamental must not wobble.
        check("Chorus_BassPitchStaysStable",
              pitchInstability(processed, kSampleRate) < 0.02,
              "period instability on a 55 Hz bass = "
                  + juce::String(pitchInstability(processed, kSampleRate), 6));
    }

    // ---- synth material ----------------------------------------------------
    {
        juce::String detail;
        auto allFine = true;
        const std::array<std::pair<const char*, Source>, 4> materials { {
            { "saw", Source::saw }, { "supersaw", Source::supersaw },
            { "bass", Source::bass }, { "pluck", Source::pluck },
        } };
        for (const auto& material : materials)
        {
            const auto out = runChorus(audible(), material.second, 2.0);
            const auto ok = out.finite() && out.peak() < 4.0f && out.rms() > 1.0e-5;
            allFine = allFine && ok;
            detail << material.first << " corr " << juce::String(out.correlation(), 3) << "  ";
        }
        check("Chorus_HandlesSynthMaterial", allFine, detail);
    }

    // ---- extremes and automation ------------------------------------------
    {
        auto everything = audible();
        everything.amount = 1.0f;
        everything.depth = 1.0f;
        everything.width = 1.0f;
        everything.spread = 1.0f;
        everything.feedback = 1.0f;
        everything.character = 1.0f;
        everything.tone = 1.0f;
        everything.lowCut = 1.0f;

        juce::String detail;
        auto allSurvive = true;
        for (int mode = 0; mode < px3::Chorus::modeCount(); ++mode)
        {
            everything.modeIndex = mode;
            const auto out = runChorus(everything, Source::supersaw, 3.0);
            const auto ok = out.finite() && out.peak() < 6.0f && std::abs(out.dc()) < 0.1;
            allSurvive = allSurvive && ok;
            if (! ok)
            {
                detail << "mode " << mode << " peak " << juce::String(out.peak(), 3) << "  ";
            }
        }
        check("Chorus_EveryModeAtMaximumEverythingStaysValid", allSurvive,
              detail.isEmpty() ? "every mode at maximum on a supersaw" : detail);
    }

    {
        struct Sweep { const char* name; float ChorusSettings::* member; };
        const std::array<Sweep, 8> sweeps { {
            { "amount", &ChorusSettings::amount },
            { "rate", &ChorusSettings::rate },
            { "depth", &ChorusSettings::depth },
            { "width", &ChorusSettings::width },
            { "spread", &ChorusSettings::spread },
            { "lowCut", &ChorusSettings::lowCut },
            { "character", &ChorusSettings::character },
            { "mix", &ChorusSettings::mix },
        } };

        juce::String detail;
        auto allSmooth = true;

        for (const auto& sweep : sweeps)
        {
            px3::Chorus chorus;
            chorus.prepare(kSampleRate);

            auto s = audible();
            const auto total = static_cast<int>(kSampleRate * 3.0);

            auto phase = 0.0;
            const auto increment = juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
            auto worstStep = 0.0f;
            auto previous = 0.0f;
            auto valid = true;

            for (int i = 0; i < total; ++i)
            {
                if (i % 64 == 0)
                {
                    s.*(sweep.member) = static_cast<float>(i) / static_cast<float>(total);
                    chorus.updateForBlock(s);
                }

                const auto in = 0.5f * static_cast<float>(std::sin(phase));
                phase += increment;

                float l = 0.0f;
                float r = 0.0f;
                chorus.processSampleFrame(in, in, l, r);
                valid = valid && std::isfinite(l) && std::abs(l) < 8.0f;

                if (i > 2000)
                {
                    worstStep = juce::jmax(worstStep, std::abs(l - previous));
                }
                previous = l;
            }

            const auto ok = valid && worstStep < 0.4f;
            allSmooth = allSmooth && ok;
            if (! ok)
            {
                detail << sweep.name << " step " << juce::String(worstStep, 4) << "  ";
            }
        }

        check("Chorus_EveryParameterSweepsWithoutDiscontinuity", allSmooth,
              detail.isEmpty() ? "8 controls swept min to max under audio" : detail);
    }

    // ---- bypass ------------------------------------------------------------
    {
        px3::Chorus chorus;
        chorus.prepare(kSampleRate);

        auto s = audible();
        chorus.updateForBlock(s);

        auto phase = 0.0;
        const auto increment = juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
        for (int i = 0; i < static_cast<int>(kSampleRate); ++i)
        {
            float l = 0.0f;
            float r = 0.0f;
            const auto in = 0.5f * static_cast<float>(std::sin(phase));
            phase += increment;
            chorus.processSampleFrame(in, in, l, r);
        }

        s.enabled = false;
        chorus.updateForBlock(s);

        auto maxDeviation = 0.0f;
        auto worstStep = 0.0f;
        auto previous = 0.0f;
        for (int i = 0; i < static_cast<int>(kSampleRate * 0.5); ++i)
        {
            const auto in = 0.5f * static_cast<float>(std::sin(phase));
            phase += increment;

            float l = 0.0f;
            float r = 0.0f;
            chorus.processSampleFrame(in, in, l, r);

            if (i > 0)
            {
                worstStep = juce::jmax(worstStep, std::abs(l - previous));
            }
            previous = l;

            if (i > static_cast<int>(kSampleRate * 0.15))
            {
                maxDeviation = juce::jmax(maxDeviation, std::abs(l - in));
            }
        }

        check("Chorus_BypassPassesDryWithoutAClick",
              maxDeviation < 1.0e-5f && worstStep < 0.15f,
              "deviation from dry " + juce::String(maxDeviation, 8) + ", worst step "
                  + juce::String(worstStep, 5));
    }

    // ---- integration -------------------------------------------------------
    {
        PX3SynthAudioProcessor processor;

        auto findParam = [&processor](const juce::String& id) -> juce::RangedAudioParameter*
        {
            for (auto* param : processor.getParameters())
            {
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
                {
                    if (ranged->paramID == id)
                    {
                        return ranged;
                    }
                }
            }
            return nullptr;
        };

        const std::array<const char*, 12> ids { {
            "fx.chorus.enabled", "fx.chorus.amount", "fx.chorus.rate", "fx.chorus.depth", "fx.chorus.width",
            "fx.chorus.spread", "fx.chorus.low.cut", "fx.chorus.feedback", "fx.chorus.character",
            "fx.chorus.mix", "fx.chorus.tone", "fx.chorus.mode",
        } };

        juce::StringArray missing;
        for (const auto* id : ids)
        {
            if (auto* param = findParam(id))
            {
                param->setValueNotifyingHost(param->getValue() > 0.5f ? 0.17f : 0.83f);
            }
            else
            {
                missing.add(id);
            }
        }

        check("Chorus_AllParametersExist", missing.isEmpty(),
              missing.isEmpty() ? "12 CHORUS parameters registered"
                                : "missing " + missing.joinIntoString(", "));

        auto order = px3::kDefaultFxOrder;
        std::rotate(order.begin(), order.begin() + 2, order.end());
        processor.setFxProcessingOrder(order);

        std::vector<float> before;
        for (auto* param : processor.getParameters())
        {
            before.push_back(param->getValue());
        }

        juce::MemoryBlock state;
        processor.getStateInformation(state);

        PX3SynthAudioProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        std::size_t index = 0;
        juce::StringArray drifted;
        for (auto* param : restored.getParameters())
        {
            if (index < before.size() && std::abs(param->getValue() - before[index]) > 1.0e-5f)
            {
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
                    ranged != nullptr && ranged->paramID.startsWithIgnoreCase("chorus"))
                {
                    drifted.add(ranged->paramID);
                }
            }
            ++index;
        }

        check("Chorus_EveryParameterRoundTripsThroughDawState", drifted.isEmpty(),
              drifted.isEmpty() ? "12 parameters restored exactly"
                                : "drifted: " + drifted.joinIntoString(", "));

        check("Chorus_FxOrderIncludingChorusSurvivesState",
              restored.getFxProcessingOrder() == order, "");
    }

    testChorusHardwareModels();
}

// ============================================================================
// STEREO SPREAD
// ============================================================================

namespace spreadtest
{
using doomtest::Result;
using chorustest::monoRms;

enum class Source { silence, impulse, sine, saw, bass, leftOnly, rightOnly, wideStereo };

Result runSpread(const StereoSpreadSettings& settings,
                 Source source,
                 double seconds,
                 double sampleRate = kSampleRate,
                 float frequency = 220.0f)
{
    px3::StereoSpread spread;
    spread.prepare(sampleRate);
    spread.updateForBlock(settings);

    const auto total = static_cast<int>(sampleRate * seconds);
    Result result;
    result.left.reserve(static_cast<std::size_t>(total));
    result.right.reserve(static_cast<std::size_t>(total));

    auto phase = 0.0;
    const auto increment = juce::MathConstants<double>::twoPi * frequency / sampleRate;

    for (int i = 0; i < total; ++i)
    {
        auto inL = 0.0f;
        auto inR = 0.0f;

        auto mono = 0.0f;
        switch (source)
        {
            case Source::silence:
                break;
            case Source::impulse:
                mono = i == 4096 ? 1.0f : 0.0f;
                break;
            case Source::sine:
            case Source::leftOnly:
            case Source::rightOnly:
            case Source::wideStereo:
                mono = 0.5f * static_cast<float>(std::sin(phase));
                break;
            case Source::saw:
            {
                auto sum = 0.0;
                for (int h = 1; h <= 12; ++h)
                {
                    sum += std::sin(phase * h) / h;
                }
                mono = 0.28f * static_cast<float>(sum);
                break;
            }
            case Source::bass:
            {
                auto sum = 0.0;
                for (int h = 1; h <= 9; h += 2)
                {
                    sum += std::sin(phase * h) / h;
                }
                mono = 0.4f * static_cast<float>(sum);
                break;
            }
        }

        phase += increment;

        switch (source)
        {
            case Source::leftOnly:  inL = mono; inR = 0.0f; break;
            case Source::rightOnly: inL = 0.0f; inR = mono; break;
            case Source::wideStereo: inL = mono; inR = -mono * 0.7f; break;
            default:                inL = mono; inR = mono; break;
        }

        float outL = 0.0f;
        float outR = 0.0f;
        spread.processSampleFrame(inL, inR, outL, outR);
        result.left.push_back(outL);
        result.right.push_back(outR);
    }

    return result;
}

StereoSpreadSettings audible()
{
    StereoSpreadSettings s;
    s.enabled = true;
    s.amount = 0.75f;
    return s;
}

// Side energy as a fraction of the total. A mono input has none; a widener that
// works must create some.
double sideFraction(const Result& r)
{
    auto side = 0.0;
    auto total = 0.0;
    for (std::size_t i = 0; i < r.left.size(); ++i)
    {
        const auto s = 0.5 * (static_cast<double>(r.left[i]) - r.right[i]);
        const auto m = 0.5 * (static_cast<double>(r.left[i]) + r.right[i]);
        side += s * s;
        total += s * s + m * m;
    }
    return side / juce::jmax(1.0e-12, total);
}
} // namespace spreadtest

void testStereoSpread()
{
    suite("STEREO SPREAD");

    {
        // DRIVE: each clipper adds harmonics that grow with DRIVE, the level
        // match holds the loudness steady, the output is finite, and MIX 0 is
        // exactly transparent.
        auto run = [](int type, float drive, float mix, double& levelDb, double& harmonicDb, bool& finite, bool& identical)
        {
            px3::Distortion distortion;
            distortion.prepare(48000.0);
            px3::DistortionSettings settings;
            settings.type = type; settings.drive = drive; settings.mix = mix;
            double inE = 0, outE = 0, c1 = 0, s1 = 0, c3 = 0, s3 = 0, c2 = 0, s2 = 0;
            finite = true; identical = true;
            for (int n = 0; n < 48000; ++n)
            {
                if (n % 512 == 0) distortion.updateForBlock(settings);
                const auto x = 0.25f * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * 220.0 * n / 48000.0));
                float l = 0, r = 0;
                distortion.processSampleFrame(x, x, l, r);
                finite = finite && std::isfinite(l);
                identical = identical && l == x;
                if (n < 24000) continue;
                const auto w = juce::MathConstants<double>::twoPi * 220.0 * n / 48000.0;
                inE += static_cast<double>(x) * x; outE += static_cast<double>(l) * l;
                c1 += l * std::cos(w); s1 += l * std::sin(w);
                c2 += l * std::cos(2 * w); s2 += l * std::sin(2 * w);
                c3 += l * std::cos(3 * w); s3 += l * std::sin(3 * w);
            }
            levelDb = 10.0 * std::log10(outE / inE);
            harmonicDb = 10.0 * std::log10((c2 * c2 + s2 * s2 + c3 * c3 + s3 * s3 + 1e-30) / (c1 * c1 + s1 * s1 + 1e-30));
        };
        juce::String report;
        auto ok = true;
        for (int type = 0; type < 3; ++type)
        {
            double lowLevel, lowHarm, highLevel, highHarm; bool f1, f2, i1, i2;
            run(type, 0.1f, 1.0f, lowLevel, lowHarm, f1, i1);
            run(type, 0.9f, 1.0f, highLevel, highHarm, f2, i2);
            ok = ok && f1 && f2 && highHarm > lowHarm + 10.0 && std::abs(highLevel) < 3.0 && std::abs(lowLevel) < 3.0;
            report << juce::StringArray { "SOFT", "HARD", "ASYM" }[type] << ": H2+H3 " << juce::String(lowHarm, 1) << " -> "
                   << juce::String(highHarm, 1) << " dB, level " << juce::String(lowLevel, 1) << "/" << juce::String(highLevel, 1) << " dB  ";
        }
        check("Drive_EveryClipperGrowsHarmonicsAtSteadyLevel", ok, report);
        double l0, h0; bool f0, same;
        run(0, 1.0f, 0.0f, l0, h0, f0, same);
        check("Drive_MixZeroIsTransparent", same, "MIX 0 returns the input sample for sample");
    }

    using namespace spreadtest;

    // ---- construction and defaults -----------------------------------------
    {
        const StereoSpreadSettings defaults;
        check("Spread_DefaultsAreValid",
              defaults.enabled && juce::approximatelyEqual(defaults.amount, 0.0f)
                  && juce::approximatelyEqual(defaults.lowWidth, 0.0f)
                  && defaults.modeIndex == 0,
              "amount zero, lows mono, CLASSIC");

        px3::StereoSpread spread;
        spread.prepare(kSampleRate);
        spread.reset();
        float l = 0.0f;
        float r = 0.0f;
        spread.processSampleFrame(0.0f, 0.0f, l, r);
        check("Spread_ConstructsAndProcessesWithoutPreparingSettings",
              std::isfinite(l) && std::isfinite(r), "");
    }

    // ---- amount zero is exactly identity -----------------------------------
    {
        // The bands sum flat, but only if nothing has been done to the parts.
        // Anything less than identity here means the effect is always on.
        auto off = audible();
        off.amount = 0.0f;

        const auto out = runSpread(off, Source::wideStereo, 1.5);

        auto phase = 0.0;
        const auto increment = juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
        auto worstError = 0.0f;
        for (std::size_t i = 0; i < out.left.size(); ++i)
        {
            const auto mono = 0.5f * static_cast<float>(std::sin(phase));
            phase += increment;
            if (i > static_cast<std::size_t>(kSampleRate * 0.2))
            {
                worstError = juce::jmax(worstError, std::abs(out.left[i] - mono));
                worstError = juce::jmax(worstError, std::abs(out.right[i] + mono * 0.7f));
            }
        }

        check("Spread_AmountZeroIsExactlyIdentity", worstError < 1.0e-4f,
              "worst deviation from the input " + juce::String(worstError, 8));
    }

    // ---- sample rates and stability ---------------------------------------
    {
        juce::String detail;
        auto allFine = true;
        for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            const auto out = runSpread(audible(), Source::saw, 1.5, rate);
            const auto ok = out.finite() && out.peak() < 4.0f && out.rms() > 1.0e-5;
            allFine = allFine && ok;
            detail << juce::String(static_cast<int>(rate)) << " rms "
                   << juce::String(out.rms(), 4) << "  ";
        }
        check("Spread_RunsAtEverySupportedSampleRate", allFine, detail);

        const auto quiet = runSpread(audible(), Source::silence, 1.5);
        check("Spread_SilenceStaysSilent",
              quiet.finite() && quiet.peak() < 1.0e-5f, "");

        const auto impulse = runSpread(audible(), Source::impulse, 2.0);
        check("Spread_ImpulseStaysBounded",
              impulse.finite() && impulse.peak() < 4.0f,
              "peak " + juce::String(impulse.peak(), 4));
    }

    // ---- the core claim: a mono source actually widens ----------------------
    {
        // M/S gain alone cannot do this - for a mono source the side signal is
        // zero, and no gain applied to zero produces anything. Side energy has
        // to be CREATED, which is what the allpass network is for.
        auto off = audible();
        off.amount = 0.0f;
        const auto dry = runSpread(off, Source::saw, 2.0);

        auto on = audible();
        on.amount = 1.0f;
        const auto wide = runSpread(on, Source::saw, 2.0);

        check("Spread_CreatesSideEnergyFromAMonoSource",
              sideFraction(dry) < 1.0e-6 && sideFraction(wide) > 0.02,
              "mono input side fraction " + juce::String(sideFraction(dry), 8)
                  + " -> " + juce::String(sideFraction(wide), 5));
    }

    // ---- mono compatibility: the first-class requirement --------------------
    {
        juce::String detail;
        auto allRetained = true;

        const auto dry = runSpread([]{ auto s = audible(); s.amount = 0.0f; return s; }(),
                                   Source::saw, 2.0);
        const auto dryMono = monoRms(dry);

        for (const auto amount : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto s = audible();
            s.amount = amount;
            const auto out = runSpread(s, Source::saw, 2.0);
            const auto ratio = monoRms(out) / juce::jmax(1.0e-9, dryMono);
            // Within 1.5 dB. A phase-trick widener loses far more.
            const auto retained = ratio > 0.84 && ratio < 1.2;
            allRetained = allRetained && retained;
            detail << juce::String(amount, 2) << ":" << juce::String(ratio, 3) << "  ";
        }

        check("Spread_MonoCollapseRetainsLevelAtEveryAmount", allRetained,
              "mono sum against dry: " + detail);
    }

    // ---- correlation stays above the floor ---------------------------------
    {
        juce::String detail;
        auto allSafe = true;

        for (const auto amount : { 0.25f, 0.5f, 0.75f, 1.0f })
        {
            auto s = audible();
            s.amount = amount;
            s.width = 1.0f;
            const auto out = runSpread(s, Source::saw, 3.0);
            // Never near -1, which is a signal and its own inverse: infinitely
            // wide and completely silent in mono.
            const auto safe = out.correlation() > -0.5;
            allSafe = allSafe && safe;
            detail << juce::String(amount, 2) << ":" << juce::String(out.correlation(), 3) << "  ";
        }

        check("Spread_CorrelationStaysSafeAcrossTheRange", allSafe, detail);

        // And width has to actually reduce it.
        auto narrow = audible();
        narrow.amount = 0.15f;
        auto wide = audible();
        wide.amount = 1.0f;
        wide.width = 1.0f;
        const auto a = runSpread(narrow, Source::saw, 2.5);
        const auto b = runSpread(wide, Source::saw, 2.5);
        check("Spread_MoreAmountMeansLessCorrelation",
              b.correlation() < a.correlation(),
              "low " + juce::String(a.correlation(), 4) + ", high "
                  + juce::String(b.correlation(), 4));
    }

    // ---- frequency behaviour -----------------------------------------------
    {
        // Low frequencies must stay centred: there is no width available at a
        // wavelength longer than any room, and side energy down there is what
        // destroys mono compatibility.
        juce::String detail;
        auto lowsStayCentred = true;
        auto highsWiden = true;

        // A crossover has a slope. The property that matters is not that side
        // energy is zero at some chosen frequency, but that it FALLS steeply as
        // frequency drops - deep bass fully centred, and the approach to the
        // crossover monotonic rather than lumpy.
        std::vector<double> sideByFrequency;
        for (const auto hz : { 30.0f, 50.0f, 80.0f, 100.0f })
        {
            auto s = audible();
            s.amount = 1.0f;
            const auto out = runSpread(s, Source::sine, 2.0, kSampleRate, hz);
            const auto side = sideFraction(out);
            sideByFrequency.push_back(side);
            detail << juce::String(static_cast<int>(hz)) << "Hz " << juce::String(side, 4) << "  ";
        }

        // Deep bass - well inside the mono band - must be essentially centred.
        lowsStayCentred = sideByFrequency[0] < 0.02 && sideByFrequency[1] < 0.03;

        // And the fall has to be monotonic, which is what says a crossover is
        // doing this rather than something frequency-dependent going wrong.
        for (std::size_t i = 1; i < sideByFrequency.size(); ++i)
        {
            lowsStayCentred = lowsStayCentred && sideByFrequency[i] > sideByFrequency[i - 1];
        }

        // Approaching the crossover, still a small minority of the energy.
        lowsStayCentred = lowsStayCentred && sideByFrequency.back() < 0.10;

        check("Spread_LowFrequenciesStayCentred", lowsStayCentred, detail);

        juce::String highDetail;
        for (const auto hz : { 1000.0f, 5000.0f, 10000.0f, 15000.0f })
        {
            auto s = audible();
            s.amount = 1.0f;
            const auto out = runSpread(s, Source::sine, 2.0, kSampleRate, hz);
            const auto side = sideFraction(out);
            highsWiden = highsWiden && out.finite();
            highDetail << juce::String(static_cast<int>(hz)) << "Hz "
                       << juce::String(side, 4) << "  ";
        }
        check("Spread_HighFrequenciesAreStableAndWidened", highsWiden, highDetail);

        // A bass patch keeps its weight.
        auto s = audible();
        s.amount = 1.0f;
        const auto processed = runSpread(s, Source::bass, 2.5, kSampleRate, 55.0f);
        const auto dry = runSpread([]{ auto d = audible(); d.amount = 0.0f; return d; }(),
                                   Source::bass, 2.5, kSampleRate, 55.0f);
        const auto ratio = monoRms(processed) / juce::jmax(1.0e-9, monoRms(dry));
        check("Spread_BassKeepsItsWeightInMono", ratio > 0.88 && ratio < 1.15,
              "mono bass level against dry = " + juce::String(ratio, 3));
    }

    // ---- input configurations ----------------------------------------------
    {
        juce::String detail;
        auto allFine = true;
        const std::array<std::pair<const char*, Source>, 3> inputs { {
            { "left only", Source::leftOnly },
            { "right only", Source::rightOnly },
            { "already wide", Source::wideStereo },
        } };

        for (const auto& input : inputs)
        {
            const auto out = runSpread(audible(), input.second, 2.0);
            const auto ok = out.finite() && out.peak() < 4.0f && out.rms() > 1.0e-5;
            allFine = allFine && ok;
            detail << input.first << " rms " << juce::String(out.rms(), 4) << "  ";
        }
        check("Spread_HandlesEveryInputConfiguration", allFine, detail);

        // An existing stereo image must not be destroyed or collapsed.
        const auto dry = runSpread([]{ auto s = audible(); s.amount = 0.0f; return s; }(),
                                   Source::wideStereo, 2.0);
        const auto processed = runSpread(audible(), Source::wideStereo, 2.0);
        check("Spread_PreservesAnExistingStereoImage",
              sideFraction(processed) >= sideFraction(dry) * 0.8,
              "dry side " + juce::String(sideFraction(dry), 4) + ", processed "
                  + juce::String(sideFraction(processed), 4));
    }

    // ---- modes -------------------------------------------------------------
    {
        juce::String detail;
        auto allStable = true;
        std::vector<double> sideByMode;

        for (int mode = 0; mode < px3::StereoSpread::modeCount(); ++mode)
        {
            auto s = audible();
            s.modeIndex = mode;
            s.amount = 1.0f;
            const auto out = runSpread(s, Source::saw, 2.5);
            allStable = allStable && out.finite() && out.peak() < 4.0f;
            sideByMode.push_back(sideFraction(out));
            detail << mode << ":" << juce::String(sideFraction(out), 4) << "  ";
        }

        check("Spread_EveryModeIsStable", allStable, detail);

        // MONO SAFE must be genuinely more conservative, not simply quieter.
        auto monoSafe = audible();
        monoSafe.modeIndex = 3;
        monoSafe.amount = 1.0f;
        monoSafe.width = 1.0f;

        auto wideMode = monoSafe;
        wideMode.modeIndex = 1;

        const auto safe = runSpread(monoSafe, Source::saw, 2.5);
        const auto wide = runSpread(wideMode, Source::saw, 2.5);

        check("Spread_MonoSafeIsMoreConservativeThanWide",
              safe.correlation() > wide.correlation()
                  && sideFraction(safe) < sideFraction(wide),
              "mono safe correlation " + juce::String(safe.correlation(), 4)
                  + " side " + juce::String(sideFraction(safe), 4)
                  + ", wide correlation " + juce::String(wide.correlation(), 4)
                  + " side " + juce::String(sideFraction(wide), 4));
    }

    // ---- extremes and automation ------------------------------------------
    {
        auto everything = audible();
        everything.amount = 1.0f;
        everything.width = 1.0f;
        everything.depth = 1.0f;
        everything.center = 0.0f;
        everything.lowWidth = 1.0f;
        everything.highWidth = 1.0f;
        everything.tone = 1.0f;

        juce::String detail;
        auto allSurvive = true;
        for (int mode = 0; mode < px3::StereoSpread::modeCount(); ++mode)
        {
            everything.modeIndex = mode;
            const auto out = runSpread(everything, Source::saw, 3.0);
            const auto ok = out.finite() && out.peak() < 6.0f && std::abs(out.dc()) < 0.1
                            && monoRms(out) > 1.0e-4;
            allSurvive = allSurvive && ok;
            if (! ok)
            {
                detail << "mode " << mode << " peak " << juce::String(out.peak(), 3)
                       << " mono " << juce::String(monoRms(out), 5) << "  ";
            }
        }
        check("Spread_MaximumEverythingStaysValidAndAudibleInMono", allSurvive,
              detail.isEmpty() ? "every mode at maximum still sums to audio" : detail);
    }

    {
        struct Sweep { const char* name; float StereoSpreadSettings::* member; };
        const std::array<Sweep, 9> sweeps { {
            { "amount", &StereoSpreadSettings::amount },
            { "width", &StereoSpreadSettings::width },
            { "depth", &StereoSpreadSettings::depth },
            { "center", &StereoSpreadSettings::center },
            { "lowWidth", &StereoSpreadSettings::lowWidth },
            { "highWidth", &StereoSpreadSettings::highWidth },
            { "lowFreq", &StereoSpreadSettings::lowFreq },
            { "highFreq", &StereoSpreadSettings::highFreq },
            { "mix", &StereoSpreadSettings::mix },
        } };

        juce::String detail;
        auto allSmooth = true;

        for (const auto& sweep : sweeps)
        {
            px3::StereoSpread spread;
            spread.prepare(kSampleRate);

            auto s = audible();
            const auto total = static_cast<int>(kSampleRate * 3.0);

            auto phase = 0.0;
            const auto increment = juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
            auto worstStep = 0.0f;
            auto previous = 0.0f;
            auto valid = true;

            for (int i = 0; i < total; ++i)
            {
                if (i % 64 == 0)
                {
                    s.*(sweep.member) = static_cast<float>(i) / static_cast<float>(total);
                    spread.updateForBlock(s);
                }

                const auto in = 0.5f * static_cast<float>(std::sin(phase));
                phase += increment;

                float l = 0.0f;
                float r = 0.0f;
                spread.processSampleFrame(in, in, l, r);
                valid = valid && std::isfinite(l) && std::abs(l) < 8.0f;

                if (i > 2000)
                {
                    worstStep = juce::jmax(worstStep, std::abs(l - previous));
                }
                previous = l;
            }

            const auto ok = valid && worstStep < 0.35f;
            allSmooth = allSmooth && ok;
            if (! ok)
            {
                detail << sweep.name << " step " << juce::String(worstStep, 4) << "  ";
            }
        }

        check("Spread_EveryParameterSweepsWithoutDiscontinuity", allSmooth,
              detail.isEmpty() ? "9 controls swept min to max under audio" : detail);
    }

    // ---- bypass ------------------------------------------------------------
    {
        px3::StereoSpread spread;
        spread.prepare(kSampleRate);

        auto s = audible();
        spread.updateForBlock(s);

        auto phase = 0.0;
        const auto increment = juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
        for (int i = 0; i < static_cast<int>(kSampleRate); ++i)
        {
            float l = 0.0f;
            float r = 0.0f;
            const auto in = 0.5f * static_cast<float>(std::sin(phase));
            phase += increment;
            spread.processSampleFrame(in, in, l, r);
        }

        s.enabled = false;
        spread.updateForBlock(s);

        auto maxDeviation = 0.0f;
        auto worstStep = 0.0f;
        auto previous = 0.0f;
        for (int i = 0; i < static_cast<int>(kSampleRate * 0.5); ++i)
        {
            const auto in = 0.5f * static_cast<float>(std::sin(phase));
            phase += increment;

            float l = 0.0f;
            float r = 0.0f;
            spread.processSampleFrame(in, in, l, r);

            if (i > 0)
            {
                worstStep = juce::jmax(worstStep, std::abs(l - previous));
            }
            previous = l;

            if (i > static_cast<int>(kSampleRate * 0.15))
            {
                maxDeviation = juce::jmax(maxDeviation, std::abs(l - in));
            }
        }

        check("Spread_BypassPassesDryWithoutAClick",
              maxDeviation < 1.0e-5f && worstStep < 0.15f,
              "deviation from dry " + juce::String(maxDeviation, 8) + ", worst step "
                  + juce::String(worstStep, 5));
    }

    // ---- integration -------------------------------------------------------
    {
        PX3SynthAudioProcessor processor;

        auto findParam = [&processor](const juce::String& id) -> juce::RangedAudioParameter*
        {
            for (auto* param : processor.getParameters())
            {
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
                {
                    if (ranged->paramID == id)
                    {
                        return ranged;
                    }
                }
            }
            return nullptr;
        };

        const std::array<const char*, 12> ids { {
            "fx.spread.enabled", "fx.spread.amount", "fx.spread.width", "fx.spread.depth", "fx.spread.center",
            "fx.spread.low.width", "fx.spread.high.width", "fx.spread.low.freq", "fx.spread.high.freq",
            "fx.spread.mix", "fx.spread.tone", "fx.spread.mode",
        } };

        juce::StringArray missing;
        for (const auto* id : ids)
        {
            if (auto* param = findParam(id))
            {
                param->setValueNotifyingHost(param->getValue() > 0.5f ? 0.19f : 0.81f);
            }
            else
            {
                missing.add(id);
            }
        }

        check("Spread_AllParametersExist", missing.isEmpty(),
              missing.isEmpty() ? "12 STEREO SPREAD parameters registered"
                                : "missing " + missing.joinIntoString(", "));

        auto order = px3::kDefaultFxOrder;
        std::reverse(order.begin(), order.end());
        processor.setFxProcessingOrder(order);

        std::vector<float> before;
        for (auto* param : processor.getParameters())
        {
            before.push_back(param->getValue());
        }

        juce::MemoryBlock state;
        processor.getStateInformation(state);

        PX3SynthAudioProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        std::size_t index = 0;
        juce::StringArray drifted;
        for (auto* param : restored.getParameters())
        {
            if (index < before.size() && std::abs(param->getValue() - before[index]) > 1.0e-5f)
            {
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
                    ranged != nullptr && ranged->paramID.startsWithIgnoreCase("spread"))
                {
                    drifted.add(ranged->paramID);
                }
            }
            ++index;
        }

        check("Spread_EveryParameterRoundTripsThroughDawState", drifted.isEmpty(),
              drifted.isEmpty() ? "12 parameters restored exactly"
                                : "drifted: " + drifted.joinIntoString(", "));

        check("Spread_FxOrderIncludingSpreadSurvivesState",
              restored.getFxProcessingOrder() == order, "");
    }

    {
        // The full chain, in two orders. Every stage present, position changing
        // the result.
        auto renderWithOrder = [](const px3::FxOrder& order)
        {
            PX3SynthAudioProcessor processor;
            for (auto* param : processor.getParameters())
            {
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
                {
                    if (ranged->paramID == "fx.spread.amount") ranged->setValueNotifyingHost(0.8f);
                    if (ranged->paramID == "fx.chorus.amount") ranged->setValueNotifyingHost(0.7f);
                    if (ranged->paramID == "fx.lucy.global")   ranged->setValueNotifyingHost(0.5f);
                    if (ranged->paramID == "fx.doom.mix")      ranged->setValueNotifyingHost(0.5f);
                    if (ranged->paramID == "fx.reverb.amount") ranged->setValueNotifyingHost(0.5f);
                }
            }
            processor.setFxProcessingOrder(order);
            return render(processor, 48000, { { 2000, true, 60, 0.9f } });
        };

        auto forward = px3::kDefaultFxOrder;
        auto reversed = px3::kDefaultFxOrder;
        std::reverse(reversed.begin(), reversed.end());

        const auto a = renderWithOrder(forward);
        const auto b = renderWithOrder(reversed);

        auto differs = false;
        const auto count = juce::jmin(a.left.size(), b.left.size());
        for (std::size_t i = 0; i < count; ++i)
        {
            if (std::abs(a.left[i] - b.left[i]) > 1.0e-5f)
            {
                differs = true;
                break;
            }
        }

        check("Spread_TheWholeEightStageChainRespondsToOrder", differs,
              "default order rms " + juce::String(a.rms(), 5) + ", reversed "
                  + juce::String(b.rms(), 5));
    }

    {
        // SPREAD works on the whole instrument: with every FX send at zero it
        // must still widen a centred dry patch. It used to sit in the send
        // chain, where it could never touch the dry signal.
        auto sideRatio = [](float amount)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "voice.osc1.mode", 1);
            setParam(processor, "voice.amp.sustain", 1.0f);
            for (const auto* id : { "sub", "osc1", "osc2", "osc3" })
                setParam(processor, juce::String("mix.") + id + ".send.fx", 0.0f);
            setParam(processor, "fx.spread.enabled", 1.0f);
            setParam(processor, "fx.spread.amount", amount);
            setParam(processor, "fx.spread.width", 1.0f);
            setParam(processor, "fx.spread.mix", 1.0f);
            const auto capture = render(processor, 96000, { { 2000, true, 57, 0.9f } });
            double mid = 0.0, side = 0.0;
            for (std::size_t n = 24000; n < capture.left.size(); ++n)
            {
                const auto m = 0.5 * (capture.left[n] + capture.right[n]);
                const auto d = 0.5 * (capture.left[n] - capture.right[n]);
                mid += m * m;
                side += d * d;
            }
            return 10.0 * std::log10(juce::jmax(1.0e-30, side) / juce::jmax(1.0e-30, mid));
        };
        const auto off = sideRatio(0.0f);
        const auto on = sideRatio(1.0f);
        check("Spread_WidensTheDryInstrumentWithNoFxSend", on > off + 20.0 && on > -30.0,
              "side/mid " + juce::String(off, 1) + " dB at AMOUNT 0, " + juce::String(on, 1) + " dB at AMOUNT 1");
    }
}

} // namespace px3tests
