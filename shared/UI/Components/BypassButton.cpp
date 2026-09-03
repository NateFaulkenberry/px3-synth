#include "BypassButton.h"

namespace px3::ui
{

BypassButton::BypassButton()
{
    setClickingTogglesState(true);
    // No text: the glyph is the label. This is also what removes the separate
    // "ON" caption every section used to carry beside its toggle.
    setButtonText({});
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void BypassButton::setAccentColour(juce::Colour colour)
{
    accent = colour;
    repaint();
}

void BypassButton::setSectionName(juce::String name)
{
    sectionName = std::move(name);
}

juce::String BypassButton::getTooltip()
{
    const auto subject = sectionName.isNotEmpty() ? sectionName : juce::String("this section");
    return getToggleState() ? "Bypass " + subject
                            : "Enable " + subject;
}

void BypassButton::paintButton(juce::Graphics& g,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto side = juce::jmin(bounds.getWidth(), bounds.getHeight());
    if (side <= 0.0f)
    {
        return;
    }

    const auto box = juce::Rectangle<float>(side, side).withCentre(bounds.getCentre());
    const auto on = getToggleState();
    const auto enabled = isEnabled();

    // A disabled section's toggle is still legible, just quiet - it has to be,
    // because clicking it is how you bring the section back.
    const auto lit = enabled ? accent : accent.withSaturation(0.0f);
    const auto glyphColour = on ? lit.brighter(shouldDrawButtonAsHighlighted ? 0.35f : 0.0f)
                                : juce::Colour::fromRGB(150, 150, 158)
                                      .brighter(shouldDrawButtonAsHighlighted ? 0.30f : 0.0f);

    // Seat: a filled disc so the glyph reads against any card colour.
    const auto seat = box.reduced(side * 0.02f);
    g.setColour(juce::Colour::fromRGBA(12, 14, 20, shouldDrawButtonAsDown ? 235 : 190));
    g.fillEllipse(seat);

    if (on)
    {
        // A faint halo, so "powered" is visible at a glance across the whole
        // window rather than needing the glyph to be read.
        g.setColour(lit.withAlpha(0.20f));
        g.fillEllipse(seat.reduced(side * 0.04f));
    }

    g.setColour(glyphColour.withAlpha(on ? 0.55f : 0.35f));
    g.drawEllipse(seat, juce::jmax(1.0f, side * 0.05f));

    // ---- the power symbol ------------------------------------------------
    const auto glyph = box.reduced(side * 0.28f);
    const auto centre = glyph.getCentre();
    const auto radius = glyph.getWidth() * 0.5f * (1.0f - kRingInset);
    const auto stroke = juce::jmax(1.2f, side * 0.10f);

    juce::Path path;

    // The broken ring. Angles are measured clockwise from twelve o'clock, which
    // is how JUCE's addCentredArc works, so the gap is simply the span the arc
    // does not cover.
    path.addCentredArc(centre.x, centre.y,
                       radius, radius,
                       0.0f,
                       kGapHalfAngle,
                       juce::MathConstants<float>::twoPi - kGapHalfAngle,
                       true);

    // The stem, rising through the gap.
    const auto stemTop = glyph.getY() + glyph.getHeight() * kStemTop;
    const auto stemBottom = glyph.getY() + glyph.getHeight() * kStemBottom;
    path.startNewSubPath(centre.x, stemTop);
    path.lineTo(centre.x, stemBottom);

    g.setColour(glyphColour);
    g.strokePath(path, juce::PathStrokeType(stroke,
                                            juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
}

bool isCardBackgroundToggleClick(const juce::MouseEvent& event)
{
    return event.mouseWasClicked() && ! event.mods.isPopupMenu();
}

} // namespace px3::ui
