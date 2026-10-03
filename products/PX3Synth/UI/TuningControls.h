#pragma once

#include <JuceHeader.h>

#include "ChipLabel.h"
#include "MixerControls.h"

#include <array>

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

    // SLOP (voice.oscN.tuning.slop): the per-voice analog drift, 0..+-12 ct.
    // Oscillators 1-3 only, so it is not part of components(): the sub card
    // shares this struct and has no slop.
    juce::Slider slopKnob;
    px3::ui::ChipLabel slopLabel;
    juce::Label slopValue;

    void configureSlop(juce::LookAndFeel& look)
    {
        slopKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slopKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slopKnob.setLookAndFeel(&look);
        slopKnob.setDoubleClickReturnValue(true, 0.0);
        slopKnob.setTooltip("Slop: each voice drifts on its own, up to 12 cents");
        slopLabel.setText("SLOP", juce::dontSendNotification);
        slopLabel.setJustificationType(juce::Justification::centred);
        slopLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe8e8e8));
        slopLabel.setFont(juce::FontOptions(11.0f));
        slopLabel.setInterceptsMouseClicks(false, false);
        slopValue.setJustificationType(juce::Justification::centred);
        slopValue.setColour(juce::Label::textColourId, juce::Colour(0xffdadada));
        slopValue.setFont(juce::FontOptions(11.0f));
        slopValue.setInterceptsMouseClicks(false, false);
        slopKnob.onValueChange = [this]
        {
            const auto cents = slopKnob.getValue() * 12.0;
            slopValue.setText(cents < 0.05 ? juce::String("0 ct") : juce::String::charToString(0x00b1) + juce::String(cents, 1) + " ct",
                              juce::dontSendNotification);
        };
        slopKnob.onValueChange();
    }

    void configureSemitoneReadout()
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
