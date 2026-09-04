#pragma once

#include <JuceHeader.h>

// A label that paints itself as a speech bubble: a rounded rectangle with a
// pointer, drawn as ONE path so the fill and the outline are continuous. Draw
// the two separately and the seam shows - the outline crosses where the arrow
// meets the body, and no amount of colour matching hides it.
//
// The arrow points up, positioned from the RIGHT edge, because that is where
// this is used: a notice under the top bar, pointing back at the control it is
// about. The component's own height therefore includes the arrow, and the body
// starts arrowHeight below the top.
//
// A Label subclass rather than a fresh component, so setText/getText and
// everything already calling them keep working; only the painting changes.
class SpeechBubbleLabel final : public juce::Label
{
public:
    // Declared because JUCE_DECLARE_NON_COPYABLE below user-declares a (deleted)
    // copy constructor, and any user-declared constructor suppresses the
    // implicit default one.
    SpeechBubbleLabel() = default;

    struct Style
    {
        juce::Colour background { juce::Colours::black };
        juce::Colour border     { juce::Colours::white };
        juce::Colour text       { juce::Colours::white };

        // Translucency as its own property rather than alpha buried in the hex,
        // matching how every other configurable fill here is described. The two
        // are separate because a speech bubble wants them separate: a dark body
        // you can read text against, with an outline that sits back further
        // than the body does. Multiplied into the colour, so a colour that
        // carries its own alpha still works and these scale it.
        // Defaulted to the look these replaced, so a bubble built without config
        // is translucent exactly as before rather than a solid black slab.
        float backgroundOpacity { 0.80f };
        float borderOpacity     { 0.35f };

        float cornerRadius { 6.0f };
        float borderWidth  { 1.0f };

        // The pointer. Inset is measured from the bubble's right edge to the
        // arrow's right side, so the arrow keeps its distance from the corner
        // as the bubble grows.
        float arrowWidth  { 14.0f };
        float arrowHeight { 8.0f };
        float arrowInsetFromRight { 18.0f };

        // Space between the outline and the text.
        float paddingX { 10.0f };
        float paddingY { 4.0f };
        float fontSize { 12.0f };
    };

    void setStyle(const Style& newStyle)
    {
        style = newStyle;
        setColour(juce::Label::textColourId, style.text);
        setFont(juce::Font(juce::FontOptions(style.fontSize)));
        repaint();
    }

    const Style& getStyle() const noexcept { return style; }

    // What the arrow costs at the top, so callers can size the component to fit
    // a line of text plus the pointer without knowing how it is drawn.
    float getArrowHeight() const noexcept { return style.arrowHeight; }

    static juce::Path buildBubblePath(juce::Rectangle<float> bounds, const Style& style)
    {
        // Inset by half the stroke: a path stroked on its centre line would
        // otherwise lose the outer half of the outline off the component edge.
        const auto half = style.borderWidth * 0.5f;
        const auto outer = bounds.reduced(half, half);

        const auto body = outer.withTrimmedTop(style.arrowHeight);
        const auto radius = juce::jlimit(0.0f,
                                         juce::jmin(body.getWidth(), body.getHeight()) * 0.5f,
                                         style.cornerRadius);

        // Where the arrow sits along the top edge. Clamped so it cannot run
        // into either corner arc, which would leave the outline crossing itself.
        const auto arrowWidth = juce::jmin(style.arrowWidth, juce::jmax(0.0f, body.getWidth() - radius * 2.0f));
        const auto rightLimit = body.getRight() - radius;
        const auto leftLimit  = body.getX() + radius + arrowWidth;
        const auto arrowRight = juce::jlimit(leftLimit, rightLimit,
                                             body.getRight() - style.arrowInsetFromRight);
        const auto arrowLeft  = arrowRight - arrowWidth;
        const auto arrowTip   = arrowLeft + arrowWidth * 0.5f;

        juce::Path path;

        if (style.arrowHeight <= 0.0f || arrowWidth <= 0.0f)
        {
            path.addRoundedRectangle(body, radius);
            return path;
        }

        // Clockwise from just after the top-left arc, so the arrow is walked
        // as part of the top edge rather than added as a second shape.
        path.startNewSubPath(body.getX() + radius, body.getY());
        path.lineTo(arrowLeft, body.getY());
        path.lineTo(arrowTip, outer.getY());
        path.lineTo(arrowRight, body.getY());
        path.lineTo(body.getRight() - radius, body.getY());
        path.addArc(body.getRight() - radius * 2.0f, body.getY(),
                    radius * 2.0f, radius * 2.0f,
                    juce::MathConstants<float>::halfPi * 0.0f,
                    juce::MathConstants<float>::halfPi, false);
        path.lineTo(body.getRight(), body.getBottom() - radius);
        path.addArc(body.getRight() - radius * 2.0f, body.getBottom() - radius * 2.0f,
                    radius * 2.0f, radius * 2.0f,
                    juce::MathConstants<float>::halfPi,
                    juce::MathConstants<float>::pi, false);
        path.lineTo(body.getX() + radius, body.getBottom());
        path.addArc(body.getX(), body.getBottom() - radius * 2.0f,
                    radius * 2.0f, radius * 2.0f,
                    juce::MathConstants<float>::pi,
                    juce::MathConstants<float>::pi * 1.5f, false);
        path.lineTo(body.getX(), body.getY() + radius);
        path.addArc(body.getX(), body.getY(),
                    radius * 2.0f, radius * 2.0f,
                    juce::MathConstants<float>::pi * 1.5f,
                    juce::MathConstants<float>::twoPi, false);
        path.closeSubPath();
        return path;
    }

    void paint(juce::Graphics& g) override
    {
        const auto path = buildBubblePath(getLocalBounds().toFloat(), style);

        g.setColour(style.background.withMultipliedAlpha(
            juce::jlimit(0.0f, 1.0f, style.backgroundOpacity)));
        g.fillPath(path);

        if (style.borderWidth > 0.0f)
        {
            g.setColour(style.border.withMultipliedAlpha(
                juce::jlimit(0.0f, 1.0f, style.borderOpacity)));
            g.strokePath(path, juce::PathStrokeType(style.borderWidth));
        }

        // The text sits inside the body, below the arrow.
        auto textArea = getLocalBounds().toFloat()
                            .withTrimmedTop(style.arrowHeight)
                            .reduced(style.paddingX, style.paddingY);

        g.setColour(findColour(juce::Label::textColourId));
        g.setFont(getFont());
        g.drawText(getText(), textArea, getJustificationType(), true);
    }

private:
    Style style;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpeechBubbleLabel)
};
