#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "MoodComponent.h"
#include "KnobLookAndFeel.h"
#include "UIConfig.h"

#include <memory>
#include <vector>

// PX3 Mood's window: the controls, handed to the SHARED MoodComponent that
// the Synth's FX panel uses. Same pedal, same layout, same knobs.
class PX3MoodAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PX3MoodAudioProcessorEditor(PX3MoodAudioProcessor& processorIn);
    ~PX3MoodAudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    MoodComponent& debugPanel() { return panel; }

private:
    PX3MoodAudioProcessor& processor;
    std::shared_ptr<const UIConfig> uiConfig;
    px3::ui::KnobLookAndFeel knobLook;

    juce::ToggleButton enabledButton, freezeButton;
    juce::Slider mixKnob { juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider clockKnob { juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider wetTimeKnob { juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider wetModifyKnob { juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider loopLengthKnob { juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider loopModifyKnob { juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider feedbackKnob { juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider spreadKnob { juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider degradeKnob { juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox };
    juce::Label mixLabel, clockLabel, wetTimeLabel, wetModifyLabel, loopLengthLabel,
                loopModifyLabel, feedbackLabel, spreadLabel, degradeLabel,
                routingLabel, wetModeLabel, loopModeLabel;
    juce::ComboBox routingBox, wetModeBox, loopModeBox;

    MoodComponent panel;

    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> boxAttachments;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PX3MoodAudioProcessorEditor)
};
