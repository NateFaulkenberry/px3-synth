#pragma once

#include <JuceHeader.h>

#include "Card.h"

#include <memory>

class UIConfig;

class SubOscComponent final : public juce::Component
{
public:
    SubOscComponent(juce::ToggleButton& enabledButtonIn,
                           juce::Label& enabledLabelIn,
                           juce::Slider& pitchIn,
                           juce::Label& pitchLabelIn,
                           juce::Label& pitchValueLabelIn,
                           juce::ComboBox& octaveBoxIn,
                           juce::Label& octaveLabelIn,
                           juce::ComboBox& waveformBoxIn,
                           juce::Label& waveformLabelIn,
                           juce::Colour accentIn);

    void setAccentColour(juce::Colour accentIn);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);
    // The panel's content box. Percentage card dimensions are resolved against
    // this and nothing else, so the component has to be told what it is.
    void setPanelContentBounds(juce::Rectangle<int> panelContent);
    void refreshFromParameters(bool enabled, int octaveIndex, int waveformIndex);
    void advanceAnimation(float deltaPhase);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    static float waveformSample(float phaseNorm, int waveformIndex);

    juce::ToggleButton& enabledButton;
    juce::Label& enabledLabel;
    juce::Slider& pitch;
    juce::Label& pitchLabel;
    juce::Label& pitchValueLabel;
    juce::ComboBox& octaveBox;
    juce::Label& octaveLabel;
    juce::ComboBox& waveformBox;
    juce::Label& waveformLabel;
    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;

    // Where the card ended up, so resized() and paint() agree without either
    // one re-deriving the geometry.
    px3::ui::CardHost card;

    bool currentEnabled { false };
    int currentWaveformIndex { 0 };
    float visualPhase { 0.0f };
};
