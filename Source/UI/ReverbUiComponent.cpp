#include "ReverbUiComponent.h"

#include "UIConfig.h"

ReverbUiComponent::ReverbUiComponent(juce::ToggleButton& enabledButtonIn,
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

void ReverbUiComponent::setAccentColour(juce::Colour accentIn)
{
    accent = accentIn;
    repaint();
}

void ReverbUiComponent::setActive(bool enabled)
{
    isActive = enabled;
    amountKnob.setEnabled(isActive);
    amountLabel.setEnabled(isActive);
    typeBox.setEnabled(isActive);
    typeLabel.setEnabled(isActive);
    amountKnob.getProperties().set("psychedelicBypassGray", !isActive);
    repaint();
}

void ReverbUiComponent::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);
    repaint();
}

void ReverbUiComponent::resized()
{
    const auto padX = uiConfig != nullptr ? uiConfig->getInt("fx.reverb.layout.padX", 10) : 10;
    const auto padY = uiConfig != nullptr ? uiConfig->getInt("fx.reverb.layout.padY", 8) : 8;
    auto area = getLocalBounds().reduced(padX, padY);

    auto top = area.removeFromTop(22);
    enabledButton.setBounds(top.removeFromLeft(22));

    area.removeFromTop(4);
    auto bottom = area.removeFromBottom(24);
    typeLabel.setBounds(bottom.removeFromLeft(56));
    typeBox.setBounds(bottom.reduced(2, 1));

    area.removeFromTop(4);
    const auto labelArea = area.removeFromBottom(22);
    const auto knobSize = juce::jmin(82, juce::jmin(area.getWidth(), area.getHeight()));
    amountKnob.setBounds(juce::Rectangle<int>(knobSize, knobSize).withCentre(area.getCentre()));
    amountLabel.setBounds(labelArea);
}

void ReverbUiComponent::paint(juce::Graphics& g)
{
    const auto borderPad = uiConfig != nullptr ? uiConfig->getFloat("fx.reverb.visual.borderPadding", 6.0f) : 6.0f;
    const auto radius = uiConfig != nullptr ? uiConfig->getFloat("fx.reverb.visual.cornerRadius", 8.0f) : 8.0f;
    auto bounds = getLocalBounds().toFloat().reduced(borderPad);
    g.setColour(isActive ? accent.withAlpha(0.08f) : juce::Colour::fromRGBA(120, 120, 120, 30));
    g.fillRoundedRectangle(bounds, radius);

    const auto textColour = uiConfig != nullptr
                                ? uiConfig->getColour("fx.reverb.visual.onLabel.textColour", juce::Colour::fromRGB(232, 232, 232))
                                : juce::Colour::fromRGB(232, 232, 232);
    const auto fontSize = uiConfig != nullptr ? uiConfig->getFloat("fx.reverb.visual.onLabel.fontSize", 11.5f) : 11.5f;
    const auto textBounds = uiConfig != nullptr
                                ? uiConfig->getRect("fx.reverb.visual.onLabel.bounds", getLocalBounds(), { 36, 11, 24, 14 })
                                : juce::Rectangle<int>(36, 11, 24, 14);
    const auto text = uiConfig != nullptr ? uiConfig->getString("fx.reverb.visual.onLabel.text", "ON") : juce::String("ON");

    g.setColour(textColour.withAlpha(isActive ? 1.0f : 0.6f));
    g.setFont(juce::FontOptions(fontSize));
    g.drawText(text, textBounds, juce::Justification::centredLeft, false);
}
