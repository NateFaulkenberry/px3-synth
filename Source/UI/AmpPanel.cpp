#include "AmpPanel.h"

#include "UIConfig.h"

AmpPanel::AmpPanel(PX3SynthAudioProcessor& processorIn, juce::Colour panelAccent)
    : processor(processorIn),
      accent(panelAccent)
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
                                                        panelAccent,
                                                        "amp.env");
    enabledButton.setVisible(false);
    enabledButton.setEnabled(false);
    enabledLabel.setVisible(false);
    enabledLabel.setEnabled(false);
    assignLabel.setVisible(false);
    assignLabel.setEnabled(false);
    assignBox.setVisible(false);
    assignBox.setEnabled(false);

    if (!processor.getAmpEnvEnabledParam().get())
    {
        auto& ampEnabled = processor.getAmpEnvEnabledParam();
        ampEnabled.beginChangeGesture();
        ampEnabled.setValueNotifyingHost(1.0f);
        ampEnabled.endChangeGesture();
    }

    addAndMakeVisible(*envelopeGraph);
}

void AmpPanel::paint(juce::Graphics& g)
{
    const auto fillAlpha = uiConfig != nullptr ? uiConfig->getFloat("amp.panel.fillAlpha", 0.0f) : 0.0f;
    const auto strokeAlpha = uiConfig != nullptr ? uiConfig->getFloat("amp.panel.strokeAlpha", 0.0f) : 0.0f;
    const auto panelRadius = uiConfig != nullptr ? uiConfig->getFloat("amp.panel.cornerRadius", 10.0f) : 10.0f;
    const auto area = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(accent.withAlpha(fillAlpha));
    g.fillRoundedRectangle(area, panelRadius);

    g.setColour(accent.withAlpha(strokeAlpha));
    g.drawRoundedRectangle(area, panelRadius, 1.0f);
}

void AmpPanel::paintOverChildren(juce::Graphics& g)
{
    if (envelopeGraph == nullptr)
    {
        return;
    }

    const auto title = uiConfig != nullptr ? uiConfig->getString("amp.env.title.text", "AMP ENV") : juce::String("AMP ENV");
    const auto titleFont = uiConfig != nullptr ? uiConfig->getFloat("amp.panel.cardTitle.fontSize", 11.0f) : 11.0f;

    g.setColour(accent.brighter(0.2f));
    g.setFont(juce::FontOptions(titleFont, juce::Font::bold));
    g.drawText(title,
               envelopeGraph->getBounds().removeFromTop(14),
               juce::Justification::centredTop,
               true);
}

void AmpPanel::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);

    if (uiConfig != nullptr)
    {
        const auto comboStyle = uiConfig->getObject("styles.combos.default");
        uiConfig->applyComboStyle(comboStyle, assignBox);
    }

    if (envelopeGraph != nullptr)
    {
        envelopeGraph->setUIConfig(uiConfig);
    }

    repaint();
}

void AmpPanel::resized()
{
    if (envelopeGraph == nullptr)
    {
        return;
    }

    const auto panelPadX = uiConfig != nullptr ? uiConfig->getInt("amp.panel.layout.padX", 12) : 12;
    const auto panelPadY = uiConfig != nullptr ? uiConfig->getInt("amp.panel.layout.padY", 10) : 10;
    auto panelArea = getLocalBounds().reduced(panelPadX, panelPadY);

    envelopeGraph->setBounds(panelArea);
}

int AmpPanel::getPreferredContentWidth() const
{
    const auto panelPadX = uiConfig != nullptr ? uiConfig->getInt("amp.panel.layout.padX", 12) : 12;
    const auto maxWidth = uiConfig != nullptr ? uiConfig->getInt("amp.env.layout.maxWidth", 360) : 360;
    return panelPadX * 2 + maxWidth;
}

int AmpPanel::getPreferredContentHeight() const
{
    const auto panelPadY = uiConfig != nullptr ? uiConfig->getInt("amp.panel.layout.padY", 10) : 10;
    const auto maxHeight = uiConfig != nullptr ? uiConfig->getInt("amp.env.layout.maxHeight", 340) : 340;
    return panelPadY * 2 + maxHeight;
}

void AmpPanel::refreshFromParameters()
{
    if (!processor.getAmpEnvEnabledParam().get())
    {
        auto& ampEnabled = processor.getAmpEnvEnabledParam();
        ampEnabled.beginChangeGesture();
        ampEnabled.setValueNotifyingHost(1.0f);
        ampEnabled.endChangeGesture();
    }

    if (envelopeGraph != nullptr)
    {
        envelopeGraph->refreshFromParameters();
    }
}
