#pragma once

#include <JuceHeader.h>

#include <memory>

#include "AmpEnvelopeComponent.h"
#include "PluginProcessor.h"

class UIConfig;

class AmpPanel final : public juce::Component
{
public:
    AmpPanel(PX3SynthAudioProcessor& processorIn, juce::Colour panelAccent);

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    void refreshFromParameters();
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);
    int getPreferredContentWidth() const;
    int getPreferredContentHeight() const;

private:
    PX3SynthAudioProcessor& processor;

    std::unique_ptr<AmpEnvelopeComponent> ampEnvelopeComponent;
    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;
};
