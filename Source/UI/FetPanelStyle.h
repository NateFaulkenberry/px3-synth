#pragma once

#include <JuceHeader.h>

namespace px3::ui
{

// The look of a silver-face limiter's front panel.
//
// This is a GENERIC archetype of a rack-mount FET limiter - the arrangement
// every unit of that era shares - and not a reproduction of any manufacturer's
// panel. No maker's name, model number or logo appears anywhere in it, and the
// meter face carries this plug-in's own identity. Same rule the AnalogEngine's
// console profiles follow.
//
// See docs/V3_1_EQ_COMP_RESEARCH.md for the circuit these controls belong to.

// A large black control knob with a white indicator, on a chromed collar. The
// numbered scale around it is engraved on the PANEL rather than printed on the
// cap, which is what the hardware does and the reason the numbers do not turn.
class FetKnobLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPosProportional,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override;
};

// The vertical bank of latching push buttons - ratio on the original, and here
// too. Light caps, dark legends, and a pressed cap sits lower and darker.
class FetPushButtonLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    void drawButtonText(juce::Graphics& g,
                        juce::TextButton& button,
                        bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;
};

// Panel furniture, drawn straight onto the silver.
namespace panel
{
// A slotted screw in a countersunk well, as the rack ears carry.
void drawScrew(juce::Graphics& g, juce::Point<float> centre, float radius);

// The engraved arc of numbers around a large knob. `marks` are drawn evenly
// across the knob's rotary sweep, which is how a panel scale is laid out.
void drawKnobScale(juce::Graphics& g,
                   juce::Rectangle<float> knobBounds,
                   const juce::StringArray& marks,
                   juce::Colour ink);

// Small engraved lettering, the way a panel is legended: dark, spaced, and
// under the control it names.
void drawLegend(juce::Graphics& g,
                juce::Rectangle<float> area,
                const juce::String& text,
                juce::Colour ink,
                float fontSize,
                juce::Justification justification = juce::Justification::centred);
} // namespace panel

} // namespace px3::ui
