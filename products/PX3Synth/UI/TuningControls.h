#pragma once

#include <JuceHeader.h>

#include "ChipLabel.h"
#include "MixerControls.h"

#include <array>
#include <memory>

// One oscillator's static tuning controls: Coarse Tune in octaves and Fine Tune
// in cents, each with its caption and a readout in its own units.
//
// Owned by the editor like every other parameter control, and laid out side by
// side by the card - the main oscillator cards and the sub card use the same
// pair, which is what keeps their tuning presented the same way.
struct TuningControls
{
    PanKnob coarseKnob;
    px3::ui::ChipLabel coarseLabel;
    juce::Label coarseValue;

    PanKnob fineKnob;
    px3::ui::ChipLabel fineLabel;
    juce::Label fineValue;
    PanKnob semitoneKnob;
    px3::ui::ChipLabel semitoneLabel;
    juce::Label semitoneValue;
    std::unique_ptr<juce::SliderParameterAttachment> semitoneAttachment;

    void bindSemitone(juce::AudioParameterFloat& parameter)
    {
        semitoneKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        semitoneKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        semitoneKnob.setLookAndFeel(&coarseKnob.getLookAndFeel());
        semitoneKnob.setDoubleClickReturnValue(true, 0.0);
        semitoneLabel.setText("SEMI", juce::dontSendNotification);
        semitoneLabel.setJustificationType(juce::Justification::centred);
        semitoneLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe8e8e8));
        semitoneLabel.setFont(juce::FontOptions(11.0f));
        semitoneValue.setJustificationType(juce::Justification::centred);
        semitoneValue.setColour(juce::Label::textColourId, juce::Colour(0xffdadada));
        semitoneValue.setFont(juce::FontOptions(11.0f));
        semitoneKnob.onValueChange = [this]
        {
            const auto value = juce::roundToInt(semitoneKnob.getValue());
            semitoneValue.setText((value > 0 ? "+" : "") + juce::String(value) + " st", juce::dontSendNotification);
        };
        semitoneAttachment = std::make_unique<juce::SliderParameterAttachment>(parameter, semitoneKnob, nullptr);
        semitoneKnob.onValueChange();
    }

    std::array<juce::Component*, 9> components()
    {
        return { { &coarseKnob, &coarseLabel, &coarseValue, &fineKnob, &fineLabel, &fineValue,
                    &semitoneKnob, &semitoneLabel, &semitoneValue } };
    }

    void setEnabled(bool enabled)
    {
        for (auto* component : components())
        {
            component->setEnabled(enabled);
        }
    }
};
