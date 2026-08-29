#include "ToggleChipButton.h"

namespace px3::ui
{

ToggleChipButton::ToggleChipButton()
{
    setClickingTogglesState(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void ToggleChipButton::setFontSize(float size)
{
    fontSize = juce::jlimit(6.0f, 20.0f, size);
    repaint();
}

void ToggleChipButton::setAccentColour(juce::Colour colour)
{
    accent = colour;
    repaint();
}

void ToggleChipButton::setStateLabels(juce::String onText, juce::String offText)
{
    onLabel = std::move(onText);
    offLabel = std::move(offText);
    repaint();
}

void ToggleChipButton::paintButton(juce::Graphics& g,
                                   bool shouldDrawButtonAsHighlighted,
                                   bool shouldDrawButtonAsDown)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    if (area.isEmpty())
    {
        return;
    }

    const auto on = getToggleState();
    const auto enabled = isEnabled();
    const auto lit = enabled ? accent : accent.withSaturation(0.0f);

    // Off is the plain chip every static label uses, so an untoggled Freeze
    // sits with the rest of the interface rather than shouting.
    // No border: a solid rounded fill only. State is carried entirely by the
    // fill colour and the text on top of it.
    const auto fill = on ? lit.withAlpha(enabled ? 0.90f : 0.45f)
                         : juce::Colour::fromRGBA(255, 255, 255, shouldDrawButtonAsDown ? 86 : 62);

    g.setColour(shouldDrawButtonAsHighlighted ? fill.brighter(0.18f) : fill);
    g.fillRoundedRectangle(area, kCornerRadius);

    // On, the text sits on a filled accent chip, so it goes dark for contrast
    // rather than staying pale on pale.
    const auto textColour = on ? juce::Colour::fromRGB(16, 18, 24).withAlpha(enabled ? 1.0f : 0.6f)
                               : juce::Colour::fromRGB(232, 232, 232).withAlpha(enabled ? 1.0f : 0.6f);

    g.setColour(textColour);
    g.setFont(juce::FontOptions(fontSize));
    const auto stateLabel = on ? onLabel : offLabel;
    const auto text = stateLabel.isNotEmpty() ? stateLabel : getButtonText();

    // Fitted rather than plain drawText: these chips are packed six to a row on
    // the busier cards, and a caption that does not fit should shrink rather
    // than silently lose its last characters.
    g.drawFittedText(text, area.reduced(4.0f, 0.0f).toNearestInt(),
                     juce::Justification::centred, 1, 0.75f);
}

} // namespace px3::ui
