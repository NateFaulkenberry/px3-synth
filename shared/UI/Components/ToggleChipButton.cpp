#include "ToggleChipButton.h"

namespace px3::ui
{

ToggleChipButton::ToggleChipButton()
{
    setClickingTogglesState(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void ToggleChipButton::setOffTint(float amount)
{
    offTint = juce::jlimit(0.0f, 1.0f, amount);
    repaint();
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
    // Off was a neutral white on every card, which read as a stray control on
    // the strongly coloured ones. Tinted toward the card's accent it belongs to
    // its card while still being obviously unlit: heavily desaturated, and at
    // the same low alpha as before.
    const auto offNeutral = juce::Colour::fromRGBA(255, 255, 255, shouldDrawButtonAsDown ? 86 : 62);
    const auto offTinted = accent.withSaturation(0.42f)
                                 .withBrightness(0.92f)
                                 .withAlpha(shouldDrawButtonAsDown ? 0.34f : 0.24f);
    const auto offFill = enabled ? offNeutral.interpolatedWith(offTinted, offTint) : offNeutral;

    // On is the card's OWN colour, darkened - not the bright accent it used to
    // flip to. That bright fill needed near-black text to stay legible, and the
    // two together read as a different palette from the card around them: a
    // deep red DOOM card grew pale pink chips with black captions. A darker,
    // slightly richer shade of the same hue keeps every chip inside its card's
    // palette, and keeps the same light text in both states.
    //
    // The fill no longer has to carry the state on its own, which is what let
    // this get darker rather than brighter: each chip names the state it is in.
    const auto onFill = lit.withSaturation(juce::jmin(1.0f, lit.getSaturation() * 1.15f))
                           .withBrightness(lit.getBrightness() * 0.50f)
                           .withAlpha(enabled ? 0.95f : 0.45f);

    const auto fill = on ? onFill : offFill;

    g.setColour(shouldDrawButtonAsHighlighted ? fill.brighter(0.18f) : fill);
    g.fillRoundedRectangle(area, kCornerRadius);

    const auto textColour = juce::Colour::fromRGB(232, 232, 232).withAlpha(enabled ? 1.0f : 0.6f);

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
