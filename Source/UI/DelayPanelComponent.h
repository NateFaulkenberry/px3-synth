#pragma once

#include <JuceHeader.h>

class DelayComponent final : public juce::Component
{
public:
    DelayComponent(juce::ToggleButton& enabledButtonIn,
                        juce::Slider& amountKnobIn,
                        juce::Label& amountLabelIn,
                        juce::ComboBox& algorithmBoxIn,
                        juce::Label& algorithmLabelIn,
                        juce::ComboBox& syncBoxIn,
                        juce::Label& syncLabelIn,
                        juce::ComboBox& modeBoxIn,
                        juce::Label& modeLabelIn,
                        juce::Slider& timeKnobIn,
                        juce::Label& timeLabelIn,
                        juce::Slider& feedbackKnobIn,
                        juce::Label& feedbackLabelIn,
                        juce::Colour accentIn);

    void setAccentColour(juce::Colour accentIn);
    void setActive(bool enabled, bool granularModeSelectable);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    juce::ToggleButton& enabledButton;
    juce::Slider& amountKnob;
    juce::Label& amountLabel;
    juce::ComboBox& algorithmBox;
    juce::Label& algorithmLabel;
    juce::ComboBox& syncBox;
    juce::Label& syncLabel;
    juce::ComboBox& modeBox;
    juce::Label& modeLabel;
    juce::Slider& timeKnob;
    juce::Label& timeLabel;
    juce::Slider& feedbackKnob;
    juce::Label& feedbackLabel;
    juce::Colour accent;
    bool isActive { true };
};
