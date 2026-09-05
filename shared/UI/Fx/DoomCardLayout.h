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
    card.addToggleRow({ { "alt", "ALT", "MAIN", "Show the knobs' alternate functions" },
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
    card.addKnobRow({ { "wetTime", "TIME", "Wet channel time: decay, delay or lag",
                        "cross", "CROSS", "Signal-dependent interference between the channels" },
                      { "wetModify", "MODIFY", "Wet character: synthetic, repeats or voices",
                        "eq", "EQ", "Global tilt: left removes highs, right removes lows" } });

    // The micro-looper's pair.
    card.addKnobRow({ { "loopLength", "LENGTH", "Micro-looper length or pace",
                        "fade", "FADE", "How much of the loop survives each lap while overdubbing" },
                      { "loopModify", "MODIFY", "Loop character: fills, station or threshold",
                        "blend", "BLEND", "Clean micro-loop blended past the wet channel" } });

    // The machine's pair, and the two that are not on the pedal's face.
    card.addKnobRow({ { "clock", "CLOCK", "Engine sample rate: loop length, pitch and wet time at once",
                        "glue", "GLUE", "End of chain saturator, then destroyer" },
                      { "overdub", "OVERDUB", "Record onto the micro-loop" },
                      { "spread", "SPREAD", "Stereo processing depth" } });

    card.addFeatureKnobRow({ "mix", "MIX", "How much DOOM you hear",
                             "balance", "BALANCE", "Micro-looper against wet channel" });
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
