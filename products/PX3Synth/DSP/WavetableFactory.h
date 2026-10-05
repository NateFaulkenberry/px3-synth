#pragma once

#include "Wavetable.h"

#include <functional>
#include <memory>
#include <vector>

namespace px3
{

// The shipped wavetables.
//
// Generated from recipes in our own source rather than shipped as binary blobs.
// That is partly licensing - a table we generated is unambiguously ours to
// ship, where a "free wavetable pack" found online usually is not - and partly
// that a recipe can be read, argued with and adjusted, which a megabyte of
// samples cannot.
//
// Every table has to EVOLVE. A table whose frames all sound alike is a waveform
// with extra steps: the scan is the instrument, and if moving it does nothing
// the table has failed however good any single frame sounds.
struct FactoryWavetable
{
    const char* name;
    const char* category;
    const char* description;

    // Builds the frames. Called off the audio thread.
    std::function<std::vector<FrameSpectrum>()> generate;
};

const std::vector<FactoryWavetable>& factoryWavetables();

// The shared, immutable table at this index - built on first use and reused by
// every oscillator of every instance in the process while any of them holds it.
// Null if the index is out of range. Message thread (or any non-audio thread).
std::shared_ptr<const Wavetable> buildFactoryWavetable(int index);

// Builds one by name, so a preset can name what it wants rather than pointing at
// a position in a list that later changes.
std::shared_ptr<const Wavetable> buildFactoryWavetable(const juce::String& name);

} // namespace px3
