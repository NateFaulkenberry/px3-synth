#include "VibeComponent.h"

#include "BypassButton.h"
#include "CardInner.h"

#include "UIConfig.h"

VibeComponent::VibeComponent(juce::ToggleButton& enabledButtonIn,
                                 juce::Slider& amountKnobIn,
                                 juce::ComboBox& typeBoxIn,
                                 juce::Label& typeLabelIn,
                                 juce::Colour accentIn)
    : enabledButton(enabledButtonIn),
      amountKnob(amountKnobIn),
      typeBox(typeBoxIn),
      typeLabel(typeLabelIn),
      accent(accentIn)
{
    // The card background toggles this section, so the pointer says it is
    // clickable. Child controls carry their own cursors.
    setMouseCursor(juce::MouseCursor::PointingHandCursor);

    addAndMakeVisible(enabledButton);
    addAndMakeVisible(amountKnob);
    addAndMakeVisible(typeBox);
    addAndMakeVisible(typeLabel);
}

void VibeComponent::setAccentColour(juce::Colour accentIn)
{
    accent = accentIn;
    repaint();
}

void VibeComponent::setActive(bool enabled)
{
    isActive = enabled;
    amountKnob.setEnabled(isActive);
    typeBox.setEnabled(isActive);
    typeLabel.setEnabled(isActive);
    amountKnob.getProperties().set("psychedelicBypassGray", !isActive);
    repaint();
}

void VibeComponent::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);
    // cardInner parses its rows in resized(), so a live reload has to redo
    // the layout as well as the paint.
    resized();
    repaint();
}

void VibeComponent::resized()
{
    card.setStyleKey("vibe");
    card.setConfig(uiConfig);
    card.layout(getLocalBounds());

    // The power glyph lights in this card's own identity colour.

    if (auto* power = dynamic_cast<px3::ui::BypassButton*>(&enabledButton))

    {

        power->setAccentColour(card.style().border.colour);

    }


    inner.setStylePath("cards.vibe.cardInner");
    inner.setConfig(uiConfig);
    inner.setRowCount(2);
    inner.layout(card.contentBelowTitle());

    // The power toggle is pinned to cardInner's corner, outside the flex flow,
    // so it stays put no matter what the first row contains.
    enabledButton.setBounds(inner.powerBounds());

    using px3::ui::ControlShape;


    // Row 1: the amount knob and its existing label underneath.
    {
        auto flex = inner.rowFlex(0);
        const auto gap = inner.rowGap(0);
        const auto row = inner.rowContent(0);

        flex.items.add(juce::FlexItem(104.0f, static_cast<float>(juce::jmax(1, row.getHeight())))
                           .withMargin(gap));
        flex.performLayout(row.toFloat());

        // Knob only - the "AMOUNT" caption was removed; the card title and the
        // knob's own tooltip already say what it is.
        px3::ui::layoutLabelledControl(flex.items.getReference(0).currentBounds.toNearestInt(),
                                       { nullptr, &amountKnob, nullptr,
                                         ControlShape::square, 0, 0, 82 },
                                       inner.rowControl(0));
    }

    // Row 2: the type dropdown with its label.
    {
        auto flex = inner.rowFlex(1);
        const auto gap = inner.rowGap(1);
        const auto row = inner.rowContent(1);

        flex.items.add(juce::FlexItem(static_cast<float>(juce::jmax(1, row.getWidth())),
                                      static_cast<float>(juce::jmax(1, row.getHeight())))
                           .withMargin(gap));
        flex.performLayout(row.toFloat());

        px3::ui::layoutLabelledControl(flex.items.getReference(0).currentBounds.toNearestInt(),
                                       { &typeLabel, &typeBox, nullptr,
                                         ControlShape::stretch, 14, 0, 24 },
                                       inner.rowControl(1));
    }
}

void VibeComponent::mouseUp(const juce::MouseEvent& event)
{
    // Clicking the card's background toggles its power, the same as clicking
    // the button. No tooltip here: the whole card is not a control, and a card
    // that explained itself on hover would be noise.
    if (px3::ui::isCardBackgroundToggleClick(event))
    {
        enabledButton.setToggleState(! enabledButton.getToggleState(), juce::sendNotification);
    }
}

void VibeComponent::paint(juce::Graphics& g)
{
    // Card and title are owned here, not by FxPanel. Because the card follows
    // this component's bounds, drag-and-drop reordering moves it automatically -
    // the panel no longer has to paint anything at the dragged position.
    card.setStyleKey("vibe");
    card.setConfig(uiConfig);
    card.layout(getLocalBounds());

    if (isActive)
    {
        card.draw(g, "VIBE");
    }
    else
    {
        card.drawInactive(g, "VIBE");
    }

}
