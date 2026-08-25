#pragma once

#include <JuceHeader.h>

#include <memory>

class UIConfig;

class SubOscComponent final : public juce::Component
{
public:
    SubOscComponent(juce::ToggleButton& enabledButtonIn,
                           juce::Label& enabledLabelIn,
                           juce::ComboBox& octaveBoxIn,
                           juce::Label& octaveLabelIn,
                           juce::ComboBox& waveformBoxIn,
                           juce::Label& waveformLabelIn,
                           juce::Colour accentIn);

    void setAccentColour(juce::Colour accentIn);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);
    void refreshFromParameters(bool enabled, int octaveIndex, int waveformIndex);
    void advanceAnimation(float deltaPhase);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    static float waveformSample(float phaseNorm, int waveformIndex);

    juce::ToggleButton& enabledButton;
    juce::Label& enabledLabel;
    juce::ComboBox& octaveBox;
    juce::Label& octaveLabel;
    juce::ComboBox& waveformBox;
    juce::Label& waveformLabel;
    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;

    bool currentEnabled { false };
    int currentWaveformIndex { 0 };
    float visualPhase { 0.0f };
};
