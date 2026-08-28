#pragma once

#include <JuceHeader.h>

#include "Card.h"

#include <memory>

class UIConfig;

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
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    juce::ToggleButton& enabledButton;
    juce::Slider& amountKnob;
    juce::Label& amountLabel;
    juce::ComboBox& typeBox;
    juce::Label& typeLabel;
    px3::ui::CardHost card;
    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;
    bool isActive { true };
};
