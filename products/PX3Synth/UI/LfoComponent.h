#pragma once

#include <JuceHeader.h>

#include "Card.h"
#include "CardInner.h"
#include "ToggleChipButton.h"

#include <memory>

class UIConfig;

// Reusable LFO UI section that visualizes and lays out controls while
// remaining independent from modulation destinations.
class LfoComponent final : public juce::Component
{
public:
    LfoComponent(juce::ToggleButton& enabledButtonIn,
                        juce::Label& assignLabelIn,
                        juce::ComboBox& assignBoxIn,
                        juce::Slider& rateKnobIn,
                        juce::Label& rateLabelIn,
                        juce::Label& rateValueLabelIn,
                        juce::Slider& amountKnobIn,
                        juce::Label& amountLabelIn,
                        juce::Label& amountValueLabelIn,
                        juce::ComboBox& waveformBoxIn,
                        juce::Label& waveformLabelIn,
                        juce::Colour accentIn,
                        const juce::String& configPrefixIn = "mod.lfo1");
    ~LfoComponent() override;

    void setAccentColour(juce::Colour accentIn);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);
    // The parent panel content box: reference for percentage dimensions.
    void setPanelContentBounds(juce::Rectangle<int> panelContent);
    void refreshFromParameters(bool enabled, float rateHz, float amount, int waveformIndex);
    void advanceAnimation(float deltaPhase);

    // RAMP TIME and KEY SYNC. Unlike the controls above these are owned here, so
    // all three LFO cards gain them from one place. RAMP TIME takes the RATE
    // knob's position while a ramp is selected - a ramp has a duration, not a
    // rate, and showing both would leave one of them dead.
    void attachRampAndKeySync(juce::RangedAudioParameter& rampTimeParameter,
                              juce::RangedAudioParameter& keySyncParameter,
                              juce::LookAndFeel* knobLookAndFeel);

    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void paint(juce::Graphics& g) override;

private:
    static float waveformSample(float phaseNorm, int waveformIndex);
    static float rampPreviewSample(float t, int waveformIndex);
    static juce::String formatRampSeconds(double seconds);

    struct WaveformComboLookAndFeel final : public juce::LookAndFeel_V4
    {
        juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox& box,
                                                                 juce::Label& label) override;
    };

    juce::ToggleButton& enabledButton;
    juce::Slider& rateKnob;
    juce::Label& rateLabel;
    juce::Label& rateValueLabel;
    juce::Slider& amountKnob;
    juce::Label& amountLabel;
    juce::Label& amountValueLabel;
    juce::Label& assignLabel;
    juce::ComboBox& assignBox;
    juce::ComboBox& waveformBox;
    juce::Label& waveformLabel;
    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;
    WaveformComboLookAndFeel waveformComboLookAndFeel;

    bool currentEnabled { true };
    int currentWaveformIndex { 0 };
    float currentRateHz { 1.0f };
    float currentAmount { 0.0f };
    float visualPhase { 0.0f };
    juce::Colour baseRateValueTextColour;
    juce::Colour baseAmountValueTextColour;
    juce::String configPrefix;

    juce::Slider rampTimeKnob;
    px3::ui::ToggleChipButton keySyncButton;
    // Empty, but visible: the layout only reserves a caption's height for a
    // caption that is showing, and the chip has to sit level with the dropdowns.
    juce::Label keySyncCaptionSpacer;
    std::unique_ptr<juce::SliderParameterAttachment> rampTimeAttachment;
    std::unique_ptr<juce::ButtonParameterAttachment> keySyncAttachment;
    juce::String rateCaptionText;
    bool rampControlsAttached { false };
    bool laidOutForRamp { false };
    px3::ui::CardHost card;
    px3::ui::CardInner inner;
};
