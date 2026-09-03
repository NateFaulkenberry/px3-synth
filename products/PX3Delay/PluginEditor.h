#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "DelayComponent.h"
#include "UIConfig.h"
#include "KnobLookAndFeel.h"

#include <memory>
#include <vector>

// PX3 Delay's window.
//
// The controls are owned here and handed to the SHARED DelayComponent, which
// is the same component the Synth's FX panel uses - so the standalone looks
// like the delay inside the Synth because it IS the delay inside the Synth,
// laid out by the same code. Nothing about the panel is duplicated.
//
// The Synth's FX panel adds things that only make sense in a synth - card
// chrome, chain ordering, macro assignment, modulation rings - and none of
// that is here.
class PX3DelayAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PX3DelayAudioProcessorEditor(PX3DelayAudioProcessor& processorIn);
    ~PX3DelayAudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    //---- for the tests ----------------------------------------------------
    DelayComponent& debugPanel() { return panel; }

private:
    void attach(juce::RangedAudioParameter& parameter, juce::Slider& slider);
    void attach(juce::RangedAudioParameter& parameter, juce::ComboBox& box);
    void attach(juce::RangedAudioParameter& parameter, juce::Button& button);

    PX3DelayAudioProcessor& processor;
    std::shared_ptr<const UIConfig> uiConfig;

    juce::ToggleButton enabledButton;
    juce::Slider amountKnob { juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox };
    juce::Label amountLabel;
    juce::ComboBox algorithmBox;
    juce::Label algorithmLabel;
    juce::ComboBox syncBox;
    juce::Label syncLabel;
    juce::ComboBox modeBox;
    juce::Label modeLabel;
    juce::Slider timeKnob { juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox };
    juce::Label timeLabel;
    juce::Slider feedbackKnob { juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox };
    juce::Label feedbackLabel;

    // The ecosystem's knob, not JUCE's default rotary.
    px3::ui::KnobLookAndFeel knobLook;

    DelayComponent panel;

    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> boxAttachments;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PX3DelayAudioProcessorEditor)
};
