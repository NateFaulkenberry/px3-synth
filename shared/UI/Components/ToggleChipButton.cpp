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

void ToggleChipButton::setStateColours(std::optional<juce::Colour> on,
                                      std::optional<juce::Colour> off)
{
    onColour = on;
    offColour = off;
    repaint();
}

void ToggleChipButton::setTextColours(std::optional<juce::Colour> on,
                                      std::optional<juce::Colour> off)
{
    onTextColour = on;
    offTextColour = off;
    repaint();
}

void ToggleChipButton::applyFromConfig(const UIConfig* config,
                                      const juce::String& styleKey,
                                      std::initializer_list<juce::Button*> buttons)
{
    if (config == nullptr) { return; }

    const auto key = "cards." + styleKey + ".controls.";

    // Absent means "keep the shade derived from the card's accent", so these are
    // optionals rather than colours with defaults - a default would replace the
    // derivation for every card to serve the one that wanted a scheme.
    const auto optional = [config](const juce::String& name) -> std::optional<juce::Colour>
    {
        if (config->getValue(name).isVoid()) { return std::nullopt; }
        return config->getColour(name, juce::Colours::white);
    };

    const auto font = config->getFloat(key + "toggleFontSize", 11.5f);
    const auto offTint = config->getFloat(key + "toggleOffTint", 0.0f);

    for (auto* button : buttons)
    {
        auto* chip = dynamic_cast<ToggleChipButton*>(button);
        if (chip == nullptr) { continue; }

        chip->setFontSize(font);
        chip->setOffTint(offTint);
        chip->setStateColours(optional(key + "toggleOnColor"), optional(key + "toggleOffColor"));
        chip->setTextColours(optional(key + "toggleOnTextColor"),
                             optional(key + "toggleOffTextColor"));
    }
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

    // A configured colour wins over the derived one, and keeps the pressed and
    // disabled treatments so a styled chip still behaves like a button.
    const auto configured = on ? onColour : offColour;
    auto fill = on ? onFill : offFill;

    if (configured.has_value())
    {
        fill = configured->withMultipliedAlpha(enabled ? 1.0f : 0.45f);
        if (shouldDrawButtonAsDown) { fill = fill.brighter(0.10f); }
        if (! enabled) { fill = fill.withSaturation(0.0f); }
    }

    g.setColour(shouldDrawButtonAsHighlighted ? fill.brighter(0.18f) : fill);
    g.fillRoundedRectangle(area, kCornerRadius);

    const auto configuredText = on ? onTextColour : offTextColour;
    const auto textColour = configuredText.value_or(juce::Colour::fromRGB(232, 232, 232))
                                .withAlpha(enabled ? 1.0f : 0.6f);

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
