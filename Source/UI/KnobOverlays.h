#pragma once

#include <JuceHeader.h>

#include "../DSP/MidiMapping.h"

namespace px3::ui
{

// The MIDI-mapping and macro indicators a knob carries, drawn from the slider's
// own property bag.
//
// These live apart from any one look-and-feel because two of them now draw
// knobs - the synth's dark rotary and the pale macro knob - and the indicators
// have to be identical on both. A knob that is macro-driven, CC-mapped or armed
// for either gesture must say so the same way wherever it sits; duplicating
// this into a second look-and-feel is how the two would quietly diverge.
struct KnobOverlayColours
{
    juce::Colour macroAccent { juce::Colour::fromRGB(34, 214, 200) };
    juce::Colour macroLabelBackground { juce::Colour::fromRGBA(226, 249, 246, 219) };
    juce::Colour macroLabelText { juce::Colour::fromRGB(12, 46, 43) };
};

// `bounds` is the knob's own circle - the indicators are placed relative to it,
// not to the component, so a look-and-feel that insets its knob still gets them
// in the right place.
// `paleSubstrate` says the knob under these indicators is light rather than
// dark. Only the CC text needs it: the bright amber that reads on a dark knob
// is nearly invisible on a white one. Everything else already carries its own
// plate or sits outside the knob.
void drawKnobOverlays(juce::Graphics& g,
                      juce::Rectangle<float> bounds,
                      const juce::Slider& slider,
                      bool renderGrayscale,
                      const KnobOverlayColours& colours,
                      bool paleSubstrate = false);

} // namespace px3::ui
