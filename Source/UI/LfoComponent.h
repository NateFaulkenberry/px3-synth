#pragma once

#include <JuceHeader.h>

#include "Card.h"

#include <memory>

class UIConfig;

// Reusable LFO UI section that visualizes and lays out controls while
// remaining independent from modulation destinations.
class LfoComponent final : public juce::Component
{
public:
    LfoComponent(juce::ToggleButton& enabledButtonIn,
                        juce::Label& enabledLabelIn,
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

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    static float waveformSample(float phaseNorm, int waveformIndex);

    struct WaveformComboLookAndFeel final : public juce::LookAndFeel_V4
    {
        juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox& box,
                                                                 juce::Label& label) override;
    };

    juce::ToggleButton& enabledButton;
    juce::Label& enabledLabel;
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
    px3::ui::CardHost card;
};
