#pragma once

#include "Wavetable.h"

namespace px3
{

// Reads a wavetable. One of these per oscillator per voice; it holds only the
// band-limit state, because everything else it needs is passed in.
//
// Deliberately separate from OscillatorUnit so the read path can be measured on
// its own. An interpolator's alias rejection is a property of the interpolator,
// and testing it through a whole synth voice measures the voice.
class WavetableReader
{
public:
    // How far the pitch has to fall before a BRIGHTER level is taken up again.
    //
    // The two directions are not symmetric on purpose. Moving to a darker level
    // has to happen the moment the pitch demands it, because the alternative is
    // aliasing. Moving back to a brighter one can wait, because the alternative
    // is only a slightly duller sound - and waiting is what stops a note sitting
    // on a boundary from toggling between levels once per vibrato cycle, which
    // would be heard as a warble. 1.15 is a little over two semitones, comfortably
    // wider than any vibrato.
    static constexpr double kBrightenHysteresis = 1.15;

    void reset() noexcept { level = 0; }

    // Audio thread.
    //
    // `phase` is the position within one cycle, [0, 1). `position` is the scan
    // through the table, [0, 1]. `increment` is how far the phase moves per
    // sample, which is what selects the band limit.
    float read(const Wavetable& table, double phase, float position, double increment) noexcept
    {
        updateLevel(table, increment);

        const auto length = table.getLevelLength(level);
        const auto readPosition = juce::jlimit(0.0, 1.0, phase) * length;

        // Frames either side of the scan position, crossfaded linearly.
        //
        // Linear is correct here rather than merely cheap: a linear combination
        // of two band-limited frames is itself band-limited, so no interpolation
        // order can make this safer. What linear crossfading is vulnerable to is
        // frames that disagree in PHASE, and that is fixed when the table is
        // built, not when it is read.
        const auto framePosition = juce::jlimit(0.0f, 1.0f, position)
                                   * static_cast<float>(table.getFrameCount() - 1);
        const auto frameA = static_cast<int>(framePosition);
        const auto frameB = juce::jmin(frameA + 1, table.getFrameCount() - 1);
        const auto frameFraction = framePosition - static_cast<float>(frameA);

        const auto a = hermite(table.getFrame(level, frameA), length, readPosition);
        const auto b = hermite(table.getFrame(level, frameB), length, readPosition);
        return a + frameFraction * (b - a);
    }

    int getLevel() const noexcept { return level; }

private:
    void updateLevel(const Wavetable& table, double increment) noexcept
    {
        const auto required = table.levelForIncrement(increment);

        if (required > level)
        {
            level = required;   // darker, immediately - see kBrightenHysteresis
            return;
        }

        if (required < level && table.levelForIncrement(increment * kBrightenHysteresis) < level)
        {
            level = required;
        }
    }

    // Four-point cubic Hermite.
    //
    // Chosen by measurement over linear and an eight-tap windowed sinc: with the
    // pyramid's minimum level length in place it matches the sinc's alias
    // rejection - and beats it at C6 and C7 - for 70% of the cost, and is
    // transparent through 10 kHz. See docs/WAVETABLE_OSCILLATOR_DESIGN.md, E.1.
    static float hermite(const float* frame, int length, double position) noexcept
    {
        const auto index = static_cast<int>(position);
        const auto t = static_cast<float>(position - index);

        // Lengths are powers of two, so wrapping is a mask. It has to wrap
        // rather than clamp: a wavetable frame is one CYCLE, and the sample
        // before the first is the last, not a repeat of the first.
        const auto mask = length - 1;
        const auto xm1 = frame[(index - 1) & mask];
        const auto x0 = frame[index & mask];
        const auto x1 = frame[(index + 1) & mask];
        const auto x2 = frame[(index + 2) & mask];

        const auto c = 0.5f * (x1 - xm1);
        const auto v = x0 - x1;
        const auto w = c + v;
        const auto a = w + v + 0.5f * (x2 - x0);
        const auto b = w + a;

        return ((a * t - b) * t + c) * t + x0;
    }

    int level { 0 };
};

} // namespace px3
