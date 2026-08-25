#include "EnvPanel.h"

#include "UIConfig.h"

EnvPanel::EnvPanel(juce::AudioParameterFloat& attack,
                   juce::AudioParameterFloat& decay,
                   juce::AudioParameterFloat& sustain,
                   juce::AudioParameterFloat& release,
                   juce::Colour panelAccent)
    : accent(panelAccent)
{
    envelopeGraph = std::make_unique<EnvelopeComponent>(attack,
                                                        decay,
                                                        sustain,
                                                        release,
                                                        panelAccent);
    addAndMakeVisible(*envelopeGraph);
}

void EnvPanel::paint(juce::Graphics& g)
{
    const auto fillAlpha = uiConfig != nullptr ? uiConfig->getFloat("env.panel.fillAlpha", 0.14f) : 0.14f;
    const auto strokeAlpha = uiConfig != nullptr ? uiConfig->getFloat("env.panel.strokeAlpha", 0.75f) : 0.75f;
    const auto panelRadius = uiConfig != nullptr ? uiConfig->getFloat("env.panel.cornerRadius", 10.0f) : 10.0f;

    const auto area = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(accent.withAlpha(fillAlpha));
    g.fillRoundedRectangle(area, panelRadius);

    g.setColour(accent.withAlpha(strokeAlpha));
    g.drawRoundedRectangle(area, panelRadius, 1.0f);

    const auto panelPadX = uiConfig != nullptr ? uiConfig->getInt("env.panel.layout.padX", 12) : 12;
    const auto panelPadY = uiConfig != nullptr ? uiConfig->getInt("env.panel.layout.padY", 10) : 10;
    auto cardArea = getLocalBounds().reduced(panelPadX, panelPadY);
    constexpr int targetCardWidth = 300;
    const auto cardWidth = juce::jmin(targetCardWidth, cardArea.getWidth());
    cardArea = cardArea.withSizeKeepingCentre(cardWidth, cardArea.getHeight());

    const auto cardBounds = cardArea.toFloat().reduced(2.0f);
    g.setColour(accent.withAlpha(0.10f));
    g.fillRoundedRectangle(cardBounds, 8.0f);
    g.setColour(juce::Colour::fromRGBA(220, 232, 252, 88));
    g.drawRoundedRectangle(cardBounds, 8.0f, 1.2f);
}

void EnvPanel::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);

    if (envelopeGraph != nullptr)
    {
        envelopeGraph->setUIConfig(uiConfig);
    }

    repaint();
}

void EnvPanel::resized()
{
    if (envelopeGraph == nullptr)
    {
        return;
    }

    const auto panelPadX = uiConfig != nullptr ? uiConfig->getInt("env.panel.layout.padX", 12) : 12;
    const auto panelPadY = uiConfig != nullptr ? uiConfig->getInt("env.panel.layout.padY", 10) : 10;
    auto panelArea = getLocalBounds().reduced(panelPadX, panelPadY);

    constexpr int targetCardWidth = 300;
    const auto cardWidth = juce::jmin(targetCardWidth, panelArea.getWidth());
    auto cardArea = juce::Rectangle<int>(cardWidth, panelArea.getHeight()).withCentre(panelArea.getCentre());
    envelopeGraph->setBounds(cardArea.reduced(10, 10));
}

void EnvPanel::refreshFromParameters()
{
    if (envelopeGraph != nullptr)
    {
        envelopeGraph->refreshFromParameters();
    }
}
