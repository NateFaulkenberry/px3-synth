#include "PluginProcessor.h"

// Alone in its own file: see products/PX3Delay/PluginEntry.cpp.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PX3MoodAudioProcessor();
}
