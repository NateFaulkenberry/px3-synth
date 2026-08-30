#include "FetPanelStyle.h"

namespace px3::ui
{

namespace
{
// The rotary sweep of a panel knob: a little over three quarters of a turn,
// with the dead space at the bottom where the pointer would be hidden by the
// hand turning it.
constexpr float kStartAngle = juce::MathConstants<float>::pi * 1.22f;
constexpr float kEndAngle = juce::MathConstants<float>::pi * 2.78f;
} // namespace

void FetKnobLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                          int x, int y, int width, int height,
                                          float sliderPosProportional,
                                          float,
                                          float,
                                          juce::Slider& slider)
{
    const auto full = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                             static_cast<float>(width), static_cast<float>(height));
    const auto diameter = juce::jmin(full.getWidth(), full.getHeight());
    const auto bounds = juce::Rectangle<float>(diameter, diameter).withCentre(full.getCentre());
    const auto centre = bounds.getCentre();
    const auto radius = diameter * 0.5f;

    // The knob's own sweep, not the caller's: the scale engraved on the panel
    // is drawn across the same span, and the two have to agree or the pointer
    // lies about the number it is on.
    const auto angle = kStartAngle + juce::jlimit(0.0f, 1.0f, sliderPosProportional) * (kEndAngle - kStartAngle);

    const auto enabled = slider.isEnabled();

    // Chromed collar, brightest at the top left where the room light is.
    const auto collar = bounds;
    g.setGradientFill(juce::ColourGradient(juce::Colour::fromRGB(238, 240, 243), collar.getX(), collar.getY(),
                                           juce::Colour::fromRGB(126, 129, 134), collar.getRight(), collar.getBottom(),
                                           false));
    g.fillEllipse(collar);
    g.setColour(juce::Colour::fromRGBA(40, 42, 46, 140));
    g.drawEllipse(collar.reduced(0.5f), 1.0f);

    // The cap: black phenolic, lit from the same direction as the collar.
    const auto cap = bounds.reduced(radius * 0.17f);
    g.setGradientFill(juce::ColourGradient(juce::Colour::fromRGB(62, 63, 67), cap.getX(), cap.getY(),
                                           juce::Colour::fromRGB(16, 16, 18), cap.getRight(), cap.getBottom(),
                                           false));
    g.fillEllipse(cap);

    // A soft highlight arc across the top of the cap, which is what stops a
    // black circle reading as a hole.
    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 26));
    g.fillEllipse(cap.reduced(cap.getWidth() * 0.12f).translated(0.0f, -cap.getHeight() * 0.16f)
                      .withHeight(cap.getHeight() * 0.42f));

    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 150));
    g.drawEllipse(cap.reduced(0.5f), 1.0f);

    // The indicator, cut from the cap's edge toward its centre.
    const auto direction = juce::Point<float>(std::sin(angle), -std::cos(angle));
    const auto capRadius = cap.getWidth() * 0.5f;
    g.setColour(juce::Colour::fromRGB(242, 244, 247).withAlpha(enabled ? 1.0f : 0.45f));
    g.drawLine({ centre + direction * (capRadius * 0.34f), centre + direction * (capRadius * 0.93f) }, 2.4f);
}

void FetPushButtonLookAndFeel::drawButtonBackground(juce::Graphics& g,
                                                    juce::Button& button,
                                                    const juce::Colour&,
                                                    bool shouldDrawButtonAsHighlighted,
                                                    bool shouldDrawButtonAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const auto pressed = button.getToggleState() || shouldDrawButtonAsDown;

    // The well the cap travels in.
    g.setColour(juce::Colour::fromRGB(58, 58, 62));
    g.fillRoundedRectangle(bounds, 2.0f);

    // A pressed cap sits lower in its well and loses its top highlight, which
    // is the whole of how a latching push button reads as latched.
    auto cap = bounds.reduced(1.6f);
    cap = pressed ? cap.withTrimmedTop(2.4f) : cap.withTrimmedBottom(2.4f);

    const auto top = pressed ? juce::Colour::fromRGB(176, 178, 182) : juce::Colour::fromRGB(238, 239, 242);
    const auto bottom = pressed ? juce::Colour::fromRGB(140, 142, 147) : juce::Colour::fromRGB(196, 198, 203);

    g.setGradientFill(juce::ColourGradient(top, cap.getX(), cap.getY(),
                                           bottom, cap.getX(), cap.getBottom(), false));
    g.fillRoundedRectangle(cap, 1.6f);

    if (shouldDrawButtonAsHighlighted && ! pressed)
    {
        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 60));
        g.fillRoundedRectangle(cap, 1.6f);
    }

    g.setColour(juce::Colour::fromRGBA(30, 30, 34, 120));
    g.drawRoundedRectangle(cap, 1.6f, 0.8f);
}

void FetPushButtonLookAndFeel::drawButtonText(juce::Graphics& g,
                                              juce::TextButton& button,
                                              bool,
                                              bool)
{
    g.setColour(juce::Colour::fromRGB(28, 29, 32).withAlpha(button.isEnabled() ? 1.0f : 0.4f));
    g.setFont(juce::FontOptions(juce::jmin(11.0f, static_cast<float>(button.getHeight()) * 0.52f), juce::Font::bold));
    g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, false);
}

float VuArc::angleFor(float db) const
{
    const auto position = juce::jlimit(0.0f, 1.0f, db / juce::jmax(1.0e-3f, fullScaleDb));
    return span - position * (span * 2.0f);
}

juce::Point<float> VuArc::directionFor(float db) const
{
    const auto angle = angleFor(db);
    return { std::sin(angle), -std::cos(angle) };
}

juce::Point<float> VuArc::pointFor(float db, float fraction) const
{
    return pivot + directionFor(db) * (radius * fraction);
}

juce::Rectangle<float> VuArc::drawnBounds() const
{
    // The scale's extremes are its two ends and its apex; nothing drawn between
    // them can fall outside the box those three define.
    auto bounds = juce::Rectangle<float>(pointFor(0.0f, 1.0f), pointFor(fullScaleDb, 1.0f));
    const auto apex = pivot.translated(0.0f, -radius);
    return bounds.getUnion(juce::Rectangle<float>(apex, apex));
}

VuArc vuArcFor(juce::Rectangle<float> face)
{
    VuArc arc;
    arc.span = 0.60f;

    // The radius is bounded by BOTH dimensions. Half the face's width has to
    // contain radius*sin(span), and the arc's sagitta - radius*(1-cos(span)) -
    // has to fit the height left under the apex. Taking the smaller is what
    // makes the arc fit a face of any proportion.
    const auto byWidth = (face.getWidth() * 0.46f) / juce::jmax(0.05f, std::sin(arc.span));
    const auto byHeight = (face.getHeight() * 0.58f) / juce::jmax(0.05f, 1.0f - std::cos(arc.span));
    arc.radius = juce::jmax(1.0f, juce::jmin(byWidth, byHeight));

    // The apex sits a little below the top edge, and the pivot follows from it.
    const auto apexY = face.getY() + face.getHeight() * 0.20f;
    arc.pivot = { face.getCentreX(), apexY + arc.radius };
    return arc;
}

namespace panel
{
void drawScrew(juce::Graphics& g, juce::Point<float> centre, float radius)
{
    const auto bounds = juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre);

    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 40));
    g.fillEllipse(bounds.expanded(1.0f));

    g.setGradientFill(juce::ColourGradient(juce::Colour::fromRGB(214, 216, 219), bounds.getX(), bounds.getY(),
                                           juce::Colour::fromRGB(140, 142, 147), bounds.getRight(), bounds.getBottom(),
                                           false));
    g.fillEllipse(bounds);

    g.setColour(juce::Colour::fromRGBA(60, 62, 66, 160));
    g.drawEllipse(bounds.reduced(0.5f), 0.8f);

    // The slot, turned slightly off vertical - a screw driven home never lands
    // square, and a panel full of perfectly aligned slots looks drawn.
    g.setColour(juce::Colour::fromRGBA(70, 72, 76, 200));
    const auto slot = juce::Point<float>(std::cos(0.5f), std::sin(0.5f)) * (radius * 0.62f);
    g.drawLine({ centre - slot, centre + slot }, 1.2f);
}

void drawKnobScale(juce::Graphics& g,
                   juce::Rectangle<float> knobBounds,
                   const juce::StringArray& marks,
                   juce::Colour ink)
{
    if (marks.size() < 2)
    {
        return;
    }

    const auto centre = knobBounds.getCentre();
    const auto radius = juce::jmin(knobBounds.getWidth(), knobBounds.getHeight()) * 0.5f;

    g.setFont(juce::FontOptions(8.5f, juce::Font::bold));

    for (int i = 0; i < marks.size(); ++i)
    {
        const auto position = static_cast<float>(i) / static_cast<float>(marks.size() - 1);
        const auto angle = kStartAngle + position * (kEndAngle - kStartAngle);
        const auto direction = juce::Point<float>(std::sin(angle), -std::cos(angle));

        g.setColour(ink.withAlpha(0.75f));
        g.drawLine({ centre + direction * (radius + 2.0f), centre + direction * (radius + 6.0f) }, 1.0f);

        g.setColour(ink);
        const auto textCentre = centre + direction * (radius + 13.0f);
        g.drawText(marks[i],
                   juce::Rectangle<float>(22.0f, 11.0f).withCentre(textCentre),
                   juce::Justification::centred, false);
    }
}

void drawLegend(juce::Graphics& g,
                juce::Rectangle<float> area,
                const juce::String& text,
                juce::Colour ink,
                float fontSize,
                juce::Justification justification)
{
    // Letter-spaced, because panel legends are engraved rather than typeset and
    // the spacing is most of what makes them read as engraved.
    juce::String spaced;
    for (int i = 0; i < text.length(); ++i)
    {
        spaced << text[i];
        if (i < text.length() - 1)
        {
            spaced << " ";
        }
    }

    g.setColour(ink);
    g.setFont(juce::FontOptions(fontSize, juce::Font::bold));
    g.drawText(spaced, area, justification, false);
}
} // namespace panel

} // namespace px3::ui
