#pragma once

#include <JuceHeader.h>

#include "SubOscComponent.h"

class MixPanel final : public juce::Component
{
public:
    MixPanel(juce::ToggleButton& subOscEnabledButton,
             juce::Label& subOscEnabledLabel,
             juce::Slider& subOscLevelKnob,
             juce::Label& subOscLevelLabel,
             juce::ComboBox& subOscOctaveBox,
             juce::Label& subOscOctaveLabel,
             juce::ComboBox& subOscWaveformBox,
             juce::Label& subOscWaveformLabel,
             juce::Colour panelAccent);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void refreshFromParameters(bool enabled, int octaveIndex, int waveformIndex);
    void advanceAnimation(float deltaPhase);

private:
    std::unique_ptr<SubOscComponent> subOscComponent;

    juce::String title { "MIX" };
    juce::Colour accent;
};
