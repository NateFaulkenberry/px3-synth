#include "WavetableImporter.h"

#include <cmath>

namespace px3
{
namespace
{
// Reads the source at a fractional position, wrapping. Cubic rather than
// linear: this runs once per output sample of every frame, and a cheap
// interpolator here shows up as a dull import that no amount of care later can
// recover.
float readCubic(const float* data, int count, double position)
{
    const auto index = static_cast<int>(std::floor(position));
    const auto t = static_cast<float>(position - index);

    const auto at = [data, count](int i)
    {
        // Clamped, not wrapped: a cycle taken out of the middle of a recording
        // has no reason for its end to meet its start, and wrapping there would
        // splice two unrelated parts of the waveform together.
        return data[juce::jlimit(0, count - 1, i)];
    };

    const auto xm1 = at(index - 1);
    const auto x0 = at(index);
    const auto x1 = at(index + 1);
    const auto x2 = at(index + 2);

    const auto c = 0.5f * (x1 - xm1);
    const auto v = x0 - x1;
    const auto w = c + v;
    const auto a = w + v + 0.5f * (x2 - x0);
    const auto b = w + a;
    return ((a * t - b) * t + c) * t + x0;
}

void removeDcAndNormalise(std::vector<float>& samples)
{
    if (samples.empty())
    {
        return;
    }

    double sum = 0.0;
    for (const auto s : samples) { sum += s; }
    const auto mean = static_cast<float>(sum / static_cast<double>(samples.size()));

    auto peak = 0.0f;
    for (auto& s : samples)
    {
        s -= mean;
        peak = juce::jmax(peak, std::abs(s));
    }

    if (peak > 1.0e-9f)
    {
        const auto gain = 1.0f / peak;
        for (auto& s : samples) { s *= gain; }
    }
}
} // namespace

void WavetableImporter::alignFundamentalPhase(FrameSpectrum& frame)
{
    if (frame.harmonicCount() < 1 || frame.phase.size() < 2)
    {
        return;
    }

    // Shifting a frame by d samples adds -2*pi*h*d/N to harmonic h, so a shift
    // that zeroes the fundamental's phase subtracts h times that phase from
    // every harmonic. One multiply per harmonic, and the waveform's shape comes
    // through untouched because a time shift is all it is.
    const auto fundamental = frame.phase[1];
    for (int h = 1; h <= frame.harmonicCount(); ++h)
    {
        frame.phase[static_cast<std::size_t>(h)] -= static_cast<float>(h) * fundamental;
    }
}

double WavetableImporter::detectPeriod(const float* samples, int count, double sampleRate)
{
    if (samples == nullptr || count < 64 || sampleRate <= 0.0)
    {
        return 0.0;
    }

    // Autocorrelation, not zero crossings. A waveform with any harmonic content
    // crosses zero several times per cycle, so counting crossings finds the
    // brightest partial rather than the pitch.
    const auto minPeriod = juce::jmax(2, static_cast<int>(sampleRate / 2000.0));
    const auto maxPeriod = juce::jmin(count / 2, static_cast<int>(sampleRate / 20.0));
    if (maxPeriod <= minPeriod)
    {
        return 0.0;
    }

    double energy = 0.0;
    for (int i = 0; i < count; ++i)
    {
        energy += static_cast<double>(samples[i]) * samples[i];
    }
    if (energy < 1.0e-12)
    {
        return 0.0;
    }

    std::vector<double> scores(static_cast<std::size_t>(maxPeriod + 1), 0.0);
    auto bestScore = 0.0;

    for (int period = minPeriod; period <= maxPeriod; ++period)
    {
        double correlation = 0.0, headEnergy = 0.0, tailEnergy = 0.0;
        const auto limit = count - period;
        for (int i = 0; i < limit; ++i)
        {
            const auto a = static_cast<double>(samples[i]);
            const auto b = static_cast<double>(samples[i + period]);
            correlation += a * b;
            headEnergy += a * a;
            tailEnergy += b * b;
        }

        // Normalised by BOTH energies, so the result is a correlation
        // coefficient in [-1, 1] and can be compared against a threshold. The
        // one-sided version this replaced grew with the signal's level, so the
        // best score was whatever lag lined up with the loudest part - which is
        // how white noise came back as a confident 106.7 Hz.
        const auto denominator = std::sqrt(headEnergy * tailEnergy);
        scores[static_cast<std::size_t>(period)] =
            denominator > 1.0e-12 ? correlation / denominator : 0.0;
        bestScore = juce::jmax(bestScore, scores[static_cast<std::size_t>(period)]);
    }

    // The FIRST strong peak, not the strongest one.
    //
    // A periodic signal correlates with itself at every multiple of its period,
    // and which multiple scores highest is decided by rounding rather than by
    // pitch. Taking the global maximum reported a 220 Hz sawtooth as 20 Hz,
    // because 2400 samples happens to be almost exactly eleven of its cycles.
    // The lowest lag that is both a local peak and nearly as good as the best is
    // the period; the rest are its multiples.
    auto bestPeriod = 0;
    for (int period = minPeriod + 1; period < maxPeriod; ++period)
    {
        const auto here = scores[static_cast<std::size_t>(period)];
        if (here >= 0.9 * bestScore
            && here >= scores[static_cast<std::size_t>(period - 1)]
            && here >= scores[static_cast<std::size_t>(period + 1)])
        {
            bestPeriod = period;
            bestScore = here;
            break;
        }
    }

    // Unpitched material still has a best lag; what it does not have is a good
    // one. Below this the "period" is whatever noise correlated best, and
    // acting on it would cut 64 arbitrary fragments and call them cycles.
    constexpr double kConfidence = 0.5;
    return bestScore >= kConfidence && bestPeriod > 0 ? static_cast<double>(bestPeriod) : 0.0;
}

WavetableImporter::Result WavetableImporter::fromAudio(const float* samples,
                                                       int count,
                                                       double sampleRate,
                                                       Options options)
{
    Result result;
    if (samples == nullptr || count < 4 || sampleRate <= 0.0)
    {
        result.description = "No usable audio.";
        return result;
    }

    const auto frameCount = juce::jlimit(2, Wavetable::kMaxFrameCount, options.frameCount);

    std::vector<float> work(samples, samples + count);
    removeDcAndNormalise(work);

    // Leading and trailing silence would otherwise become frames of nothing,
    // and a scan that begins in silence wastes the first part of its range.
    auto first = 0;
    auto last = count - 1;
    constexpr float kSilence = 0.001f;
    while (first < last && std::abs(work[static_cast<std::size_t>(first)]) < kSilence) { ++first; }
    while (last > first && std::abs(work[static_cast<std::size_t>(last)]) < kSilence) { --last; }
    const auto usable = last - first + 1;
    if (usable < 4)
    {
        result.description = "The audio is silent.";
        return result;
    }

    const auto* audio = work.data() + first;
    const auto period = detectPeriod(audio, usable, sampleRate);

    // A single-cycle file has to be recognised by its LENGTH, not by detection.
    // Autocorrelation needs to see a period twice to find it once, so a file
    // that is exactly one cycle long can never be measured - and asked anyway,
    // it confidently returns some short sub-multiple, because a sine correlates
    // almost perfectly with itself at a small lag too. A power-of-two length up
    // to 4096 samples is the convention every single-cycle library follows.
    const auto isPowerOfTwo = count > 0 && (count & (count - 1)) == 0;
    const auto singleCycle = (isPowerOfTwo && count <= 4096)
                             || period <= 0.0
                             || usable < static_cast<int>(period * 2.0);

    std::vector<float> cycle(static_cast<std::size_t>(Wavetable::kFrameSize), 0.0f);

    if (singleCycle)
    {
        for (int i = 0; i < Wavetable::kFrameSize; ++i)
        {
            const auto position = static_cast<double>(i) * usable / Wavetable::kFrameSize;
            cycle[static_cast<std::size_t>(i)] = readCubic(audio, usable, position);
        }

        auto spectrum = analyseFrame(cycle.data(), Wavetable::kFrameSize);
        if (options.phase == PhaseMode::alignFundamental) { alignFundamentalPhase(spectrum); }
        else { std::fill(spectrum.phase.begin(), spectrum.phase.end(), 0.0f); }

        // One cycle, so every frame is the same and the scan does nothing. That
        // is the honest outcome for a single-cycle file, and it is said out loud
        // rather than dressed up with 64 identical frames pretending otherwise.
        result.frames.assign(2, spectrum);
        result.description = "Single cycle: " + juce::String(usable) + " samples.";
        return result;
    }

    // Cycles are taken at evenly spaced points across the material, each one a
    // whole period starting where a period starts.
    const auto cycles = static_cast<double>(usable) / period;
    const auto span = juce::jmax(0.0, cycles - 1.0);

    for (int f = 0; f < frameCount; ++f)
    {
        const auto which = frameCount > 1
                             ? span * f / (frameCount - 1)
                             : 0.0;
        const auto start = which * period;

        for (int i = 0; i < Wavetable::kFrameSize; ++i)
        {
            const auto position = start + static_cast<double>(i) * period / Wavetable::kFrameSize;
            cycle[static_cast<std::size_t>(i)] = readCubic(audio, usable, position);
        }

        auto spectrum = analyseFrame(cycle.data(), Wavetable::kFrameSize);
        if (options.phase == PhaseMode::alignFundamental) { alignFundamentalPhase(spectrum); }
        else { std::fill(spectrum.phase.begin(), spectrum.phase.end(), 0.0f); }

        result.frames.push_back(std::move(spectrum));
    }

    result.description = juce::String(frameCount) + " frames from "
                         + juce::String(cycles, 1) + " cycles at "
                         + juce::String(sampleRate / period, 1) + " Hz.";
    return result;
}

WavetableImporter::Result WavetableImporter::fromImage(const juce::Image& image, Options options)
{
    Result result;
    if (! image.isValid() || image.getWidth() < 2 || image.getHeight() < 1)
    {
        result.description = "No usable image.";
        return result;
    }

    const auto frameCount = juce::jlimit(2, Wavetable::kMaxFrameCount, options.frameCount);
    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);

    std::vector<float> row(static_cast<std::size_t>(Wavetable::kFrameSize), 0.0f);

    for (int f = 0; f < frameCount; ++f)
    {
        // Top row is the first frame, which is the direction an image is read.
        const auto y = frameCount > 1
                         ? juce::jlimit(0, image.getHeight() - 1,
                                        juce::roundToInt(static_cast<double>(f)
                                                         * (image.getHeight() - 1) / (frameCount - 1)))
                         : 0;

        for (int i = 0; i < Wavetable::kFrameSize; ++i)
        {
            const auto x = juce::jlimit(0, image.getWidth() - 1,
                                        juce::roundToInt(static_cast<double>(i)
                                                         * (image.getWidth() - 1)
                                                         / (Wavetable::kFrameSize - 1)));
            const auto colour = pixels.getPixelColour(x, y);

            // Brightness, so a colour image works as well as a greyscale one,
            // and mapped to a signal that swings either side of zero - a
            // brightness of 0..1 read as an amplitude would be a waveform that
            // is never negative, which is a DC offset with a wobble on it.
            row[static_cast<std::size_t>(i)] =
                static_cast<float>(colour.getBrightness()) * 2.0f - 1.0f;
        }

        // Per row, because a bright band across the image would otherwise put a
        // different offset in every frame and make the scan pump.
        std::vector<float> frame(row);
        removeDcAndNormalise(frame);

        auto spectrum = analyseFrame(frame.data(), Wavetable::kFrameSize);
        if (options.phase == PhaseMode::alignFundamental) { alignFundamentalPhase(spectrum); }
        else { std::fill(spectrum.phase.begin(), spectrum.phase.end(), 0.0f); }

        result.frames.push_back(std::move(spectrum));
    }

    result.description = juce::String(frameCount) + " frames from a "
                         + juce::String(image.getWidth()) + "x"
                         + juce::String(image.getHeight()) + " image.";
    return result;
}

} // namespace px3
