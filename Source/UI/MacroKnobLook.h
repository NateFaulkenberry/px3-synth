#pragma once

#include <JuceHeader.h>

#include "KnobOverlays.h"

namespace px3::ui
{

// The macro knobs, and only the macro knobs.
//
// Everything else in this synth is a dark knob on a dark panel. The macros are
// a performance layer that sits outside the panels and stays put while they
// change, so they are drawn as a pale hardware knob instead: a light bezel
// carrying a ring of value dots, a raised off-white cap, and a thin accent arc
// showing the value. It reads as a different KIND of control at a glance, which
// is what it is.
//
// It draws the same MIDI and macro indicators every other knob has, through the
// shared drawKnobOverlays, so a macro knob mapped to a CC says so exactly the
// way a filter cutoff does.
class MacroKnobLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    // Handed in from UIConfig by the editor, like the main knob look's are: a
    // look-and-feel is shared by many knobs and has no config prefix of its own.
    KnobOverlayColours overlayColours;

    // The tick cut into the cap. Its own colour rather than the accent's,
    // because the cap is pale and the lit holes are already saying the value.
    juce::Colour pointerColour { juce::Colour::fromRGB(51, 51, 51) };
    juce::Colour pointerDisabledColour { juce::Colour::fromRGB(150, 150, 154) };

    void drawRotarySlider(juce::Graphics& g,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPos,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override;
};

} // namespace px3::ui
