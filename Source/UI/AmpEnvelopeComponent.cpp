#include "AmpEnvelopeComponent.h"

#include "UIConfig.h"

AmpEnvelopeComponent::AmpEnvelopeComponent(PX3SynthAudioProcessor& processorIn, juce::Colour accentIn)
    : processor(processorIn)
{
    enabledLabel.setText("ON", juce::dontSendNotification);
    enabledLabel.setJustificationType(juce::Justification::centredLeft);
    enabledLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    enabledLabel.setFont(juce::FontOptions(11.5f));
    enabledLabel.setInterceptsMouseClicks(false, false);

    assignLabel.setText("AMP", juce::dontSendNotification);
    assignLabel.setJustificationType(juce::Justification::centred);
    assignLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    assignLabel.setFont(juce::FontOptions(11.5f));
    assignLabel.setInterceptsMouseClicks(false, false);

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

    // "amp.env" would derive the key "env" and the title "ENV", and cards.env
    // does not exist - so the graph fell back to cards.defaults for both its
    // frame and its row heights. It carries the AMP ENV identity instead.
    envelopeGraph->setCardIdentity("ampEnv", "AMP ENV");

    addAndMakeVisible(*envelopeGraph);
}

void AmpEnvelopeComponent::resized()
{
    if (envelopeGraph != nullptr)
    {
        envelopeGraph->setBounds(getLocalBounds());
    }
}

void AmpEnvelopeComponent::setPanelContentBounds(juce::Rectangle<int> panelContent)
{
    // This component draws nothing itself. It hosts the envelope graph, which
    // owns the one card - drawing a second one here is what put a generic ENV
    // card inside the AMP ENV card.
    if (envelopeGraph != nullptr)
    {
        envelopeGraph->setPanelContentBounds(panelContent);
    }
    repaint();
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
    repaint();
}
