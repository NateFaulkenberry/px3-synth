#pragma once

#include <JuceHeader.h>

#include "FilterTypes.h"
#include "FilterComponent.h"
#include "ToggleChipButton.h"

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
    void setSceneStyle(juce::Colour background,
                       juce::Colour foreground,
                       juce::Colour accentIn,
                       float cornerRadius,
                       float borderWidth);

    // SERIES / PARALLEL and the parallel balance. Owned by the panel, because
    // they belong to neither filter card: they say how the two are connected.
    void attachRouting(juce::RangedAudioParameter& routingParameter,
                       juce::RangedAudioParameter& balanceParameter);

    // Scene-managed parts (InstrumentScene.json: filter.*).
    juce::Component& getRoutingButton() noexcept { return routingButton; }
    juce::Component& getBalanceLabel() noexcept { return balanceLabel; }
    juce::Component& getBalanceSlider() noexcept { return balanceSlider; }
    juce::Component* getFilterCard(int index) const noexcept
    {
        return index >= 0 && index < kFilterInstanceCount ? filterComponents[static_cast<std::size_t>(index)].get() : nullptr;
    }
    // The controls inside each card, placed against the card's current bounds.
    // Still hand-laid (CardInner rows); the editor calls it after every scene
    // pass.
    void layoutCardControls();

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

    px3::ui::ToggleChipButton routingButton;
    juce::Label balanceLabel;
    juce::Slider balanceSlider;
    std::unique_ptr<juce::ButtonParameterAttachment> routingAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> balanceAttachment;
    bool routingAttached { false };

    std::array<std::unique_ptr<FilterComponent>, kFilterInstanceCount> filterComponents;

    juce::Colour accent;
    juce::Colour sceneBackground;
    float sceneCornerRadius { 0.0f };
    float sceneBorderWidth { 0.0f };
    bool hasSceneStyle { false };
    std::shared_ptr<const UIConfig> uiConfig;
};
