#include "ComponentCardDrawing.h"

namespace px3::ui
{
juce::Rectangle<float> drawComponentCard(juce::Graphics& g,
                                         juce::Rectangle<float> bounds,
                                         const ComponentCardStyle& style)
{
    const auto cardBounds = bounds.reduced(style.borderPadding);
    const auto fillBounds = cardBounds.reduced(style.fillInset);

    g.setColour(style.backgroundColour.withAlpha(style.backgroundAlpha));
    g.fillRoundedRectangle(fillBounds, style.cornerRadius);

    g.setColour(style.topFillColour.withAlpha(style.topFillAlpha));
    juce::Path topFill;
    const auto clampedRatio = juce::jlimit(0.0f, 1.0f, style.topFillHeightRatio);
    const auto topHalf = fillBounds.withTrimmedBottom(fillBounds.getHeight() * (1.0f - clampedRatio));
    topFill.addRoundedRectangle(topHalf.getX(),
                                topHalf.getY(),
                                topHalf.getWidth(),
                                topHalf.getHeight(),
                                style.cornerRadius,
                                style.cornerRadius,
                                true,
                                true,
                                false,
                                false);
    g.fillPath(topFill);

    if (style.drawOutline && style.outlineThickness > 0.0f)
    {
        g.setColour(style.outlineColour.withAlpha(style.outlineAlpha));
        g.drawRoundedRectangle(cardBounds, style.cornerRadius, style.outlineThickness);
    }

    return cardBounds;
}

} // namespace px3::ui
