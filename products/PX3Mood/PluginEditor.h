#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "MoodComponent.h"
#include "ChipLabel.h"
#include "ToggleChipButton.h"
#include "BypassButton.h"
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

    // The Synth's power glyph, not a stock ToggleButton. The shared panel takes
    // a ToggleButton& and draws whatever it is handed, so a plain one renders
    // as a system checkbox in the corner of a card.
    px3::ui::BypassButton enabledButton;
    // The chip the Synth uses, not a stock ToggleButton: MoodComponent takes a
    // ToggleButton& and draws whatever it is given, so a plain one renders as
    // a system checkbox next to knobs that are anything but.
    px3::ui::ToggleChipButton freezeButton;
    juce::Slider mixKnob { juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider clockKnob { juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider wetTimeKnob { juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider wetModifyKnob { juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider loopLengthKnob { juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider loopModifyKnob { juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider feedbackKnob { juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider spreadKnob { juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider degradeKnob { juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox };
    // ChipLabel, the same type the Synth passes this component: a plain
    // juce::Label draws bare text with no chip behind it, which over artwork is
    // not a caption so much as a rumour of one.
    px3::ui::ChipLabel mixLabel, clockLabel, wetTimeLabel, wetModifyLabel, loopLengthLabel,
                       loopModifyLabel, feedbackLabel, spreadLabel, degradeLabel,
                       routingLabel, wetModeLabel, loopModeLabel;
    juce::ComboBox routingBox, wetModeBox, loopModeBox;

    MoodComponent panel;

    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> boxAttachments;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PX3MoodAudioProcessorEditor)
};
