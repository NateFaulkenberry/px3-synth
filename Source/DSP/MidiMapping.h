#pragma once

#include <JuceHeader.h>

namespace px3
{

// One hardware control driving one or more synth parameters.
//
// Destinations are parameter IDs - the same strings createParameterStateTree
// serializes under - and never pointers. A mapping outlives the editor that
// made it, and must survive a parameter it names being removed from a later
// build, so it cannot hold anything that only exists while the UI is open.
//
// The struct exists rather than a bare pair of ints so that a per-destination
// range can be added later without changing the persistence shape or the apply
// path. Nothing here reads it yet.
struct MidiMapping
{
    // The identity. Matching is on the CC number alone; see below.
    int ccNumber { -1 };

    // The channel the mapping was TAUGHT on, recorded but not matched against.
    // A controller sending on channel 1 into a DAW routing on channel 2 would
    // silently do nothing under strict matching, and silence is the worst
    // failure mode for a feature whose appeal is that it just works. Keeping
    // the channel means turning strict matching on later is a behaviour
    // change rather than a data migration.
    int learnedChannel { 1 };

    juce::StringArray parameterIds;

    bool isValid() const noexcept
    {
        return ccNumber >= 0 && ccNumber <= 127 && ! parameterIds.isEmpty();
    }
};

// What the UI stamps on a knob so the rest of the system can find it.
//
// Every mappable knob is a plain juce::Slider bound by a
// SliderParameterAttachment, in six different files, with no common subclass
// and no single choke point. Rather than hard-coding a list of knobs or
// building a parallel registry, the parameter's own ID is stamped onto the
// slider at the moment it is attached - using Component::getProperties, which
// is where this UI already keeps per-knob metadata like "modulatedPos" and
// "knobBypassed".
//
// "Eligible for MIDI mapping" is then a property of the control rather than a
// list somebody has to remember to update.
namespace knob_properties
{
inline const juce::Identifier parameterId { "px3ParamId" };

// Drawn by the shared rotary look-and-feel: the CC this knob is mapped to
// (absent or -1 when unmapped), and whether it is currently selected for
// assignment.
inline const juce::Identifier midiCc { "px3MidiCc" };
inline const juce::Identifier midiSelected { "px3MidiSelected" };

// Macro state, drawn by the same look-and-feel: a bitmask of the macros
// driving this knob, and whether it can be clicked in the active assignment
// mode. Deliberately separate identifiers from the MIDI ones, so the two
// systems cannot end up drawing over each other.
inline const juce::Identifier macroMask { "px3MacroMask" };
inline const juce::Identifier macroAssignable { "px3MacroAssignable" };
} // namespace knob_properties

} // namespace px3
