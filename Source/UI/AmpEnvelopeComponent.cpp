#include "AmpEnvelopeComponent.h"

#include "UIConfig.h"

AmpEnvelopeComponent::AmpEnvelopeComponent(PX3SynthAudioProcessor& processorIn, juce::Colour accentIn)
    : processor(processorIn)
{
    const auto applyChipLabelStyle = [](juce::Label& label)
    {
        label.setColour(juce::Label::backgroundColourId, juce::Colour::fromRGBA(255, 255, 255, 54));
        label.setColour(juce::Label::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 96));
    };

    enabledLabel.setText("ON", juce::dontSendNotification);
    enabledLabel.setJustificationType(juce::Justification::centredLeft);
    enabledLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    enabledLabel.setFont(juce::FontOptions(11.5f));
    enabledLabel.setInterceptsMouseClicks(false, false);
    applyChipLabelStyle(enabledLabel);

    assignLabel.setText("AMP", juce::dontSendNotification);
    assignLabel.setJustificationType(juce::Justification::centred);
    assignLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    assignLabel.setFont(juce::FontOptions(11.5f));
    assignLabel.setInterceptsMouseClicks(false, false);
    applyChipLabelStyle(assignLabel);

    enabledButton.setButtonText("");
    enabledButton.setClickingTogglesState(true);
    enabledButton.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(210, 210, 210));
    enabledButton.setColour(juce::ToggleButton::tickColourId, juce::Colour::fromRGB(196, 196, 196));

    assignBox.addItem("AMP", 1);
    assignBox.setSelectedId(1, juce::dontSendNotification);
    assignBox.setEnabled(false);
    assignBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    assignBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    assignBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));

    envelopeGraph = std::make_unique<EnvelopeComponent>(processor.getAttackParam(),
                                                        processor.getDecayParam(),
                                                        processor.getSustainParam(),
                                                        processor.getReleaseParam(),
                                                        processor.getAmpEnvEnabledParam(),
                                                        enabledButton,
                                                        enabledLabel,
                                                        assignLabel,
                                                        assignBox,
                                                        nullptr,
                                                        nullptr,
                                                        nullptr,
                                                        accentIn,
                                                        "amp.env");

    enabledButton.setVisible(false);
    enabledButton.setEnabled(false);
    enabledLabel.setVisible(false);
    enabledLabel.setEnabled(false);
    assignLabel.setVisible(false);
    assignLabel.setEnabled(false);
    assignBox.setVisible(false);
    assignBox.setEnabled(false);

    addAndMakeVisible(*envelopeGraph);
}

void AmpEnvelopeComponent::resized()
{
    if (envelopeGraph != nullptr)
    {
        envelopeGraph->setBounds(getLocalBounds());
    }
}

void AmpEnvelopeComponent::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    if (configIn != nullptr)
    {
        const auto comboStyle = configIn->getObject("styles.combos.default");
        configIn->applyComboStyle(comboStyle, assignBox);
    }

    if (envelopeGraph != nullptr)
    {
        envelopeGraph->setUIConfig(std::move(configIn));
    }
}

void AmpEnvelopeComponent::refreshFromParameters()
{
    if (envelopeGraph != nullptr)
    {
        envelopeGraph->refreshFromParameters();
    }
}
