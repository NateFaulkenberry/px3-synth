#include "OscPanel.h"

#include <cmath>

OscPanel::OscPanel(juce::Slider& macroA,
                   juce::Slider& macroB,
                   juce::Slider& macroC,
                   juce::Label& macroALabel,
                   juce::Label& macroBLabel,
                   juce::Label& macroCLabel,
                   juce::ComboBox& modeBox,
                   juce::Label& modeLabel,
                   juce::ComboBox& vowelBox,
                   juce::Label& vowelLabel,
                   juce::Label& lfoAssignLabelIn,
                   juce::ComboBox& lfoAssignBoxIn,
                   juce::Slider& lfoRateKnob,
                   juce::Label& lfoRateLabel,
                   juce::Label& lfoRateValueLabel,
                   juce::ComboBox& lfoWaveformBox,
                   juce::Label& lfoWaveformLabel,
                   juce::Colour panelAccent,
                   juce::Colour lfoAccent)
    : lfoAssignLabel(lfoAssignLabelIn),
      lfoAssignBox(lfoAssignBoxIn),
      accent(panelAccent)
{
    oscillatorDisplayComponent = std::make_unique<OscillatorDisplayComponent>(macroA,
                                                                               macroB,
                                                                               macroC,
                                                                               macroALabel,
                                                                               macroBLabel,
                                                                               macroCLabel,
                                                                               modeBox,
                                                                               modeLabel,
                                                                               vowelBox,
                                                                               vowelLabel,
                                                                               panelAccent);
    addAndMakeVisible(*oscillatorDisplayComponent);

    addAndMakeVisible(lfoAssignLabel);
    addAndMakeVisible(lfoAssignBox);

    lfoComponent = std::make_unique<LfoComponent>(lfoRateKnob,
                                                  lfoRateLabel,
                                                  lfoRateValueLabel,
                                                  lfoWaveformBox,
                                                  lfoWaveformLabel,
                                                  lfoAccent);
    addAndMakeVisible(*lfoComponent);
}

void OscPanel::paint(juce::Graphics& g)
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

void OscPanel::resized()
{
    auto panelArea = getLocalBounds().reduced(12, 10);
    panelArea.removeFromTop(26);

    auto oscArea = panelArea.removeFromLeft(static_cast<int>(std::lround(static_cast<double>(panelArea.getWidth()) * 0.58)));
    panelArea.removeFromLeft(12);
    auto lfoArea = panelArea;

    if (oscillatorDisplayComponent != nullptr)
    {
        oscillatorDisplayComponent->setBounds(oscArea.reduced(4, 2));
    }

    auto lfoInner = lfoArea.reduced(10, 6);
    const auto assignRow = lfoInner.removeFromBottom(22);
    auto assign = assignRow;
    auto labelArea = assign.removeFromLeft(52);
    lfoAssignLabel.setBounds(labelArea);
    lfoAssignBox.setBounds(assign.reduced(1, 0));

    if (lfoComponent != nullptr)
    {
        lfoComponent->setBounds(lfoInner.reduced(2, 2));
    }
}

void OscPanel::refreshFromSelections(int modeIndex, int vowelIndex)
{
    if (oscillatorDisplayComponent != nullptr)
    {
        oscillatorDisplayComponent->refreshFromSelections(modeIndex, vowelIndex);
    }
}

void OscPanel::refreshLfoFromParameters(float rateHz, int waveformIndex)
{
    if (lfoComponent != nullptr)
    {
        lfoComponent->refreshFromParameters(rateHz, waveformIndex);
    }
}

void OscPanel::advanceAnimation(float oscDeltaPhase, float lfoDeltaPhase)
{
    if (oscillatorDisplayComponent != nullptr)
    {
        oscillatorDisplayComponent->advanceAnimation(oscDeltaPhase);
    }

    if (lfoComponent != nullptr)
    {
        lfoComponent->advanceAnimation(lfoDeltaPhase);
    }
}
