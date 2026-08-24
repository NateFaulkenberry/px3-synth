#include "VibeUiComponent.h"

VibeUiComponent::VibeUiComponent(juce::ToggleButton& enabledButtonIn,
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

void VibeUiComponent::setAccentColour(juce::Colour accentIn)
{
    accent = accentIn;
    repaint();
}

void VibeUiComponent::setActive(bool enabled)
{
    isActive = enabled;
    amountKnob.setEnabled(isActive);
    amountLabel.setEnabled(isActive);
    typeBox.setEnabled(isActive);
    typeLabel.setEnabled(isActive);
    amountKnob.getProperties().set("psychedelicBypassGray", !isActive);
    repaint();
}

void VibeUiComponent::resized()
{
    auto area = getLocalBounds().reduced(10, 8);

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

void VibeUiComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(6.0f);
    g.setColour(isActive ? accent.withAlpha(0.08f) : juce::Colour::fromRGBA(120, 120, 120, 30));
    g.fillRoundedRectangle(bounds, 8.0f);

    g.setColour(juce::Colour::fromRGB(232, 232, 232).withAlpha(isActive ? 1.0f : 0.6f));
    g.setFont(juce::FontOptions(11.5f));
    g.drawText("ON", 36, 11, 24, 14, juce::Justification::centredLeft, false);
}
