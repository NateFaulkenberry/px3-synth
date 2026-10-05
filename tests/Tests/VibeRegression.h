#pragma once

#include "VibeMeasure.h"

// Black-box character of a Uni-Vibe-style effect: the numbers that separate the
// pedal from a generic phaser. Templated on the effect so the same measurement
// can be pointed at any implementation with the prepare / updateForBlock /
// processSampleFrame contract (it was, to show the previous VIBE failed it).
namespace px3tests::vibemeasure
{
struct VibeCharacter
{
    double riseFraction { 0.5 };        // phase track at 1 kHz, VIBRATO, intensity 0.8, 2 Hz
    double riseFractionLow { 0.5 };     // the same at intensity 0.3
    double notchSpanOctaves { 0.0 };    // how far the deepest notch travels
    double meanDeepNotches { 0.0 };     // deep notches per frame, 100 Hz - 8 kHz
    double framesWithTwoNotches { 0.0 };// share of frames showing two or more deep notches
    double vibratoBassSwingDb { 0.0 };  // 50 Hz level swing over the cycle, VIBRATO
    double vibratoTrebleSwingDb { 0.0 };// 4 kHz level swing over the cycle, VIBRATO
    double vibratoTrebleOverBassDb { 0.0 }; // mean gain at 4 kHz minus mean gain at 300 Hz, VIBRATO
};

// `configure(effect, intensity, hz, mode)` sets the effect up; `process(effect,
// x)` returns the left output for a mono input.
template <typename Effect, typename Configure, typename Process>
VibeCharacter measureCharacter(Configure&& configure, Process&& process)
{
    VibeCharacter c;
    constexpr double hz = 2.0;
    const auto framesPerCycle = static_cast<int>(std::lround(kFrameHz / hz)); // 25
    const auto settle = static_cast<int>(kRate * 1.5);
    const auto total = settle + framesPerCycle * kFrame * 8;

    auto notchTrack = [&](float intensity, std::vector<std::vector<double>>& framesOut)
    {
        Effect effect;
        configure(effect, intensity, hz, 0);
        std::vector<float> in;
        const auto out = run(total, probeSignal, [&](int, float x) { return process(effect, x); }, &in);
        framesOut = responseFrames(in, out, settle);
        std::vector<double> track;
        for (const auto& f : framesOut) track.push_back(notchLog2Hz(f));
        return track;
    };

    // The sweep's shape, read from the phase it puts on a 1 kHz tone. Every
    // stage's phase at a fixed frequency moves monotonically with its LDR, so
    // the unwrapped phase traces the lamp. 5 ms frames (five whole cycles).
    auto phaseTrack = [&](float intensity)
    {
        Effect effect;
        configure(effect, intensity, hz, 1);
        std::vector<float> in;
        const auto out = run(total, [](int n)
        {
            return static_cast<float>(0.1 * std::sin(juce::MathConstants<double>::twoPi * 1000.0 * n / kRate));
        }, [&](int, float x) { return process(effect, x); }, &in);
        constexpr int frame = 240;
        std::vector<double> track;
        double previous = 0.0, offset = 0.0;
        for (int start = settle; start + frame <= total; start += frame)
        {
            const auto ratio = dft(out.data() + start, frame, 1000.0) / dft(in.data() + start, frame, 1000.0);
            auto phase = std::arg(ratio);
            if (! track.empty())
            {
                while (phase + offset - previous > juce::MathConstants<double>::pi) offset -= juce::MathConstants<double>::twoPi;
                while (phase + offset - previous < -juce::MathConstants<double>::pi) offset += juce::MathConstants<double>::twoPi;
            }
            previous = phase + offset;
            track.push_back(previous);
        }
        return track;
    };
    const auto phaseFramesPerCycle = static_cast<int>(std::lround(kRate / 240.0 / hz)); // 100
    c.riseFraction = riseFraction(phaseTrack(0.8f), phaseFramesPerCycle);
    c.riseFractionLow = riseFraction(phaseTrack(0.3f), phaseFramesPerCycle);

    std::vector<std::vector<double>> frames;
    const auto track = notchTrack(0.6f, frames);
    double lo = 1e9, hi = -1e9, notches = 0.0, twos = 0.0;
    for (std::size_t i = 0; i < track.size(); ++i)
    {
        lo = juce::jmin(lo, track[i]);
        hi = juce::jmax(hi, track[i]);
        const auto count = notchCount(frames[i], 10.0);
        notches += count;
        twos += count >= 2 ? 1.0 : 0.0;
    }
    c.framesWithTwoNotches = twos / juce::jmax<double>(1.0, static_cast<double>(frames.size()));
    c.notchSpanOctaves = hi - lo;
    c.meanDeepNotches = notches / juce::jmax<double>(1.0, static_cast<double>(frames.size()));

    // VIBRATO, two tones at once: 50 Hz and 4 kHz (and 300 Hz for the tilt).
    {
        Effect effect;
        configure(effect, 1.0f, hz, 1);
        std::vector<float> in;
        const auto out = run(total, [](int n)
        {
            const auto t = static_cast<double>(n) / kRate;
            return static_cast<float>(0.05 * std::sin(juce::MathConstants<double>::twoPi * 50.0 * t)
                                      + 0.05 * std::sin(juce::MathConstants<double>::twoPi * 300.0 * t + 1.0)
                                      + 0.05 * std::sin(juce::MathConstants<double>::twoPi * 4000.0 * t + 2.0));
        }, [&](int, float x) { return process(effect, x); }, &in);
        c.vibratoBassSwingDb = toneSwingDb(out, 50.0, settle);
        c.vibratoTrebleSwingDb = toneSwingDb(out, 4000.0, settle);
        c.vibratoTrebleOverBassDb = meanToneGainDb(in, out, 4000.0, settle) - meanToneGainDb(in, out, 300.0, settle);
    }
    return c;
}
} // namespace px3tests::vibemeasure
