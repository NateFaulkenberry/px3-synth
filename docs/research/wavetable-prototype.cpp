// Wavetable prototype - settles E.1 (sample interpolation order) and E.2
// (whether the mip crossfade is audible) by measurement.
//
// Standalone on purpose: none of this touches the synth, so it must not pay for
// rebuilding it.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static constexpr double kPi = 3.14159265358979323846;
static constexpr double kSampleRate = 48000.0;
static constexpr int kFrameSize = 2048;
static constexpr int kFrameCount = 64;
static constexpr int kMinLevelLength = 256;

// ---------------------------------------------------------------- FFT -------
static void fft(std::vector<std::complex<double>>& a)
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

// ------------------------------------------------------------ wavetable -----
// A mip level: frames band-limited to `harmonics` and stored at reduced length.
// Reducing the LENGTH as well as the bandwidth is what makes the pyramid cost
// about 2x the base table instead of 2x per level.
struct Level
{
    int length { 0 };
    int harmonics { 0 };
    std::vector<float> data;   // frameCount * length
    const float* frame(int f) const { return data.data() + static_cast<size_t>(f) * length; }
};

struct Wavetable
{
    std::vector<Level> levels;
};

// Frame f is a saw-like spectrum whose tilt evolves across the table, built
// additively so its harmonic content is EXACT - any inharmonic energy measured
// later is therefore the oscillator's, not the table's.
static double harmonicAmplitude(int frame, int h)
{
    const double t = static_cast<double>(frame) / (kFrameCount - 1);
    const double tilt = 1.0 + 2.0 * t;                 // brighter across the table
    return std::pow(1.0 / h, tilt) * (h % 2 == 0 ? (0.3 + 0.7 * t) : 1.0);
}

static Wavetable buildTable(int levelCount)
{
    Wavetable wt;
    for (int l = 0; l < levelCount; ++l)
    {
        Level lev;
        // Bandwidth halves per level, but the LENGTH stops shrinking at
        // kMinLevelLength.
        //
        // Shrinking all the way down looks like free memory and is not: by
        // level 7 the table is 16 samples and by level 8 it is 8, at which point
        // the interpolator is reconstructing the waveform almost entirely by
        // itself and its error dominates everything else. Surge guards the same
        // cliff from the other side, refusing its top mip levels unless the
        // table is still 128 or 64 samples long.
        lev.length = std::max(kMinLevelLength, kFrameSize >> l);
        lev.harmonics = (kFrameSize / 2) >> l;
        lev.data.assign(static_cast<size_t>(kFrameCount) * lev.length, 0.0f);

        for (int f = 0; f < kFrameCount; ++f)
        {
            // Normalised from the FULL-BANDWIDTH spectrum, and the same gain
            // used at every level.
            //
            // Normalising each level to its own energy looks tidier - it keeps
            // the RMS equal across levels - but it is wrong, and measurably so.
            // Dropping the top harmonics then BOOSTS the retained ones to make
            // up the energy, which changes the part of the spectrum the listener
            // can actually hear. Measured, that turned a harmless top-octave
            // truncation into a -24.8 dB difference BELOW 15 kHz at the 3 kHz
            // boundary. With one shared gain the levels differ only where they
            // are supposed to: above the cutoff.
            double norm = 0.0;
            for (int h = 1; h <= kFrameSize / 2; ++h)
            {
                const auto a = harmonicAmplitude(f, h);
                norm += a * a;
            }
            norm = norm > 0.0 ? 1.0 / std::sqrt(2.0 * norm) : 1.0;

            float* dst = lev.data.data() + static_cast<size_t>(f) * lev.length;
            for (int i = 0; i < lev.length; ++i)
            {
                const double phase = 2.0 * kPi * i / lev.length;
                double v = 0.0;
                for (int h = 1; h <= lev.harmonics; ++h)
                {
                    v += harmonicAmplitude(f, h) * std::sin(phase * h);
                }
                dst[i] = static_cast<float>(v * norm);
            }
        }
        wt.levels.push_back(std::move(lev));
    }
    return wt;
}

// -------------------------------------------------------- interpolators -----
enum class Interp { Linear, Hermite, Sinc };

static constexpr int kSincTaps = 8;
static constexpr int kSincPhases = 512;
static std::vector<float> sincTable;   // kSincPhases * kSincTaps

static void buildSincTable()
{
    sincTable.assign(kSincPhases * kSincTaps, 0.0f);
    for (int p = 0; p < kSincPhases; ++p)
    {
        const double frac = static_cast<double>(p) / kSincPhases;
        double sum = 0.0;
        for (int t = 0; t < kSincTaps; ++t)
        {
            const double x = (t - (kSincTaps / 2 - 1)) - frac;
            double s = (std::abs(x) < 1e-9) ? 1.0 : std::sin(kPi * x) / (kPi * x);
            // Blackman window over the tap span, which is what keeps the
            // stopband deep enough to be worth the taps.
            const double w = 0.42 - 0.5 * std::cos(2.0 * kPi * (t + 0.5) / kSincTaps)
                             + 0.08 * std::cos(4.0 * kPi * (t + 0.5) / kSincTaps);
            s *= w;
            sincTable[static_cast<size_t>(p * kSincTaps + t)] = static_cast<float>(s);
            sum += s;
        }
        for (int t = 0; t < kSincTaps; ++t)
        {
            sincTable[static_cast<size_t>(p * kSincTaps + t)] /= static_cast<float>(sum);
        }
    }
}

static inline float readFrame(const float* f, int len, double pos, Interp interp)
{
    const int i0 = static_cast<int>(pos);
    const double frac = pos - i0;
    const int mask = len - 1;

    switch (interp)
    {
        case Interp::Linear:
        {
            const float a = f[i0 & mask];
            const float b = f[(i0 + 1) & mask];
            return a + static_cast<float>(frac) * (b - a);
        }
        case Interp::Hermite:
        {
            const float xm1 = f[(i0 - 1) & mask];
            const float x0 = f[i0 & mask];
            const float x1 = f[(i0 + 1) & mask];
            const float x2 = f[(i0 + 2) & mask];
            const float c = 0.5f * (x1 - xm1);
            const float v = x0 - x1;
            const float w = c + v;
            const float a = w + v + 0.5f * (x2 - x0);
            const float b = w + a;
            const float t = static_cast<float>(frac);
            return ((a * t - b) * t + c) * t + x0;
        }
        case Interp::Sinc:
        {
            const int p = static_cast<int>(frac * kSincPhases) & (kSincPhases - 1);
            const float* k = sincTable.data() + static_cast<size_t>(p * kSincTaps);
            float sum = 0.0f;
            for (int t = 0; t < kSincTaps; ++t)
            {
                sum += k[t] * f[(i0 + t - (kSincTaps / 2 - 1)) & mask];
            }
            return sum;
        }
    }
    return 0.0f;
}

// --------------------------------------------------------- oscillator -------
// Level chosen from the phase INCREMENT, not the note number: the increment
// already folds in sample rate, tuning and any pitch modulation.
static int levelForIncrement(const Wavetable& wt, double increment)
{
    const double maxHarmonic = 0.5 / increment;
    for (size_t l = 0; l < wt.levels.size(); ++l)
    {
        if (wt.levels[l].harmonics <= maxHarmonic) { return static_cast<int>(l); }
    }
    return static_cast<int>(wt.levels.size()) - 1;
}

struct RenderOptions
{
    Interp interp { Interp::Hermite };
    bool crossfadeLevels { false };
    double position { 0.5 };
};

static std::vector<float> render(const Wavetable& wt, double f0, int samples,
                                 const RenderOptions& opt)
{
    std::vector<float> out(static_cast<size_t>(samples), 0.0f);
    const double increment = f0 / kSampleRate;
    double phase = 0.0;

    const double framePos = opt.position * (kFrameCount - 1);
    const int fa = static_cast<int>(framePos);
    const int fb = std::min(fa + 1, kFrameCount - 1);
    const float frameFrac = static_cast<float>(framePos - fa);

    // Continuous level, so the crossfade has something to fade between.
    const double maxHarmonic = 0.5 / increment;
    const double exact = std::log2(std::max(1.0, (kFrameSize / 2.0) / maxHarmonic));
    const int hardLevel = levelForIncrement(wt, increment);

    for (int i = 0; i < samples; ++i)
    {
        auto readAt = [&](int level)
        {
            const auto& L = wt.levels[static_cast<size_t>(
                std::min<int>(level, static_cast<int>(wt.levels.size()) - 1))];
            const double pos = phase * L.length;
            const float a = readFrame(L.frame(fa), L.length, pos, opt.interp);
            const float b = readFrame(L.frame(fb), L.length, pos, opt.interp);
            return a + frameFrac * (b - a);
        };

        float v;
        if (opt.crossfadeLevels)
        {
            const int lo = static_cast<int>(std::floor(exact));
            const float mix = static_cast<float>(exact - lo);
            const float a = readAt(std::max(0, lo));
            const float b = readAt(std::max(0, lo + 1));
            v = a + mix * (b - a);
        }
        else
        {
            v = readAt(hardLevel);
        }

        out[static_cast<size_t>(i)] = v;
        phase += increment;
        if (phase >= 1.0) { phase -= 1.0; }
    }
    return out;
}

// A continuous pitch glide, which is the only way a level boundary actually
// gets crossed. Rendering separate steady tones either side of a boundary - as
// the first version of this did - cannot show a transition artifact, because
// there is no transition in it.
static std::vector<float> renderGlide(const Wavetable& wt, double fromHz, double toHz,
                                      int samples, const RenderOptions& opt)
{
    std::vector<float> out(static_cast<size_t>(samples), 0.0f);
    double phase = 0.0;

    const double framePos = opt.position * (kFrameCount - 1);
    const int fa = static_cast<int>(framePos);
    const int fb = std::min(fa + 1, kFrameCount - 1);
    const float frameFrac = static_cast<float>(framePos - fa);

    for (int i = 0; i < samples; ++i)
    {
        const double t = static_cast<double>(i) / samples;
        const double f0 = fromHz * std::pow(toHz / fromHz, t);
        const double increment = f0 / kSampleRate;

        auto readAt = [&](int level)
        {
            const auto& L = wt.levels[static_cast<size_t>(
                std::min<int>(std::max(0, level), static_cast<int>(wt.levels.size()) - 1))];
            const double pos = phase * L.length;
            const float a = readFrame(L.frame(fa), L.length, pos, opt.interp);
            const float b = readFrame(L.frame(fb), L.length, pos, opt.interp);
            return a + frameFrac * (b - a);
        };

        float v;
        if (opt.crossfadeLevels)
        {
            const double maxHarmonic = 0.5 / increment;
            const double exact = std::log2(std::max(1.0, (kFrameSize / 2.0) / maxHarmonic));
            const int lo = static_cast<int>(std::floor(exact));
            const float mix = static_cast<float>(exact - lo);
            const float a = readAt(lo);
            const float b = readAt(lo + 1);
            v = a + mix * (b - a);
        }
        else
        {
            v = readAt(levelForIncrement(wt, increment));
        }

        out[static_cast<size_t>(i)] = v;
        phase += increment;
        if (phase >= 1.0) { phase -= 1.0; }
    }
    return out;
}

// Largest single-sample step against the local level - the same discontinuity
// measure the synth's own click tests use.
static double worstLocalStep(const std::vector<float>& sig)
{
    constexpr int window = 256;
    double worst = 0.0;
    for (size_t i = window; i < sig.size(); ++i)
    {
        double sum = 0.0;
        for (size_t k = i - window; k < i; ++k)
        {
            sum += static_cast<double>(sig[k]) * sig[k];
        }
        const double rms = std::sqrt(sum / window);
        if (rms < 1e-9) { continue; }
        worst = std::max(worst, std::abs(static_cast<double>(sig[i]) - sig[i - 1]) / rms);
    }
    return worst;
}

// -------------------------------------------------------- measurement -------
struct Spectrum { double harmonic; double inharmonic; double topHarmonic; };

static Spectrum analyse(const std::vector<float>& sig, double f0, int n)
{
    std::vector<std::complex<double>> a(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        // Blackman-Harris, not Hann: Hann's sidelobes sat around -56 dB, which
        // is exactly where the first run's low-note comparison flatlined. A
        // measurement floor is not a result.
        const double t = 2.0 * kPi * i / (n - 1);
        const double w = 0.35875 - 0.48829 * std::cos(t)
                       + 0.14128 * std::cos(2.0 * t) - 0.01168 * std::cos(3.0 * t);
        a[static_cast<size_t>(i)] = { sig[static_cast<size_t>(i)] * w, 0.0 };
    }
    fft(a);

    const double binHz = kSampleRate / n;
    Spectrum s { 0.0, 0.0, 0.0 };
    for (int b = 1; b < n / 2; ++b)
    {
        const double p = std::norm(a[static_cast<size_t>(b)]);
        const double hz = b * binHz;
        const double nearest = std::round(hz / f0);
        if (nearest >= 1.0 && std::abs(hz - nearest * f0) <= 6.0 * binHz) { s.harmonic += p; }
        else { s.inharmonic += p; }
    }
    return s;
}

static double dB(double v) { return 10.0 * std::log10(std::max(v, 1e-30)); }

int main()
{
    buildSincTable();
    const auto wt = buildTable(9);

    std::printf("\nWAVETABLE PROTOTYPE\n");
    std::printf("  %d frames x %d samples, %zu mip levels\n",
                kFrameCount, kFrameSize, wt.levels.size());
    size_t bytes = 0;
    for (const auto& l : wt.levels) { bytes += l.data.size() * sizeof(float); }
    std::printf("  pyramid: %.2f MB (base level alone: %.2f MB)\n\n",
                bytes / 1048576.0, wt.levels[0].data.size() * sizeof(float) / 1048576.0);

    // ---- E.1 sample interpolation order -----------------------------------
    std::printf("E.1  SAMPLE INTERPOLATION - alias rejection (tone:inharmonic, dB, higher is cleaner)\n\n");
    struct Note { const char* name; double hz; };
    const Note notes[] = { { "C2", 65.41 }, { "C3", 130.81 }, { "C4", 261.63 },
                           { "C5", 523.25 }, { "C6", 1046.50 }, { "C7", 2093.00 } };

    std::printf("  %-10s", "interp");
    for (const auto& n : notes) { std::printf("%10s", n.name); }
    std::printf("%12s\n", "ns/sample");

    const int n = 1 << 15;
    for (const auto interp : { Interp::Linear, Interp::Hermite, Interp::Sinc })
    {
        const char* label = interp == Interp::Linear ? "linear"
                          : interp == Interp::Hermite ? "hermite" : "sinc8";
        std::printf("  %-10s", label);

        for (const auto& note : notes)
        {
            RenderOptions o; o.interp = interp; o.position = 0.5;
            const auto sig = render(wt, note.hz, n, o);
            const auto s = analyse(sig, note.hz, n);
            std::printf("%10.2f", dB(s.harmonic) - dB(s.inharmonic));
        }

        // Cost, measured on the same work the oscillator actually does.
        RenderOptions o; o.interp = interp;
        const auto t0 = std::chrono::high_resolution_clock::now();
        volatile float sink = 0.0f;
        for (int r = 0; r < 20; ++r)
        {
            const auto sig = render(wt, 261.63, 8192, o);
            sink = sink + sig[100];
        }
        const auto t1 = std::chrono::high_resolution_clock::now();
        const auto ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / (20.0 * 8192.0);
        std::printf("%12.2f\n", ns);
    }

    // ---- E.1b high-frequency droop ----------------------------------------
    // The other half of the interpolation trade: a cheap interpolator is a
    // lowpass. Measured across the AUDIBLE band - the first version quoted the
    // droop at harmonic 90, which sits at 23.5 kHz where every interpolator
    // rolls off and nobody can hear the difference.
    std::printf("\nE.1b HIGH-FREQUENCY DROOP at C4 (dB, 0 = no loss)\n\n");
    const double probeHz[] = { 2000.0, 5000.0, 10000.0, 15000.0, 20000.0 };
    std::printf("  %-10s", "interp");
    for (const auto hz : probeHz) { std::printf("%11.0f", hz); }
    std::printf("\n");

    for (const auto interp : { Interp::Linear, Interp::Hermite, Interp::Sinc })
    {
        const char* label = interp == Interp::Linear ? "linear"
                          : interp == Interp::Hermite ? "hermite" : "sinc8";
        RenderOptions o; o.interp = interp; o.position = 0.5;
        const double f0 = 261.63;
        const auto sig = render(wt, f0, n, o);

        std::vector<std::complex<double>> a(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            const double t = 2.0 * kPi * i / (n - 1);
            const double w = 0.35875 - 0.48829 * std::cos(t)
                           + 0.14128 * std::cos(2.0 * t) - 0.01168 * std::cos(3.0 * t);
            a[static_cast<size_t>(i)] = { sig[static_cast<size_t>(i)] * w, 0.0 };
        }
        fft(a);

        const double binHz = kSampleRate / n;
        const int frame = static_cast<int>(0.5 * (kFrameCount - 1));
        auto measured = [&](int h)
        {
            const int b = static_cast<int>(std::round(h * f0 / binHz));
            double peak = 0.0;
            for (int k = b - 3; k <= b + 3; ++k)
            {
                peak = std::max(peak, std::abs(a[static_cast<size_t>(k)]));
            }
            return peak;
        };
        const double refRatio = measured(1) / harmonicAmplitude(frame, 1);

        std::printf("  %-10s", label);
        for (const auto hz : probeHz)
        {
            const int h = static_cast<int>(std::round(hz / f0));
            std::printf("%11.2f", 20.0 * std::log10(
                (measured(h) / harmonicAmplitude(frame, h)) / refRatio));
        }
        std::printf("\n");
    }

    // ---- E.2 is the mip crossfade audible? --------------------------------
    // Asked directly: at the pitch where the level changes, how different do the
    // two levels actually sound? If they are the same, switching between them
    // cannot be heard and the crossfade is paying for nothing.
    //
    // The glide-based version of this test could not answer it - its worst-step
    // figure was identical for both modes because it was measuring the
    // waveform's own steep edge, not the transition.
    std::printf("\nE.2  MIP LEVEL TRANSITION - how different are the two levels at the boundary?\n\n");
    std::printf("  A total difference figure cannot answer this on its own: if the whole\n");
    std::printf("  difference sits above 15 kHz then the switch is inaudible however large\n");
    std::printf("  it reads. Split accordingly.\n\n");
    std::printf("  %-12s %10s %13s %13s %13s\n",
                "boundary", "pitch Hz", "gap dB", "diff <15k dB", "diff >15k dB");

    for (size_t l = 0; l + 1 < wt.levels.size(); ++l)
    {
        // The pitch at which selection switches from level l to level l+1.
        const double boundaryHz = 0.5 * kSampleRate / wt.levels[l].harmonics;
        if (boundaryHz < 40.0 || boundaryHz > 12000.0) { continue; }

        RenderOptions o; o.interp = Interp::Hermite; o.position = 0.5;

        // Force each level in turn at exactly that pitch.
        auto renderAtLevel = [&](int level)
        {
            std::vector<float> out(static_cast<size_t>(n), 0.0f);
            const double inc = boundaryHz / kSampleRate;
            double phase = 0.0;
            const double framePos = o.position * (kFrameCount - 1);
            const int fa = static_cast<int>(framePos);
            const int fb = std::min(fa + 1, kFrameCount - 1);
            const float ff = static_cast<float>(framePos - fa);
            const auto& L = wt.levels[static_cast<size_t>(level)];
            for (int i = 0; i < n; ++i)
            {
                const double pos = phase * L.length;
                const float a = readFrame(L.frame(fa), L.length, pos, o.interp);
                const float b = readFrame(L.frame(fb), L.length, pos, o.interp);
                out[static_cast<size_t>(i)] = a + ff * (b - a);
                phase += inc;
                if (phase >= 1.0) { phase -= 1.0; }
            }
            return out;
        };

        const auto lo = renderAtLevel(static_cast<int>(l));
        const auto hi = renderAtLevel(static_cast<int>(l + 1));

        double eLo = 0.0, eHi = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double a = lo[static_cast<size_t>(i)];
            const double b = hi[static_cast<size_t>(i)];
            eLo += a * a; eHi += b * b;
        }
        const double levelGap = 20.0 * std::log10(std::sqrt(eLo) / std::sqrt(eHi));

        // The difference signal, split at 15 kHz - above that, a change in
        // content is not something a listener can report.
        std::vector<std::complex<double>> d(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            const double t = 2.0 * kPi * i / (n - 1);
            const double w = 0.35875 - 0.48829 * std::cos(t)
                           + 0.14128 * std::cos(2.0 * t) - 0.01168 * std::cos(3.0 * t);
            d[static_cast<size_t>(i)] = { (lo[static_cast<size_t>(i)] - hi[static_cast<size_t>(i)]) * w, 0.0 };
        }
        fft(d);

        std::vector<std::complex<double>> ref(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            const double t = 2.0 * kPi * i / (n - 1);
            const double w = 0.35875 - 0.48829 * std::cos(t)
                           + 0.14128 * std::cos(2.0 * t) - 0.01168 * std::cos(3.0 * t);
            ref[static_cast<size_t>(i)] = { lo[static_cast<size_t>(i)] * w, 0.0 };
        }
        fft(ref);

        const double binHz = kSampleRate / n;
        double below = 0.0, above = 0.0, refTotal = 0.0;
        for (int b = 1; b < n / 2; ++b)
        {
            const double p = std::norm(d[static_cast<size_t>(b)]);
            if (b * binHz < 15000.0) { below += p; } else { above += p; }
            refTotal += std::norm(ref[static_cast<size_t>(b)]);
        }

        std::printf("  %-12s %10.1f %13.4f %13.2f %13.2f\n",
                    (std::to_string(l) + " -> " + std::to_string(l + 1)).c_str(),
                    boundaryHz, levelGap,
                    10.0 * std::log10(std::max(below / refTotal, 1e-30)),
                    10.0 * std::log10(std::max(above / refTotal, 1e-30)));
    }

    std::printf("\n  Cost of the crossfade (it reads two levels instead of one):\n");
    for (const bool crossfade : { false, true })
    {
        RenderOptions o; o.interp = Interp::Hermite; o.crossfadeLevels = crossfade;
        const auto t0 = std::chrono::high_resolution_clock::now();
        volatile float sink = 0.0f;
        for (int r = 0; r < 20; ++r)
        {
            const auto g = renderGlide(wt, 200.0, 6400.0, 16384, o);
            sink = sink + g[100];
        }
        const auto t1 = std::chrono::high_resolution_clock::now();
        std::printf("    %-14s %8.2f ns/sample\n", crossfade ? "crossfade" : "hard switch",
                    std::chrono::duration<double, std::nano>(t1 - t0).count() / (20.0 * 16384.0));
    }

    std::printf("\n");
    return 0;
}
