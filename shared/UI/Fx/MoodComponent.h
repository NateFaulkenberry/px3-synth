#pragma once

#include <JuceHeader.h>

#include "Card.h"
#include "CardInner.h"

#include <memory>

class UIConfig;

class MoodComponent final : public juce::Component
{
public:
    MoodComponent(juce::ToggleButton& enabledButtonIn,
                  juce::ToggleButton& freezeButtonIn,
                  juce::Slider& mixKnobIn,
                  juce::Label& mixLabelIn,
                  juce::Slider& clockKnobIn,
                  juce::Label& clockLabelIn,
                  juce::Slider& wetTimeKnobIn,
                  juce::Label& wetTimeLabelIn,
                  juce::Slider& wetModifyKnobIn,
                  juce::Label& wetModifyLabelIn,
                  juce::Slider& loopLengthKnobIn,
                  juce::Label& loopLengthLabelIn,
                  juce::Slider& loopModifyKnobIn,
                  juce::Label& loopModifyLabelIn,
                  juce::Slider& feedbackKnobIn,
                  juce::Label& feedbackLabelIn,
                  juce::Slider& spreadKnobIn,
                  juce::Label& spreadLabelIn,
                  juce::Slider& degradeKnobIn,
                  juce::Label& degradeLabelIn,
                  juce::ComboBox& routingBoxIn,
                  juce::Label& routingLabelIn,
                  juce::ComboBox& wetModeBoxIn,
                  juce::Label& wetModeLabelIn,
                  juce::ComboBox& loopModeBoxIn,
                  juce::Label& loopModeLabelIn,
                  juce::Colour accentIn);

    void setAccentColour(juce::Colour accentIn);
    void setActive(bool enabled);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);

    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;
    void paint(juce::Graphics& g) override;

private:
    juce::ToggleButton& enabledButton;
    juce::ToggleButton& freezeButton;

    juce::Slider& mixKnob;
    juce::Label& mixLabel;
    juce::Slider& clockKnob;
    juce::Label& clockLabel;
    juce::Slider& wetTimeKnob;
    juce::Label& wetTimeLabel;
    juce::Slider& wetModifyKnob;
    juce::Label& wetModifyLabel;
    juce::Slider& loopLengthKnob;
    juce::Label& loopLengthLabel;
    juce::Slider& loopModifyKnob;
    juce::Label& loopModifyLabel;
    juce::Slider& feedbackKnob;
    juce::Label& feedbackLabel;
    juce::Slider& spreadKnob;
    juce::Label& spreadLabel;
    juce::Slider& degradeKnob;
    juce::Label& degradeLabel;

    juce::ComboBox& routingBox;
    juce::Label& routingLabel;
    juce::ComboBox& wetModeBox;
    juce::Label& wetModeLabel;
    juce::ComboBox& loopModeBox;
    juce::Label& loopModeLabel;

    px3::ui::CardHost card;
    px3::ui::CardInner inner;
    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;
    bool isActive { true };
};
