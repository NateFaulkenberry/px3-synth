#pragma once

#include <JuceHeader.h>

#include "Card.h"
#include "CardInner.h"
#include "TuningControls.h"

#include <memory>

class UIConfig;

class SubOscComponent final : public juce::Component
{
public:
    SubOscComponent(juce::ToggleButton& enabledButtonIn,
                           TuningControls& tuningIn,
                           juce::ComboBox& waveformBoxIn,
                           juce::Label& waveformLabelIn,
                           juce::Colour accentIn);

    void setAccentColour(juce::Colour accentIn);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);
    // The panel's content box. Percentage card dimensions are resolved against
    // this and nothing else, so the component has to be told what it is.
    void setPanelContentBounds(juce::Rectangle<int> panelContent);
    void refreshFromParameters(bool enabled, int waveformIndex);
    void advanceAnimation(float deltaPhase);

    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void paint(juce::Graphics& g) override;

private:
    static float waveformSample(float phaseNorm, int waveformIndex);

    juce::ToggleButton& enabledButton;
    TuningControls& tuning;
    juce::ComboBox& waveformBox;
    juce::Label& waveformLabel;
    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;

    // Where the card ended up, so resized() and paint() agree without either
    // one re-deriving the geometry.
    px3::ui::CardHost card;
    px3::ui::CardInner inner;

    bool currentEnabled { false };
    int currentWaveformIndex { 0 };
    float visualPhase { 0.0f };
};
