// Oscillator DSP prototype - settles the anti-aliasing, nonlinear-processing,
// noise and phase-precision decisions in docs/OSCILLATOR_DSP_DESIGN.md by
// measurement rather than by argument.
//
// Standalone on purpose, like the wavetable prototypes beside it: none of this
// touches the synth, so it must not pay for rebuilding it.
//
//   clang++ -O3 -std=c++17 -o oscproto docs/research/oscillator-prototype.cpp && ./oscproto
//
// Measurement method (the lessons of WAVETABLE_OSCILLATOR_DESIGN.md "A note on
// three broken measurements" apply unchanged):
//   - every test tone is snapped to an exact FFT bin and the waveform is exactly
//     periodic in the analysis frame, so no window is needed and none is used;
//   - the harmonic grid bin is odd and the frame length a power of two, so a
//     folded component can never land on a harmonic and hide there;
//   - "alias" is every bin off the harmonic grid. It is reported against the
//     fundamental for waveforms that have a strong one, and against the total
//     harmonic power for pulses, whose fundamental shrinks with their width.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kN = 1 << 16;
constexpr int kWarmup = 1 << 14;
constexpr int kTotal = kWarmup + kN;
using Vec = std::vector<double>;

double frac(double x) { return x - std::floor(x); }
double db(double power) { return 10.0 * std::log10(std::max(power, 1.0e-30)); }
double midiHz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

// ------------------------------------------------------------------ FFT -----
void fft(std::vector<std::complex<double>>& a)
{
    const int n = static_cast<int>(a.size());
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) { j ^= bit; }
        j ^= bit;
        if (i < j) { std::swap(a[static_cast<size_t>(i)], a[static_cast<size_t>(j)]); }
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * kPi / len;
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len)
        {
            std::complex<double> w(1.0, 0.0);
            for (int k = 0; k < len / 2; ++k)
            {
                const auto u = a[static_cast<size_t>(i + k)];
                const auto v = a[static_cast<size_t>(i + k + len / 2)] * w;
                a[static_cast<size_t>(i + k)] = u + v;
                a[static_cast<size_t>(i + k + len / 2)] = u - v;
                w *= wl;
            }
        }
    }
}

// --------------------------------------------------------------- tones ------
struct Tone
{
    int bin { 1 };        // fundamental bin
    int grid { 1 };       // harmonic grid bin (fundamental / q for ratio p/q)
    double hz { 0.0 };
    double dt { 0.0 };    // cycles per sample, exactly bin / kN
};

Tone toneNear(double hz, double fs, int q = 1)
{
    auto grid = std::max(1, static_cast<int>(std::lround(hz * kN / fs / q)));
    if (grid % 2 == 0) { ++grid; }
    Tone t;
    t.grid = grid;
    t.bin = grid * q;
    t.hz = t.bin * fs / kN;
    t.dt = static_cast<double>(t.bin) / kN;
    return t;
}

Vec tail(const Vec& y) { return Vec(y.end() - kN, y.end()); }

struct Report
{
    double alias { 0 };        // vs fundamental
    double alias15k { 0 };     // vs fundamental, alias below 15 kHz only
    double worst { 0 };        // single worst alias bin vs fundamental
    double aliasVsSignal { 0 };// vs total harmonic power
    double droop10k { 0 };     // harmonic nearest 10 kHz against its ideal level, dB
    double mean { 0 };
};

template <typename Ideal>
Report analyse(const Vec& signal, double fs, const Tone& tone, Ideal idealAmp)
{
    std::vector<std::complex<double>> a(signal.begin(), signal.end());
    fft(a);
    Vec p(static_cast<size_t>(kN / 2 + 1));
    for (int k = 0; k <= kN / 2; ++k) { p[static_cast<size_t>(k)] = std::norm(a[static_cast<size_t>(k)]); }

    const auto fundamental = p[static_cast<size_t>(tone.bin)];
    double alias = 0.0, alias15 = 0.0, worst = 0.0, harmonics = 0.0;
    for (int k = 1; k < kN / 2; ++k)
    {
        if (k % tone.grid == 0) { harmonics += p[static_cast<size_t>(k)]; continue; }
        alias += p[static_cast<size_t>(k)];
        if (k * fs / kN < 15000.0) { alias15 += p[static_cast<size_t>(k)]; }
        worst = std::max(worst, p[static_cast<size_t>(k)]);
    }

    Report r;
    r.alias = db(alias) - db(fundamental);
    r.alias15k = db(alias15) - db(fundamental);
    r.worst = db(worst) - db(fundamental);
    r.aliasVsSignal = db(alias) - db(harmonics);
    double sum = 0.0;
    for (const auto v : signal) { sum += v; }
    r.mean = sum / kN;

    // The harmonic nearest 10 kHz that the ideal waveform actually has.
    auto h = static_cast<int>(std::lround(10000.0 / tone.hz));
    if (idealAmp(h) < 1.0e-9) { ++h; }
    if (h >= 2 && h * tone.bin < kN / 2)
    {
        const auto measured = std::sqrt(p[static_cast<size_t>(h * tone.bin)] / fundamental);
        r.droop10k = 20.0 * std::log10(std::max(1.0e-12, measured / idealAmp(h)));
    }
    return r;
}

double sawIdeal(int h) { return 1.0 / h; }
double oddIdeal(int h) { return (h % 2) ? 1.0 / h : 0.0; }
double triangleIdeal(int h) { return (h % 2) ? 1.0 / (static_cast<double>(h) * h) : 0.0; }
double noIdeal(int) { return 1.0; }

// ------------------------------------------------- band-limited corrections --
// The residual of a band-limited step: S(tau) - H(tau), where S is the running
// integral of the interpolation kernel and H the unit step. tau is the sample's
// time relative to the discontinuity, in samples. 2 points = linear B-spline
// (classic PolyBLEP), 4 points = cubic B-spline (Valimaki, Pekonen and Nam 2012).
double blepResidual(double tau, int points)
{
    if (points == 2)
    {
        if (tau <= -1.0 || tau >= 1.0) { return 0.0; }
        return tau < 0.0 ? 0.5 * (tau + 1.0) * (tau + 1.0) : -0.5 * (1.0 - tau) * (1.0 - tau);
    }
    if (tau <= -2.0 || tau >= 2.0) { return 0.0; }
    const auto x = -std::abs(tau);
    double lower;   // S(x) for x in [-2, 0]
    if (x <= -1.0)
    {
        const auto u = x + 2.0;
        lower = u * u * u * u / 24.0;
    }
    else
    {
        lower = 0.5 + 2.0 * x / 3.0 - x * x * x / 3.0 - x * x * x * x / 8.0;
    }
    return tau < 0.0 ? lower : -lower;   // S(tau) - 1 = -S(-tau)
}

// The residual of a band-limited ramp (a slope change of one per sample): the
// integral of the step residual. Even in tau.
double blampResidual(double tau, int points)
{
    const auto s = std::abs(tau);
    if (points == 2)
    {
        return s >= 1.0 ? 0.0 : (1.0 - s) * (1.0 - s) * (1.0 - s) / 6.0;
    }
    if (s >= 2.0) { return 0.0; }
    const auto x = -s;
    if (x <= -1.0)
    {
        const auto u = x + 2.0;
        return u * u * u * u * u / 120.0;
    }
    return 7.0 / 30.0 + x / 2.0 + x * x / 3.0 - x * x * x * x / 12.0 - x * x * x * x * x / 40.0;
}

struct Event
{
    double time;    // in samples
    double step;    // value discontinuity
    double slope;   // slope discontinuity, per sample
};

void applyEvents(Vec& y, const std::vector<Event>& events, int points)
{
    const auto half = points / 2;
    for (const auto& e : events)
    {
        const auto base = static_cast<int>(std::floor(e.time));
        for (int m = base - half + 1; m <= base + half; ++m)
        {
            if (m < 0 || m >= static_cast<int>(y.size())) { continue; }
            const auto tau = m - e.time;
            y[static_cast<size_t>(m)] += e.step * blepResidual(tau, points) + e.slope * blampResidual(tau, points);
        }
    }
}

// The cubic B-spline correction rolls the passband off as sinc^4 of the
// normalised frequency (-2.5 dB at 10 kHz at 48 kHz). A symmetric three-tap
// pre-emphasis, [-a, 1+2a, -a], buys most of it back. Applied to a periodic
// frame, so it wraps.
Vec compensate(const Vec& y, double a)
{
    Vec out(y.size());
    const auto n = y.size();
    for (size_t m = 0; m < n; ++m)
    {
        out[m] = -a * y[(m + n - 1) % n] + (1.0 + 2.0 * a) * y[m] - a * y[(m + 1) % n];
    }
    return out;
}

// ------------------------------------------------------------ generators ---
Vec saw(const Tone& t, int points)   // points 0 = naive
{
    Vec y(static_cast<size_t>(kTotal));
    for (int m = 0; m < kTotal; ++m) { y[static_cast<size_t>(m)] = 2.0 * frac(m * t.dt) - 1.0; }
    if (points > 0)
    {
        std::vector<Event> events;
        for (int k = 1; k / t.dt < kTotal + 2; ++k) { events.push_back({ k / t.dt, -2.0, 0.0 }); }
        applyEvents(y, events, points);
    }
    return tail(y);
}

Vec sawDpw(const Tone& t)
{
    Vec y(static_cast<size_t>(kTotal));
    double previous = 0.0;
    for (int m = 0; m < kTotal; ++m)
    {
        const auto x = 2.0 * frac(m * t.dt) - 1.0;
        const auto parabola = x * x;
        y[static_cast<size_t>(m)] = (parabola - previous) / (4.0 * t.dt);
        previous = parabola;
    }
    return tail(y);
}

double polyBlep2(double t, double dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}

Vec pulse(const Tone& t, double width, int points)
{
    Vec y(static_cast<size_t>(kTotal));
    for (int m = 0; m < kTotal; ++m) { y[static_cast<size_t>(m)] = frac(m * t.dt) < width ? 1.0 : -1.0; }
    if (points > 0)
    {
        std::vector<Event> events;
        for (int k = 0; k / t.dt < kTotal + 2; ++k)
        {
            if (k > 0) { events.push_back({ k / t.dt, 2.0, 0.0 }); }
            events.push_back({ (k + width) / t.dt, -2.0, 0.0 });
        }
        applyEvents(y, events, points);
    }
    return tail(y);
}

Vec triangle(const Tone& t, int points)
{
    Vec y(static_cast<size_t>(kTotal));
    for (int m = 0; m < kTotal; ++m) { y[static_cast<size_t>(m)] = 1.0 - 4.0 * std::abs(frac(m * t.dt) - 0.5); }
    if (points > 0)
    {
        std::vector<Event> events;
        const auto slope = 8.0 * t.dt;
        for (int k = 0; k / t.dt < kTotal + 2; ++k)
        {
            if (k > 0) { events.push_back({ k / t.dt, 0.0, slope }); }
            events.push_back({ (k + 0.5) / t.dt, 0.0, -slope });
        }
        applyEvents(y, events, points);
    }
    return tail(y);
}

// DaisySP's triangle: a leaky integral of the PolyBLEP square.
Vec triangleIntegratedSquare(const Tone& t)
{
    Vec y(static_cast<size_t>(kTotal));
    double last = 0.0;
    for (int m = 0; m < kTotal; ++m)
    {
        const auto p = frac(m * t.dt);
        auto sq = p < 0.5 ? 1.0 : -1.0;
        sq += polyBlep2(p, t.dt);
        sq -= polyBlep2(frac(p + 0.5), t.dt);
        last = t.dt * sq + (1.0 - t.dt) * last;
        y[static_cast<size_t>(m)] = 4.0 * last;
    }
    return tail(y);
}

// Hard sync, exact in continuous time: the slave's phase is ratio * the master's.
Vec syncSaw(const Tone& t, double ratio, int points)
{
    Vec y(static_cast<size_t>(kTotal));
    for (int m = 0; m < kTotal; ++m)
    {
        y[static_cast<size_t>(m)] = 2.0 * frac(ratio * frac(m * t.dt)) - 1.0;
    }
    if (points > 0)
    {
        std::vector<Event> events;
        auto preReset = ratio - std::floor(ratio);
        if (preReset <= 0.0) { preReset = 1.0; }
        for (int k = 0; k / t.dt < kTotal + 2; ++k)
        {
            if (k > 0) { events.push_back({ k / t.dt, -2.0 * preReset, 0.0 }); }
            for (int j = 1; j < ratio; ++j)
            {
                events.push_back({ (k + j / ratio) / t.dt, -2.0, 0.0 });
            }
        }
        applyEvents(y, events, points);
    }
    return tail(y);
}

// The sync PX3 ships: the reset lands on the sample, not between samples.
Vec syncSawQuantised(const Tone& t, double ratio)
{
    Vec y(static_cast<size_t>(kTotal));
    double master = 0.0, slave = 0.0;
    for (int m = 0; m < kTotal; ++m)
    {
        master += t.dt;
        if (master >= 1.0) { master -= 1.0; slave = 0.0; }
        slave += t.dt * ratio;
        if (slave >= 1.0) { slave -= 1.0; }
        y[static_cast<size_t>(m)] = 2.0 * slave - 1.0;
    }
    return tail(y);
}

// ------------------------------------------------------------ halfband ------
double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 40; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

struct Halfband
{
    std::vector<double> taps;   // odd length, centred
    int centre { 0 };

    Halfband(int length, double beta)
    {
        taps.assign(static_cast<size_t>(length), 0.0);
        centre = length / 2;
        for (int n = 0; n < length; ++n)
        {
            const auto k = n - centre;
            const auto sinc = k == 0 ? 0.5 : std::sin(kPi * k / 2.0) / (kPi * k);
            const auto r = 2.0 * n / (length - 1) - 1.0;
            taps[static_cast<size_t>(n)] = sinc * besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / besselI0(beta);
        }
    }

    Vec decimate(const Vec& x) const
    {
        Vec y(x.size() / 2);
        for (size_t m = 0; m < y.size(); ++m)
        {
            double acc = 0.0;
            const auto centreIndex = static_cast<long>(2 * m);
            for (int j = 0; j < static_cast<int>(taps.size()); ++j)
            {
                if (j != centre && ((j - centre) % 2) == 0) { continue; }
                const auto i = centreIndex - (j - centre);
                if (i >= 0 && i < static_cast<long>(x.size())) { acc += taps[static_cast<size_t>(j)] * x[static_cast<size_t>(i)]; }
            }
            y[m] = acc;
        }
        return y;
    }

    Vec interpolate(const Vec& x) const
    {
        Vec y(x.size() * 2);
        for (size_t m = 0; m < y.size(); ++m)
        {
            double acc = 0.0;
            for (int j = 0; j < static_cast<int>(taps.size()); ++j)
            {
                const auto i = static_cast<long>(m) - (j - centre);
                if (i >= 0 && i < static_cast<long>(y.size()) && (i % 2) == 0)
                {
                    acc += taps[static_cast<size_t>(j)] * 2.0 * x[static_cast<size_t>(i / 2)];
                }
            }
            y[m] = acc;
        }
        return y;
    }

    std::vector<double> oddTaps() const
    {
        std::vector<double> odd;
        for (int j = centre + 1; j < static_cast<int>(taps.size()); j += 2) { odd.push_back(taps[static_cast<size_t>(j)]); }
        return odd;
    }
};

const Halfband& halfband()
{
    static const Halfband h(63, 8.0);
    return h;
}

// Short halfbands for the FM decimator. With the two oversampled points taken at
// t = n and t = n + 1/2, a halfband with h non-zero taps a side has a latency of
// exactly h - 1 samples - so 15 taps (h = 4) lands on 3, the latency the 4-point
// PolyBLEP line already has, and every mode can share one.
const Halfband& halfband15()
{
    static const Halfband h(15, 5.0);
    return h;
}

const Halfband& halfband23()
{
    static const Halfband h(23, 6.0);
    return h;
}

// Streaming polyphase halfbands, the form production would run. Buffers are
// written twice so a window never wraps.
struct PolyDecimator
{
    std::vector<double> odd;
    std::vector<double> line;
    int length { 0 }, pos { 0 };

    explicit PolyDecimator(const Halfband& h) : odd(h.oddTaps())
    {
        length = 4 * static_cast<int>(odd.size());
        line.assign(static_cast<size_t>(2 * length), 0.0);
    }

    void push(double v)
    {
        line[static_cast<size_t>(pos)] = v;
        line[static_cast<size_t>(pos + length)] = v;
        pos = (pos + 1) % length;
    }

    double process(double a, double b)
    {
        push(a);
        push(b);
        const auto half = static_cast<int>(odd.size());
        const auto* newest = &line[static_cast<size_t>(pos + length - 1)];
        const auto centre = -(2 * half - 1);
        double acc = 0.5 * newest[centre];
        for (int j = 0; j < half; ++j)
        {
            acc += odd[static_cast<size_t>(j)] * (newest[centre + 2 * j + 1] + newest[centre - 2 * j - 1]);
        }
        return acc;
    }
};

struct PolyInterpolator
{
    std::vector<double> odd;
    std::vector<double> line;
    int length { 0 }, pos { 0 };

    explicit PolyInterpolator(const Halfband& h) : odd(h.oddTaps())
    {
        length = 2 * static_cast<int>(odd.size());
        line.assign(static_cast<size_t>(2 * length), 0.0);
    }

    void process(double x, double& first, double& second)
    {
        line[static_cast<size_t>(pos)] = x;
        line[static_cast<size_t>(pos + length)] = x;
        pos = (pos + 1) % length;
        const auto half = static_cast<int>(odd.size());
        const auto* newest = &line[static_cast<size_t>(pos + length - 1)];
        const auto c = -half;   // centre sample, half inputs back
        first = newest[c];
        double acc = 0.0;
        for (int j = 0; j < half; ++j)
        {
            acc += odd[static_cast<size_t>(j)] * (newest[c - j] + newest[c + 1 + j]);
        }
        second = 2.0 * acc;
    }
};

// ------------------------------------------------------------ nonlinear -----
double logCosh(double v)
{
    const auto a = std::abs(v);
    return a + std::log1p(std::exp(-2.0 * a)) - std::log(2.0);
}

// First-order ADAA (Parker, Zavalishin and Le Bivic 2016) for ANY static curve,
// with the antiderivative tabulated: cubic Hermite through exact values and
// exact slopes (the slope of the antiderivative is the curve itself), so the
// interpolation error is O(h^4) and C1 across cells.
struct AdaaTable
{
    double range { 12.0 }, step { 0.0 }, inverse { 0.0 };
    std::vector<double> big, value, slope;   // antiderivative, curve, curve's slope

    template <typename Curve, typename CurveSlope>
    AdaaTable(Curve curve, CurveSlope curveSlope, int nodes)
    {
        step = 2.0 * range / (nodes - 1);
        inverse = 1.0 / step;
        big.assign(static_cast<size_t>(nodes), 0.0);
        value.assign(static_cast<size_t>(nodes), 0.0);
        slope.assign(static_cast<size_t>(nodes), 0.0);
        for (int i = 0; i < nodes; ++i)
        {
            const auto x = -range + i * step;
            value[static_cast<size_t>(i)] = curve(x);
            slope[static_cast<size_t>(i)] = curveSlope(x);
        }
        // Simpson over each cell from the left end.
        for (int i = 1; i < nodes; ++i)
        {
            const auto a = -range + (i - 1) * step;
            constexpr int sub = 16;
            const auto h = step / sub;
            double acc = curve(a) + curve(a + step);
            for (int k = 1; k < sub; ++k) { acc += (k % 2 ? 4.0 : 2.0) * curve(a + k * h); }
            big[static_cast<size_t>(i)] = big[static_cast<size_t>(i - 1)] + acc * h / 3.0;
        }
    }

    static double hermite(double v0, double v1, double s0, double s1, double t, double h)
    {
        const auto t2 = t * t;
        const auto t3 = t2 * t;
        return (2.0 * t3 - 3.0 * t2 + 1.0) * v0 + (t3 - 2.0 * t2 + t) * h * s0
               + (-2.0 * t3 + 3.0 * t2) * v1 + (t3 - t2) * h * s1;
    }

    double antiderivative(double x) const
    {
        if (x >= range) { return big.back() + value.back() * (x - range); }
        if (x <= -range) { return big.front() + value.front() * (x + range); }
        const auto position = (x + range) * inverse;
        const auto i = std::min(static_cast<int>(position), static_cast<int>(big.size()) - 2);
        const auto t = position - i;
        return hermite(big[static_cast<size_t>(i)], big[static_cast<size_t>(i + 1)],
                       value[static_cast<size_t>(i)], value[static_cast<size_t>(i + 1)], t, step);
    }

    double curveAt(double x) const
    {
        if (x >= range) { return value.back(); }
        if (x <= -range) { return value.front(); }
        const auto position = (x + range) * inverse;
        const auto i = std::min(static_cast<int>(position), static_cast<int>(big.size()) - 2);
        const auto t = position - i;
        return hermite(value[static_cast<size_t>(i)], value[static_cast<size_t>(i + 1)],
                       slope[static_cast<size_t>(i)], slope[static_cast<size_t>(i + 1)], t, step);
    }
};

struct AdaaState
{
    double x { 0.0 }, big { 0.0 };
    bool primed { false };

    double process(const AdaaTable& table, double v)
    {
        const auto g = table.antiderivative(v);
        if (!primed) { primed = true; x = v; big = g; return table.curveAt(v); }
        const auto d = v - x;
        const auto y = std::abs(d) < 1.0e-5 ? table.curveAt(0.5 * (v + x)) : (g - big) / d;
        x = v;
        big = g;
        return y;
    }
};

const AdaaTable& tanhTable()
{
    static const AdaaTable t([](double x) { return std::tanh(x); },
                             [](double x) { const auto y = std::tanh(x); return 1.0 - y * y; }, 2049);
    return t;
}

// The two soft clippers every PX3 source passes through in series - the
// oscillator's own tanh(x), then the voice's tanh(0.92 x) - are ONE static
// curve, so one table can anti-alias both.
constexpr double kVoiceSourceDrive = 0.92;
const AdaaTable& sourceTable()
{
    static const AdaaTable t([](double x) { return std::tanh(kVoiceSourceDrive * std::tanh(x)); },
                             [](double x)
                             {
                                 const auto inner = std::tanh(x);
                                 const auto outer = std::tanh(kVoiceSourceDrive * inner);
                                 return kVoiceSourceDrive * (1.0 - outer * outer) * (1.0 - inner * inner);
                             }, 2049);
    return t;
}

enum class Shaper { direct, adaaExact, adaaTable, composite1x, compositeAdaa };

Vec shape(const Vec& x, double drive, Shaper how)
{
    Vec y(x.size());
    double previous = drive * x[0];
    AdaaState state;
    for (size_t m = 0; m < x.size(); ++m)
    {
        const auto v = drive * x[m];
        switch (how)
        {
            case Shaper::direct: y[m] = std::tanh(v); break;
            case Shaper::adaaExact:
            {
                const auto d = v - previous;
                y[m] = std::abs(d) < 1.0e-6 ? std::tanh(0.5 * (v + previous)) : (logCosh(v) - logCosh(previous)) / d;
                break;
            }
            case Shaper::adaaTable: y[m] = state.process(tanhTable(), v); break;
            case Shaper::composite1x: y[m] = std::tanh(kVoiceSourceDrive * std::tanh(v)); break;
            case Shaper::compositeAdaa: y[m] = state.process(sourceTable(), v); break;
        }
        previous = v;
    }
    return y;
}

// Three periodic frames back to back, keep the middle one: past every filter's
// transient, and still exactly periodic.
template <typename Process>
Vec periodic(const Vec& x, Process process)
{
    Vec three;
    three.reserve(x.size() * 3);
    for (int i = 0; i < 3; ++i) { three.insert(three.end(), x.begin(), x.end()); }
    const auto y = process(three);
    const auto scale = y.size() / three.size();
    return Vec(y.begin() + static_cast<long>(x.size() / scale * scale / 1),
               y.begin() + static_cast<long>(2 * x.size()));
}

Vec oversampledShape(const Vec& x, double drive, int factor, Shaper how)
{
    auto up = x;
    for (int f = 1; f < factor; f *= 2) { up = halfband().interpolate(up); }
    up = shape(up, drive, how);
    for (int f = 1; f < factor; f *= 2) { up = halfband().decimate(up); }
    return up;
}

// ------------------------------------------------------------------ FM ------
Vec fm(const Tone& t, double ratio, double index, int factor, const Halfband& filter = halfband())
{
    const auto frames = 3 * kN * factor;
    Vec y(static_cast<size_t>(frames));
    const auto dt = t.dt / factor;
    for (int m = 0; m < frames; ++m)
    {
        const auto u = m * dt;
        y[static_cast<size_t>(m)] = std::sin(2.0 * kPi * u + index * std::sin(2.0 * kPi * ratio * u));
    }
    for (int f = 1; f < factor; f *= 2) { y = filter.decimate(y); }
    return Vec(y.begin() + kN, y.begin() + 2 * kN);
}

// ----------------------------------------------------------------- noise ----
struct SplitMix32
{
    uint32_t state;
    uint32_t next()
    {
        uint32_t z = (state += 0x9E3779B9u);
        z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
        z = (z ^ (z >> 13)) * 0xC2B2AE35u;
        return z ^ (z >> 16);
    }
    double white() { return (static_cast<double>(next()) / 4294967296.0) * 2.0 - 1.0; }
};

struct Lcg   // the generator PX3 ships, with the seed every instance shares
{
    uint32_t seed { 0x13579BDFu };
    double white()
    {
        seed = seed * 1664525u + 1013904223u;
        const auto bits = static_cast<int32_t>((seed >> 9) & 0x007FFFFFu);
        return (static_cast<double>(bits) / 4194303.5) * 2.0 - 1.0;
    }
};

struct PinkKellet
{
    std::array<double, 7> b {};
    double process(double white)
    {
        b[0] = 0.99886 * b[0] + white * 0.0555179;
        b[1] = 0.99332 * b[1] + white * 0.0750759;
        b[2] = 0.96900 * b[2] + white * 0.1538520;
        b[3] = 0.86650 * b[3] + white * 0.3104856;
        b[4] = 0.55000 * b[4] + white * 0.5329522;
        b[5] = -0.7616 * b[5] - white * 0.0168980;
        const auto pink = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + white * 0.5362;
        b[6] = white * 0.115926;
        return pink * 0.11;
    }
};

// Kellet's refined filter made sample-rate aware: each of its five real poles is
// moved to the same frequency in hertz at the new rate (p' = p^(44100/fs)), each
// branch keeps its DC gain, and white is scaled so its density per hertz is the
// same at every rate. The two terms that shape the top octave are left alone.
struct PinkKelletMapped
{
    std::array<double, 5> pole { { 0.99886, 0.99332, 0.96900, 0.86650, 0.55000 } };
    std::array<double, 5> gain { { 0.0555179, 0.0750759, 0.1538520, 0.3104856, 0.5329522 } };
    std::array<double, 7> b {};
    double scale { 1.0 };

    explicit PinkKelletMapped(double fs)
    {
        const auto r = 44100.0 / fs;
        for (size_t i = 0; i < pole.size(); ++i)
        {
            const auto mapped = std::pow(pole[i], r);
            gain[i] *= (1.0 - mapped) / (1.0 - pole[i]);
            pole[i] = mapped;
        }
        scale = std::sqrt(fs / 44100.0);
    }

    double process(double white)
    {
        const auto w = white * scale;
        double pink = 0.0;
        for (size_t i = 0; i < pole.size(); ++i)
        {
            b[i] = pole[i] * b[i] + w * gain[i];
            pink += b[i];
        }
        b[5] = -0.7616 * b[5] - w * 0.0168980;
        pink += b[5] + b[6] + w * 0.5362;
        b[6] = w * 0.115926;
        return pink * 0.11;
    }
};

// Kellet's response, refitted at the running sample rate. His five real poles
// are kept at their frequencies in hertz, one more pole covers the top octave
// his b5/b6 terms used to shape, and the branch gains plus a direct term are
// solved by linear least squares against his OWN 44.1 kHz response over
// 10 Hz..20 kHz. For fixed poles the response is linear in the gains, so this
// is a 7x7 solve - run once in prepare, never on the audio thread.
struct PinkFitted
{
    std::array<double, 6> pole {};
    std::array<double, 6> gain {};
    std::array<double, 6> state {};
    double direct { 0.0 };

    static std::complex<double> kelletResponse(double hz)
    {
        const std::complex<double> z1 = std::polar(1.0, -2.0 * kPi * hz / 44100.0);   // z^-1
        constexpr std::array<double, 5> p { { 0.99886, 0.99332, 0.96900, 0.86650, 0.55000 } };
        constexpr std::array<double, 5> g { { 0.0555179, 0.0750759, 0.1538520, 0.3104856, 0.5329522 } };
        std::complex<double> h(0.5362, 0.0);
        for (size_t i = 0; i < p.size(); ++i) { h += g[i] / (1.0 - p[i] * z1); }
        h += -0.0168980 / (1.0 + 0.7616 * z1);
        h += 0.115926 * z1;
        return h * 0.11;
    }

    explicit PinkFitted(double fs)
    {
        constexpr std::array<double, 5> kellet { { 0.99886, 0.99332, 0.96900, 0.86650, 0.55000 } };
        for (size_t i = 0; i < kellet.size(); ++i)
        {
            const auto hz = -std::log(kellet[i]) * 44100.0 / (2.0 * kPi);
            pole[i] = std::exp(-2.0 * kPi * hz / fs);
        }
        pole[5] = std::exp(-2.0 * kPi * 12000.0 / fs);

        constexpr int unknowns = 7;
        std::array<std::array<double, unknowns + 1>, unknowns> normal {};
        constexpr int points = 480;
        for (int k = 0; k < points; ++k)
        {
            const auto hz = 10.0 * std::pow(2000.0, static_cast<double>(k) / (points - 1));
            const auto target = kelletResponse(hz);
            const auto weight = 1.0 / std::abs(target);
            const std::complex<double> z1 = std::polar(1.0, -2.0 * kPi * hz / fs);
            std::array<std::complex<double>, unknowns> basis {};
            for (size_t i = 0; i < pole.size(); ++i) { basis[i] = 1.0 / (1.0 - pole[i] * z1); }
            basis[6] = 1.0;
            // Real and imaginary parts are separate equations; the unknowns are real.
            for (int part = 0; part < 2; ++part)
            {
                std::array<double, unknowns> row {};
                for (int j = 0; j < unknowns; ++j) { row[static_cast<size_t>(j)] = weight * (part == 0 ? basis[static_cast<size_t>(j)].real() : basis[static_cast<size_t>(j)].imag()); }
                const auto rhs = weight * (part == 0 ? target.real() : target.imag());
                for (int a = 0; a < unknowns; ++a)
                {
                    for (int b = 0; b < unknowns; ++b) { normal[static_cast<size_t>(a)][static_cast<size_t>(b)] += row[static_cast<size_t>(a)] * row[static_cast<size_t>(b)]; }
                    normal[static_cast<size_t>(a)][unknowns] += row[static_cast<size_t>(a)] * rhs;
                }
            }
        }
        // Gaussian elimination with partial pivoting.
        for (int c = 0; c < unknowns; ++c)
        {
            int best = c;
            for (int r = c + 1; r < unknowns; ++r) { if (std::abs(normal[static_cast<size_t>(r)][static_cast<size_t>(c)]) > std::abs(normal[static_cast<size_t>(best)][static_cast<size_t>(c)])) best = r; }
            std::swap(normal[static_cast<size_t>(c)], normal[static_cast<size_t>(best)]);
            for (int r = 0; r < unknowns; ++r)
            {
                if (r == c) continue;
                const auto f = normal[static_cast<size_t>(r)][static_cast<size_t>(c)] / normal[static_cast<size_t>(c)][static_cast<size_t>(c)];
                for (int k = c; k <= unknowns; ++k) { normal[static_cast<size_t>(r)][static_cast<size_t>(k)] -= f * normal[static_cast<size_t>(c)][static_cast<size_t>(k)]; }
            }
        }
        for (int j = 0; j < 6; ++j) { gain[static_cast<size_t>(j)] = normal[static_cast<size_t>(j)][unknowns] / normal[static_cast<size_t>(j)][static_cast<size_t>(j)]; }
        direct = normal[6][unknowns] / normal[6][6];
        // Fitted against a 44.1 kHz response, where white noise has its density
        // spread over 22 kHz; keep the density per hertz the same at this rate.
        const auto scale = std::sqrt(fs / 44100.0);
        for (auto& g : gain) { g *= scale; }
        direct *= scale;
    }

    double process(double white)
    {
        double out = direct * white;
        for (size_t i = 0; i < pole.size(); ++i)
        {
            state[i] = pole[i] * state[i] + gain[i] * white;
            out += state[i];
        }
        return out;
    }
};

// Alternating real poles and zeros, designed in HERTZ through the bilinear
// transform with prewarping - so the corner frequencies, and with them the
// slope, are the same at every sample rate by construction.
struct PinkDesigned
{
    struct Section { double b0, b1, a1, x1 = 0.0, y1 = 0.0; };
    std::vector<Section> sections;
    double gain { 1.0 };

    PinkDesigned(double fs, double spacingOctaves)
    {
        const auto k = 2.0 * fs;
        const auto top = std::min(20000.0, 0.45 * fs);
        for (double pole = 6.0; pole < top; pole *= std::pow(2.0, spacingOctaves))
        {
            const auto zero = std::min(pole * std::pow(2.0, spacingOctaves * 0.5), 0.49 * fs);
            const auto wp = k * std::tan(kPi * pole / fs);
            const auto wz = k * std::tan(kPi * zero / fs);
            const auto a0 = k + wp;
            sections.push_back({ (k + wz) / a0, (wz - k) / a0, (wp - k) / a0 });
        }
        const std::complex<double> z = std::polar(1.0, -2.0 * kPi * 1000.0 / fs);
        std::complex<double> h(1.0, 0.0);
        for (const auto& s : sections) { h *= (s.b0 + s.b1 * z) / (1.0 + s.a1 * z); }
        // Unity at 1 kHz, and white scaled so its density per hertz does not
        // fall as the sample rate rises.
        gain = std::sqrt(fs / 48000.0) / std::abs(h);
    }

    double process(double white)
    {
        auto x = white * gain;
        for (auto& s : sections)
        {
            const auto y = s.b0 * x + s.b1 * s.x1 - s.a1 * s.y1;
            s.x1 = x;
            s.y1 = y;
            x = y;
        }
        return x;
    }
};

struct SlopeReport { double slope; double ripple; double densityAt1k; };

template <typename Gen>
SlopeReport pinkSlope(Gen& generator, SplitMix32& rng, double fs)
{
    // The segment grows with the sample rate so every rate has the same bin
    // width in hertz: at 96 kHz a 2^14 segment left the 25 Hz band a single bin
    // wide, and its variance read as filter ripple.
    const int segment = fs > 50000.0 ? 1 << 15 : 1 << 14;
    constexpr int segments = 192;
    Vec density(static_cast<size_t>(segment / 2), 0.0);
    Vec window(static_cast<size_t>(segment));
    double windowPower = 0.0;
    for (int i = 0; i < segment; ++i)
    {
        window[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / segment);
        windowPower += window[static_cast<size_t>(i)] * window[static_cast<size_t>(i)];
    }
    for (int i = 0; i < 1 << 16; ++i) { generator.process(rng.white()); }
    for (int s = 0; s < segments; ++s)
    {
        std::vector<std::complex<double>> a(static_cast<size_t>(segment));
        for (int i = 0; i < segment; ++i) { a[static_cast<size_t>(i)] = generator.process(rng.white()) * window[static_cast<size_t>(i)]; }
        fft(a);
        for (int k = 1; k < segment / 2; ++k) { density[static_cast<size_t>(k)] += std::norm(a[static_cast<size_t>(k)]) / (fs * windowPower); }
    }
    std::vector<std::pair<double, double>> points;
    for (double centre = 25.0; centre <= 16000.0; centre *= std::pow(2.0, 1.0 / 3.0))
    {
        const auto lo = static_cast<int>(centre * std::pow(2.0, -1.0 / 6.0) * segment / fs);
        const auto hi = static_cast<int>(centre * std::pow(2.0, 1.0 / 6.0) * segment / fs);
        double sum = 0.0;
        int count = 0;
        for (int k = std::max(1, lo); k <= hi; ++k) { sum += density[static_cast<size_t>(k)]; ++count; }
        if (count > 0) { points.push_back({ std::log2(centre), db(sum / count / segments) }); }
    }
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (const auto& p : points) { sx += p.first; sy += p.second; sxx += p.first * p.first; sxy += p.first * p.second; }
    const auto n = static_cast<double>(points.size());
    const auto slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
    const auto intercept = (sy - slope * sx) / n;
    double ripple = 0.0;
    for (const auto& p : points) { ripple = std::max(ripple, std::abs(p.second - (intercept + slope * p.first))); }
    return { slope, ripple, intercept + slope * std::log2(1000.0) };
}

// ---------------------------------------------------------------- timing ----
// Block rendering into a buffer, the way a voice runs, so the compiler sees the
// same loop shape it will see in production.
template <typename Render>
double nsPerSample(Render render)
{
    constexpr int block = 512;
    constexpr int blocks = 4096;
    std::vector<double> buffer(block);
    double checksum = 0.0;
    const auto start = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b)
    {
        render(buffer.data(), block);
        checksum += buffer[static_cast<size_t>(block - 1)];
    }
    const auto end = std::chrono::steady_clock::now();
    volatile double sink = checksum;
    (void) sink;
    return std::chrono::duration<double, std::nano>(end - start).count() / (block * blocks);
}

void printReport(const char* label, const Report& r)
{
    std::printf("    %-28s alias %7.1f  <15k %7.1f  worst %7.1f  10k %6.2f dB\n", label, r.alias, r.alias15k, r.worst, r.droop10k);
}
} // namespace

int main()
{
    constexpr std::array<int, 8> notes { 36, 48, 60, 72, 84, 96, 108, 120 };

    // ---------------------------------------------------------------- saw --
    std::printf("\nSAW - alias relative to the fundamental, dB (lower is cleaner); 10k = harmonic nearest 10 kHz against 1/h\n");
    for (const auto fs : { 44100.0, 48000.0, 96000.0 })
    {
        std::printf("\n  fs %.0f\n", fs);
        for (const auto note : notes)
        {
            const auto t = toneNear(midiHz(note), fs);
            std::printf("  MIDI %d (%.1f Hz)\n", note, t.hz);
            printReport("naive", analyse(saw(t, 0), fs, t, sawIdeal));
            printReport("DPW2", analyse(sawDpw(t), fs, t, sawIdeal));
            printReport("PolyBLEP 2-point", analyse(saw(t, 2), fs, t, sawIdeal));
            printReport("PolyBLEP 4-point", analyse(saw(t, 4), fs, t, sawIdeal));
            printReport("PolyBLEP 4 + comp a=0.20", analyse(compensate(saw(t, 4), 0.20), fs, t, sawIdeal));
            printReport("PolyBLEP 4 + comp a=0.25", analyse(compensate(saw(t, 4), 0.25), fs, t, sawIdeal));
            if (fs == 48000.0)
            {
                Vec y(static_cast<size_t>(3 * kN * 2));
                for (size_t m = 0; m < y.size(); ++m) { y[m] = 2.0 * frac(m * t.dt / 2.0) - 1.0; }
                y = halfband().decimate(y);
                printReport("naive at 2x, decimated", analyse(Vec(y.begin() + kN, y.begin() + 2 * kN), fs, t, sawIdeal));
            }
        }
    }

    // ------------------------------------------------------ square/triangle --
    std::printf("\nSQUARE 50%% and TRIANGLE, fs 48000\n");
    for (const auto note : notes)
    {
        const auto t = toneNear(midiHz(note), 48000.0);
        std::printf("  MIDI %d\n", note);
        printReport("square naive", analyse(pulse(t, 0.5, 0), 48000.0, t, oddIdeal));
        printReport("square PolyBLEP 2", analyse(pulse(t, 0.5, 2), 48000.0, t, oddIdeal));
        printReport("square PolyBLEP 4", analyse(pulse(t, 0.5, 4), 48000.0, t, oddIdeal));
        printReport("square PolyBLEP 4 + comp", analyse(compensate(pulse(t, 0.5, 4), 0.20), 48000.0, t, oddIdeal));
        printReport("triangle naive", analyse(triangle(t, 0), 48000.0, t, triangleIdeal));
        printReport("triangle PolyBLAMP 2", analyse(triangle(t, 2), 48000.0, t, triangleIdeal));
        printReport("triangle PolyBLAMP 4", analyse(triangle(t, 4), 48000.0, t, triangleIdeal));
        printReport("triangle PolyBLAMP 4 + comp", analyse(compensate(triangle(t, 4), 0.20), 48000.0, t, triangleIdeal));
        printReport("triangle int. square", analyse(triangleIntegratedSquare(t), 48000.0, t, triangleIdeal));
    }

    // ---------------------------------------------------------- overshoot --
    // The compensation FIR [-a, 1+2a, -a] rings at every edge: on a raw step it
    // overshoots by 2a. Spectra do not show that, and a taller peak drives the
    // voice's soft clip harder, so the peak is measured against the ideal
    // band-limited waveform's own Gibbs peak.
    std::printf("\nOVERSHOOT - peak of the band-limited waveform (ideal band-limited peak in brackets) and 10 kHz level, fs 48000\n");
    for (const auto note : { 36, 48, 60, 72, 84, 96, 108 })
    {
        const auto t = toneNear(midiHz(note), 48000.0);
        double idealSquare = 0.0, idealSaw = 0.0;
        for (int m = 0; m < 4096; ++m)
        {
            const auto p = static_cast<double>(m) / 4096.0;
            double sq = 0.0, sw = 0.0;
            for (int k = 1; k * t.hz < 24000.0; ++k)
            {
                sw += -2.0 / (kPi * k) * std::sin(2.0 * kPi * k * p);
                if (k % 2) { sq += 4.0 / (kPi * k) * std::sin(2.0 * kPi * k * p); }
            }
            idealSquare = std::max(idealSquare, std::abs(sq));
            idealSaw = std::max(idealSaw, std::abs(sw));
        }
        std::printf("  MIDI %3d  saw [%.3f] square [%.3f]\n", note, idealSaw, idealSquare);
        for (const auto a : { 0.0, 0.10, 0.15, 0.20, 0.25 })
        {
            const auto sawWave = compensate(saw(t, 4), a);
            const auto squareWave = compensate(pulse(t, 0.5, 4), a);
            double sawPeak = 0.0, squarePeak = 0.0;
            for (const auto v : sawWave) { sawPeak = std::max(sawPeak, std::abs(v)); }
            for (const auto v : squareWave) { squarePeak = std::max(squarePeak, std::abs(v)); }
            const auto r = analyse(sawWave, 48000.0, t, sawIdeal);
            std::printf("    a %.2f  saw peak %.3f  square peak %.3f  saw 10k %+5.2f dB  saw alias <15k %6.1f\n",
                        a, sawPeak, squarePeak, r.droop10k, r.alias15k);
        }
    }

    // ----------------------------------------------------------------- PWM --
    std::printf("\nPWM - alias against TOTAL harmonic power (a narrow pulse has almost no fundamental), fs 48000\n");
    for (const auto note : { 48, 60, 84, 96, 108 })
    {
        const auto t = toneNear(midiHz(note), 48000.0);
        std::printf("  MIDI %d (%.4f cycles/sample; a pulse narrower than a sample below %.1f%%)\n", note, t.dt, t.dt * 100.0);
        for (const auto w : { 0.01, 0.05, 0.10, 0.25, 0.50, 0.75, 0.90, 0.95, 0.99 })
        {
            const auto n = analyse(pulse(t, w, 0), 48000.0, t, noIdeal);
            const auto b = analyse(pulse(t, w, 2), 48000.0, t, noIdeal);
            const auto c = analyse(pulse(t, w, 4), 48000.0, t, noIdeal);
            const auto d = analyse(compensate(pulse(t, w, 4), 0.20), 48000.0, t, noIdeal);
            std::printf("    width %4.0f%%  naive %7.1f  PolyBLEP2 %7.1f  PolyBLEP4 %7.1f  4+comp %7.1f   mean %+.4f (ideal %+.4f)\n",
                        w * 100.0, n.aliasVsSignal, b.aliasVsSignal, c.aliasVsSignal, d.aliasVsSignal, c.mean, 2.0 * w - 1.0);
        }
    }

    // ---------------------------------------------------------------- sync --
    std::printf("\nHARD SYNC - saw slave, fs 48000 - alias total (<15k)\n");
    for (const auto note : { 48, 72, 96 })
    {
        const auto t = toneNear(midiHz(note), 48000.0);
        std::printf("  MIDI %d\n", note);
        for (const auto ratio : { 1.5, 2.37, 4.6, 7.9 })
        {
            const auto q = analyse(syncSawQuantised(t, ratio), 48000.0, t, noIdeal);
            const auto n = analyse(syncSaw(t, ratio, 0), 48000.0, t, noIdeal);
            const auto e2 = analyse(syncSaw(t, ratio, 2), 48000.0, t, noIdeal);
            const auto e4 = analyse(syncSaw(t, ratio, 4), 48000.0, t, noIdeal);
            std::printf("    ratio %4.2f  sample-reset %6.1f (%6.1f)  exact-naive %6.1f (%6.1f)  PolyBLEP2 %6.1f (%6.1f)  PolyBLEP4 %6.1f (%6.1f)\n",
                        ratio, q.alias, q.alias15k, n.alias, n.alias15k, e2.alias, e2.alias15k, e4.alias, e4.alias15k);
        }
    }

    // ---------------------------------------------------------------- tanh --
    std::printf("\nTANH - input a 4-point PolyBLEP saw or a sine, fs 48000 - alias total / <15k, vs fundamental\n");
    for (const auto note : { 48, 72, 96, 108 })
    {
        const auto t = toneNear(midiHz(note), 48000.0);
        const auto sawInput = saw(t, 4);
        Vec sineInput(static_cast<size_t>(kN));
        for (int m = 0; m < kN; ++m) { sineInput[static_cast<size_t>(m)] = std::sin(2.0 * kPi * m * t.dt); }
        const auto input = analyse(sawInput, 48000.0, t, sawIdeal);
        std::printf("  MIDI %d  (saw input itself %6.1f / %6.1f)\n", note, input.alias, input.alias15k);
        for (const auto drive : { 0.92, 3.3, 8.0 })
        {
            for (const auto* name : { "saw", "sine" })
            {
                const auto& x = std::string(name) == "saw" ? sawInput : sineInput;
                const auto r = [&](Shaper how, int factor)
                {
                    return analyse(periodic(x, [&](const Vec& in) { return factor == 1 ? shape(in, drive, how) : oversampledShape(in, drive, factor, how); }),
                                   48000.0, t, noIdeal);
                };
                const auto direct = r(Shaper::direct, 1);
                const auto exact = r(Shaper::adaaExact, 1);
                const auto table = r(Shaper::adaaTable, 1);
                const auto os2 = r(Shaper::direct, 2);
                const auto os4 = r(Shaper::direct, 4);
                const auto os2adaa = r(Shaper::adaaTable, 2);
                std::printf("    %-4s drive %4.2f  1x %6.1f/%6.1f  ADAA %6.1f/%6.1f  ADAAtab %6.1f/%6.1f  2x %6.1f/%6.1f  4x %6.1f/%6.1f  2x+ADAA %6.1f/%6.1f\n",
                            name, drive, direct.alias, direct.alias15k, exact.alias, exact.alias15k, table.alias, table.alias15k,
                            os2.alias, os2.alias15k, os4.alias, os4.alias15k, os2adaa.alias, os2adaa.alias15k);
            }
        }
    }

    std::printf("\nVOICE SOURCE STAGE - the oscillator's tanh(x) then the voice's tanh(0.92 x), as shipped vs one ADAA curve\n");
    for (const auto note : { 36, 48, 60, 72, 84, 96, 108 })
    {
        const auto t = toneNear(midiHz(note), 48000.0);
        for (const auto* name : { "saw", "square" })
        {
            const auto x = std::string(name) == "saw" ? saw(t, 4) : pulse(t, 0.5, 4);
            // The modes' own level into the clipper is ~0.75.
            const auto in = analyse(x, 48000.0, t, noIdeal);
            const auto shipped = analyse(periodic(x, [&](const Vec& v) { return shape(v, 0.75, Shaper::composite1x); }), 48000.0, t, noIdeal);
            const auto adaa = analyse(periodic(x, [&](const Vec& v) { return shape(v, 0.75, Shaper::compositeAdaa); }), 48000.0, t, noIdeal);
            const auto a = periodic(x, [&](const Vec& v) { return shape(v, 0.75, Shaper::composite1x); });
            const auto b = periodic(x, [&](const Vec& v) { return shape(v, 0.75, Shaper::compositeAdaa); });
            double rmsA = 0.0, rmsB = 0.0;
            for (size_t i = 0; i < a.size(); ++i) { rmsA += a[i] * a[i]; rmsB += b[i] * b[i]; }
            std::printf("  MIDI %3d %-6s  input %6.1f/%6.1f  shipped two tanh %6.1f/%6.1f  one ADAA curve %6.1f/%6.1f  level change %+.2f dB\n",
                        note, name, in.alias, in.alias15k, shipped.alias, shipped.alias15k, adaa.alias, adaa.alias15k,
                        10.0 * std::log10(rmsB / rmsA));
        }
    }

    // ------------------------------------------------------------------ FM --
    std::printf("\nFM - sine carrier and modulator, fs 48000 - alias total / <15k\n");
    for (const auto note : { 48, 72, 84, 96 })
    {
        for (const auto ratio : { 1.0, 3.5 })
        {
            const auto t = toneNear(midiHz(note), 48000.0, ratio == 1.0 ? 1 : 2);
            for (const auto index : { 2.0, 5.0, 10.0 })
            {
                const auto a = analyse(fm(t, ratio, index, 1), 48000.0, t, noIdeal);
                const auto b = analyse(fm(t, ratio, index, 2), 48000.0, t, noIdeal);
                const auto c = analyse(fm(t, ratio, index, 4), 48000.0, t, noIdeal);
                const auto d = analyse(fm(t, ratio, index, 2, halfband15()), 48000.0, t, noIdeal);
                const auto e = analyse(fm(t, ratio, index, 2, halfband23()), 48000.0, t, noIdeal);
                const auto carson = t.hz + (index + 1.0) * ratio * t.hz;
                std::printf("  MIDI %3d ratio %.1f index %4.1f  Carson top %6.0f Hz  1x %6.1f/%6.1f  2x FIR63 %6.1f/%6.1f  2x FIR23 %6.1f/%6.1f  2x FIR15 %6.1f/%6.1f  4x %6.1f/%6.1f\n",
                            note, ratio, index, carson, a.alias, a.alias15k, b.alias, b.alias15k, e.alias, e.alias15k,
                            d.alias, d.alias15k, c.alias, c.alias15k);
            }
        }
    }

    // -------------------------------------------------------------- noise ---
    std::printf("\nPINK NOISE - fitted slope over 25 Hz..16 kHz (ideal -3.01 dB/oct), worst deviation from the fit, density at 1 kHz\n");
    for (const auto fs : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        SplitMix32 rngA { 1234u }, rngB { 1234u }, rngC { 1234u }, rngD { 1234u }, rngE { 1234u };
        PinkKellet kellet;
        PinkKelletMapped mapped(fs);
        PinkFitted fitted(fs);
        const auto f = pinkSlope(fitted, rngE, fs);
        PinkDesigned wide(fs, 1.5), narrow(fs, 1.0);
        const auto k = pinkSlope(kellet, rngA, fs);
        const auto m = pinkSlope(mapped, rngD, fs);
        const auto w = pinkSlope(wide, rngB, fs);
        const auto n = pinkSlope(narrow, rngC, fs);
        std::printf("  fs %6.0f  Kellet %+.3f ripple %.2f 1k %5.1f   mapped %+.3f ripple %.2f 1k %5.1f   FITTED %+.3f ripple %.2f 1k %5.1f   designed 1.5 oct %+.3f ripple %.2f   1 oct %+.3f ripple %.2f\n",
                    fs, k.slope, k.ripple, k.densityAt1k, m.slope, m.ripple, m.densityAt1k, f.slope, f.ripple, f.densityAt1k, w.slope, w.ripple, n.slope, n.ripple);
    }

    std::printf("\nNOISE STREAMS - normalised cross-correlation, worst over lags -64..64, 2^20 samples\n");
    {
        constexpr int count = 1 << 20;
        const auto correlate = [&](Vec& a, Vec& b)
        {
            double worst = 0.0, ea = 0.0, eb = 0.0;
            for (int i = 0; i < count; ++i) { ea += a[static_cast<size_t>(i)] * a[static_cast<size_t>(i)]; eb += b[static_cast<size_t>(i)] * b[static_cast<size_t>(i)]; }
            for (int lag = -64; lag <= 64; ++lag)
            {
                double c = 0.0;
                for (int i = 64; i < count - 64; ++i) { c += a[static_cast<size_t>(i)] * b[static_cast<size_t>(i + lag)]; }
                worst = std::max(worst, std::abs(c) / std::sqrt(ea * eb));
            }
            return worst;
        };
        Vec a(count), b(count), c(count);
        Lcg lcgA, lcgB;
        for (int i = 0; i < count; ++i) { a[static_cast<size_t>(i)] = lcgA.white(); b[static_cast<size_t>(i)] = lcgB.white(); }
        std::printf("  shipped LCG, two voices     %.4f\n", correlate(a, b));
        const auto seedFor = [](uint32_t voice, uint32_t note, uint32_t osc)
        {
            uint32_t h = (voice + 1u) * 747796405u;
            h ^= note * 2891336453u;
            h ^= (osc + 1u) * 277803737u;
            h ^= h >> 16;
            return h * 2246822519u;
        };
        SplitMix32 sA { seedFor(0, 1, 0) }, sB { seedFor(1, 1, 0) }, sC { seedFor(0, 1, 1) }, sD { seedFor(0, 2, 0) };
        for (int i = 0; i < count; ++i) { a[static_cast<size_t>(i)] = sA.white(); b[static_cast<size_t>(i)] = sB.white(); c[static_cast<size_t>(i)] = sC.white(); }
        std::printf("  hashed SplitMix, voice 0/1  %.4f\n", correlate(a, b));
        std::printf("  hashed SplitMix, osc 1/2    %.4f\n", correlate(a, c));
        for (int i = 0; i < count; ++i) { b[static_cast<size_t>(i)] = sD.white(); }
        std::printf("  hashed SplitMix, note 1/2   %.4f\n", correlate(a, b));
        std::printf("  (independent white noise at 2^20 samples sits near %.4f)\n", 3.0 / std::sqrt(static_cast<double>(count)));
    }

    // ------------------------------------------------------ phase precision --
    std::printf("\nPHASE ACCUMULATOR PRECISION - pitch error after 10 s, cents\n");
    for (const auto fs : { 44100.0, 96000.0 })
    {
        for (const auto note : { 24, 36, 60, 96, 120 })
        {
            const auto hz = midiHz(note);
            const auto samples = static_cast<long>(10.0 * fs);
            float pf = 0.0f;
            double pd = 0.0;
            long wrapsF = 0, wrapsD = 0;
            const auto dtf = static_cast<float>(hz / fs);
            const auto dtd = hz / fs;
            for (long i = 0; i < samples; ++i)
            {
                pf += dtf; if (pf >= 1.0f) { pf -= 1.0f; ++wrapsF; }
                pd += dtd; if (pd >= 1.0) { pd -= 1.0; ++wrapsD; }
            }
            const auto ideal = hz * samples / fs;
            std::printf("  fs %6.0f MIDI %3d  float32 %+8.4f   double %+.2e\n", fs, note,
                        1200.0 * std::log2((wrapsF + pf) / ideal), 1200.0 * std::log2((wrapsD + pd) / ideal));
        }
    }

    // ------------------------------------------------ polyphase sanity check --
    {
        SplitMix32 rng { 7u };
        Vec x(4096);
        for (auto& v : x) { v = rng.white(); }
        const auto slow = halfband().decimate(halfband().interpolate(x));
        PolyInterpolator up(halfband());
        PolyDecimator down(halfband());
        Vec fast;
        for (const auto v : x)
        {
            double a, b;
            up.process(v, a, b);
            fast.push_back(down.process(a, b));
        }
        double best = 1.0e9;
        int bestLag = 0;
        for (int lag = 0; lag < 100; ++lag)
        {
            double worst = 0.0;
            for (int i = 200; i < 3800; ++i) { worst = std::max(worst, std::abs(fast[static_cast<size_t>(i + lag)] - slow[static_cast<size_t>(i)])); }
            if (worst < best) { best = worst; bestLag = lag; }
        }
        std::printf("\nPOLYPHASE HALFBAND matches the reference filter: worst difference %.2e at a latency of %d samples\n", best, bestLag);
    }

    // ---------------------------------------------------------------- cost --
    std::printf("\nCOST - ns per output sample, block-rendered, -O3 (compare within the table; absolute values are machine-specific)\n");
    {
        const auto dt = midiHz(60) / 48000.0;
        double p = 0.0;
        std::printf("  saw naive                    %6.2f\n", nsPerSample([&](double* out, int n)
        {
            for (int i = 0; i < n; ++i) { p += dt; if (p >= 1.0) p -= 1.0; out[i] = 2.0 * p - 1.0; }
        }));
        p = 0.0;
        std::printf("  saw PolyBLEP 2, stateless    %6.2f\n", nsPerSample([&](double* out, int n)
        {
            for (int i = 0; i < n; ++i) { p += dt; if (p >= 1.0) p -= 1.0; out[i] = 2.0 * p - 1.0 - polyBlep2(p, dt); }
        }));

        struct Saw4 { double phase = 0.0, z1 = 0.0, z2 = 0.0, z3 = 0.0, carry = 0.0; };
        Saw4 s4;
        const auto saw4 = [&](Saw4& s, bool comp) -> double
        {
            s.phase += dt;
            auto y = s.carry;
            s.carry = 0.0;
            if (s.phase >= 1.0)
            {
                s.phase -= 1.0;
                const auto tau = s.phase / dt;
                s.z2 += -2.0 * blepResidual(tau - 2.0, 4);
                s.z1 += -2.0 * blepResidual(tau - 1.0, 4);
                y += -2.0 * blepResidual(tau, 4);
                s.carry = -2.0 * blepResidual(tau + 1.0, 4);
            }
            y += 2.0 * s.phase - 1.0;
            const auto out = comp ? -0.2 * s.z3 + 1.4 * s.z2 - 0.2 * s.z1 : s.z2;
            s.z3 = s.z2;
            s.z2 = s.z1;
            s.z1 = y;
            return out;
        };
        std::printf("  saw PolyBLEP 4, streaming    %6.2f\n", nsPerSample([&](double* out, int n) { for (int i = 0; i < n; ++i) out[i] = saw4(s4, false); }));
        std::printf("  saw PolyBLEP 4 + comp        %6.2f\n", nsPerSample([&](double* out, int n) { for (int i = 0; i < n; ++i) out[i] = saw4(s4, true); }));
        std::printf("  super saw, 7 x PolyBLEP 4    %6.2f\n", nsPerSample([&](double* out, int n)
        {
            static std::array<Saw4, 7> stack {};
            for (int i = 0; i < n; ++i) { double sum = 0.0; for (auto& s : stack) sum += saw4(s, false); out[i] = sum; }
        }));

        double x = 0.0;
        const auto ramp = [&]() { x += 0.0137; if (x > 1.0) x -= 2.0; return 0.75 * x; };
        std::printf("  tanh                         %6.2f\n", nsPerSample([&](double* out, int n) { for (int i = 0; i < n; ++i) out[i] = std::tanh(ramp()); }));
        std::printf("  two tanh (shipped source)    %6.2f\n", nsPerSample([&](double* out, int n) { for (int i = 0; i < n; ++i) out[i] = std::tanh(0.92 * std::tanh(ramp())); }));
        double previous = 0.0;
        std::printf("  ADAA, exact log-cosh         %6.2f\n", nsPerSample([&](double* out, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                const auto v = ramp();
                const auto d = v - previous;
                out[i] = std::abs(d) < 1.0e-6 ? std::tanh(0.5 * (v + previous)) : (logCosh(v) - logCosh(previous)) / d;
                previous = v;
            }
        }));
        AdaaState state;
        std::printf("  ADAA, tabulated (any curve)  %6.2f\n", nsPerSample([&](double* out, int n)
        {
            for (int i = 0; i < n; ++i) out[i] = state.process(sourceTable(), ramp());
        }));

        PolyInterpolator up(halfband());
        PolyDecimator down(halfband());
        std::printf("  tanh at 2x, polyphase FIR63  %6.2f\n", nsPerSample([&](double* out, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                double a, b;
                up.process(ramp(), a, b);
                out[i] = down.process(std::tanh(3.0 * a), std::tanh(3.0 * b));
            }
        }));

        double carrier = 0.0, modulator = 0.0;
        std::printf("  FM 1x                        %6.2f\n", nsPerSample([&](double* out, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                carrier += dt; if (carrier >= 1.0) carrier -= 1.0;
                modulator += dt * 3.5; if (modulator >= 1.0) modulator -= 1.0;
                out[i] = std::sin(2.0 * kPi * carrier + 5.0 * std::sin(2.0 * kPi * modulator));
            }
        }));
        PolyDecimator fmDown(halfband());
        std::printf("  FM 2x + polyphase decimator  %6.2f\n", nsPerSample([&](double* out, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                double pair[2];
                for (auto& v : pair)
                {
                    carrier += dt * 0.5; if (carrier >= 1.0) carrier -= 1.0;
                    modulator += dt * 1.75; if (modulator >= 1.0) modulator -= 1.0;
                    v = std::sin(2.0 * kPi * carrier + 5.0 * std::sin(2.0 * kPi * modulator));
                }
                out[i] = fmDown.process(pair[0], pair[1]);
            }
        }));

        PolyDecimator midDown(halfband23());
        std::printf("  FM 2x + 23-tap decimator     %6.2f\n", nsPerSample([&](double* out, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                double pair[2];
                for (auto& v : pair)
                {
                    carrier += dt * 0.5; if (carrier >= 1.0) carrier -= 1.0;
                    modulator += dt * 1.75; if (modulator >= 1.0) modulator -= 1.0;
                    v = std::sin(2.0 * kPi * carrier + 5.0 * std::sin(2.0 * kPi * modulator));
                }
                out[i] = midDown.process(pair[0], pair[1]);
            }
        }));
        PolyDecimator shortDown(halfband15());
        std::printf("  FM 2x + 15-tap decimator     %6.2f\n", nsPerSample([&](double* out, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                double pair[2];
                for (auto& v : pair)
                {
                    carrier += dt * 0.5; if (carrier >= 1.0) carrier -= 1.0;
                    modulator += dt * 1.75; if (modulator >= 1.0) modulator -= 1.0;
                    v = std::sin(2.0 * kPi * carrier + 5.0 * std::sin(2.0 * kPi * modulator));
                }
                out[i] = shortDown.process(pair[0], pair[1]);
            }
        }));

        SplitMix32 rng { 99u };
        Lcg lcg;
        PinkKellet kellet;
        PinkKelletMapped mapped(48000.0);
        PinkFitted fitted(48000.0);
        PinkDesigned designed(48000.0, 1.0);
        std::printf("  white LCG (shipped)          %6.2f\n", nsPerSample([&](double* out, int n) { for (int i = 0; i < n; ++i) out[i] = lcg.white(); }));
        std::printf("  white SplitMix32             %6.2f\n", nsPerSample([&](double* out, int n) { for (int i = 0; i < n; ++i) out[i] = rng.white(); }));
        std::printf("  pink Kellet                  %6.2f\n", nsPerSample([&](double* out, int n) { for (int i = 0; i < n; ++i) out[i] = kellet.process(rng.white()); }));
        std::printf("  pink Kellet mapped           %6.2f\n", nsPerSample([&](double* out, int n) { for (int i = 0; i < n; ++i) out[i] = mapped.process(rng.white()); }));
        std::printf("  pink fitted                  %6.2f\n", nsPerSample([&](double* out, int n) { for (int i = 0; i < n; ++i) out[i] = fitted.process(rng.white()); }));
        std::printf("  pink designed, 1 oct         %6.2f   (%zu sections)\n", nsPerSample([&](double* out, int n) { for (int i = 0; i < n; ++i) out[i] = designed.process(rng.white()); }),
                    designed.sections.size());
    }

    return 0;
}
