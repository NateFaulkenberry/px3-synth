#include "MacroKnobLook.h"

namespace px3::ui
{

void MacroKnobLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                            int x,
                                            int y,
                                            int width,
                                            int height,
                                            float sliderPos,
                                            float rotaryStartAngle,
                                            float rotaryEndAngle,
                                            juce::Slider& slider)
{
    const auto fullBounds = juce::Rectangle<float>(static_cast<float>(x),
                                                   static_cast<float>(y),
                                                   static_cast<float>(width),
                                                   static_cast<float>(height));

    // Same 10 px inset the main knob look uses, so a macro knob occupies the
    // same share of its cell as any other knob of the same size.
    const auto diameter = juce::jmax(8.0f, juce::jmin(fullBounds.getWidth(),
                                                      fullBounds.getHeight()) - 10.0f);
    const auto bounds = juce::Rectangle<float>(diameter, diameter).withCentre(fullBounds.getCentre());
    const auto centre = bounds.getCentre();
    const auto radius = diameter * 0.5f;
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    const auto accent = overlayColours.macroAccent;
    const auto enabled = slider.isEnabled();

    // ---- shadow -------------------------------------------------------------
    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 92));
    g.fillEllipse(bounds.translated(0.0f, 2.4f).expanded(0.6f));

    // ---- bezel --------------------------------------------------------------
    // Lit from above, which is what makes a flat circle read as a disc.
    juce::ColourGradient bezel(juce::Colour::fromRGB(238, 238, 238), centre.x, bounds.getY(),
                               juce::Colour::fromRGB(198, 198, 200), centre.x, bounds.getBottom(),
                               false);
    g.setGradientFill(bezel);
    g.fillEllipse(bounds);

    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 40));
    g.drawEllipse(bounds.reduced(0.5f), 1.0f);

    // ---- the value dots -----------------------------------------------------
    //
    // The dots ARE the value indicator. They are drilled holes in the bezel:
    // grey and empty by default, and lit from behind up to the value, so the
    // reading is a ring of dots rather than a drawn arc.
    //
    // Every dot is drawn the same way whether lit or not - same recess, same
    // rim - and only what shows through the hole changes. That is what keeps
    // them reading as one row of holes with a light behind some of them,
    // instead of two different kinds of dot.
    constexpr int dotCount = 21;
    const auto dotRadius = juce::jmax(0.9f, radius * 0.042f);
    const auto dotRing = radius * 0.86f;
    const auto dotSize = dotRadius * 2.0f;

    // Clearly darker than the bezel around them (198-238), or the hole reads as
    // a ring drawn on the surface rather than a recess cut into it.
    const auto emptyTop = juce::Colour::fromRGB(112, 112, 117);
    const auto emptyBottom = juce::Colour::fromRGB(163, 163, 168);

    for (int i = 0; i < dotCount; ++i)
    {
        const auto t = static_cast<float>(i) / static_cast<float>(dotCount - 1);
        const auto dotAngle = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
        // Nothing lit at zero. Lighting the first dot there would be a level
        // meter's convention - always one LED on - but the brief for these is
        // that the holes are grey by DEFAULT and the light comes up through
        // them as the knob turns, so zero has to mean none.
        const auto lit = enabled && sliderPos > 1.0e-4f && t <= sliderPos + 1.0e-4f;

        const juce::Point<float> at(centre.x + std::sin(dotAngle) * dotRing,
                                    centre.y - std::cos(dotAngle) * dotRing);
        const auto hole = juce::Rectangle<float>(dotSize, dotSize).withCentre(at);

        // Light spilling out of a lit hole, onto the bezel around it.
        if (lit)
        {
            g.setColour(accent.withAlpha(0.30f));
            g.fillEllipse(juce::Rectangle<float>(dotSize * 2.1f, dotSize * 2.1f).withCentre(at));
        }

        // The catch light on the LOWER rim, which is what says "recess". A
        // raised stud takes its light on the upper rim; this is the same
        // gradient upside down, and the two read completely differently.
        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 190));
        g.fillEllipse(hole.translated(0.0f, juce::jmax(0.5f, dotRadius * 0.35f)));

        // What is behind the hole: the accent where the light is on, the dark
        // of an empty hole where it is not. Dark at the top, lighter at the
        // bottom - the inverse of the cap above it, because this is a dent and
        // that is a dome.
        juce::ColourGradient inside(lit ? accent.darker(0.35f) : emptyTop,
                                    at.x, hole.getY(),
                                    lit ? accent.brighter(0.30f) : emptyBottom,
                                    at.x, hole.getBottom(),
                                    false);
        g.setGradientFill(inside);
        g.fillEllipse(hole);

        // The drilled edge itself, darkest where the bezel overhangs it.
        g.setColour(juce::Colour::fromRGBA(0, 0, 0, lit ? 70 : 95));
        g.drawEllipse(hole.reduced(0.25f), juce::jmax(0.6f, dotRadius * 0.28f));
    }

    // ---- the cap ------------------------------------------------------------
    const auto cap = bounds.reduced(radius * 0.30f);
    const auto capCentre = cap.getCentre();
    const auto capRadius = cap.getWidth() * 0.5f;

    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 46));
    g.fillEllipse(cap.translated(0.0f, 1.2f));

    juce::ColourGradient capFill(juce::Colour::fromRGB(250, 250, 250),
                                 capCentre.x - capRadius * 0.45f,
                                 capCentre.y - capRadius * 0.65f,
                                 juce::Colour::fromRGB(206, 206, 210),
                                 capCentre.x + capRadius * 0.55f,
                                 capCentre.y + capRadius * 0.75f,
                                 false);
    g.setGradientFill(capFill);
    g.fillEllipse(cap);

    // A soft highlight across the top of the cap, which is what gives it the
    // domed look rather than the flat disc a plain gradient leaves.
    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 120));
    g.fillEllipse(cap.reduced(capRadius * 0.16f).translated(0.0f, -capRadius * 0.30f)
                     .withHeight(cap.getHeight() * 0.42f));

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 150));
    g.drawEllipse(cap.reduced(0.5f), 1.0f);

    // ---- the pointer --------------------------------------------------------
    // A tick cut into the cap near its edge, the way a hardware knob marks its
    // position - not a line from the centre.
    const auto tickLength = capRadius * 0.34f;
    const auto tickOuter = capRadius * 0.80f;
    const juce::Point<float> tickFrom(capCentre.x + std::sin(angle) * tickOuter,
                                      capCentre.y - std::cos(angle) * tickOuter);
    const juce::Point<float> tickTo(capCentre.x + std::sin(angle) * (tickOuter - tickLength),
                                    capCentre.y - std::cos(angle) * (tickOuter - tickLength));

    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 60));
    g.drawLine(juce::Line<float>(tickFrom.translated(0.0f, 1.0f),
                                 tickTo.translated(0.0f, 1.0f)),
               juce::jmax(1.8f, radius * 0.075f));

    g.setColour(enabled ? pointerColour : pointerDisabledColour);
    g.drawLine(juce::Line<float>(tickFrom, tickTo), juce::jmax(1.6f, radius * 0.065f));

    // ---- everything every other knob shows ----------------------------------
    drawKnobOverlays(g, bounds, slider, ! enabled, overlayColours, true);
}

} // namespace px3::ui
