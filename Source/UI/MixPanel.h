#pragma once

#include <JuceHeader.h>

class MixPanel final : public juce::Component
{
public:
    explicit MixPanel(juce::Colour panelAccent);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void refreshFromParameters(bool enabled, int octaveIndex, int waveformIndex);
    void advanceAnimation(float deltaPhase);

private:
    juce::String title { "MIX" };
    juce::Colour accent;
};
