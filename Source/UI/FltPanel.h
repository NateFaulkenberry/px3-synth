#pragma once

#include <JuceHeader.h>

#include "../DSP/FilterTypes.h"
#include "FilterComponent.h"

#include <array>
#include <memory>

class UIConfig;

class FltPanel final : public juce::Component
{
public:
    FltPanel(std::array<juce::ToggleButton*, kFilterInstanceCount> enabledButtons,
             std::array<juce::Label*, kFilterInstanceCount> enabledLabels,
             std::array<juce::Slider*, kFilterInstanceCount> cutoffKnobs,
             std::array<juce::Label*, kFilterInstanceCount> cutoffLabels,
             std::array<juce::Slider*, kFilterInstanceCount> resonanceKnobs,
             std::array<juce::Label*, kFilterInstanceCount> resonanceLabels,
             std::array<juce::ComboBox*, kFilterInstanceCount> filterTypeBoxes,
             std::array<juce::AudioParameterBool*, kFilterInstanceCount> enabledParams,
             std::array<juce::AudioParameterFloat*, kFilterInstanceCount> cutoffParams,
             std::array<juce::AudioParameterFloat*, kFilterInstanceCount> resonanceParams,
             std::array<juce::AudioParameterChoice*, kFilterInstanceCount> filterTypeParams,
             juce::Colour panelAccent);
    ~FltPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void refreshFromParameters();
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);

private:
    struct FilterComboLookAndFeel final : public juce::LookAndFeel_V4
    {
        juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox& box,
                                                                 juce::Label& label) override;
    };

    std::array<juce::ToggleButton*, kFilterInstanceCount> enabledButtons;
    std::array<juce::Label*, kFilterInstanceCount> enabledLabels;
    std::array<juce::Slider*, kFilterInstanceCount> cutoffKnobs;
    std::array<juce::Label*, kFilterInstanceCount> cutoffLabels;
    std::array<juce::Slider*, kFilterInstanceCount> resonanceKnobs;
    std::array<juce::Label*, kFilterInstanceCount> resonanceLabels;
    std::array<juce::ComboBox*, kFilterInstanceCount> filterTypeBoxes;

    FilterComboLookAndFeel filterComboLookAndFeel;

    std::array<std::unique_ptr<FilterComponent>, kFilterInstanceCount> filterComponents;

    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;
};
