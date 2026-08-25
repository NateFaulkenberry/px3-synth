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
        : accent(panelAccent)
{
    oscillatorComponent = std::make_unique<OscillatorComponent>(macroA,
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
    addAndMakeVisible(*oscillatorComponent);

    lfoComponent = std::make_unique<LfoComponent>(lfoAssignLabelIn,
                                                  lfoAssignBoxIn,
                                                  lfoRateKnob,
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

    if (oscillatorComponent != nullptr)
    {
        auto oscCardBounds = oscArea.reduced(4, 2);
        constexpr int targetCardWidth = 300;
        const auto cardWidth = juce::jmin(targetCardWidth, oscCardBounds.getWidth());
        oscCardBounds = oscCardBounds.withSizeKeepingCentre(cardWidth, oscCardBounds.getHeight());
        oscillatorComponent->setBounds(oscCardBounds);
    }

    auto lfoInner = lfoArea.reduced(10, 6);
    constexpr int targetCardWidth = 300;
    const auto lfoCardWidth = juce::jmax(1, juce::jmin(targetCardWidth, lfoInner.getWidth()));
    auto lfoColumn = juce::Rectangle<int>(lfoCardWidth, lfoInner.getHeight()).withCentre(lfoInner.getCentre());

    if (lfoComponent != nullptr)
    {
        lfoComponent->setBounds(lfoColumn.reduced(2, 2));
    }
}

void OscPanel::refreshFromSelections(int modeIndex, int vowelIndex)
{
    if (oscillatorComponent != nullptr)
    {
        oscillatorComponent->refreshFromSelections(modeIndex, vowelIndex);
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
    if (oscillatorComponent != nullptr)
    {
        oscillatorComponent->advanceAnimation(oscDeltaPhase);
    }

    if (lfoComponent != nullptr)
    {
        lfoComponent->advanceAnimation(lfoDeltaPhase);
    }
}
