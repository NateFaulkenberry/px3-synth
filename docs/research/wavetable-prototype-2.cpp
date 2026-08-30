// Wavetable prototype 2 - settles F.3 (frame count) and F.4 (phase alignment).
//
// F.4 is the one that decides whether imported audio sounds any good. Frames cut
// from a recording arrive at arbitrary phase; interpolating between two frames
// whose harmonics disagree in phase is a comb filter, and the morph thins out or
// hollows instead of evolving. This measures that directly.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

static constexpr double kPi = 3.14159265358979323846;
static constexpr int kFrameSize = 2048;
static constexpr int kHarmonics = 64;

// A frame as a spectrum: amplitude and phase per harmonic. Building tables in
// the frequency domain is what makes alignment expressible at all - a time
// shift of d samples is just a phase slope, -2*pi*h*d/N.
struct Spec { std::vector<double> amp, phase; };

static std::vector<float> synth(const Spec& s)
{
    std::vector<float> out(static_cast<size_t>(kFrameSize), 0.0f);
    for (int i = 0; i < kFrameSize; ++i)
    {
        double v = 0.0;
        for (int h = 1; h <= kHarmonics; ++h)
        {
            v += s.amp[static_cast<size_t>(h)]
                 * std::sin(2.0 * kPi * h * i / kFrameSize + s.phase[static_cast<size_t>(h)]);
        }
        out[static_cast<size_t>(i)] = static_cast<float>(v);
    }
    return out;
}

static double rms(const std::vector<float>& v)
{
    double e = 0.0;
    for (const auto x : v) { e += static_cast<double>(x) * x; }
    return std::sqrt(e / v.size());
}

// ------------------------------------------------------------- alignment ----
enum class Align { None, ZeroPhase, CrossCorrelate, DiscardPhase };

// Rotating a frame in time by d samples: harmonic h gains -2*pi*h*d/N.
static Spec rotate(const Spec& s, double d)
{
    Spec r = s;
    for (int h = 1; h <= kHarmonics; ++h)
    {
        r.phase[static_cast<size_t>(h)] =
            s.phase[static_cast<size_t>(h)] - 2.0 * kPi * h * d / kFrameSize;
    }
    return r;
}

// Correlation between two spectra under a time shift d, in closed form: no need
// to synthesise and slide waveforms.
static double correlationAt(const Spec& a, const Spec& b, double d)
{
    double sum = 0.0;
    for (int h = 1; h <= kHarmonics; ++h)
    {
        const double dp = a.phase[static_cast<size_t>(h)]
                          - (b.phase[static_cast<size_t>(h)] - 2.0 * kPi * h * d / kFrameSize);
        sum += a.amp[static_cast<size_t>(h)] * b.amp[static_cast<size_t>(h)] * std::cos(dp);
    }
    return sum;
}

static std::vector<Spec> alignFrames(const std::vector<Spec>& in, Align mode)
{
    if (mode == Align::None) { return in; }

    std::vector<Spec> out;
    out.reserve(in.size());

    for (size_t f = 0; f < in.size(); ++f)
    {
        if (mode == Align::DiscardPhase)
        {
            // Keep the amplitude spectrum and throw the phases away entirely.
            // Every frame then has identical phase structure, so nothing can
            // cancel at any intermediate position - the morph is perfect by
            // construction. What it costs is the waveform's SHAPE: the frame no
            // longer looks like the audio it came from, and any asymmetry or
            // phase character in the source is gone.
            Spec z = in[f];
            for (int h = 1; h <= kHarmonics; ++h) { z.phase[static_cast<size_t>(h)] = 0.0; }
            out.push_back(std::move(z));
        }
        else if (mode == Align::ZeroPhase)
        {
            // Shift so the FUNDAMENTAL sits at zero phase. Predictable and
            // frame-independent, but it only pins harmonic 1 - the rest land
            // wherever they land.
            const double d = in[f].phase[1] * kFrameSize / (2.0 * kPi);
            out.push_back(rotate(in[f], d));
        }
        else
        {
            // Shift to maximise correlation with the frame BEFORE it, so
            // consecutive frames line up as a whole rather than at one harmonic.
            if (f == 0) { out.push_back(in[f]); continue; }
            double best = -1e30, bestD = 0.0;
            for (int step = 0; step < 2048; ++step)
            {
                const double d = static_cast<double>(step) * kFrameSize / 2048.0;
                const double c = correlationAt(out[f - 1], in[f], d);
                if (c > best) { best = c; bestD = d; }
            }
            out.push_back(rotate(in[f], bestD));
        }
    }
    return out;
}

// ------------------------------------------------------------------ tables --
// Amplitudes evolve across the table; phases are the variable under test.
static std::vector<Spec> makeFrames(int frameCount, bool randomPhase, unsigned seed)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> uni(0.0, 2.0 * kPi);

    std::vector<Spec> frames;
    for (int f = 0; f < frameCount; ++f)
    {
        Spec s;
        s.amp.assign(kHarmonics + 1, 0.0);
        s.phase.assign(kHarmonics + 1, 0.0);
        const double t = frameCount > 1 ? static_cast<double>(f) / (frameCount - 1) : 0.0;
        const double tilt = 1.0 + 2.0 * (1.0 - t);
        double norm = 0.0;
        for (int h = 1; h <= kHarmonics; ++h)
        {
            s.amp[static_cast<size_t>(h)] = std::pow(1.0 / h, tilt);
            norm += s.amp[static_cast<size_t>(h)] * s.amp[static_cast<size_t>(h)];
            // Imported cycles arrive at arbitrary phase. A table generated
            // additively does not, which is why the generated case is the easy
            // one and the imported case is the one that has to be measured.
            s.phase[static_cast<size_t>(h)] = randomPhase ? uni(rng) : 0.0;
        }
        norm = 1.0 / std::sqrt(2.0 * norm);
        for (int h = 1; h <= kHarmonics; ++h) { s.amp[static_cast<size_t>(h)] *= norm; }
        frames.push_back(std::move(s));
    }
    return frames;
}

// How much energy the morph loses halfway between two frames.
//
// Interpolating between two frames that agree in phase preserves the harmonics;
// between two that disagree, each harmonic partially cancels and the sound
// hollows out. Reported as the worst dip, in dB, against the level the two
// endpoints imply.
static double worstMorphDip(const std::vector<Spec>& frames)
{
    double worst = 0.0;
    for (size_t f = 0; f + 1 < frames.size(); ++f)
    {
        const auto a = synth(frames[f]);
        const auto b = synth(frames[f + 1]);
        const double ra = rms(a), rb = rms(b);

        for (const double mix : { 0.25, 0.5, 0.75 })
        {
            std::vector<float> m(static_cast<size_t>(kFrameSize));
            for (int i = 0; i < kFrameSize; ++i)
            {
                m[static_cast<size_t>(i)] = static_cast<float>(
                    a[static_cast<size_t>(i)] * (1.0 - mix) + b[static_cast<size_t>(i)] * mix);
            }
            const double expected = ra * (1.0 - mix) + rb * mix;
            const double dip = 20.0 * std::log10(rms(m) / expected);
            worst = std::min(worst, dip);
        }
    }
    return worst;
}

int main()
{
    std::printf("\nWAVETABLE PROTOTYPE 2\n\n");

    // ---- F.4 phase alignment ----------------------------------------------
    std::printf("F.4  PHASE ALIGNMENT - energy lost halfway through the morph\n\n");
    std::printf("  A frame table is scanned by crossfading between frames. If two\n");
    std::printf("  frames disagree in phase their harmonics partially cancel at every\n");
    std::printf("  intermediate position, and the sound hollows out mid-scan instead of\n");
    std::printf("  evolving. Worst dip across all adjacent pairs, in dB (0 = no loss).\n\n");

    std::printf("  %-26s %16s %16s\n", "alignment", "generated", "imported");
    for (const auto mode : { Align::None, Align::ZeroPhase, Align::CrossCorrelate,
                             Align::DiscardPhase })
    {
        const char* label = mode == Align::None ? "none"
                          : mode == Align::ZeroPhase ? "zero-phase fundamental"
                          : mode == Align::CrossCorrelate ? "cross-correlate"
                          : "discard phase";
        const auto gen = alignFrames(makeFrames(64, false, 1), mode);
        const auto imp = alignFrames(makeFrames(64, true, 1), mode);
        std::printf("  %-26s %13.2f dB %13.2f dB\n", label, worstMorphDip(gen), worstMorphDip(imp));
    }

    // What discarding phase costs, since the dip figure alone would make it look
    // free: how far the frame's SHAPE moves from the audio it came from, after
    // best-case time alignment. Same spectrum, different waveform.
    {
        std::printf("\n  Cost of discarding phase - shape error against the source frame\n");
        std::printf("  (spectrum is identical by construction; this is the waveform):\n\n");
        const auto source = makeFrames(8, true, 7);
        double worst = 0.0;
        for (size_t f = 0; f < source.size(); ++f)
        {
            Spec z = source[f];
            for (int h = 1; h <= kHarmonics; ++h) { z.phase[static_cast<size_t>(h)] = 0.0; }
            const auto a = synth(source[f]);
            // Best case over every time shift, so the figure is shape error and
            // not merely a shift.
            double best = 1e30;
            for (int step = 0; step < 512; ++step)
            {
                const auto b = synth(rotate(z, static_cast<double>(step) * kFrameSize / 512.0));
                double e = 0.0;
                for (int i = 0; i < kFrameSize; ++i)
                {
                    const double d = a[static_cast<size_t>(i)] - b[static_cast<size_t>(i)];
                    e += d * d;
                }
                best = std::min(best, std::sqrt(e / kFrameSize));
            }
            worst = std::max(worst, best / rms(a));
        }
        std::printf("    worst shape error: %.1f%% of the frame's own level\n", worst * 100.0);
    }

    // ---- F.3 frame count ---------------------------------------------------
    std::printf("\nF.3  FRAME COUNT - does the morph get smoother with more frames?\n\n");
    std::printf("  With linear frame interpolation the spectrum is piecewise linear in\n");
    std::printf("  position: continuous, but with a kink at every frame. 'kink' is the\n");
    std::printf("  worst second difference of a harmonic's amplitude along the scan,\n");
    std::printf("  relative to its mean - it is what a slow sweep would ratchet on.\n\n");
    std::printf("  %-10s %14s %16s %14s\n", "frames", "worst kink", "memory/table", "worst dip");

    for (const int frameCount : { 16, 32, 64, 128, 256 })
    {
        const auto frames = alignFrames(makeFrames(frameCount, false, 1), Align::ZeroPhase);

        // Track one harmonic's amplitude along a fine scan of the position.
        constexpr int steps = 2048;
        constexpr int probeHarmonic = 8;
        std::vector<double> trace(static_cast<size_t>(steps), 0.0);
        for (int s = 0; s < steps; ++s)
        {
            const double pos = static_cast<double>(s) / (steps - 1) * (frameCount - 1);
            const int fa = std::min(static_cast<int>(pos), frameCount - 1);
            const int fb = std::min(fa + 1, frameCount - 1);
            const double frac = pos - fa;
            trace[static_cast<size_t>(s)] =
                frames[static_cast<size_t>(fa)].amp[probeHarmonic] * (1.0 - frac)
                + frames[static_cast<size_t>(fb)].amp[probeHarmonic] * frac;
        }

        double mean = 0.0;
        for (const auto v : trace) { mean += v; }
        mean /= steps;

        double worstKink = 0.0;
        for (int s = 1; s + 1 < steps; ++s)
        {
            const double d2 = trace[static_cast<size_t>(s + 1)]
                              - 2.0 * trace[static_cast<size_t>(s)]
                              + trace[static_cast<size_t>(s - 1)];
            worstKink = std::max(worstKink, std::abs(d2) / mean);
        }

        // 1.25 MB measured for 64 frames in prototype 1; memory is linear here.
        const double mb = 1.25 * frameCount / 64.0;
        std::printf("  %-10d %14.6f %13.2f MB %11.2f dB\n",
                    frameCount, worstKink, mb, worstMorphDip(frames));
    }

    std::printf("\n");
    return 0;
}
