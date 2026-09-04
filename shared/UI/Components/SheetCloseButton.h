#pragma once

#include <JuceHeader.h>

#include "UIConfig.h"

#include <type_traits>

namespace px3::ui
{

// The close control: an X in a ring, built as a Path for the same reasons the
// power symbol is - two primitives cost less than parsing artwork, stay crisp
// at any size, and can be tinted per use without a second asset.
//
// It deliberately mirrors BypassButton's seat and ring so the two read as
// members of one family: the power button turns a section on, this one shuts
// something, and nothing else on the panel is a circular glyph.
//
// Header-only and in shared rather than beside the bus-insert sheets it was
// written for, because it is now the close control everywhere - the sheets,
// the macro depth panel and the update notice. One glyph, one set of style
// keys, three sizes.
class SheetCloseButton final : public juce::Button
{
public:
    struct Style
    {
        int size { 24 };
        // From the top-right corner of whatever the button is placed against.
        // A corner rather than a centre so the anchor does not move when the
        // thing it sits on changes height.
        int offsetX { 0 };
        int offsetY { 0 };
        float ringWidth { 1.6f };
        float glyphWidth { 2.0f };
        float glyphInset { 0.32f };   // fraction of the button, per side
        juce::Colour seat { juce::Colour::fromRGBA(12, 14, 20, 190) };
        juce::Colour ring { juce::Colour::fromRGBA(237, 241, 247, 150) };
        juce::Colour glyph { juce::Colour::fromRGB(237, 241, 247) };
        juce::Colour hover { juce::Colour::fromRGB(185, 191, 200) };
    };

    SheetCloseButton()
        : juce::Button("CLOSE")
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTooltip("Close");
    }

    void applyStyle(const Style& styleIn)
    {
        style = styleIn;
        repaint();
    }

    const Style& getStyle() const noexcept { return style; }

    // Reads the style keys under `base`, leaving anything absent at its current
    // value. Every user reads the same names, so a close button is styled the
    // same way wherever it appears.
    static void readStyleFrom(const UIConfig* config, const juce::String& base, Style& style)
    {
        if (config == nullptr) { return; }

        const auto number = [&](const char* key, auto& field)
        {
            if (const auto value = config->getValue(base + key); ! value.isVoid())
            {
                field = static_cast<std::remove_reference_t<decltype(field)>>(
                    static_cast<double>(value));
            }
        };

        const auto colour = [&](const char* key, juce::Colour& field)
        {
            if (const auto value = config->getValue(base + key); ! value.isVoid())
            {
                field = config->getColour(base + key, field);
            }
        };

        number(".size", style.size);
        number(".offsetX", style.offsetX);
        number(".offsetY", style.offsetY);
        number(".ringWidth", style.ringWidth);
        number(".glyphWidth", style.glyphWidth);
        number(".glyphInset", style.glyphInset);
        colour(".seatColor", style.seat);
        colour(".ringColor", style.ring);
        colour(".glyphColor", style.glyph);
        colour(".hoverColor", style.hover);
    }

    // Where this button goes against the top-right corner of `host`, in the
    // same coordinates. Shared so every close button is anchored identically
    // rather than each site re-deriving the same two sums.
    juce::Rectangle<int> boundsWithin(juce::Rectangle<int> host) const
    {
        return { host.getRight() - style.size + style.offsetX,
                 host.getY() + style.offsetY,
                 style.size, style.size };
    }

private:
    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const auto side = juce::jmin(bounds.getWidth(), bounds.getHeight());
        if (side <= 0.0f)
        {
            return;
        }

        const auto box = juce::Rectangle<float>(side, side).withCentre(bounds.getCentre());
        const auto tint = shouldDrawButtonAsHighlighted ? style.hover : style.glyph;

        g.setColour(style.seat.withMultipliedAlpha(shouldDrawButtonAsDown ? 1.2f : 1.0f));
        g.fillEllipse(box);

        g.setColour((shouldDrawButtonAsHighlighted ? style.hover : style.ring)
                        .withMultipliedAlpha(isEnabled() ? 1.0f : 0.4f));
        g.drawEllipse(box.reduced(style.ringWidth * 0.5f), style.ringWidth);

        // The X, inset from the ring so the two never touch.
        const auto inset = juce::jlimit(0.1f, 0.45f, style.glyphInset) * side;
        const auto glyph = box.reduced(inset);
        g.setColour(tint.withMultipliedAlpha(isEnabled() ? 1.0f : 0.4f));
        g.drawLine(glyph.getX(), glyph.getY(), glyph.getRight(), glyph.getBottom(), style.glyphWidth);
        g.drawLine(glyph.getRight(), glyph.getY(), glyph.getX(), glyph.getBottom(), style.glyphWidth);
    }

    Style style;
};

} // namespace px3::ui
