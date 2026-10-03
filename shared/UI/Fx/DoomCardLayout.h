#pragma once

#include "FxCardComponent.h"

#include <JuceHeader.h>

namespace px3::ui::doomLayout
{

// DOOM's card layout, declared ONCE and shared by the Synth's card and the
// standalone plug-in, the same way LUCY's is. The two are compared control by
// control by FxProducts_AStandaloneCardMatchesTheSynthsCardExactly, and two
// copies of a twelve-knob declaration that have to be edited together is not a
// realistic thing to maintain.
//
// Six primary knobs, each carrying a second function, as the pedal DOOM takes
// its control philosophy from prints them:
//
//   TIME        / CROSS      what the wet channel does with time
//   WET MODIFY  / EQ         what kind of wet thing it is
//   LENGTH      / FADE       how the loop behaves
//   LOOP MODIFY / BLEND      how the loop transforms
//   CLOCK       / GLUE       how fast and how degraded the whole machine is
//   MIX         / BALANCE    how much DOOM you hear
//
// The ALT switch selects which of a pair is displayed. Both are real
// parameters, attached and automatable whichever way it is set.

inline void declareRows(FxCardComponent& card,
                        const juce::StringArray& wetModeChoices,
                        const juce::StringArray& loopModeChoices,
                        const juce::StringArray& routingChoices)
{
    card.addToggleRow({ { "alt", "SHIFT", "MAIN", "Show each knob's second function (printed below it on the pedal)" },
                        { "loopActive", "LOOPER", "LISTEN",
                          "Play the captured micro-loop, or keep listening" },
                        { "wetActive", "WET ON", "WET OFF", "Engage the wet channel" },
                        { "freeze", "FROZEN", "FREEZE", "Freeze the wet channel and repeat it" },
                        { "loopHalf", "HALF", "FULL", "Halve the micro-loop length" },
                        { "clockSmooth", "SMOOTH", "STEPPED",
                          "Sweep the clock continuously instead of in harmonised steps" },
                        { "crossSource", "CROSS: CHAN", "CROSS: INPUT",
                          "Modulate from your playing, or let each channel modulate the other" } });

    card.addChoiceRow({ { "wetMode", "WET", "What the wet channel does", wetModeChoices },
                        { "routing", "ROUTE", "What the wet channel is fed", routingChoices },
                        { "loopMode", "LOOP", "What the micro-looper does", loopModeChoices } });

    // The wet channel's pair.
    card.addKnobRow({ { "wetTime", "WET TIME", "Wet channel time: reverb decay, delay time or slip lag, depending on WET mode",
                        "cross", "INTERFERE", "Cross-modulation: how much one channel (or your input) disturbs the other. Source set by the CROSS switch" },
                      { "wetModify", "WET CHAR", "Wet character. REVERB: synthetic to natural; DELAY: number of repeats; SLIP: number of voices",
                        "eq", "TILT", "Output tilt EQ: left darkens (removes highs), right thins (removes lows)" } });

    // The micro-looper's pair.
    card.addKnobRow({ { "loopLength", "LOOP LEN", "Micro-looper length (or pace, depending on LOOP mode)",
                        "fade", "DECAY", "Overdub decay: how much of the loop survives each lap" },
                      { "loopModify", "LOOP CHAR", "Loop character. ENV: trigger threshold; TAPE: fills; STRETCH: station",
                        "blend", "DRY LOOP", "How much of the clean micro-loop bypasses the wet channel" } });

    // The machine's pair, and the two that are not on the pedal's face.
    card.addKnobRow({ { "clock", "CLOCK", "Engine sample rate: loop length, pitch and wet time change together, like a sampler's clock",
                        "glue", "DRIVE", "Output saturation, level-matched; folds and crushes near the top" },
                      { "overdub", "OVERDUB", "Record your playing onto the micro-loop" },
                      { "spread", "WIDTH", "Stereo processing depth: 0 is mono, full is wide" } });

    card.addFeatureKnobRow({ "mix", "MIX", "Dry against DOOM",
                             "balance", "LOOP/WET", "Balance between the micro-looper and the wet channel" });
}

inline void wireAltSwitch(FxCardComponent& card)
{
    if (auto* alt = card.toggle("alt"))
    {
        alt->onClick = [&card, alt] { card.setAltMode(alt->getToggleState()); };
    }

    card.setAltMode(false);
}

} // namespace px3::ui::doomLayout
