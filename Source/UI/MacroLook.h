#pragma once

#include <JuceHeader.h>

#include "UIConfig.h"

namespace px3::ui
{

// The macro colour language, in one place, so the strip and the knob
// look-and-feel cannot drift apart.
//
// Deliberately NOT purple. Purple is the modulation family's colour throughout
// this UI - the LFO and envelope cards, their rings, their accents - and a
// macro is a different kind of thing; sharing the colour made a macro-driven
// knob read as a modulated one. Deliberately not amber either, which is MIDI.
//
// Teal is unused anywhere else: the palette runs blue (OSC), red (FILTER),
// green (AMP), purple (LFO/ENV) and amber (MIDI).
inline juce::Colour macroAccentColour(const UIConfig* config)
{
    const auto fallback = juce::Colour::fromRGB(34, 214, 200);
    return config != nullptr ? config->getColour("macro.colors.accent", fallback) : fallback;
}

// The label drawn INSIDE a destination knob.
//
// A knob is a busy, mostly dark object with a moving ring and a pointer over
// it. Coloured text on that reads as one more piece of the knob; a translucent
// light plate with dark text on it reads as a tag stuck on top, which is what
// it is. Dark enough to stay legible against the plate at 8 px.
inline juce::Colour macroLabelBackgroundColour(const UIConfig* config)
{
    const auto fallback = juce::Colour::fromRGBA(226, 249, 246, 219);
    return config != nullptr
             ? config->getColour("macro.colors.labelBackground", fallback)
             : fallback;
}

inline juce::Colour macroLabelTextColour(const UIConfig* config)
{
    const auto fallback = juce::Colour::fromRGB(12, 46, 43);
    return config != nullptr ? config->getColour("macro.colors.labelText", fallback) : fallback;
}

// The pointer cut into the macro knob's cap.
//
// Dark rather than accent-coloured: the cap is off-white, so a teal tick on it
// competes with the lit holes around the bezel, which are what actually read
// the value. The tick says WHERE the knob is pointing; the holes say how far it
// has come.
inline juce::Colour macroPointerColour(const UIConfig* config)
{
    const auto fallback = juce::Colour::fromRGB(51, 51, 51);
    return config != nullptr ? config->getColour("macro.colors.pointer", fallback) : fallback;
}

// The same tick when the knob is disabled.
inline juce::Colour macroPointerDisabledColour(const UIConfig* config)
{
    const auto fallback = juce::Colour::fromRGB(150, 150, 154);
    return config != nullptr
             ? config->getColour("macro.colors.pointerDisabled", fallback)
             : fallback;
}

} // namespace px3::ui
