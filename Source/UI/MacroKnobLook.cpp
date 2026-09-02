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

    // ---- the value dots around the bezel ------------------------------------
    // The row of dots is what makes the value readable at this size: the arc
    // alone is a few pixels thick on a 60 px knob.
    constexpr int dotCount = 21;
    const auto dotRadius = juce::jmax(0.9f, radius * 0.042f);
    const auto dotRing = radius * 0.86f;

    for (int i = 0; i < dotCount; ++i)
    {
        const auto t = static_cast<float>(i) / static_cast<float>(dotCount - 1);
        const auto dotAngle = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
        const auto lit = enabled && t <= sliderPos + 1.0e-4f;

        const juce::Point<float> at(centre.x + std::sin(dotAngle) * dotRing,
                                    centre.y - std::cos(dotAngle) * dotRing);

        if (lit)
        {
            g.setColour(accent.withAlpha(0.28f));
            g.fillEllipse(juce::Rectangle<float>(dotRadius * 4.0f, dotRadius * 4.0f).withCentre(at));
        }

        g.setColour(lit ? accent.brighter(0.15f) : juce::Colour::fromRGBA(120, 120, 124, 168));
        g.fillEllipse(juce::Rectangle<float>(dotRadius * 2.0f, dotRadius * 2.0f).withCentre(at));
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

    // ---- the value arc ------------------------------------------------------
    // After the cap, in the gap between it and the dots. Drawn before, the cap
    // covered the inner half of it and left a hairline.
    const auto arcRadius = radius * 0.775f;
    if (sliderPos > 1.0e-4f && enabled)
    {
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                          rotaryStartAngle, angle, true);

        g.setColour(accent.withAlpha(0.30f));
        g.strokePath(arc, juce::PathStrokeType(juce::jmax(2.4f, radius * 0.10f),
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
        g.setColour(accent);
        g.strokePath(arc, juce::PathStrokeType(juce::jmax(1.1f, radius * 0.045f),
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

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

    g.setColour(enabled ? accent.darker(0.15f) : juce::Colour::fromRGB(150, 150, 154));
    g.drawLine(juce::Line<float>(tickFrom, tickTo), juce::jmax(1.6f, radius * 0.065f));

    // ---- everything every other knob shows ----------------------------------
    drawKnobOverlays(g, bounds, slider, ! enabled, overlayColours, true);
}

} // namespace px3::ui
