#pragma once

#include <JuceHeader.h>

namespace px3::ui
{

// A labelled toggle drawn as a chip - the same rounded shape as the plugin's
// static chip labels, but it carries its own text and shows its state.
//
// It replaces a tick box sitting beside a separately painted caption. That
// arrangement had two problems: the caption could not be clicked, because it was
// paint output rather than a component, and nothing tied the two together
// visually. One button that draws its own label solves both.
//
// Off reads as a plain chip. On fills with the accent colour and brightens the
// text, so the state is obvious without having to find a tick.
class ToggleChipButton final : public juce::ToggleButton
{
public:
    ToggleChipButton();

    void setAccentColour(juce::Colour colour);

    // Cards that pack many chips into one row need a smaller face than the
    // default. Set per card from cards.<key>.controls.toggleFontSize.
    void setFontSize(float size);

    // How far the OFF chip is tinted toward the card's accent. 0 leaves the
    // neutral white every static chip label uses; 1 is fully the card's colour.
    void setOffTint(float amount);

    // Distinct text per state, so the chip says which state it is IN rather
    // than leaving the fill colour to carry that alone.
    void setStateLabels(juce::String onText, juce::String offText);

private:
    void paintButton(juce::Graphics& g,
                     bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override;

    static constexpr float kCornerRadius = 7.0f;

    juce::Colour accent { juce::Colour::fromRGB(120, 200, 255) };
    float fontSize { 11.5f };
    float offTint { 0.0f };
    juce::String onLabel;
    juce::String offLabel;
};

} // namespace px3::ui
