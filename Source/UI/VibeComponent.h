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
    void paint(juce::Graphics& g) override;

private:
    juce::ToggleButton& enabledButton;
    juce::Slider& amountKnob;
    juce::ComboBox& typeBox;
    juce::Label& typeLabel;
    px3::ui::CardHost card;
    px3::ui::CardInner inner;
    // Where paint() draws the "ON" text. It sits beside the bypass button
    // in row 1, so the layout decides it - it used to be an absolute rect
    // from UIConfig, which would have left the word behind when the button
    // moved into the row.
    juce::Rectangle<int> onLabelBounds;
    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;
    bool isActive { true };
};
