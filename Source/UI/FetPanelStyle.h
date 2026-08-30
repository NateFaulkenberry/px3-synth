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

// The geometry of a moving-coil meter's scale.
//
// Separated out and made a pure function of the face rectangle because getting
// it wrong is invisible until it is rendered: the first version derived the
// radius from the face's HEIGHT alone, so on a face wider than it was tall the
// 0 dB tick landed beyond the right edge and the needle's tail ran out of the
// bottom. The arc must fit the face it is drawn on, whatever shape that is.
struct VuArc
{
    juce::Point<float> pivot;
    float radius { 1.0f };
    float span { 0.6f };        // radians either side of vertical
    float fullScaleDb { 20.0f };

    // Gain reduction reads BACKWARDS: 0 at the right, and the needle falls to
    // the left as the unit works. That is why the meter "drops".
    float angleFor(float db) const;
    juce::Point<float> directionFor(float db) const;
    // A point on the scale at `db`, `fraction` of the way out from the pivot.
    juce::Point<float> pointFor(float db, float fraction) const;

    // Every extreme of the drawn scale, for a caller that wants to check it
    // lands where it should.
    juce::Rectangle<float> drawnBounds() const;
};

// Fits an arc to a meter face, leaving room for the tick labels inside it.
VuArc vuArcFor(juce::Rectangle<float> face);

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
