#pragma once

#include "FxCardComponent.h"

#include <JuceHeader.h>

namespace px3::ui::lucyLayout
{

// LUCY's card layout, declared ONCE.
//
// The Synth's card and the standalone plug-in are required to be the same card
// - FxProducts_AStandaloneCardMatchesTheSynthsCardExactly compares them control
// by control - and the two used to hold two copies of this declaration that had
// to be edited together. With twelve paired knobs that stopped being realistic,
// so both call this and the parity is structural rather than a discipline.
//
// The arrangement follows the pedal LUCY takes its control philosophy from: six
// primary knobs each carrying a second function, three categorical switches,
// and a freeze control. GLOBAL keeps the large feature-knob position every PX3
// FX card gives its macro.

inline void declareRows(FxCardComponent& card,
                        const juce::StringArray& modeChoices,
                        const juce::StringArray& slopeChoices,
                        const juce::StringArray& packetChoices,
                        const juce::StringArray& weightingChoices,
                        const juce::StringArray& freezeChoices)
{
    // ALT is the only control here that is not a parameter. It selects which
    // function the six paired knobs are showing, which is a property of the
    // panel rather than of the sound - the alternate parameters are attached
    // and automatable whichever way it is set.
    card.addToggleRow({ { "alt", "SHIFT", "MAIN", "Show each knob's second function" },
                        { "gateOn", "GATE ON", "GATE OFF", "Silence anything below the threshold" },
                        { "verbPost", "VERB LAST", "VERB FIRST",
                          "Reverb placement: after the codec, or in front of it so the reverb is degraded too" },
                        { "filterInvert", "REJECT", "PASS",
                          "Keep the band, or keep everything but the band" },
                        { "slow", "SLOW ON", "SLOW OFF",
                          "Bigger, darker, slower, and with more latency" } });

    card.addChoiceRow({ { "mode", "MODE", "Type of degradation", modeChoices },
                        { "packets", "PACKETS", "Connection-style dropouts", packetChoices },
                        { "freeze", "FREEZE", "Capture the spectrum and hold or evolve it",
                          freezeChoices } });

    card.addChoiceRow({ { "slope", "FILTER SLOPE", "Band filter slope, in dB per octave", slopeChoices },
                        { "weighting", "PROTECT", "Which end of the spectrum the codec keeps intact when it drops bits",
                          weightingChoices } });

    // The six primaries, paired with their alternates. The pedal prints the
    // second function under the first; so does the card.
    card.addKnobRow({ { "filter", "BAND", "Band filter width; fully down is no filtering",
                        "gate", "GATE", "Gate threshold (when GATE is on)" },
                      { "verb", "VERB", "Reverb amount",
                        "decay", "VERB SIZE", "Reverb size and length" },
                      { "freq", "FREQ", "Band filter centre frequency",
                        "limiterThreshold", "LIMIT", "Output limiter threshold; lower means more limiting" } });

    card.addKnobRow({ { "speed", "SPEED", "How fast the loss, packets and freeze evolve",
                        "autoGain", "AUTO GAIN", "Gain compensation for the loss modes" },
                      { "loss", "CODEC LOSS", "How degraded the codec is, and how much of the spectrum it reaches",
                        "lossGain", "LOSS GAIN", "Wet gain, plus or minus 36 dB" },
                      { "spread", "WIDTH", "Stereo: packet alternation between sides and reverb width" } });

    card.addFeatureKnobRow({ "global", "AMOUNT", "How strongly the whole effect is expressed (0 = bypassed sound)",
                             "freezer", "FROZEN MIX", "Live signal against the frozen spectrum (when FREEZE is on)" });
}

// Connects the ALT switch to the card it belongs to.
//
// Separate from declareRows because the toggle only exists once the row has
// been added, and because the Synth and the standalone add their parameter
// attachments in between.
inline void wireAltSwitch(FxCardComponent& card)
{
    if (auto* alt = card.toggle("alt"))
    {
        alt->onClick = [&card, alt] { card.setAltMode(alt->getToggleState()); };
    }

    // Start on the primary functions, which is what the switch reads as OFF.
    card.setAltMode(false);
}

} // namespace px3::ui::lucyLayout
