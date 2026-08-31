#pragma once

#include <JuceHeader.h>

#include <memory>
#include <vector>

namespace px3
{

// One frame of a wavetable, described by its harmonics rather than its samples.
//
// Both things that produce tables - the factory generators and the importer -
// naturally work in the frequency domain: a generator writes the harmonics it
// wants, and an importer has to look at them anyway to align phase. Building the
// band-limited pyramid from a spectrum is then exact, because truncating a
// harmonic series is exactly what band-limiting means. Building it from samples
// would mean filtering, which is an approximation of the same thing.
struct FrameSpectrum
{
    // Index 0 is DC and is ignored - a wavetable frame with DC in it is an
    // offset, not a sound. Harmonic h lives at index h.
    std::vector<float> amplitude;
    std::vector<float> phase;

    int harmonicCount() const
    {
        return juce::jmax(0, static_cast<int>(amplitude.size()) - 1);
    }
};

// A copy of a table, downsampled for drawing.
//
// The UI gets a copy rather than the live pointer. Not because reading it would
// race - the message thread owns retirement, so it could read it safely - but
// because a display wants 48 frames of 256 points and the table holds 64 frames
// of up to 4096, and because taking the copy once per table change is cheaper
// than walking the real thing on every repaint.
struct WavetableDisplay
{
    juce::String name;
    juce::String category;
    bool fromUserLibrary { false };
    std::vector<std::vector<float>> frames;

    bool isEmpty() const { return frames.empty(); }
};

// The inverse of what Wavetable::build consumes: turns one cycle of samples into
// the harmonics it is made of.
//
// Needed by anything that produces a frame as a WAVEFORM rather than as a
// spectrum - a generator written as a waveshaper, and every audio import. Its
// output round-trips through Wavetable::build to the waveform it came from,
// which is the property the tests hold it to.
FrameSpectrum analyseFrame(const float* samples, int length);

// An immutable band-limited wavetable, shared by every voice and every
// oscillator that uses it.
//
// Immutable is the load-bearing word: it is what lets one table be handed to the
// audio thread without a lock, and what lets three oscillators and sixty-four
// voices share one allocation instead of sixty-four.
class Wavetable
{
public:
    static constexpr int kFrameSize = 2048;
    static constexpr int kDefaultFrameCount = 64;
    static constexpr int kMaxFrameCount = 256;

    // A level's length is derived from how many harmonics it carries, not from
    // the frame size, and it carries FOUR times as many samples as Nyquist
    // demands.
    //
    // This "headroom" is the single biggest quality knob in the whole
    // oscillator, and it is not intuitive: a level whose top harmonic sits at
    // its own Nyquist is critically sampled, and no interpolator can read it
    // cleanly. Measured, alias rejection tracks headroom at roughly 16 dB per
    // doubling and is almost independent of everything else -
    //
    //     headroom  1     2     4     8    16    32
    //     rejection 48    52    68    83    98   116  dB
    //
    // - so headroom 4 is where the curve first clears 60 dB with margin. See
    // docs/WAVETABLE_OSCILLATOR_DESIGN.md, E.3.
    static constexpr int kLevelHeadroom = 4;

    // Below this a level is short enough that the interpolator, rather than the
    // band-limiting, decides how it sounds. Only the darkest levels reach it,
    // and they have headroom to spare by then.
    static constexpr int kMinLevelLength = 256;

    // Harmonics in level 0, halving every level after it.
    //
    // 512 rather than the 1023 a 2048-point frame could hold: keeping headroom 4
    // for 1023 harmonics would need an 8192-sample level and 4.35 MB per table,
    // to reproduce content above 16 kHz that is inaudible and that only exists
    // at all on the lowest notes. The ceiling this sets is about 16.5 kHz at
    // every pitch, which is the deliberate trade for a 70 dB alias floor.
    static constexpr int kMaxHarmonics = 512;

    static constexpr int kMaxLevels = 11;

    // Builds the pyramid. Off the audio thread - it allocates and runs an
    // inverse transform per frame per level.
    //
    // Returns null if the frames are unusable, rather than a silent table: a
    // caller that cannot tell the difference between "empty" and "failed" ends
    // up shipping silence.
    static std::shared_ptr<const Wavetable> build(juce::String name,
                                                  juce::String category,
                                                  const std::vector<FrameSpectrum>& frames);

    const juce::String& getName() const noexcept { return name; }
    const juce::String& getCategory() const noexcept { return category; }

    int getFrameCount() const noexcept { return frameCount; }
    int getLevelCount() const noexcept { return static_cast<int>(levels.size()); }

    int getLevelLength(int level) const noexcept
    {
        return levels[static_cast<std::size_t>(clampLevel(level))].length;
    }

    int getLevelHarmonics(int level) const noexcept
    {
        return levels[static_cast<std::size_t>(clampLevel(level))].harmonics;
    }

    // Audio thread. One frame of one level, `getLevelLength(level)` samples long.
    const float* getFrame(int level, int frame) const noexcept
    {
        const auto& l = levels[static_cast<std::size_t>(clampLevel(level))];
        const auto f = juce::jlimit(0, frameCount - 1, frame);
        return l.data.data() + static_cast<std::size_t>(f) * static_cast<std::size_t>(l.length);
    }

    // Which level to read, given how far the phase moves per sample.
    //
    // Selected from the INCREMENT rather than from the note number, because the
    // increment already carries the sample rate, the tuning, the pitch bend and
    // the vibrato. Choosing by note is the classic way to get this wrong and
    // then discover it at 96 kHz.
    int levelForIncrement(double increment) const noexcept
    {
        if (increment <= 0.0)
        {
            return 0;
        }

        // Harmonics that still fit under Nyquist at this speed.
        const auto affordable = 0.5 / increment;
        for (std::size_t l = 0; l < levels.size(); ++l)
        {
            if (static_cast<double>(levels[l].harmonics) <= affordable)
            {
                return static_cast<int>(l);
            }
        }
        return static_cast<int>(levels.size()) - 1;
    }

    std::size_t getSizeInBytes() const noexcept { return sizeInBytes; }

private:
    struct Level
    {
        int length { 0 };
        int harmonics { 0 };
        std::vector<float> data;   // frameCount * length, frame-major
    };

    int clampLevel(int level) const noexcept
    {
        return juce::jlimit(0, static_cast<int>(levels.size()) - 1, level);
    }

    juce::String name;
    juce::String category;
    int frameCount { 0 };
    std::vector<Level> levels;
    std::size_t sizeInBytes { 0 };
};

} // namespace px3
