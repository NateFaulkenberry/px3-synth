#pragma once

#include <JuceHeader.h>

namespace px3::ui
{

// The power toggle used on every section of the plugin.
//
// It draws the standard power symbol - IEC 60417-5009: a ring broken at the top
// with a vertical stem rising through the gap. That is built here as a Path
// rather than imported as SVG artwork: the symbol is two primitives, so drawing
// it costs less than parsing it, it stays crisp at any size, and it can be
// tinted per component without a second asset.
//
// It derives from juce::ToggleButton so that every existing parameter
// attachment and every `juce::ToggleButton&` this plugin already passes around
// keeps working untouched.
//
// Toggle state is ENABLED, not bypassed: on means powered, and the glyph lights
// up. The class is named for what the control does to the section, not for the
// polarity of the parameter behind it.
class BypassButton final : public juce::ToggleButton
{
public:
    BypassButton();

    // Tints the lit glyph. Each card passes its own identity colour.
    void setAccentColour(juce::Colour colour);

    // The section this powers, e.g. "Sub Osc". Used only for the hover text.
    void setSectionName(juce::String name);

    // State-dependent, so the hover text says what a click will DO rather than
    // what the control currently is.
    juce::String getTooltip() override;

private:
    void paintButton(juce::Graphics& g,
                     bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override;

    // The ring is broken at the top: this is the half-angle of that gap, in
    // radians, measured from vertical.
    static constexpr float kGapHalfAngle = 0.62f;
    // Stem and ring as fractions of the glyph box, so the proportions hold at
    // any button size.
    static constexpr float kStemTop = 0.06f;
    static constexpr float kStemBottom = 0.46f;
    static constexpr float kRingInset = 0.14f;

    juce::Colour accent { juce::Colour::fromRGB(120, 200, 255) };
    juce::String sectionName;
};

// True when a mouse-up on a card's own background should toggle that card's
// power.
//
// A click that lands on a control never gets here - JUCE delivers it to the
// child, and only what the child does not want reaches the card. A DISABLED
// child does pass its clicks through, which is deliberate: on a bypassed card
// every control is greyed out, so clicking anywhere brings the section back.
//
// Drags are excluded so that dragging a knob past the edge of its cell, or
// dragging an FX card to reorder it, cannot toggle anything.
bool isCardBackgroundToggleClick(const juce::MouseEvent& event);

} // namespace px3::ui
