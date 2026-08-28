#pragma once

#include <JuceHeader.h>

#include "Card.h"

#include <array>
#include <memory>

class UIConfig;

// Reusable oscillator UI component that owns oscillator-only layout, mode UI,
// and waveform visualization while using externally-owned controls.
class OscillatorComponent final : public juce::Component
{
public:
    OscillatorComponent(juce::ToggleButton& enabledButtonIn,
                        juce::Label& enabledLabelIn,
                        juce::Slider& pitchIn,
                        juce::Label& pitchLabelIn,
                        juce::Label& pitchValueLabelIn,
                        juce::Slider& macroAIn,
                        juce::Slider& macroBIn,
                        juce::Slider& macroCIn,
                        juce::Label& macroALabelIn,
                        juce::Label& macroBLabelIn,
                        juce::Label& macroCLabelIn,
                        juce::ComboBox& modeBoxIn,
                        juce::Label& modeLabelIn,
                        juce::ComboBox& vowelBoxIn,
                        juce::Label& vowelLabelIn,
                        juce::Colour accentIn);

    void setAccentColour(juce::Colour accentIn);
    // Which oscillator this instance is. Drives both the card's title, which is
    // the component's own content, and which style block it reads - so Osc 1,
    // 2 and 3 share one implementation and can still be styled independently.
    void setInstanceIndex(int oneBasedIndex);
    // The panel's content box: the only reference for percentage dimensions.
    void setPanelContentBounds(juce::Rectangle<int> panelContent);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);
    void refreshFromParameters(bool enabled, int modeIndex, int vowelIndex);
    void advanceAnimation(float deltaPhase);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void applyModeUi();
    void layoutMacroControls(const juce::Rectangle<int>& area);
    void applyEnabledUi();

    juce::ToggleButton& enabledButton;
    juce::Label& enabledLabel;
    juce::Slider& pitch;
    juce::Label& pitchLabel;
    juce::Label& pitchValueLabel;
    juce::Slider& macroA;
    juce::Slider& macroB;
    juce::Slider& macroC;
    juce::Label& macroALabel;
    juce::Label& macroBLabel;
    juce::Label& macroCLabel;
    juce::ComboBox& modeBox;
    juce::Label& modeLabel;
    juce::ComboBox& vowelBox;
    juce::Label& vowelLabel;
    px3::ui::CardHost card;
    int instanceIndex { 1 };

    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;
    bool currentEnabled { true };

    float phase { 0.0f };
    int lastModeIndex { -1 };
};
