#pragma once

#include <JuceHeader.h>

namespace px3::ui
{

// A speech bubble: a rounded rectangle with a pointer, drawn as ONE path so the
// fill and the outline are continuous. Draw the two separately and the seam
// shows - the outline crosses where the arrow meets the body, and no amount of
// colour matching hides it. The workaround is to stroke only the arrow's two
// leading edges, which then leaves the body's outline running straight across
// the arrow's mouth. One path has neither problem.
//
// The shape lives apart from anything that draws it because two very different
// things need it: the update notice, which is a Label, and the macro depth
// panel, which is a popover full of controls. They share an outline, not an
// implementation.
struct SpeechBubble
{
    // Which edge the pointer leaves from, and therefore what it is pointing at:
    // top for a notice hanging below the control it refers to, left for a
    // popover opened to the right of the knob it belongs to.
    enum class ArrowEdge { top, left };

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
        //
        // Defaulted to the look these replaced, so a bubble built without
        // config is translucent as before rather than a solid black slab.
        float backgroundOpacity { 0.80f };
        float borderOpacity     { 0.35f };

        float cornerRadius { 6.0f };
        float borderWidth  { 1.0f };

        ArrowEdge arrowEdge { ArrowEdge::top };

        // The arrow's extent on each screen axis, NOT "along the edge" and
        // "out of it". Measured this way the same two numbers describe both
        // orientations without swapping meaning: a top arrow is arrowWidth
        // across and pokes arrowHeight upwards, a left arrow is arrowHeight
        // tall and pokes arrowWidth leftwards.
        float arrowWidth  { 14.0f };
        float arrowHeight { 8.0f };

        // Where the arrow sits along its edge.
        //
        // A top arrow keeps its distance from the right-hand corner, because
        // the control it points at is up there and the bubble grows leftwards.
        // A left arrow is given an absolute Y instead: it tracks a knob that
        // the panel may have been moved to clear, so it cannot be expressed as
        // a fixed distance from either end. Negative means no arrow yet - the
        // owner has not said where to aim - and the bubble draws plain.
        float arrowInsetFromRight { 18.0f };
        float arrowCentreFromTop  { -1.0f };

        // A shadow cast by the bubble, from the bubble's own path - arrow
        // included - so it is the shape's shadow rather than a soft rectangle
        // behind it. Off by default at opacity zero, so a bubble that says
        // nothing about a shadow draws exactly as it did.
        //
        // The shadow falls OUTSIDE the path, and a component's paint is
        // clipped to its own bounds, so an owner has to reserve room for it:
        // see shadowMargin, and MacroDepthPanel, which grows its preferred
        // bounds by that much and insets the bubble inside them.
        juce::Colour shadowColour { juce::Colours::black };
        float shadowOpacity { 0.0f };
        float shadowRadius  { 0.0f };
        float shadowOffsetX { 0.0f };
        float shadowOffsetY { 0.0f };

        // Text inset, for the bubbles that hold text themselves.
        float paddingX { 10.0f };
        float paddingY { 4.0f };
        float fontSize { 12.0f };
    };

    // What the arrow costs on each side, so an owner can reserve the strip it
    // occupies without knowing how it is drawn. Exactly one is ever non-zero.
    static float arrowInsetTop(const Style& style)
    {
        return style.arrowEdge == ArrowEdge::top ? juce::jmax(0.0f, style.arrowHeight) : 0.0f;
    }

    static float arrowInsetLeft(const Style& style)
    {
        return style.arrowEdge == ArrowEdge::left ? juce::jmax(0.0f, style.arrowWidth) : 0.0f;
    }

    static juce::Path buildPath(juce::Rectangle<float> bounds, const Style& style)
    {
        // Inset by half the stroke: a path stroked on its centre line would
        // otherwise lose the outer half of the outline off the component edge.
        const auto half = style.borderWidth * 0.5f;
        const auto outer = bounds.reduced(half, half);

        const auto onTop = style.arrowEdge == ArrowEdge::top;

        const auto body = onTop ? outer.withTrimmedTop(arrowInsetTop(style))
                                : outer.withTrimmedLeft(arrowInsetLeft(style));

        const auto radius = juce::jlimit(0.0f,
                                         juce::jmin(body.getWidth(), body.getHeight()) * 0.5f,
                                         style.cornerRadius);

        juce::Path path;

        if (body.getWidth() <= 0.0f || body.getHeight() <= 0.0f)
        {
            return path;
        }

        // The arrow's span along its own edge, clamped so it cannot reach into
        // either corner arc - an arrow that overlapped one would leave the
        // outline crossing itself.
        const auto edgeLength = onTop ? body.getWidth() : body.getHeight();
        const auto span = juce::jmin(onTop ? style.arrowWidth : style.arrowHeight,
                                     juce::jmax(0.0f, edgeLength - radius * 2.0f));
        const auto depth = onTop ? style.arrowHeight : style.arrowWidth;

        const auto plain = [&]
        {
            juce::Path rect;
            rect.addRoundedRectangle(body, radius);
            return rect;
        };

        if (depth <= 0.0f || span <= 0.0f) { return plain(); }

        // A left arrow with nowhere to aim is no arrow. The owner sets the
        // target when it places the panel; until then the bubble is plain
        // rather than pointing at the top-left corner by default.
        if (! onTop && style.arrowCentreFromTop < 0.0f) { return plain(); }

        if (onTop)
        {
            const auto rightLimit = body.getRight() - radius;
            const auto leftLimit  = body.getX() + radius + span;
            const auto arrowRight = juce::jlimit(leftLimit, rightLimit,
                                                 body.getRight() - style.arrowInsetFromRight);
            const auto arrowLeft  = arrowRight - span;
            const auto arrowTip   = arrowLeft + span * 0.5f;

            // Clockwise from just after the top-left arc, so the arrow is
            // walked as part of the top edge rather than added as a shape.
            path.startNewSubPath(body.getX() + radius, body.getY());
            path.lineTo(arrowLeft, body.getY());
            path.lineTo(arrowTip, outer.getY());
            path.lineTo(arrowRight, body.getY());
            path.lineTo(body.getRight() - radius, body.getY());
        }
        else
        {
            path.startNewSubPath(body.getX() + radius, body.getY());
            path.lineTo(body.getRight() - radius, body.getY());
        }

        path.addArc(body.getRight() - radius * 2.0f, body.getY(),
                    radius * 2.0f, radius * 2.0f,
                    0.0f,
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

        if (! onTop)
        {
            // Up the left edge, so the arrow is walked in the same direction
            // the rest of the outline is travelling.
            const auto lowLimit  = body.getBottom() - radius;
            const auto highLimit = body.getY() + radius + span;
            const auto arrowLow  = juce::jlimit(highLimit, lowLimit,
                                                style.arrowCentreFromTop + span * 0.5f);
            const auto arrowHigh = arrowLow - span;
            const auto arrowTip  = arrowLow - span * 0.5f;

            path.lineTo(body.getX(), arrowLow);
            path.lineTo(outer.getX(), arrowTip);
            path.lineTo(body.getX(), arrowHigh);
        }

        path.lineTo(body.getX(), body.getY() + radius);
        path.addArc(body.getX(), body.getY(),
                    radius * 2.0f, radius * 2.0f,
                    juce::MathConstants<float>::pi * 1.5f,
                    juce::MathConstants<float>::twoPi, false);
        path.closeSubPath();
        return path;
    }

    // How much room outside the bubble its shadow needs, so an owner can size
    // itself to fit one. Zero when there is no shadow, which keeps a bubble
    // that does not want one exactly the size it was.
    static float shadowMargin(const Style& style)
    {
        if (style.shadowOpacity <= 0.0f || style.shadowRadius < 0.5f) { return 0.0f; }
        return style.shadowRadius
             + juce::jmax(std::abs(style.shadowOffsetX), std::abs(style.shadowOffsetY));
    }

    // Shadow, then fill, then stroke, on the one path.
    static void paintBackground(juce::Graphics& g, juce::Rectangle<float> bounds,
                                const Style& style)
    {
        const auto path = buildPath(bounds, style);

        if (path.isEmpty()) { return; }

        // DropShadow takes an integer radius and refuses anything below one, so
        // a configured radius that rounds to zero is no shadow rather than a
        // hard black copy of the bubble offset by a few pixels.
        if (style.shadowOpacity > 0.0f && style.shadowRadius >= 0.5f)
        {
            const juce::DropShadow shadow(
                style.shadowColour.withMultipliedAlpha(juce::jlimit(0.0f, 1.0f, style.shadowOpacity)),
                juce::roundToInt(style.shadowRadius),
                { juce::roundToInt(style.shadowOffsetX), juce::roundToInt(style.shadowOffsetY) });
            shadow.drawForPath(g, path);
        }

        g.setColour(style.background.withMultipliedAlpha(
            juce::jlimit(0.0f, 1.0f, style.backgroundOpacity)));
        g.fillPath(path);

        if (style.borderWidth > 0.0f)
        {
            g.setColour(style.border.withMultipliedAlpha(
                juce::jlimit(0.0f, 1.0f, style.borderOpacity)));
            g.strokePath(path, juce::PathStrokeType(style.borderWidth));
        }
    }
};

} // namespace px3::ui

// A label that paints itself as a speech bubble.
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

    using Style = px3::ui::SpeechBubble::Style;

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
    float getArrowHeight() const noexcept { return px3::ui::SpeechBubble::arrowInsetTop(style); }

    static juce::Path buildBubblePath(juce::Rectangle<float> bounds, const Style& style)
    {
        return px3::ui::SpeechBubble::buildPath(bounds, style);
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();

        px3::ui::SpeechBubble::paintBackground(g, bounds, style);

        // The text sits inside the body, clear of the arrow.
        auto textArea = bounds
                            .withTrimmedTop(px3::ui::SpeechBubble::arrowInsetTop(style))
                            .withTrimmedLeft(px3::ui::SpeechBubble::arrowInsetLeft(style))
                            .reduced(style.paddingX, style.paddingY);

        g.setColour(findColour(juce::Label::textColourId));
        g.setFont(getFont());
        g.drawText(getText(), textArea, getJustificationType(), true);
    }

private:
    Style style;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpeechBubbleLabel)
};
