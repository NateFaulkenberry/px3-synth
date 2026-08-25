#pragma once

#include <JuceHeader.h>

#include <memory>

#include "EnvelopeComponent.h"
#include "LfoComponent.h"

class UIConfig;

class ModPanel final : public juce::Component
{
public:
    ModPanel(juce::AudioParameterFloat& attack,
             juce::AudioParameterFloat& decay,
             juce::AudioParameterFloat& sustain,
             juce::AudioParameterFloat& release,
             juce::AudioParameterBool& envEnabled,
             juce::ToggleButton& envEnabledButton,
             juce::Label& envEnabledLabel,
             juce::ToggleButton& lfoEnabledButton,
             juce::Label& lfoEnabledLabel,
             juce::Label& lfoAssignLabel,
             juce::ComboBox& lfoAssignBox,
             juce::Slider& lfoRateKnob,
             juce::Label& lfoRateLabel,
             juce::Label& lfoRateValueLabel,
             juce::ComboBox& lfoWaveformBox,
             juce::Label& lfoWaveformLabel,
             juce::Colour panelAccent,
             juce::Colour lfoAccent);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void refreshFromParameters();
    void refreshLfoFromParameters(bool enabled, float rateHz, int waveformIndex);
    void advanceAnimation(float lfoDeltaSeconds);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);

private:
    std::unique_ptr<EnvelopeComponent> envelopeGraph;
    std::unique_ptr<LfoComponent> lfoComponent;

    juce::Colour accent;
    juce::Colour lfoHeaderAccent;
    std::shared_ptr<const UIConfig> uiConfig;
};