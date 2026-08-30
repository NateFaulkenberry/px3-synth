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
// See docs/EQ_COMP_RESEARCH.md for the circuit these controls belong to.

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

    // A real VU movement is linear in AMPLITUDE, not in decibels. That is the
    // whole reason its scale looks the way it does: -20 is crammed against the
    // left stop while 0 to +3 spreads across the last third. Positions are
    // therefore taken as 0..1 across the sweep, and the caller converts.
    float angleForPosition(float position) const;
    juce::Point<float> directionForPosition(float position) const;
    juce::Point<float> pointForPosition(float position, float fraction) const;

    // Where a level sits on a VU face, with +3 dB at full scale.
    static float positionForLevelDb(float db);
    // Where a gain reduction sits: 0 dB at rest on the RIGHT, falling left as
    // the unit works, on the same amplitude-linear movement.
    static float positionForReductionDb(float db);

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

// The engraved arc of numbers around a knob. `marks` are spread evenly across
// the sweep, which is how a panel scale is laid out.
//
// The sweep is passed in rather than assumed: the scale is engraved on the
// PANEL and the pointer belongs to the knob, so if the two disagree by even a
// few degrees the numbers lie about the value. Callers pass the slider's own
// juce::Slider::getRotaryParameters().
void drawKnobScale(juce::Graphics& g,
                   juce::Rectangle<float> knobBounds,
                   const juce::StringArray& marks,
                   juce::Colour ink,
                   float startAngle,
                   float endAngle);

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
