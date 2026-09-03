#include "SemanticVersion.h"

namespace px3::update
{

SemanticVersion SemanticVersion::parse(const juce::String& text)
{
    SemanticVersion result;

    auto trimmed = text.trim();
    if (trimmed.startsWithIgnoreCase("v")) { trimmed = trimmed.substring(1); }
    if (trimmed.isEmpty()) { return result; }

    // Build metadata ("+sha") is ignored for ordering, which is what SemVer
    // says: it identifies a build, not a version.
    trimmed = trimmed.upToFirstOccurrenceOf("+", false, false);

    const auto core = trimmed.upToFirstOccurrenceOf("-", false, false);
    const auto suffix = trimmed.fromFirstOccurrenceOf("-", false, false);

    juce::StringArray parts;
    parts.addTokens(core, ".", {});
    if (parts.size() != 3) { return result; }

    for (const auto& part : parts)
    {
        // containsOnly rather than getIntValue, because getIntValue turns
        // "1.x.0" into 1.0.0 rather than rejecting it - which would make a
        // malformed release look like a real one.
        if (part.isEmpty() || ! part.containsOnly("0123456789")) { return result; }
    }

    result.major = parts[0].getIntValue();
    result.minor = parts[1].getIntValue();
    result.patch = parts[2].getIntValue();
    result.preRelease = suffix;
    result.isValid = true;
    return result;
}

juce::String SemanticVersion::toString() const
{
    if (! isValid) { return {}; }

    auto text = juce::String(major) + "." + juce::String(minor) + "." + juce::String(patch);
    if (preRelease.isNotEmpty()) { text += "-" + preRelease; }
    return text;
}

int SemanticVersion::compare(const SemanticVersion& other) const noexcept
{
    if (major != other.major) { return major < other.major ? -1 : 1; }
    if (minor != other.minor) { return minor < other.minor ? -1 : 1; }
    if (patch != other.patch) { return patch < other.patch ? -1 : 1; }

    // Same numbers: a pre-release is BELOW the release of the same number.
    const auto mine = isPreRelease();
    const auto theirs = other.isPreRelease();
    if (mine != theirs) { return mine ? -1 : 1; }
    if (! mine) { return 0; }

    return preRelease.compare(other.preRelease) < 0 ? -1
         : (preRelease == other.preRelease ? 0 : 1);
}

} // namespace px3::update
