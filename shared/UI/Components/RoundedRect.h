#pragma once

#include <JuceHeader.h>

class UIConfig;

namespace px3::ui
{

// Four independently sized corners.
//
// JUCE's addRoundedRectangle takes one radius and a set of booleans for which
// corners use it, so it cannot express "8 at the top, square at the bottom" -
// which is the shape most panels in this plugin actually want.
//
// Parsed with the same shorthand-plus-override convention the rest of the
// config uses for insets: `radius` sets all four, and `radiusTopLeft` and its
// siblings override individual corners.
struct CornerRadii
{
    float topLeft { 0.0f };
    float topRight { 0.0f };
    float bottomRight { 0.0f };
    float bottomLeft { 0.0f };

    static CornerRadii all(float radius) { return { radius, radius, radius, radius }; }

    // `base` is the path of the block holding `radius`, e.g.
    // "keyboard.whiteKey.border".
    static CornerRadii fromConfig(const UIConfig* config,
                                  const juce::String& base,
                                  CornerRadii fallback);

    bool isUniform() const noexcept
    {
        return topLeft == topRight && topRight == bottomRight && bottomRight == bottomLeft;
    }

    bool isSquare() const noexcept { return isUniform() && topLeft <= 0.0f; }

    // Clamped so no pair of corners on one edge can exceed that edge's length,
    // which would otherwise produce a self-intersecting path.
    CornerRadii clampedTo(juce::Rectangle<float> bounds) const;
};

// A rectangle with four independent corners. Returns a plain rectangle path
// when every corner is square, so the common case costs nothing extra.
juce::Path roundedRectanglePath(juce::Rectangle<float> bounds, CornerRadii radii);

// fill/draw helpers, so callers do not repeat the uniform-vs-not branch.
void fillRounded(juce::Graphics& g, juce::Rectangle<float> bounds, CornerRadii radii);
void drawRounded(juce::Graphics& g, juce::Rectangle<float> bounds, CornerRadii radii, float lineWidth);

} // namespace px3::ui
