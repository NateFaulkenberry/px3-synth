#include "EnvPanel.h"

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
    const auto area = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(accent.withAlpha(0.14f));
    g.fillRoundedRectangle(area, 10.0f);

    g.setColour(accent.withAlpha(0.10f));
    g.fillRoundedRectangle(area.withTrimmedBottom(area.getHeight() * 0.5f), 10.0f);

    g.setColour(accent.withAlpha(0.75f));
    g.drawRoundedRectangle(area, 10.0f, 1.0f);

    g.setColour(accent.brighter(0.30f));
    g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
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
