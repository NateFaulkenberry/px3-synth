#include "FltPanel.h"

FltPanel::FltPanel(juce::Slider& cutoffKnobIn,
                   juce::Label& cutoffLabelIn,
                   juce::Slider& resonanceKnobIn,
                   juce::Label& resonanceLabelIn,
                   juce::ComboBox& filterTypeBoxIn,
                   juce::AudioParameterFloat& cutoffParam,
                   juce::AudioParameterFloat& resonanceParam,
                   juce::AudioParameterChoice& filterTypeParam,
                   juce::Colour panelAccent)
    : cutoffKnob(cutoffKnobIn),
      cutoffLabel(cutoffLabelIn),
      resonanceKnob(resonanceKnobIn),
      resonanceLabel(resonanceLabelIn),
      filterTypeBox(filterTypeBoxIn),
      accent(panelAccent)
{
    addAndMakeVisible(cutoffKnob);
    addAndMakeVisible(cutoffLabel);
    addAndMakeVisible(resonanceKnob);
    addAndMakeVisible(resonanceLabel);
    addAndMakeVisible(filterTypeBox);

    filterResponseComponent = std::make_unique<FilterResponseComponent>(cutoffParam,
                                                                        resonanceParam,
                                                                        filterTypeParam,
                                                                        panelAccent);
    addAndMakeVisible(*filterResponseComponent);
}

void FltPanel::paint(juce::Graphics& g)
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

void FltPanel::resized()
{
    auto panelArea = getLocalBounds().reduced(12, 10);
    panelArea.removeFromTop(26);

    auto filterArea = panelArea.reduced(4, 0);
    const auto row = juce::Rectangle<int>(filterArea.getX(),
                                          filterArea.getBottom() - 22,
                                          filterArea.getWidth(),
                                          18);
    filterTypeBox.setBounds(row.reduced(1, 0));

    auto responseArea = filterArea.withBottom(juce::jmax(filterArea.getY(), row.getY() - 6));
    auto knobBand = responseArea.removeFromTop(120);
    const auto knobSize = juce::jlimit(56, 110, juce::jmin((knobBand.getWidth() - 24) / 2, knobBand.getHeight() - 24));

    auto leftKnob = juce::Rectangle<int>(knobSize, knobSize)
                        .withCentre({ knobBand.getX() + knobBand.getWidth() / 4, knobBand.getCentreY() + 8 });
    auto rightKnob = juce::Rectangle<int>(knobSize, knobSize)
                         .withCentre({ knobBand.getX() + (knobBand.getWidth() * 3) / 4, knobBand.getCentreY() + 8 });

    cutoffLabel.setBounds(juce::Rectangle<int>(leftKnob.getX(), leftKnob.getY() - 20, leftKnob.getWidth(), 18));
    cutoffKnob.setBounds(leftKnob);
    resonanceLabel.setBounds(juce::Rectangle<int>(rightKnob.getX(), rightKnob.getY() - 20, rightKnob.getWidth(), 18));
    resonanceKnob.setBounds(rightKnob);

    if (filterResponseComponent != nullptr)
    {
        filterResponseComponent->setBounds(responseArea.withTrimmedLeft(8).withTrimmedRight(8).reduced(0, 4));
    }
}

void FltPanel::refreshFromParameters()
{
    if (filterResponseComponent != nullptr)
    {
        filterResponseComponent->refreshFromParameters();
    }
}
