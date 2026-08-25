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
    const auto topFillAlpha = uiConfig != nullptr ? uiConfig->getFloat("env.panel.topFillAlpha", 0.10f) : 0.10f;
    const auto strokeAlpha = uiConfig != nullptr ? uiConfig->getFloat("env.panel.strokeAlpha", 0.75f) : 0.75f;
    const auto titleFontSize = uiConfig != nullptr ? uiConfig->getFloat("env.panel.title.fontSize", 15.0f) : 15.0f;
    const auto panelRadius = uiConfig != nullptr ? uiConfig->getFloat("env.panel.cornerRadius", 10.0f) : 10.0f;

    const auto area = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(accent.withAlpha(fillAlpha));
    g.fillRoundedRectangle(area, panelRadius);

    g.setColour(accent.withAlpha(topFillAlpha));
    g.fillRoundedRectangle(area.withTrimmedBottom(area.getHeight() * 0.5f), panelRadius);

    g.setColour(accent.withAlpha(strokeAlpha));
    g.drawRoundedRectangle(area, panelRadius, 1.0f);

    g.setColour(accent.brighter(0.30f));
    g.setFont(juce::FontOptions(titleFontSize, juce::Font::bold));
    g.drawText(title, getLocalBounds().removeFromTop(24), juce::Justification::centred);

    auto cardArea = getLocalBounds().reduced(12, 10);
    cardArea.removeFromTop(26);
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

    auto panelArea = getLocalBounds().reduced(12, 10);
    panelArea.removeFromTop(26);

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
