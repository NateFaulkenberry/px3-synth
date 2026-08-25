#pragma once

#include <JuceHeader.h>

class VibeComponent final : public juce::Component
{
public:
    VibeComponent(juce::ToggleButton& enabledButtonIn,
                    juce::Slider& amountKnobIn,
                    juce::Label& amountLabelIn,
                    juce::ComboBox& typeBoxIn,
                    juce::Label& typeLabelIn,
                    juce::Colour accentIn);

    void setAccentColour(juce::Colour accentIn);
    void setActive(bool enabled);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    juce::ToggleButton& enabledButton;
    juce::Slider& amountKnob;
    juce::Label& amountLabel;
    juce::ComboBox& typeBox;
    juce::Label& typeLabel;
    juce::Colour accent;
    bool isActive { true };
};
