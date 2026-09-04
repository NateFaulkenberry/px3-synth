#pragma once

#include <JuceHeader.h>

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

    void paint(juce::Graphics& g) override
    {
        if (getText().isEmpty())
        {
            return;
        }

        const auto compactLabel = static_cast<bool>(getProperties().getWithDefault("compactLabel", false));
        const auto horizontalPadding = compactLabel ? 4.0f : 8.0f;
        auto area = getLocalBounds().toFloat().reduced(2.0f, 1.0f);

        g.setColour(style.background.withMultipliedAlpha(juce::jlimit(0.0f, 1.0f, style.backgroundOpacity)));
        g.fillRoundedRectangle(area, style.cornerRadius);

        if (style.outlineWidth > 0.0f)
        {
            g.setColour(style.outline.withMultipliedAlpha(juce::jlimit(0.0f, 1.0f, style.outlineOpacity)));
            g.drawRoundedRectangle(area, style.cornerRadius, style.outlineWidth);
        }

        g.setColour(findColour(juce::Label::textColourId));
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

private:
    Style style;
};

} // namespace px3::ui
