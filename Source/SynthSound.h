#pragma once

#include <JuceHeader.h>

class SynthSound final : public juce::SynthesiserSound
{
public:
    bool appliesToNote(int) override;
    bool appliesToChannel(int) override;
};
