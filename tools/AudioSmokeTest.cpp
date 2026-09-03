// Factory-default audio smoke test.
//
// Deliberately built with PX3_DIAGNOSTICS=0 - the shipping configuration - and
// deliberately sets NO parameters. It answers the one question every other test
// assumes: does a freshly loaded plugin make sound when you press a key?

#include <JuceHeader.h>

#include "PluginProcessor.h"

#include <cstdio>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

struct SmokeResult
{
    double peak { 0.0 };
    double rms { 0.0 };
    bool sawNonFinite { false };
};

SmokeResult renderDefaultNote(double sampleRate, int blockSize)
{
    PX3SynthAudioProcessor processor;   // factory defaults only, nothing set
    processor.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    processor.prepareToPlay(sampleRate, blockSize);

    juce::AudioBuffer<float> buffer(2, blockSize);
    const auto totalSamples = static_cast<int>(1.5 * sampleRate);
    const auto noteOn = static_cast<int>(0.05 * sampleRate);

    SmokeResult result;
    double energy = 0.0;
    long long count = 0;
    auto delivered = false;

    for (int position = 0; position < totalSamples; position += blockSize)
    {
        buffer.clear();
        juce::MidiBuffer midi;
        if (!delivered && position + blockSize > noteOn)
        {
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), juce::jmax(0, noteOn - position));
            delivered = true;
        }
        processor.processBlock(buffer, midi);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto* data = buffer.getReadPointer(ch);
            for (int i = 0; i < blockSize; ++i)
            {
                const auto v = static_cast<double>(data[i]);
                if (!std::isfinite(v)) result.sawNonFinite = true;
                result.peak = std::max(result.peak, std::abs(v));
                energy += v * v;
                ++count;
            }
        }
    }

    result.rms = count > 0 ? std::sqrt(energy / static_cast<double>(count)) : 0.0;
    return result;
}

}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf("FACTORY-DEFAULT AUDIO SMOKE TEST (shipping build, PX3_DIAGNOSTICS=%d)\n",
                (int) PX3_DIAGNOSTICS);
    std::printf("  no parameters are set: this is exactly what a freshly loaded plugin does\n\n");

    auto failures = 0;
    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    const int blocks[] = { 64, 512 };

    for (const auto rate : rates)
    {
        for (const auto block : blocks)
        {
            const auto r = renderDefaultNote(rate, block);
            const auto ok = r.peak > 1.0e-4 && !r.sawNonFinite;
            if (!ok) ++failures;
            std::printf("  %6.0f Hz / %4d samples : peak %.6f  rms %.6f  %s%s\n",
                        rate, block, r.peak, r.rms,
                        ok ? "AUDIBLE" : "*** SILENT ***",
                        r.sawNonFinite ? "  NON-FINITE!" : "");
            std::fflush(stdout);
        }
    }

    std::printf("\n  %d failure(s)\n", failures);
    return failures;
}
