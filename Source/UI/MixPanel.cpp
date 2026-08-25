#include "MixPanel.h"

MixPanel::MixPanel(juce::ToggleButton& subOscEnabledButton,
                   juce::Label& subOscEnabledLabel,
                   juce::Slider& subOscLevelKnob,
                   juce::Label& subOscLevelLabel,
                   juce::ComboBox& subOscOctaveBox,
                   juce::Label& subOscOctaveLabel,
                   juce::ComboBox& subOscWaveformBox,
                   juce::Label& subOscWaveformLabel,
                   juce::Colour panelAccent)
    : accent(panelAccent)
{
    subOscComponent = std::make_unique<SubOscComponent>(subOscEnabledButton,
                                                        subOscEnabledLabel,
                                                        subOscLevelKnob,
                                                        subOscLevelLabel,
                                                        subOscOctaveBox,
                                                        subOscOctaveLabel,
                                                        subOscWaveformBox,
                                                        subOscWaveformLabel,
                                                        panelAccent);
    addAndMakeVisible(*subOscComponent);
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
    if (subOscComponent == nullptr)
    {
        return;
    }

    auto panelArea = getLocalBounds().reduced(12, 10);
    panelArea.removeFromTop(26);
    const auto componentWidth = juce::jmin(panelArea.getWidth() - 8, 320);
    const auto componentHeight = juce::jmin(panelArea.getHeight() - 6, 260);
    subOscComponent->setBounds(juce::Rectangle<int>(componentWidth, componentHeight)
                                   .withCentre(panelArea.getCentre()));
}

void MixPanel::refreshFromParameters(bool enabled, int octaveIndex, int waveformIndex)
{
    if (subOscComponent != nullptr)
    {
        subOscComponent->refreshFromParameters(enabled, octaveIndex, waveformIndex);
    }
}

void MixPanel::advanceAnimation(float deltaPhase)
{
    if (subOscComponent != nullptr)
    {
        subOscComponent->advanceAnimation(deltaPhase);
    }
}
