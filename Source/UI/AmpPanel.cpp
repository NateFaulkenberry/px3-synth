#include "AmpPanel.h"

#include "UIConfig.h"

AmpPanel::AmpPanel(PX3SynthAudioProcessor& processorIn, juce::Colour panelAccent)
    : processor(processorIn),
      accent(panelAccent)
{
    ampEnvelopeComponent = std::make_unique<AmpEnvelopeComponent>(processor, panelAccent);

    if (!processor.getAmpEnvEnabledParam().get())
    {
        auto& ampEnabled = processor.getAmpEnvEnabledParam();
        ampEnabled.beginChangeGesture();
        ampEnabled.setValueNotifyingHost(1.0f);
        ampEnabled.endChangeGesture();
    }

    addAndMakeVisible(*ampEnvelopeComponent);
}

void AmpPanel::setKnobLookAndFeel(juce::LookAndFeel* lookAndFeel)
{
    if (ampEnvelopeComponent != nullptr)
    {
        ampEnvelopeComponent->setKnobLookAndFeel(lookAndFeel);
    }
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

void AmpPanel::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);

    if (ampEnvelopeComponent != nullptr)
    {
        ampEnvelopeComponent->setUIConfig(uiConfig);
    }

    repaint();
}

void AmpPanel::resized()
{
    if (ampEnvelopeComponent == nullptr)
    {
        return;
    }

    const auto panelPadX = uiConfig != nullptr ? uiConfig->getInt("amp.panel.layout.padX", 12) : 12;
    const auto panelPadY = uiConfig != nullptr ? uiConfig->getInt("amp.panel.layout.padY", 10) : 10;
    auto panelArea = getLocalBounds().reduced(panelPadX, panelPadY);

    ampEnvelopeComponent->setBounds(panelArea);
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

    if (ampEnvelopeComponent != nullptr)
    {
        ampEnvelopeComponent->refreshFromParameters();
    }
}
