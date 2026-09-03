#pragma once

#include "Wavetable.h"

namespace px3
{

// Turns audio or an image into wavetable frames.
//
// An arbitrary audio file is not a wavetable. Chopping one into 64 equal pieces
// and calling them single-cycle waves is the obvious approach and it produces
// garbage: the pieces do not start where a cycle starts, so every frame is a
// different fragment of the waveform at a different phase, and scanning between
// them is a crossfade between unrelated shapes rather than a morph.
//
// So cycles are found rather than assumed, and then aligned - see
// docs/WAVETABLE_OSCILLATOR_DESIGN.md F.4, where skipping alignment cost 19 dB
// of the morph.
//
// All of this allocates and runs transforms. Message thread or a background job,
// never the audio thread.
class WavetableImporter
{
public:
    enum class PhaseMode
    {
        // Rotate each frame so its fundamental starts at zero. A pure time
        // shift, so the waveform's shape is untouched. The default.
        alignFundamental,

        // Keep the amplitudes, discard the phases. The morph becomes exact
        // because every frame then has identical phase structure - and the
        // waveform stops resembling the audio it came from, by about 70% of its
        // own level. Offered for material where a clean scan matters more than
        // the shape.
        discardPhase
    };

    struct Options
    {
        int frameCount { Wavetable::kDefaultFrameCount };
        PhaseMode phase { PhaseMode::alignFundamental };
    };

    struct Result
    {
        std::vector<FrameSpectrum> frames;

        // What the importer decided, in the words the user needs to hear it in -
        // an import that silently does something reasonable is impossible to
        // argue with when it does something unreasonable.
        juce::String description;

        bool ok() const { return ! frames.empty(); }
    };

    // Mono samples. A stereo source should be summed before it gets here: two
    // channels that differ only in stereo detail make two different tables out
    // of one sound.
    static Result fromAudio(const float* samples, int count, double sampleRate, Options options);
    static Result fromAudio(const float* samples, int count, double sampleRate)
    {
        return fromAudio(samples, count, sampleRate, Options {});
    }

    // Rows are frames, columns are sample positions, brightness is amplitude -
    // the convention okwt established and the one an image-to-wavetable user
    // will already expect.
    static Result fromImage(const juce::Image& image, Options options);
    static Result fromImage(const juce::Image& image)
    {
        return fromImage(image, Options {});
    }

    // Exposed for testing: rotates a frame so its fundamental starts at zero
    // phase. A pure time shift - every harmonic moves by h times the same
    // amount, which is what makes it shape-preserving.
    static void alignFundamentalPhase(FrameSpectrum& frame);

    // Estimated period in samples, or 0 if the material has no usable pitch.
    static double detectPeriod(const float* samples, int count, double sampleRate);
};

} // namespace px3
