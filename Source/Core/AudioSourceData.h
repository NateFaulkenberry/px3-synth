#pragma once

#include <JuceHeader.h>

#include <vector>

/**
 * Immutable audio-source payload shared between message-thread loading code and
 * real-time voice rendering.
 *
 * A loader job populates this struct off the audio thread, then the processor
 * publishes it through a shared_ptr. Voices only read from this structure.
 *
 * Important design goals:
 * 1. No file I/O or decode work during processBlock.
 * 2. Clear ownership/lifetime via shared_ptr<const AudioSourceData>.
 * 3. Reusable waveform preview data for UI drawing without rescanning samples.
 */
struct AudioSourceData
{
    // Interleaving is avoided here: JUCE AudioBuffer gives efficient per-channel
    // random reads for grain interpolation.
    juce::AudioBuffer<float> samples;

    // Original file sample rate. Used to derive playback ratio and preserve
    // pitch expectations when grains are read at different host sample rates.
    double sampleRate { 44100.0 };

    // Limited to mono/stereo in current loader path.
    int numChannels { 0 };

    // Number of valid samples per channel in `samples`.
    int numSamples { 0 };

    // Peak magnitude observed during load. Used for safe normalization and to
    // avoid divide-by-zero behavior when content is near silence.
    float peak { 1.0f };

    // Downsampled absolute-amplitude envelope used by UI waveform previews.
    // This keeps paint() work lightweight and deterministic.
    std::vector<float> waveformPreview;
};
