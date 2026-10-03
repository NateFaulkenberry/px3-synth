#include "ToggleChipButton.h"
#include "Theme.h"

namespace px3::ui
{

ToggleChipButton::ToggleChipButton()
{
    setClickingTogglesState(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    getToggleStateValue().addListener(this);
}

ToggleChipButton::~ToggleChipButton()
{
    getToggleStateValue().removeListener(this);
}

juce::String ToggleChipButton::currentCaption() const
{
    const auto& label = getToggleState() ? onLabel : offLabel;
    return label.isNotEmpty() ? label : getButtonText();
}

void ToggleChipButton::syncCaptionToState()
{
    const auto& label = getToggleState() ? onLabel : offLabel;
    if (label.isNotEmpty() && getButtonText() != label)
    {
        setButtonText(label);
    }
}

void ToggleChipButton::clicked()
{
    juce::ToggleButton::clicked();
    syncCaptionToState();
}

void ToggleChipButton::valueChanged(juce::Value& value)
{
    juce::ignoreUnused(value);
    syncCaptionToState();
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
    syncCaptionToState();
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

    // The theme's switch: a recessed key. Off is the inset well with the
    // caption in the label colour; on lights a stripe and the caption in the
    // card's identity colour and lifts the face slightly. The same in every
    // card - the per-card fill colours in UIConfig are no longer painted (they
    // made one card's switches a different control from the next card's).
    namespace tc = px3::ui::theme::colour;
    const auto on = getToggleState();
    const auto enabled = isEnabled();
    const auto lit = enabled ? accent : accent.withSaturation(0.0f);

    px3::ui::theme::drawInset(g, area, px3::ui::theme::space::insetRadius);
    if (on)
    {
        g.setColour(lit.withAlpha(enabled ? 0.16f : 0.08f));
        g.fillRoundedRectangle(area.reduced(1.0f), px3::ui::theme::space::insetRadius);
        g.setColour(lit.withAlpha(enabled ? 0.9f : 0.4f));
        g.fillRoundedRectangle(area.reduced(4.0f, 0.0f).removeFromBottom(2.0f).translated(0.0f, -1.0f), 1.0f);
    }
    if (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown)
    {
        g.setColour(juce::Colours::white.withAlpha(shouldDrawButtonAsDown ? 0.10f : 0.05f));
        g.fillRoundedRectangle(area.reduced(1.0f), px3::ui::theme::space::insetRadius);
    }
    juce::ignoreUnused(offTint);

    const auto textColour = (on ? lit.interpolatedWith(tc::textValue, 0.35f) : tc::textLabel)
                                .withAlpha(enabled ? 1.0f : 0.5f);
    g.setColour(textColour);
    g.setFont(px3::ui::theme::font(px3::ui::theme::Type::label)
                  .withHeight(juce::jmin(px3::ui::theme::size(px3::ui::theme::Type::label), area.getHeight() - 4.0f)));
    const auto text = currentCaption();

    // Fitted rather than plain drawText: these chips are packed six to a row on
    // the busier cards, and a caption that does not fit should shrink rather
    // than silently lose its last characters.
    g.drawFittedText(text, area.reduced(4.0f, 0.0f).toNearestInt(),
                     juce::Justification::centred, 1, 0.75f);
}

} // namespace px3::ui
