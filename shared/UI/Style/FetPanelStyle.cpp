#include "FetPanelStyle.h"
#include "Theme.h"

namespace px3::ui
{

void FetPushButtonLookAndFeel::drawButtonBackground(juce::Graphics& g,
                                                    juce::Button& button,
                                                    const juce::Colour&,
                                                    bool shouldDrawButtonAsHighlighted,
                                                    bool shouldDrawButtonAsDown)
{
    // The theme's switch key, the one the mixer and every card use: a recessed
    // well; latched, it tints, lights a stripe along its foot and turns the
    // legend the accent. The domed silver caps were the last skeuomorphic keys.
    namespace th = theme;
    const auto area = button.getLocalBounds().toFloat().reduced(0.5f);
    if (area.isEmpty()) { return; }
    const auto enabled = button.isEnabled();
    const auto on = button.getToggleState();
    const auto lit = enabled ? accent : accent.withSaturation(0.0f);

    th::drawInset(g, area, th::space::insetRadius);
    if (on)
    {
        g.setColour(lit.withAlpha(enabled ? 0.18f : 0.10f));
        g.fillRect(area.reduced(1.0f));
        g.setColour(lit.withAlpha(enabled ? 0.9f : 0.45f));
        g.fillRect(area.reduced(4.0f, 0.0f).removeFromBottom(2.0f).translated(0.0f, -1.0f));
    }
    if (enabled && (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown))
    {
        g.setColour(juce::Colours::white.withAlpha(shouldDrawButtonAsDown ? 0.10f : 0.05f));
        g.fillRect(area.reduced(1.0f));
    }
}

void FetPushButtonLookAndFeel::drawButtonText(juce::Graphics& g,
                                              juce::TextButton& button,
                                              bool,
                                              bool)
{
    namespace th = theme;
    const auto enabled = button.isEnabled();
    const auto on = button.getToggleState();
    const auto colour = on ? accent.interpolatedWith(th::colour::textValue, 0.35f) : th::colour::textLabel;
    g.setColour(colour.withAlpha(enabled ? 1.0f : 0.45f));
    g.setFont(th::font(th::Type::label).withHeight(juce::jmin(th::size(th::Type::label),
                                                              static_cast<float>(button.getHeight()) - 4.0f)));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(3, 0), juce::Justification::centred, 1, 0.75f);
}

float VuArc::angleForPosition(float position) const
{
    // 0 is the left stop, 1 the right.
    return -span + juce::jlimit(0.0f, 1.0f, position) * (span * 2.0f);
}

juce::Point<float> VuArc::directionForPosition(float position) const
{
    const auto angle = angleForPosition(position);
    return { std::sin(angle), -std::cos(angle) };
}

juce::Point<float> VuArc::pointForPosition(float position, float fraction) const
{
    return pivot + directionForPosition(position) * (radius * fraction);
}

float VuArc::positionForLevelDb(float db)
{
    // Linear in amplitude, normalised so +3 dB reaches the right stop. This is
    // what puts 0 VU at about 71% of the sweep, as it is on a real face.
    constexpr auto kFullScaleDb = 3.0f;
    const auto amplitude = std::pow(10.0f, juce::jlimit(-60.0f, kFullScaleDb, db) / 20.0f);
    const auto fullScale = std::pow(10.0f, kFullScaleDb / 20.0f);
    return juce::jlimit(0.0f, 1.0f, amplitude / fullScale);
}

float VuArc::positionForReductionDb(float db)
{
    // No reduction rests at the right stop; the needle falls left as the unit
    // works. Same amplitude-linear movement, so the deep end crowds together
    // exactly as it does on the hardware.
    return juce::jlimit(0.0f, 1.0f, std::pow(10.0f, -juce::jmax(0.0f, db) / 20.0f));
}

juce::Rectangle<float> VuArc::drawnBounds() const
{
    // The scale's extremes are its two ends and its apex; nothing drawn between
    // them can fall outside the box those three define.
    auto bounds = juce::Rectangle<float>(pointForPosition(0.0f, 1.0f), pointForPosition(1.0f, 1.0f));
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
                   juce::Colour ink,
                   float startAngle,
                   float endAngle)
{
    if (marks.size() < 2)
    {
        return;
    }

    const auto centre = knobBounds.getCentre();
    const auto radius = juce::jmin(knobBounds.getWidth(), knobBounds.getHeight()) * 0.5f;

    // The theme's display numerals, as every other scale in the instrument.
    g.setFont(theme::font(theme::Type::display).withHeight(8.5f));

    for (int i = 0; i < marks.size(); ++i)
    {
        const auto position = static_cast<float>(i) / static_cast<float>(marks.size() - 1);
        const auto angle = startAngle + position * (endAngle - startAngle);
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
    // The theme's label type - the same caption every card prints - in the
    // panel's own ink. `fontSize` is no longer used: one size, as everywhere.
    theme::drawLabel(g, text, area, theme::Type::label, ink, justification);
    juce::ignoreUnused(fontSize);
}
} // namespace panel

} // namespace px3::ui
