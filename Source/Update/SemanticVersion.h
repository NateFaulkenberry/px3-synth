#pragma once

#include <JuceHeader.h>

namespace px3::update
{

// A version, compared as numbers rather than as text.
//
// The whole reason this type exists is that "1.10.0" < "1.9.0" as a string and
// the opposite as a version. Comparing versions as strings is the classic way
// to ship an updater that stops offering updates at the tenth minor release.
//
// The project's own versions are SemVer MAJOR.MINOR.PATCH - CMakeLists refuses
// anything else - so that is what this parses. A leading "v" is accepted
// because that is how a git tag and a GitHub release name usually spell it.
// A pre-release suffix is parsed and REMEMBERED rather than discarded, because
// an updater that treats 1.6.0-beta1 as 1.6.0 will offer a stable user a
// pre-release and then tell them they are up to date.
struct SemanticVersion
{
    int major { 0 };
    int minor { 0 };
    int patch { 0 };
    // Empty for a release. Present for anything with a "-suffix".
    juce::String preRelease;

    bool isValid { false };

    // Accepts "1.6.0", "v1.6.0", "1.6.0-beta.1". Returns an invalid version
    // for anything else, which callers must check rather than compare - an
    // invalid version compares equal to everything and would silently mean
    // "no update".
    static SemanticVersion parse(const juce::String& text);

    bool isPreRelease() const noexcept { return preRelease.isNotEmpty(); }

    juce::String toString() const;

    // Ordering, by SemVer's rule: numbers first, and a pre-release sorts
    // BELOW the release it leads to, so 1.6.0-beta < 1.6.0.
    int compare(const SemanticVersion& other) const noexcept;

    bool operator<(const SemanticVersion& o) const noexcept  { return compare(o) < 0; }
    bool operator>(const SemanticVersion& o) const noexcept  { return compare(o) > 0; }
    bool operator==(const SemanticVersion& o) const noexcept { return compare(o) == 0; }
    bool operator!=(const SemanticVersion& o) const noexcept { return compare(o) != 0; }
    bool operator<=(const SemanticVersion& o) const noexcept { return compare(o) <= 0; }
    bool operator>=(const SemanticVersion& o) const noexcept { return compare(o) >= 0; }
};

} // namespace px3::update
