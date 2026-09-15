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

    std::array<juce::Component*, 6> components()
    {
        return { { &coarseKnob, &coarseLabel, &coarseValue, &fineKnob, &fineLabel, &fineValue } };
    }

    void setEnabled(bool enabled)
    {
        for (auto* component : components())
        {
            component->setEnabled(enabled);
        }
    }
};
