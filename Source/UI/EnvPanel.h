#pragma once

#include <JuceHeader.h>

#include "EnvelopeComponent.h"

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

private:
    std::unique_ptr<EnvelopeComponent> envelopeGraph;

    juce::String title { "ENV" };
    juce::Colour accent;
};
