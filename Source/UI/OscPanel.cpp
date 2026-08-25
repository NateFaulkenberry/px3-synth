#include "OscPanel.h"

#include "UIConfig.h"

#include <cmath>

OscPanel::OscPanel(juce::ToggleButton& subEnabledButton,
                   juce::Label& subEnabledLabel,
                   juce::ComboBox& subOctaveBox,
                   juce::Label& subOctaveLabel,
                   juce::ComboBox& subWaveformBox,
                   juce::Label& subWaveformLabel,
                   juce::Slider& osc1MacroA,
                   juce::Slider& osc1MacroB,
                   juce::Slider& osc1MacroC,
                   juce::ToggleButton& osc1EnabledButton,
                   juce::Label& osc1EnabledLabel,
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
                   juce::ToggleButton& osc2EnabledButton,
                   juce::Label& osc2EnabledLabel,
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
                   juce::ToggleButton& osc3EnabledButton,
                   juce::Label& osc3EnabledLabel,
                   juce::Label& osc3MacroALabel,
                   juce::Label& osc3MacroBLabel,
                   juce::Label& osc3MacroCLabel,
                   juce::ComboBox& osc3ModeBox,
                   juce::Label& osc3ModeLabel,
                   juce::ComboBox& osc3VowelBox,
                   juce::Label& osc3VowelLabel,
                   juce::Colour subAccent,
                                     juce::Colour oscAccent)
        : accent(oscAccent),
          subHeaderAccent(subAccent),
                    oscHeaderAccent(oscAccent)
{
    subOscComponent = std::make_unique<SubOscComponent>(subEnabledButton,
                                                        subEnabledLabel,
                                                        subOctaveBox,
                                                        subOctaveLabel,
                                                        subWaveformBox,
                                                        subWaveformLabel,
                                                        subAccent);
    addAndMakeVisible(*subOscComponent);

    oscillatorComponents[0] = std::make_unique<OscillatorComponent>(osc1EnabledButton,
                                                                     osc1EnabledLabel,
                                                                     osc1MacroA,
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
    oscillatorComponents[1] = std::make_unique<OscillatorComponent>(osc2EnabledButton,
                                                                     osc2EnabledLabel,
                                                                     osc2MacroA,
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
    oscillatorComponents[2] = std::make_unique<OscillatorComponent>(osc3EnabledButton,
                                                                     osc3EnabledLabel,
                                                                     osc3MacroA,
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
}

void OscPanel::paint(juce::Graphics& g)
{
    const auto fillAlpha = uiConfig != nullptr ? uiConfig->getFloat("osc.panel.fillAlpha", 0.14f) : 0.14f;
    const auto strokeAlpha = uiConfig != nullptr ? uiConfig->getFloat("osc.panel.strokeAlpha", 0.75f) : 0.75f;
    const auto panelRadius = uiConfig != nullptr ? uiConfig->getFloat("osc.panel.cornerRadius", 10.0f) : 10.0f;
    const auto cardTitleFontSize = uiConfig != nullptr ? uiConfig->getFloat("osc.panel.cardTitle.fontSize", 11.0f) : 11.0f;

    const auto area = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(accent.withAlpha(fillAlpha));
    g.fillRoundedRectangle(area, panelRadius);

    g.setColour(accent.withAlpha(strokeAlpha));
    g.drawRoundedRectangle(area, panelRadius, 1.0f);

    const auto drawCardTitle = [&g, cardTitleFontSize](const juce::String& text, juce::Rectangle<int> bounds, juce::Colour colour)
    {
        g.setColour(colour.brighter(0.2f));
        g.setFont(juce::FontOptions(cardTitleFontSize, juce::Font::bold));
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
}

void OscPanel::resized()
{
    const auto padX = uiConfig != nullptr ? uiConfig->getInt("osc.panel.layout.padX", 12) : 12;
    const auto padY = uiConfig != nullptr ? uiConfig->getInt("osc.panel.layout.padY", 10) : 10;
    auto panelArea = getLocalBounds().reduced(padX, padY);

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

}

void OscPanel::refreshOscillatorFromParameters(int oscIndex, bool enabled, int modeIndex, int vowelIndex)
{
    const auto idx = juce::jlimit(0, 2, oscIndex);
    auto& oscillatorComponent = oscillatorComponents[static_cast<std::size_t>(idx)];
    if (oscillatorComponent != nullptr)
    {
        oscillatorComponent->refreshFromParameters(enabled, modeIndex, vowelIndex);
    }
}

void OscPanel::refreshSubOscFromParameters(bool enabled, int octaveIndex, int waveformIndex)
{
    if (subOscComponent != nullptr)
    {
        subOscComponent->refreshFromParameters(enabled, octaveIndex, waveformIndex);
    }
}

void OscPanel::advanceAnimation(float oscDeltaPhase)
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
}

void OscPanel::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);

    if (subOscComponent != nullptr)
    {
        subOscComponent->setUIConfig(uiConfig);
    }
    for (auto& oscillatorComponent : oscillatorComponents)
    {
        if (oscillatorComponent != nullptr)
        {
            oscillatorComponent->setUIConfig(uiConfig);
        }
    }
    repaint();
}
