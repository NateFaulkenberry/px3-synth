#pragma once

#include <JuceHeader.h>

namespace px3::version
{
inline juce::String string()
{
    return juce::String(ProjectInfo::versionString);
}

inline int major()
{
    return static_cast<int>(ProjectInfo::versionNumber >> 16);
}

inline int minor()
{
    return static_cast<int>((ProjectInfo::versionNumber >> 8) & 0xFF);
}

inline int patch()
{
    return static_cast<int>(ProjectInfo::versionNumber & 0xFF);
}
} // namespace px3::version
