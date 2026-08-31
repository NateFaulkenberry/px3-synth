#pragma once

#include "Wavetable.h"

namespace px3
{

// Where imported wavetables live between sessions.
//
// A preset stores the NAME of a user table, not the table itself. Embedding a
// 2.25 MB pyramid - or even the 512 KB of frames it is built from - in every
// preset that uses it would make presets enormous and would store the same
// table once per preset. The cost of the reference is that a preset can outlive
// the table it names, which is handled by saying so rather than by failing.
//
// What is stored is the FRAME SPECTRA, not the built pyramid: the pyramid is
// derived, rebuilding it takes milliseconds, and its layout is an
// implementation detail that has already changed once during development.
class WavetableLibrary
{
public:
    static juce::File userDirectory();

    // Message thread. Overwrites a table of the same name.
    static bool save(const juce::String& name,
                     const std::vector<FrameSpectrum>& frames,
                     juce::String& error);

    static juce::StringArray userTableNames();

    // Null if it is not there. The caller decides what to do about that; this
    // does not silently substitute something else.
    static std::shared_ptr<const Wavetable> load(const juce::String& name);

    static bool remove(const juce::String& name);

    static constexpr const char* kFileExtension = ".px3wt";

private:
    // Bumped if the layout below changes. An unreadable file is reported, not
    // guessed at.
    static constexpr int kFormatVersion = 1;
};

} // namespace px3
