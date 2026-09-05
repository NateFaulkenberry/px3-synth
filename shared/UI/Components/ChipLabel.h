#pragma once

#include <JuceHeader.h>

#include "UIConfig.h"

#include <initializer_list>

namespace px3::ui
{

// The rounded "chip" label used throughout the plugin.
//
// It exists because setting only Label::backgroundColourId and
// outlineColourId gets you JUCE's default rendering, which is a SQUARE
// rectangle. The labels the editor owns were drawn by a class that rounded
// them; the ones ModPanel and AmpEnvelopeComponent owned were not, which is why
// LFO 2/3 and ENV 1/2/3 had square chips while everything else was rounded.
//
// One definition, so the two cannot drift apart again.
class ChipLabel : public juce::Label
{
public:
    // The chip's own colours, so a card can carry a scheme rather than every
    // caption in the plugin being the same translucent white.
    //
    // Defaulted to what was hard-coded here, so a chip nobody has styled looks
    // exactly as it did. Opacity is separate from the colour for the same
    // reason it is everywhere else in this project: "how see-through" is a
    // thing people adjust on its own, and burying it in a hex value makes that
    // an edit to two digits in the middle of a number.
    struct Style
    {
        juce::Colour background { juce::Colours::white };
        float backgroundOpacity { 54.0f / 255.0f };
        juce::Colour outline { juce::Colours::white };
        float outlineOpacity { 96.0f / 255.0f };
        float outlineWidth { 1.0f };
        float cornerRadius { kCornerRadius };
    };

    void setChipStyle(const Style& newStyle)
    {
        style = newStyle;
        repaint();
    }

    const Style& getChipStyle() const noexcept { return style; }

    // Greys the chip out when its card is bypassed, matching the knobs beside
    // it. A bypassed card already dims its artwork and desaturates its knobs;
    // without this the captions kept their full colour scheme and were the one
    // thing on a switched-off card still shouting.
    //
    // A repaint rather than a colour edit: the style is what the card's config
    // says the chip IS, and rewriting it on bypass would mean restoring it on
    // un-bypass and losing anything applied in between.
    void setGreyedOut(bool shouldBeGrey)
    {
        if (greyedOut == shouldBeGrey) { return; }
        greyedOut = shouldBeGrey;
        repaint();
    }

    bool isGreyedOut() const noexcept { return greyedOut; }

    void paint(juce::Graphics& g) override
    {
        if (getText().isEmpty())
        {
            return;
        }

        const auto compactLabel = static_cast<bool>(getProperties().getWithDefault("compactLabel", false));
        const auto horizontalPadding = compactLabel ? 4.0f : 8.0f;
        auto area = getLocalBounds().toFloat().reduced(2.0f, 1.0f);

        g.setColour(shade(style.background.withMultipliedAlpha(juce::jlimit(0.0f, 1.0f, style.backgroundOpacity))));
        g.fillRoundedRectangle(area, style.cornerRadius);

        if (style.outlineWidth > 0.0f)
        {
            g.setColour(shade(style.outline.withMultipliedAlpha(juce::jlimit(0.0f, 1.0f, style.outlineOpacity))));
            g.drawRoundedRectangle(area, style.cornerRadius, style.outlineWidth);
        }

        g.setColour(shade(findColour(juce::Label::textColourId)));
        g.setFont(getFont());
        // Fitted, not ellipsised. drawText's last argument is
        // useEllipsesIfTooBig, and a caption that does not quite fit its chip
        // is far more useful shrunk by a few percent than cut short: RESONANCE
        // overflowed its 84px chip by ONE pixel and read "Resonan...". Thirteen
        // labels were over, from that one pixel up to AUTO GAIN by twelve.
        //
        // drawFittedText shrinks only as much as it needs to, so a label that
        // already fits is drawn at its full size and is untouched by this.
        g.drawFittedText(getText(),
                         area.reduced(horizontalPadding, 0.0f).toNearestInt(),
                         juce::Justification::centred,
                         1,
                         kMinimumTextScale);
    }

    static constexpr float kCornerRadius = 7.0f;
    // Enough for the longest caption in the plugin - AUTO GAIN, which needs
    // about 0.78 of its natural width - with room to spare.
    static constexpr float kMinimumTextScale = 0.7f;

    // The chip style a card declares, read from cards.<styleKey>.controls.
    //
    // Shared because the cards are built two ways: FxCardComponent owns its
    // captions, while Delay, Mood and Vibe are handed theirs by whoever owns
    // them. Both need the same keys to mean the same thing, and a second copy
    // of these lookups is how that stops being true.
    static Style styleFromConfig(const UIConfig* config, const juce::String& styleKey)
    {
        Style style;
        if (config == nullptr) { return style; }

        const auto key = "cards." + styleKey + ".controls.";
        style.background = config->getColour(key + "labelBackground", style.background);
        style.backgroundOpacity = config->getFloat(key + "labelBackgroundOpacity",
                                                   style.backgroundOpacity);
        style.outline = config->getColour(key + "labelOutline", style.outline);
        style.outlineOpacity = config->getFloat(key + "labelOutlineOpacity", style.outlineOpacity);
        style.outlineWidth = config->getFloat(key + "labelOutlineWidth", style.outlineWidth);
        style.cornerRadius = config->getFloat(key + "labelCornerRadius", style.cornerRadius);
        return style;
    }

    // Applies that style, and the text colour and font beside it, to captions a
    // component was handed rather than owns.
    //
    // Takes juce::Label because that is what those components hold; a caption
    // that is not a chip keeps its colour and font and simply has no chip to
    // style, which is what a plain Label should do rather than an error.
    static void applyFromConfig(const UIConfig* config,
                                const juce::String& styleKey,
                                std::initializer_list<juce::Label*> labels)
    {
        if (config == nullptr) { return; }

        const auto key = "cards." + styleKey + ".controls.";
        const auto chip = styleFromConfig(config, styleKey);
        const auto colour = config->getColour(key + "labelColour",
                                              juce::Colour::fromRGB(232, 232, 232));
        const auto font = config->getFloat(key + "labelFontSize", 11.5f);

        for (auto* label : labels)
        {
            if (label == nullptr) { continue; }
            label->setColour(juce::Label::textColourId, colour);
            label->setFont(juce::FontOptions(font));
            if (auto* asChip = dynamic_cast<ChipLabel*>(label)) { asChip->setChipStyle(chip); }
        }
    }

    // Greys out captions a component was handed rather than owns. A caption
    // that is not a chip is left alone, the same as applyFromConfig does.
    static void setGreyedOut(bool shouldBeGrey, std::initializer_list<juce::Label*> labels)
    {
        for (auto* label : labels)
        {
            if (auto* asChip = dynamic_cast<ChipLabel*>(label)) { asChip->setGreyedOut(shouldBeGrey); }
        }
    }

private:
    // Perceived brightness, not the average of the channels, and the same
    // measure the knob's bypass greyscale uses - so a grey caption and a grey
    // knob beside it are the same grey rather than two different ones.
    juce::Colour shade(juce::Colour colour) const
    {
        if (! greyedOut) { return colour; }
        const auto value = juce::jlimit(0.0f, 1.0f, colour.getPerceivedBrightness());
        return juce::Colour::fromFloatRGBA(value, value, value, colour.getFloatAlpha());
    }

    Style style;
    bool greyedOut { false };
};

} // namespace px3::ui
