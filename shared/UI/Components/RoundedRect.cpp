#include "RoundedRect.h"

#include "UIConfig.h"

namespace px3::ui
{

CornerRadii CornerRadii::fromConfig(const UIConfig* config,
                                    const juce::String& base,
                                    CornerRadii fallback)
{
    if (config == nullptr)
    {
        return fallback;
    }

    auto radii = fallback;

    // The shorthand first, then per-corner overrides on top - the same order
    // padding and paddingTop are read in.
    if (const auto shared = config->getValue(base + ".radius"); ! shared.isVoid())
    {
        radii = all(static_cast<float>(static_cast<double>(shared)));
    }

    const auto corner = [&](const char* key, float& target)
    {
        if (const auto value = config->getValue(base + key); ! value.isVoid())
        {
            target = static_cast<float>(static_cast<double>(value));
        }
    };

    corner(".radiusTopLeft", radii.topLeft);
    corner(".radiusTopRight", radii.topRight);
    corner(".radiusBottomRight", radii.bottomRight);
    corner(".radiusBottomLeft", radii.bottomLeft);
    return radii;
}

CornerRadii CornerRadii::clampedTo(juce::Rectangle<float> bounds) const
{
    const auto limit = juce::jmax(0.0f, juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f);
    return { juce::jlimit(0.0f, limit, topLeft),
             juce::jlimit(0.0f, limit, topRight),
             juce::jlimit(0.0f, limit, bottomRight),
             juce::jlimit(0.0f, limit, bottomLeft) };
}

juce::Path roundedRectanglePath(juce::Rectangle<float> bounds, CornerRadii radii)
{
    juce::Path path;

    if (bounds.isEmpty())
    {
        return path;
    }

    const auto r = radii.clampedTo(bounds);

    if (r.isSquare())
    {
        path.addRectangle(bounds);
        return path;
    }

    if (r.isUniform())
    {
        path.addRoundedRectangle(bounds, r.topLeft);
        return path;
    }

    const auto x = bounds.getX();
    const auto y = bounds.getY();
    const auto right = bounds.getRight();
    const auto bottom = bounds.getBottom();

    // Quarter-circle arcs joined by straight edges, clockwise from the top
    // left.
    //
    // addCentredArc measures angles CLOCKWISE FROM 12 O'CLOCK, so each corner's
    // arc spans the quarter that actually contains it: the top-right corner
    // runs from straight up to the right, the bottom-right from right to down,
    // and so on. Getting these a quarter-turn out draws each arc in the wrong
    // corner and the outline crosses itself - which is what "jagged/broken"
    // corners were.
    constexpr auto up = 0.0f;
    const auto rightAngle = juce::MathConstants<float>::halfPi;
    const auto down = juce::MathConstants<float>::pi;
    const auto leftAngle = juce::MathConstants<float>::pi * 1.5f;
    const auto fullTurn = juce::MathConstants<float>::twoPi;

    path.startNewSubPath(x + r.topLeft, y);

    path.lineTo(right - r.topRight, y);
    if (r.topRight > 0.0f)
    {
        path.addCentredArc(right - r.topRight, y + r.topRight, r.topRight, r.topRight,
                           0.0f, up, rightAngle);
    }

    path.lineTo(right, bottom - r.bottomRight);
    if (r.bottomRight > 0.0f)
    {
        path.addCentredArc(right - r.bottomRight, bottom - r.bottomRight, r.bottomRight, r.bottomRight,
                           0.0f, rightAngle, down);
    }

    path.lineTo(x + r.bottomLeft, bottom);
    if (r.bottomLeft > 0.0f)
    {
        path.addCentredArc(x + r.bottomLeft, bottom - r.bottomLeft, r.bottomLeft, r.bottomLeft,
                           0.0f, down, leftAngle);
    }

    path.lineTo(x, y + r.topLeft);
    if (r.topLeft > 0.0f)
    {
        path.addCentredArc(x + r.topLeft, y + r.topLeft, r.topLeft, r.topLeft,
                           0.0f, leftAngle, fullTurn);
    }

    path.closeSubPath();
    return path;
}

void fillRounded(juce::Graphics& g, juce::Rectangle<float> bounds, CornerRadii radii)
{
    const auto r = radii.clampedTo(bounds);
    if (r.isSquare())
    {
        g.fillRect(bounds);
        return;
    }
    if (r.isUniform())
    {
        g.fillRoundedRectangle(bounds, r.topLeft);
        return;
    }
    g.fillPath(roundedRectanglePath(bounds, r));
}

void drawRounded(juce::Graphics& g, juce::Rectangle<float> bounds, CornerRadii radii, float lineWidth)
{
    const auto r = radii.clampedTo(bounds);
    if (r.isSquare())
    {
        g.drawRect(bounds, lineWidth);
        return;
    }
    if (r.isUniform())
    {
        g.drawRoundedRectangle(bounds, r.topLeft, lineWidth);
        return;
    }
    g.strokePath(roundedRectanglePath(bounds, r), juce::PathStrokeType(lineWidth));
}

} // namespace px3::ui
