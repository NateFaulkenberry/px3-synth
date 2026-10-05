#pragma once

// Objective reverb measurements, shared by PX3Tests (the pinned thresholds) and
// PX3Diag reverb-renders (the listening set and the preset screen), so the two
// can never disagree about what a number means.
//
// Everything here takes an impulse response or a rendered signal as plain
// vectors at a given sample rate and is independent of the Reverb class.
//
//  - echoDensity: Abel & Huang (AES 2006), Hann-weighted. Fraction of samples
//    beyond one standard deviation, normalised by the Gaussian value 0.3173.
//    1.0 = as diffuse as noise.
//  - mixingTimeMs: first time the echo density reaches 0.9 and stays there.
//  - bandRt60: Schroeder backward integration (T20, -5..-25 dB) per octave,
//    after a 4th-order Butterworth band-pass.
//  - ringingDb: the worst narrowband peak in the decay-compensated late tail,
//    against a 1/3-octave running median, 200 Hz - 8 kHz. Reported next to the
//    same measure on a decaying Gaussian noise of the same RT60, because a
//    finite-length Welch estimate of pure noise is never flat.
//  - decayNonlinearityDb: ISO 3382 linear-regression residual of the EDC.
//  - correlation: inter-channel correlation, broadband or per band.

#include <JuceHeader.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace px3tests::reverbmeasure
{
inline int samplesFor(double ms, double sr) { return static_cast<int>(ms * 0.001 * sr); }

inline double echoDensity(const std::vector<float>& h, int centre, int window)
{
    const auto first = std::max(0, centre - window / 2);
    const auto last = std::min(static_cast<int>(h.size()), centre + window / 2);
    const auto n = last - first;
    if (n < 32) return 0.0;
    double wsum = 0.0, energy = 0.0;
    std::vector<double> w(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        w[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(juce::MathConstants<double>::twoPi * (i + 0.5) / n);
        wsum += w[static_cast<std::size_t>(i)];
    }
    for (int i = 0; i < n; ++i)
    {
        const auto v = static_cast<double>(h[static_cast<std::size_t>(first + i)]);
        energy += w[static_cast<std::size_t>(i)] * v * v;
    }
    const auto sigma = std::sqrt(energy / wsum);
    if (sigma < 1.0e-15) return 0.0;
    double beyond = 0.0;
    for (int i = 0; i < n; ++i)
        if (std::abs(static_cast<double>(h[static_cast<std::size_t>(first + i)])) > sigma) beyond += w[static_cast<std::size_t>(i)];
    return beyond / wsum / 0.3173105;
}

inline double mixingTimeMs(const std::vector<float>& h, double sr, double threshold = 0.9, double untilMs = 800.0)
{
    const auto win = samplesFor(20.0, sr);
    for (double t = 2.0; t < untilMs; t += 1.0)
    {
        if (echoDensity(h, samplesFor(t, sr), win) < threshold) continue;
        bool holds = true;
        for (const auto k : { 10.0, 20.0, 30.0 })
            holds = holds && echoDensity(h, samplesFor(t + k, sr), win) >= threshold * 0.95;
        if (holds) return t;
    }
    return untilMs;
}

// Biquad band-pass (RBJ, constant 0 dB peak), run twice for a 4th-order band.
inline std::vector<float> bandpass(const std::vector<float>& x, double sr, double centre, double q = 1.414)
{
    const auto w = juce::MathConstants<double>::twoPi * centre / sr;
    const auto alpha = std::sin(w) / (2.0 * q);
    const auto a0 = 1.0 + alpha;
    const double b0 = alpha / a0, b2 = -alpha / a0, a1 = -2.0 * std::cos(w) / a0, a2 = (1.0 - alpha) / a0;
    std::vector<float> y(x.size());
    for (int pass = 0; pass < 2; ++pass)
    {
        const auto& in = pass == 0 ? x : y;
        double z1 = 0.0, z2 = 0.0;
        std::vector<float> out(x.size());
        for (std::size_t n = 0; n < x.size(); ++n)
        {
            const auto v = static_cast<double>(in[n]);
            const auto o = b0 * v + z1;
            z1 = -a1 * o + z2;
            z2 = b2 * v - a2 * o;
            out[n] = static_cast<float>(o);
        }
        y = std::move(out);
    }
    return y;
}

inline std::vector<double> schroederDb(const std::vector<float>& x)
{
    std::vector<double> edc(x.size());
    double acc = 0.0;
    for (std::size_t i = x.size(); i-- > 0;)
    {
        acc += static_cast<double>(x[i]) * x[i];
        edc[i] = acc;
    }
    const auto total = edc.empty() ? 1.0 : std::max(1.0e-30, edc[0]);
    for (auto& v : edc) v = 10.0 * std::log10(v / total + 1.0e-30);
    return edc;
}

// T60 extrapolated from the fit over [topDb, bottomDb] of the EDC.
inline double rt60(const std::vector<float>& x, double sr, double topDb = -5.0, double bottomDb = -25.0)
{
    const auto edc = schroederDb(x);
    std::size_t i0 = 0, i1 = 0;
    while (i0 < edc.size() && edc[i0] > topDb) ++i0;
    i1 = i0;
    while (i1 < edc.size() && edc[i1] > bottomDb) ++i1;
    if (i1 >= edc.size() || i1 <= i0 + 8) return 0.0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    const auto n = static_cast<double>(i1 - i0);
    for (auto i = i0; i < i1; ++i)
    {
        const auto t = static_cast<double>(i) / sr;
        sx += t; sy += edc[i]; sxx += t * t; sxy += t * edc[i];
    }
    const auto slope = (n * sxy - sx * sy) / std::max(1.0e-30, n * sxx - sx * sx);
    return slope < 0.0 ? -60.0 / slope : 0.0;
}

inline double bandRt60(const std::vector<float>& x, double sr, double centre)
{
    return rt60(bandpass(x, sr, centre), sr);
}

inline double decayNonlinearityDb(const std::vector<float>& x)
{
    const auto edc = schroederDb(x);
    std::size_t i0 = 0;
    while (i0 < edc.size() && edc[i0] > -5.0) ++i0;
    auto i1 = i0;
    while (i1 < edc.size() && edc[i1] > -35.0) ++i1;
    if (i1 >= edc.size() || i1 <= i0 + 8) return 0.0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    const auto n = static_cast<double>(i1 - i0);
    for (auto i = i0; i < i1; ++i)
    {
        const auto t = static_cast<double>(i);
        sx += t; sy += edc[i]; sxx += t * t; sxy += t * edc[i];
    }
    const auto slope = (n * sxy - sx * sy) / std::max(1.0e-30, n * sxx - sx * sx);
    const auto offset = (sy - slope * sx) / n;
    double worst = 0.0;
    for (auto i = i0; i < i1; ++i) worst = std::max(worst, std::abs(edc[i] - (offset + slope * static_cast<double>(i))));
    return worst;
}

inline double correlation(const std::vector<float>& a, const std::vector<float>& b, std::size_t from, std::size_t to)
{
    double ab = 0, aa = 0, bb = 0;
    to = std::min({ to, a.size(), b.size() });
    for (auto i = from; i < to; ++i)
    {
        ab += static_cast<double>(a[i]) * b[i];
        aa += static_cast<double>(a[i]) * a[i];
        bb += static_cast<double>(b[i]) * b[i];
    }
    return ab / std::sqrt(std::max(1.0e-30, aa * bb));
}

inline double energy(const std::vector<float>& x, std::size_t from = 0, std::size_t to = static_cast<std::size_t>(-1))
{
    double e = 0.0;
    to = std::min(to, x.size());
    for (auto i = from; i < to; ++i) e += static_cast<double>(x[i]) * x[i];
    return e;
}

// Welch power spectrum, Hann, 50 % overlap.
inline std::vector<double> welch(const std::vector<float>& x, int order = 13)
{
    const auto n = 1 << order;
    juce::dsp::FFT fft(order);
    std::vector<double> acc(static_cast<std::size_t>(n / 2 + 1), 0.0);
    std::vector<float> frame(static_cast<std::size_t>(2 * n));
    int frames = 0;
    for (std::size_t start = 0; start + static_cast<std::size_t>(n) <= x.size(); start += static_cast<std::size_t>(n / 2))
    {
        std::fill(frame.begin(), frame.end(), 0.0f);
        for (int i = 0; i < n; ++i)
            frame[static_cast<std::size_t>(i)] = x[start + static_cast<std::size_t>(i)]
                * static_cast<float>(0.5 - 0.5 * std::cos(juce::MathConstants<double>::twoPi * i / n));
        fft.performFrequencyOnlyForwardTransform(frame.data());
        for (int k = 0; k <= n / 2; ++k)
            acc[static_cast<std::size_t>(k)] += static_cast<double>(frame[static_cast<std::size_t>(k)]) * frame[static_cast<std::size_t>(k)];
        ++frames;
    }
    for (auto& v : acc) v /= std::max(1, frames);
    return acc;
}

struct Ringing { double worstDb { 0.0 }; double worstHz { 0.0 }; };

// Worst spectral peak of the late tail [startMs, startMs + durMs], decay-
// compensated with the measured broadband RT60, against a 1/3-octave median.
inline Ringing ringing(const std::vector<float>& h, double sr, double startMs, double durMs, double rtSeconds, double hiHz = 8000.0)
{
    const auto a = static_cast<std::size_t>(samplesFor(startMs, sr));
    const auto b = std::min(h.size(), static_cast<std::size_t>(samplesFor(startMs + durMs, sr)));
    if (b <= a + 9000) return {};
    std::vector<float> seg(h.begin() + static_cast<std::ptrdiff_t>(a), h.begin() + static_cast<std::ptrdiff_t>(b));
    const auto rt = std::max(0.05, rtSeconds);
    for (std::size_t i = 0; i < seg.size(); ++i)
        seg[i] *= static_cast<float>(std::pow(10.0, 3.0 * static_cast<double>(i) / sr / rt));
    const auto p = welch(seg);
    const auto n = static_cast<double>((p.size() - 1) * 2);
    std::vector<double> db(p.size());
    for (std::size_t k = 0; k < p.size(); ++k) db[k] = 10.0 * std::log10(p[k] + 1.0e-30);
    Ringing r;
    r.worstDb = -1.0e9;
    std::vector<double> local;
    for (std::size_t k = 1; k < p.size(); ++k)
    {
        const auto hz = static_cast<double>(k) * sr / n;
        if (hz < 200.0 || hz > hiHz) continue;
        const auto lo = static_cast<std::size_t>(std::floor(hz / std::pow(2.0, 1.0 / 6.0) * n / sr));
        const auto hi = std::min(p.size() - 1, static_cast<std::size_t>(std::ceil(hz * std::pow(2.0, 1.0 / 6.0) * n / sr)));
        local.assign(db.begin() + static_cast<std::ptrdiff_t>(lo), db.begin() + static_cast<std::ptrdiff_t>(hi + 1));
        std::nth_element(local.begin(), local.begin() + static_cast<std::ptrdiff_t>(local.size() / 2), local.end());
        const auto prominence = db[k] - local[local.size() / 2];
        if (prominence > r.worstDb) { r.worstDb = prominence; r.worstHz = hz; }
    }
    return r;
}

// The same measure on exponentially decaying Gaussian noise with the same RT,
// worst of three seeds: what "no ringing at all" reads as for this window.
inline double ringingNoiseReferenceDb(double sr, double startMs, double durMs, double rtSeconds, double hiHz = 8000.0)
{
    double worst = 0.0;
    for (int seed = 1; seed <= 3; ++seed)
    {
        juce::Random random(seed);
        const auto length = static_cast<std::size_t>(samplesFor(startMs + durMs + 50.0, sr));
        std::vector<float> noise(length);
        for (std::size_t i = 0; i < length; ++i)
        {
            // Box-Muller
            const auto u1 = std::max(1.0e-12, static_cast<double>(random.nextFloat()));
            const auto u2 = static_cast<double>(random.nextFloat());
            const auto g = std::sqrt(-2.0 * std::log(u1)) * std::cos(juce::MathConstants<double>::twoPi * u2);
            noise[i] = static_cast<float>(g * std::pow(10.0, -3.0 * static_cast<double>(i) / sr / std::max(0.05, rtSeconds)));
        }
        worst = std::max(worst, ringing(noise, sr, startMs, durMs, rtSeconds, hiHz).worstDb);
    }
    return worst;
}

// Crest factor (peak / rms) of consecutive frames: noise reads ~3, isolated
// discrete taps read 10+.
inline double worstCrest(const std::vector<float>& h, double sr, double fromMs, double toMs, double frameMs = 5.0)
{
    double worst = 0.0;
    for (double t = fromMs; t + frameMs <= toMs; t += frameMs)
    {
        const auto a = static_cast<std::size_t>(samplesFor(t, sr));
        const auto b = std::min(h.size(), static_cast<std::size_t>(samplesFor(t + frameMs, sr)));
        double e = 0.0, pk = 0.0;
        for (auto i = a; i < b; ++i) { e += static_cast<double>(h[i]) * h[i]; pk = std::max(pk, std::abs(static_cast<double>(h[i]))); }
        if (b <= a || e <= 1.0e-24) continue;
        worst = std::max(worst, pk / std::sqrt(e / static_cast<double>(b - a)));
    }
    return worst;
}

inline double spectralCentroidHz(const std::vector<float>& x, double sr)
{
    if (x.size() < 8192) return 0.0;
    const auto p = welch(x);
    const auto n = static_cast<double>((p.size() - 1) * 2);
    double num = 0.0, den = 0.0;
    for (std::size_t k = 1; k < p.size(); ++k) { num += p[k] * static_cast<double>(k) * sr / n; den += p[k]; }
    return den > 0.0 ? num / den : 0.0;
}

inline double maxStep(const std::vector<float>& x)
{
    double worst = 0.0;
    for (std::size_t i = 1; i < x.size(); ++i) worst = std::max(worst, static_cast<double>(std::abs(x[i] - x[i - 1])));
    return worst;
}

// Spring dispersion: the repetition period of the band around `centreHz`
// (envelope autocorrelation peak, 25..250 ms). A dispersive spring returns
// high frequencies later every round trip, so the period rises with
// frequency (Valimaki, Parker & Abel 2010).
inline double echoPeriodMs(const std::vector<float>& h, double sr, double centreHz, double untilMs = 900.0)
{
    const auto n = std::min(h.size(), static_cast<std::size_t>(samplesFor(untilMs, sr)));
    std::vector<float> seg(h.begin(), h.begin() + static_cast<std::ptrdiff_t>(n));
    const auto band = bandpass(seg, sr, centreHz, 4.0);
    // Energy envelope, 2 ms smoothing, decimated to 0.5 ms steps.
    const auto step = std::max(1, samplesFor(0.5, sr));
    std::vector<double> env;
    double acc = 0.0;
    const auto c = std::exp(-1.0 / (0.002 * sr));
    for (std::size_t i = 0; i < band.size(); ++i)
    {
        acc = c * acc + (1.0 - c) * static_cast<double>(band[i]) * band[i];
        if (i % static_cast<std::size_t>(step) == 0) env.push_back(std::sqrt(acc));
    }
    double mean = 0.0;
    for (const auto v : env) mean += v;
    mean /= static_cast<double>(std::max<std::size_t>(1, env.size()));
    for (auto& v : env) v -= mean;
    double best = -1.0e30;
    int bestLag = 0;
    for (int lag = 50; lag < 500 && lag < static_cast<int>(env.size()); ++lag)
    {
        double r = 0.0;
        for (std::size_t i = 0; i + static_cast<std::size_t>(lag) < env.size(); ++i) r += env[i] * env[i + static_cast<std::size_t>(lag)];
        if (r > best) { best = r; bestLag = lag; }
    }
    return bestLag * 0.5;
}

// Time of the strongest arrival of the band around `centreHz` in the first
// `windowMs` (2 ms energy envelope). Two bands' difference is the spring's
// chirp: a dispersive spring delivers its highs after its lows.
inline double firstArrivalMs(const std::vector<float>& h, double sr, double centreHz, double windowMs)
{
    const auto n = std::min(h.size(), static_cast<std::size_t>(samplesFor(windowMs, sr)));
    std::vector<float> seg(h.begin(), h.begin() + static_cast<std::ptrdiff_t>(n));
    const auto band = bandpass(seg, sr, centreHz, 3.0);
    double acc = 0.0, best = -1.0;
    std::size_t at = 0;
    const auto c = std::exp(-1.0 / (0.002 * sr));
    for (std::size_t i = 0; i < band.size(); ++i)
    {
        acc = c * acc + (1.0 - c) * static_cast<double>(band[i]) * band[i];
        if (acc > best) { best = acc; at = i; }
    }
    return static_cast<double>(at) / sr * 1000.0;
}

// Clicks and zipper steps: the worst crest factor (peak / rms) of the
// second difference over 10 ms windows. A smooth signal - a sine through a
// reverb, a gliding delay - reads ~2-5; a step reads far higher.
inline double worstSecondDifferenceCrest(const std::vector<float>& x, std::size_t from, double sr)
{
    const auto window = static_cast<std::size_t>(std::max(64, samplesFor(10.0, sr)));
    double worst = 0.0;
    for (auto start = from + 2; start + window < x.size(); start += window / 2)
    {
        double e = 0.0, pk = 0.0;
        for (auto i = start; i < start + window; ++i)
        {
            const auto d = static_cast<double>(x[i]) - 2.0 * x[i - 1] + x[i - 2];
            e += d * d;
            pk = std::max(pk, std::abs(d));
        }
        if (e <= 1.0e-24) continue;
        worst = std::max(worst, pk / std::sqrt(e / static_cast<double>(window)));
    }
    return worst;
}

inline double spectralFlatness(const std::vector<float>& h, double sr, double startMs, int length = 16384)
{
    const auto a = static_cast<std::size_t>(samplesFor(startMs, sr));
    if (a + static_cast<std::size_t>(length) > h.size()) return 0.0;
    std::vector<float> seg(h.begin() + static_cast<std::ptrdiff_t>(a), h.begin() + static_cast<std::ptrdiff_t>(a) + length);
    const auto p = welch(seg, 14);
    const auto n = static_cast<double>((p.size() - 1) * 2);
    double logSum = 0.0, sum = 0.0;
    int count = 0;
    for (std::size_t k = 1; k < p.size(); ++k)
    {
        const auto hz = static_cast<double>(k) * sr / n;
        if (hz < 100.0 || hz > 10000.0) continue;
        logSum += std::log(p[k] + 1.0e-30);
        sum += p[k];
        ++count;
    }
    return count > 0 ? std::exp(logSum / count) / (sum / count) : 0.0;
}
} // namespace px3tests::reverbmeasure
