#include "VibeComponent.h"

#include "UIConfig.h"

VibeComponent::VibeComponent(juce::ToggleButton& enabledButtonIn,
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

void VibeComponent::setAccentColour(juce::Colour accentIn)
{
    accent = accentIn;
    repaint();
}

void VibeComponent::setActive(bool enabled)
{
    isActive = enabled;
    amountKnob.setEnabled(isActive);
    amountLabel.setEnabled(isActive);
    typeBox.setEnabled(isActive);
    typeLabel.setEnabled(isActive);
    amountKnob.getProperties().set("psychedelicBypassGray", !isActive);
    repaint();
}

void VibeComponent::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);
    repaint();
}

void VibeComponent::resized()
{
    const auto padX = uiConfig != nullptr ? uiConfig->getInt("fx.vibe.layout.padX", 10) : 10;
    const auto padY = uiConfig != nullptr ? uiConfig->getInt("fx.vibe.layout.padY", 8) : 8;
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

void VibeComponent::paint(juce::Graphics& g)
{
    const auto borderPad = uiConfig != nullptr ? uiConfig->getFloat("fx.vibe.visual.borderPadding", 6.0f) : 6.0f;
    const auto radius = uiConfig != nullptr ? uiConfig->getFloat("fx.vibe.visual.cornerRadius", 8.0f) : 8.0f;
    const auto bgTintAlpha = uiConfig != nullptr ? uiConfig->getFloat("fx.vibe.visual.bgTintAlpha", 0.08f) : 0.08f;
    const auto topFillAlpha = uiConfig != nullptr ? uiConfig->getFloat("fx.vibe.visual.topFillAlpha", 0.10f) : 0.10f;
    auto bounds = getLocalBounds().toFloat().reduced(borderPad);
    const auto fillColour = isActive ? accent : juce::Colour::fromRGBA(120, 120, 120, 180);
    const auto bgTintColour = uiConfig != nullptr ? uiConfig->getColour("fx.vibe.visual.bgTintColour", fillColour)
                                                  : fillColour;
    const auto topFillColour = uiConfig != nullptr ? uiConfig->getColour("fx.vibe.visual.topFillColour", fillColour)
                                                   : fillColour;
    g.setColour(bgTintColour.withAlpha(bgTintAlpha));
    g.fillRoundedRectangle(bounds, radius);
    g.setColour(topFillColour.withAlpha(topFillAlpha));
    juce::Path topFill;
    const auto topHalf = bounds.withTrimmedBottom(bounds.getHeight() * 0.5f);
    topFill.addRoundedRectangle(topHalf.getX(),
                                topHalf.getY(),
                                topHalf.getWidth(),
                                topHalf.getHeight(),
                                radius,
                                radius,
                                true,
                                true,
                                false,
                                false);
    g.fillPath(topFill);

    const auto textColour = uiConfig != nullptr
                                ? uiConfig->getColour("fx.vibe.visual.onLabel.textColour", juce::Colour::fromRGB(232, 232, 232))
                                : juce::Colour::fromRGB(232, 232, 232);
    const auto fontSize = uiConfig != nullptr ? uiConfig->getFloat("fx.vibe.visual.onLabel.fontSize", 11.5f) : 11.5f;
    const auto textBounds = uiConfig != nullptr
                                ? uiConfig->getRect("fx.vibe.visual.onLabel.bounds", getLocalBounds(), { 36, 11, 24, 14 })
                                : juce::Rectangle<int>(36, 11, 24, 14);
    const auto text = uiConfig != nullptr ? uiConfig->getString("fx.vibe.visual.onLabel.text", "ON") : juce::String("ON");

    g.setColour(textColour.withAlpha(isActive ? 1.0f : 0.6f));
    g.setFont(juce::FontOptions(fontSize));
    g.drawText(text, textBounds, juce::Justification::centredLeft, false);
}
