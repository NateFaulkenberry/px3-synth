#pragma once

#include <JuceHeader.h>

#include "Card.h"
#include "CardInner.h"

#include <memory>

class UIConfig;

class VibeComponent final : public juce::Component
{
public:
    VibeComponent(juce::ToggleButton& enabledButtonIn,
                    juce::Slider& amountKnobIn,
                    juce::ComboBox& typeBoxIn,
                    juce::Label& typeLabelIn,
                    juce::Colour accentIn);

    void setAccentColour(juce::Colour accentIn);
    void setActive(bool enabled);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);

    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;
    void paint(juce::Graphics& g) override;

private:
    juce::ToggleButton& enabledButton;
    juce::Slider& amountKnob;
    juce::ComboBox& typeBox;
    juce::Label& typeLabel;
    px3::ui::CardHost card;
    px3::ui::CardInner inner;
    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;
    bool isActive { true };
};
