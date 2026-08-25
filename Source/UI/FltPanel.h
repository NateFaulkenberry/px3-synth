#pragma once

#include <JuceHeader.h>

#include "FilterResponseComponent.h"

class FltPanel final : public juce::Component
{
public:
    FltPanel(juce::Slider& cutoffKnob,
             juce::Label& cutoffLabel,
             juce::Slider& resonanceKnob,
             juce::Label& resonanceLabel,
             juce::ComboBox& filterTypeBox,
             juce::AudioParameterFloat& cutoffParam,
             juce::AudioParameterFloat& resonanceParam,
             juce::AudioParameterChoice& filterTypeParam,
             juce::Colour panelAccent);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void refreshFromParameters();

private:
    juce::Slider& cutoffKnob;
    juce::Label& cutoffLabel;
    juce::Slider& resonanceKnob;
    juce::Label& resonanceLabel;
    juce::ComboBox& filterTypeBox;

    std::unique_ptr<FilterResponseComponent> filterResponseComponent;

    juce::String title { "FLT" };
    juce::Colour accent;
};
