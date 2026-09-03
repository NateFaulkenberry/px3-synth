#pragma once

#include <JuceHeader.h>

#include "FilterTypes.h"
#include "FilterComponent.h"

#include <array>
#include <memory>

class UIConfig;

class FltPanel final : public juce::Component
{
public:
    FltPanel(std::array<juce::ToggleButton*, kFilterInstanceCount> enabledButtons,
             std::array<juce::Slider*, kFilterInstanceCount> cutoffKnobs,
             std::array<juce::Label*, kFilterInstanceCount> cutoffLabels,
             std::array<juce::Slider*, kFilterInstanceCount> resonanceKnobs,
             std::array<juce::Label*, kFilterInstanceCount> resonanceLabels,
             std::array<juce::ComboBox*, kFilterInstanceCount> filterTypeBoxes,
             std::array<juce::Label*, kFilterInstanceCount> filterTypeLabels,
             // Comb mode's controls. Shown in place of cutoff and resonance
             // when the filter is in comb mode, hidden otherwise.
             std::array<juce::Slider*, kFilterInstanceCount> combTuneKnobs,
             std::array<juce::Slider*, kFilterInstanceCount> combDecayKnobs,
             std::array<juce::Slider*, kFilterInstanceCount> combDampingKnobs,
             std::array<juce::Slider*, kFilterInstanceCount> combDispersionKnobs,
             std::array<juce::Slider*, kFilterInstanceCount> combDriveKnobs,
             std::array<juce::Slider*, kFilterInstanceCount> combMixKnobs,
             std::array<juce::Button*, kFilterInstanceCount> combInvertButtons,
             std::array<juce::Label*, kFilterInstanceCount> combTuneLabels,
             std::array<juce::Label*, kFilterInstanceCount> combDecayLabels,
             std::array<juce::Label*, kFilterInstanceCount> combDampingLabels,
             std::array<juce::Label*, kFilterInstanceCount> combDispersionLabels,
             std::array<juce::Label*, kFilterInstanceCount> combDriveLabels,
             std::array<juce::Label*, kFilterInstanceCount> combMixLabels,
             std::array<juce::AudioParameterBool*, kFilterInstanceCount> enabledParams,
             std::array<juce::AudioParameterFloat*, kFilterInstanceCount> cutoffParams,
             std::array<juce::AudioParameterFloat*, kFilterInstanceCount> resonanceParams,
             std::array<juce::AudioParameterChoice*, kFilterInstanceCount> filterTypeParams,
             // The comb parameters the response graph draws from. The knobs alone
             // cannot serve: the graph needs the values, not the controls.
             std::array<juce::AudioParameterFloat*, kFilterInstanceCount> combTuneParams,
             std::array<juce::AudioParameterFloat*, kFilterInstanceCount> combDecayParams,
             std::array<juce::AudioParameterFloat*, kFilterInstanceCount> combDampingParams,
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
    std::array<juce::Slider*, kFilterInstanceCount> cutoffKnobs;
    std::array<juce::Label*, kFilterInstanceCount> cutoffLabels;
    std::array<juce::Slider*, kFilterInstanceCount> resonanceKnobs;
    std::array<juce::Label*, kFilterInstanceCount> resonanceLabels;
    std::array<juce::ComboBox*, kFilterInstanceCount> filterTypeBoxes;
    // Which mode each row was last laid out for. Row 2 shows a different set of
    // controls in comb mode, and that choice is made in resized() - which a
    // parameter change does not otherwise trigger.
    std::array<int, kFilterInstanceCount> lastLaidOutModes { { -1, -1 } };
    std::array<juce::AudioParameterFloat*, kFilterInstanceCount> combTuneParams { { nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kFilterInstanceCount> combDecayParams { { nullptr, nullptr } };
    std::array<juce::AudioParameterFloat*, kFilterInstanceCount> combDampingParams { { nullptr, nullptr } };
    std::array<juce::Slider*, kFilterInstanceCount> combTuneKnobs;
    std::array<juce::Slider*, kFilterInstanceCount> combDecayKnobs;
    std::array<juce::Slider*, kFilterInstanceCount> combDampingKnobs;
    std::array<juce::Slider*, kFilterInstanceCount> combDispersionKnobs;
    std::array<juce::Slider*, kFilterInstanceCount> combDriveKnobs;
    std::array<juce::Slider*, kFilterInstanceCount> combMixKnobs;
    std::array<juce::Button*, kFilterInstanceCount> combInvertButtons;
    std::array<juce::Label*, kFilterInstanceCount> combTuneLabels;
    std::array<juce::Label*, kFilterInstanceCount> combDecayLabels;
    std::array<juce::Label*, kFilterInstanceCount> combDampingLabels;
    std::array<juce::Label*, kFilterInstanceCount> combDispersionLabels;
    std::array<juce::Label*, kFilterInstanceCount> combDriveLabels;
    std::array<juce::Label*, kFilterInstanceCount> combMixLabels;
    std::array<juce::Label*, kFilterInstanceCount> filterTypeLabels;
    std::array<juce::Colour, kFilterInstanceCount> cutoffLabelBaseColours;
    std::array<juce::Colour, kFilterInstanceCount> resonanceLabelBaseColours;
    std::array<juce::Colour, kFilterInstanceCount> filterTypeBoxBaseBgColours;
    std::array<juce::Colour, kFilterInstanceCount> filterTypeBoxBaseTextColours;
    std::array<juce::Colour, kFilterInstanceCount> filterTypeBoxBaseOutlineColours;

    FilterComboLookAndFeel filterComboLookAndFeel;

    std::array<std::unique_ptr<FilterComponent>, kFilterInstanceCount> filterComponents;

    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;
};
