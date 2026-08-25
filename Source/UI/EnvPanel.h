#pragma once

#include <JuceHeader.h>

#include <memory>

#include "EnvelopeComponent.h"

class UIConfig;

class EnvPanel final : public juce::Component
{
public:
    EnvPanel(juce::AudioParameterFloat& attack,
             juce::AudioParameterFloat& decay,
             juce::AudioParameterFloat& sustain,
             juce::AudioParameterFloat& release,
             juce::Colour panelAccent);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void refreshFromParameters();
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);

private:
    std::unique_ptr<EnvelopeComponent> envelopeGraph;

    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;
};
