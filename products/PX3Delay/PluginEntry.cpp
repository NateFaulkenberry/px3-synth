#include "PluginProcessor.h"

// The plug-in entry point, alone in its own file.
//
// Every product defines createPluginFilter(), and the test binary compiles
// more than one product - so a definition sitting in PluginProcessor.cpp makes
// the tests fail to link the moment a second product exists. Keeping it here
// means the plug-in targets take this file and the test target does not, with
// no conditional compilation to get wrong.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PX3DelayAudioProcessor();
}
