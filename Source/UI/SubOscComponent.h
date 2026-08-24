#pragma once

#include <JuceHeader.h>

class SubOscComponent final : public juce::Component
{
public:
    SubOscComponent(juce::ToggleButton& enabledButtonIn,
                           juce::Label& enabledLabelIn,
                           juce::Slider& levelKnobIn,
                           juce::Label& levelLabelIn,
                           juce::ComboBox& octaveBoxIn,
                           juce::Label& octaveLabelIn,
                           juce::ComboBox& waveformBoxIn,
                           juce::Label& waveformLabelIn,
                           juce::Colour accentIn);

    void setAccentColour(juce::Colour accentIn);
    void refreshFromParameters(bool enabled, int octaveIndex, int waveformIndex);
    void advanceAnimation(float deltaPhase);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    static float waveformSample(float phaseNorm, int waveformIndex);

    juce::ToggleButton& enabledButton;
    juce::Label& enabledLabel;
    juce::Slider& levelKnob;
    juce::Label& levelLabel;
    juce::ComboBox& octaveBox;
    juce::Label& octaveLabel;
    juce::ComboBox& waveformBox;
    juce::Label& waveformLabel;
    juce::Colour accent;

    bool currentEnabled { false };
    int currentWaveformIndex { 0 };
    float visualPhase { 0.0f };
};
