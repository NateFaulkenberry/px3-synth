#pragma once

#include <JuceHeader.h>

#include <array>

// Reusable oscillator UI component that owns oscillator-only layout, mode UI,
// and waveform visualization while using externally-owned controls.
class OscillatorDisplayComponent final : public juce::Component
{
public:
    OscillatorDisplayComponent(juce::Slider& macroAIn,
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
    void refreshFromSelections(int modeIndex, int vowelIndex);
    void advanceAnimation(float deltaPhase);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void applyModeUi();
    void layoutMacroControls(const juce::Rectangle<int>& area);

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
    juce::Colour accent;

    float phase { 0.0f };
    int lastModeIndex { -1 };
};
