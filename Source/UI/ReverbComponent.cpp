#include "ReverbComponent.h"

#include "CardInner.h"

#include "UIConfig.h"

ReverbComponent::ReverbComponent(juce::ToggleButton& enabledButtonIn,
                                     juce::Slider& amountKnobIn,
                                     juce::Label& amountLabelIn,
                                     juce::ComboBox& typeBoxIn,
                                     juce::Label& typeLabelIn,
                                     juce::Colour accentIn)
    : enabledButton(enabledButtonIn),
      amountKnob(amountKnobIn),
      amountLabel(amountLabelIn),
      typeBox(typeBoxIn),
      typeLabel(typeLabelIn),
      accent(accentIn)
{
    addAndMakeVisible(enabledButton);
    addAndMakeVisible(amountKnob);
    addAndMakeVisible(amountLabel);
    addAndMakeVisible(typeBox);
    addAndMakeVisible(typeLabel);
}

void ReverbComponent::setAccentColour(juce::Colour accentIn)
{
    accent = accentIn;
    repaint();
}

void ReverbComponent::setActive(bool enabled)
{
    isActive = enabled;
    amountKnob.setEnabled(isActive);
    amountLabel.setEnabled(isActive);
    typeBox.setEnabled(isActive);
    typeLabel.setEnabled(isActive);
    amountKnob.getProperties().set("psychedelicBypassGray", !isActive);
    repaint();
}

void ReverbComponent::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);
    // cardInner parses its rows in resized(), so a live reload has to redo
    // the layout as well as the paint.
    resized();
    repaint();
}

void ReverbComponent::resized()
{
    card.setStyleKey("reverb");
    card.setConfig(uiConfig);
    card.layout(getLocalBounds());

    inner.setStylePath("cards.reverb.cardInner");
    inner.setConfig(uiConfig);
    inner.setRowCount(3);
    inner.layout(card.contentBelowTitle());

    using px3::ui::ControlShape;

    // Row 1: the bypass button, with the painted "ON" text beside it.
    {
        auto flex = inner.rowFlex(0);
        const auto gap = inner.rowGap(0);
        const auto row = inner.rowContent(0);
        const auto cellHeight = static_cast<float>(juce::jmax(1, row.getHeight()));

        flex.items.add(juce::FlexItem(24.0f, cellHeight).withMargin(gap));
        flex.items.add(juce::FlexItem(26.0f, cellHeight).withMargin(gap));
        flex.performLayout(row.toFloat());

        const auto buttonCell = flex.items.getReference(0).currentBounds.toNearestInt();
        enabledButton.setBounds(juce::Rectangle<int>(22, 22).withCentre(buttonCell.getCentre()));
        onLabelBounds = flex.items.getReference(1).currentBounds.toNearestInt();
    }

    // Row 2: the amount knob and its existing label underneath.
    {
        auto flex = inner.rowFlex(1);
        const auto gap = inner.rowGap(1);
        const auto row = inner.rowContent(1);

        flex.items.add(juce::FlexItem(104.0f, static_cast<float>(juce::jmax(1, row.getHeight())))
                           .withMargin(gap));
        flex.performLayout(row.toFloat());

        px3::ui::layoutLabelledControl(flex.items.getReference(0).currentBounds.toNearestInt(),
                                       { nullptr, &amountKnob, &amountLabel,
                                         ControlShape::square, 0, 22, 82 },
                                       inner.rowControl(1));
    }

    // Row 3: the type dropdown with its label.
    {
        auto flex = inner.rowFlex(2);
        const auto gap = inner.rowGap(2);
        const auto row = inner.rowContent(2);

        flex.items.add(juce::FlexItem(static_cast<float>(juce::jmax(1, row.getWidth())),
                                      static_cast<float>(juce::jmax(1, row.getHeight())))
                           .withMargin(gap));
        flex.performLayout(row.toFloat());

        px3::ui::layoutLabelledControl(flex.items.getReference(0).currentBounds.toNearestInt(),
                                       { &typeLabel, &typeBox, nullptr,
                                         ControlShape::stretch, 14, 0, 24 },
                                       inner.rowControl(2));
    }
}

void ReverbComponent::paint(juce::Graphics& g)
{
    // Card and title are owned here, not by FxPanel. Because the card follows
    // this component's bounds, drag-and-drop reordering moves it automatically -
    // the panel no longer has to paint anything at the dragged position.
    card.setStyleKey("reverb");
    card.setConfig(uiConfig);
    card.layout(getLocalBounds());

    if (isActive)
    {
        card.draw(g, "REVERB");
    }
    else
    {
        card.drawInactive(g, "REVERB");
    }

    const auto textColour = uiConfig != nullptr
                                ? uiConfig->getColour("fx.reverb.visual.onLabel.textColour", juce::Colour::fromRGB(232, 232, 232))
                                : juce::Colour::fromRGB(232, 232, 232);
    const auto fontSize = uiConfig != nullptr ? uiConfig->getFloat("fx.reverb.visual.onLabel.fontSize", 11.5f) : 11.5f;
    // Beside the button, wherever row 1 put it.
    const auto textBounds = onLabelBounds;

    g.setColour(textColour.withAlpha(isActive ? 1.0f : 0.6f));
    g.setFont(juce::FontOptions(fontSize));
    g.drawText("ON", textBounds, juce::Justification::centredLeft, false);
}
