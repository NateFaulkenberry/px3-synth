#pragma once

#include <JuceHeader.h>

#include <utility>
#include <vector>

namespace px3::presets
{

// The shipped factory library.
//
// Bump this whenever the definitions below change. The library is rewritten on
// the next launch when the installed version does not match, which is the only
// way a refreshed preset reaches someone who has already run the plugin once -
// the writer skips files that already exist. User presets live in a separate
// root and are never touched by this.
inline constexpr int kFactoryLibraryVersion = 2;

struct FactoryPreset
{
    const char* name;
    const char* category;
    const char* author;
    const char* description;

    // Values are in each parameter's OWN units - seconds, hertz, semitones,
    // decibels, a 0..1 amount, or a choice INDEX - not normalised. Several
    // ranges are skewed, so a hand-written normalised value is a guess;
    // converting through the parameter itself is not.
    std::vector<std::pair<const char*, float>> params;
};

std::vector<FactoryPreset> factoryPresets();

} // namespace px3::presets
