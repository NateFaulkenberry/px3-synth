#include "OscPanel.h"

#include <cmath>

OscPanel::OscPanel(juce::ToggleButton& subEnabledButton,
                   juce::Label& subEnabledLabel,
                   juce::Slider& subLevelKnob,
                   juce::Label& subLevelLabel,
                   juce::ComboBox& subOctaveBox,
                   juce::Label& subOctaveLabel,
                   juce::ComboBox& subWaveformBox,
                   juce::Label& subWaveformLabel,
                   juce::Slider& osc1MacroA,
                   juce::Slider& osc1MacroB,
                   juce::Slider& osc1MacroC,
                   juce::Label& osc1MacroALabel,
                   juce::Label& osc1MacroBLabel,
                   juce::Label& osc1MacroCLabel,
                   juce::ComboBox& osc1ModeBox,
                   juce::Label& osc1ModeLabel,
                   juce::ComboBox& osc1VowelBox,
                   juce::Label& osc1VowelLabel,
                   juce::Slider& osc2MacroA,
                   juce::Slider& osc2MacroB,
                   juce::Slider& osc2MacroC,
                   juce::Label& osc2MacroALabel,
                   juce::Label& osc2MacroBLabel,
                   juce::Label& osc2MacroCLabel,
                   juce::ComboBox& osc2ModeBox,
                   juce::Label& osc2ModeLabel,
                   juce::ComboBox& osc2VowelBox,
                   juce::Label& osc2VowelLabel,
                   juce::Slider& osc3MacroA,
                   juce::Slider& osc3MacroB,
                   juce::Slider& osc3MacroC,
                   juce::Label& osc3MacroALabel,
                   juce::Label& osc3MacroBLabel,
                   juce::Label& osc3MacroCLabel,
                   juce::ComboBox& osc3ModeBox,
                   juce::Label& osc3ModeLabel,
                   juce::ComboBox& osc3VowelBox,
                   juce::Label& osc3VowelLabel,
                   juce::Label& lfoAssignLabelIn,
                   juce::ComboBox& lfoAssignBoxIn,
                   juce::Slider& lfoRateKnob,
                   juce::Label& lfoRateLabel,
                   juce::Label& lfoRateValueLabel,
                   juce::ComboBox& lfoWaveformBox,
                   juce::Label& lfoWaveformLabel,
                   juce::Colour subAccent,
                   juce::Colour oscAccent,
                   juce::Colour lfoAccent)
        : accent(oscAccent),
          subHeaderAccent(subAccent),
          oscHeaderAccent(oscAccent),
          lfoHeaderAccent(lfoAccent)
{
    subOscComponent = std::make_unique<SubOscComponent>(subEnabledButton,
                                                        subEnabledLabel,
                                                        subLevelKnob,
                                                        subLevelLabel,
                                                        subOctaveBox,
                                                        subOctaveLabel,
                                                        subWaveformBox,
                                                        subWaveformLabel,
                                                        subAccent);
    addAndMakeVisible(*subOscComponent);

    oscillatorComponents[0] = std::make_unique<OscillatorComponent>(osc1MacroA,
                                                                     osc1MacroB,
                                                                     osc1MacroC,
                                                                     osc1MacroALabel,
                                                                     osc1MacroBLabel,
                                                                     osc1MacroCLabel,
                                                                     osc1ModeBox,
                                                                     osc1ModeLabel,
                                                                     osc1VowelBox,
                                                                     osc1VowelLabel,
                                                                     oscAccent);
    oscillatorComponents[1] = std::make_unique<OscillatorComponent>(osc2MacroA,
                                                                     osc2MacroB,
                                                                     osc2MacroC,
                                                                     osc2MacroALabel,
                                                                     osc2MacroBLabel,
                                                                     osc2MacroCLabel,
                                                                     osc2ModeBox,
                                                                     osc2ModeLabel,
                                                                     osc2VowelBox,
                                                                     osc2VowelLabel,
                                                                     oscAccent);
    oscillatorComponents[2] = std::make_unique<OscillatorComponent>(osc3MacroA,
                                                                     osc3MacroB,
                                                                     osc3MacroC,
                                                                     osc3MacroALabel,
                                                                     osc3MacroBLabel,
                                                                     osc3MacroCLabel,
                                                                     osc3ModeBox,
                                                                     osc3ModeLabel,
                                                                     osc3VowelBox,
                                                                     osc3VowelLabel,
                                                                     oscAccent);

    for (auto& oscillatorComponent : oscillatorComponents)
    {
        if (oscillatorComponent != nullptr)
        {
            addAndMakeVisible(*oscillatorComponent);
        }
    }

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

    const auto drawCardTitle = [&g](const juce::String& text, juce::Rectangle<int> bounds, juce::Colour colour)
    {
        g.setColour(colour.brighter(0.2f));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(text, bounds.removeFromTop(14), juce::Justification::centredTop, true);
    };

    if (subOscComponent != nullptr)
    {
        drawCardTitle("Sub Osc", subOscComponent->getBounds(), subHeaderAccent);
    }
    for (int oscIndex = 0; oscIndex < static_cast<int>(oscillatorComponents.size()); ++oscIndex)
    {
        if (oscillatorComponents[static_cast<std::size_t>(oscIndex)] != nullptr)
        {
            drawCardTitle("Osc " + juce::String(oscIndex + 1),
                          oscillatorComponents[static_cast<std::size_t>(oscIndex)]->getBounds(),
                          oscHeaderAccent);
        }
    }
    if (lfoComponent != nullptr)
    {
        drawCardTitle("LFO", lfoComponent->getBounds(), lfoHeaderAccent);
    }
}

void OscPanel::resized()
{
    auto panelArea = getLocalBounds().reduced(12, 10);
    panelArea.removeFromTop(26);

    constexpr int columnCount = 5;
    constexpr int gap = 8;
    const auto totalGap = gap * (columnCount - 1);
    const auto columnWidth = juce::jmax(1, (panelArea.getWidth() - totalGap) / columnCount);

    juce::Rectangle<int> columns[columnCount];
    auto cursor = panelArea.getX();
    for (int i = 0; i < columnCount; ++i)
    {
        columns[i] = { cursor, panelArea.getY(), columnWidth, panelArea.getHeight() };
        cursor += columnWidth + gap;
    }

    if (subOscComponent != nullptr)
    {
        subOscComponent->setBounds(columns[0].reduced(2, 2));
    }

    for (int oscIndex = 0; oscIndex < static_cast<int>(oscillatorComponents.size()); ++oscIndex)
    {
        if (oscillatorComponents[static_cast<std::size_t>(oscIndex)] != nullptr)
        {
            oscillatorComponents[static_cast<std::size_t>(oscIndex)]->setBounds(columns[oscIndex + 1].reduced(2, 2));
        }
    }

    if (lfoComponent != nullptr)
    {
        lfoComponent->setBounds(columns[4].reduced(2, 2));
    }
}

void OscPanel::refreshOscillatorFromSelections(int oscIndex, int modeIndex, int vowelIndex)
{
    const auto idx = juce::jlimit(0, 2, oscIndex);
    auto& oscillatorComponent = oscillatorComponents[static_cast<std::size_t>(idx)];
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

void OscPanel::refreshSubOscFromParameters(bool enabled, int octaveIndex, int waveformIndex)
{
    if (subOscComponent != nullptr)
    {
        subOscComponent->refreshFromParameters(enabled, octaveIndex, waveformIndex);
    }
}

void OscPanel::advanceAnimation(float oscDeltaPhase, float lfoDeltaPhase)
{
    for (auto& oscillatorComponent : oscillatorComponents)
    {
        if (oscillatorComponent != nullptr)
        {
            oscillatorComponent->advanceAnimation(oscDeltaPhase);
        }
    }

    if (subOscComponent != nullptr)
    {
        subOscComponent->advanceAnimation(oscDeltaPhase);
    }

    if (lfoComponent != nullptr)
    {
        lfoComponent->advanceAnimation(lfoDeltaPhase);
    }
}
