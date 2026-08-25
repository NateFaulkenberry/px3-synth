#include "MixPanel.h"

MixPanel::MixPanel(juce::Colour panelAccent)
    : accent(panelAccent)
{
}

void MixPanel::paint(juce::Graphics& g)
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
}

void MixPanel::resized()
{
    juce::ignoreUnused(this);
}

void MixPanel::refreshFromParameters(bool enabled, int octaveIndex, int waveformIndex)
{
    juce::ignoreUnused(enabled, octaveIndex, waveformIndex);
}

void MixPanel::advanceAnimation(float deltaPhase)
{
    juce::ignoreUnused(deltaPhase);
}
