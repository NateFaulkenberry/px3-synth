#pragma once

#include <JuceHeader.h>

#include "Card.h"

#include <memory>

#include "EnvelopeComponent.h"
#include "PluginProcessor.h"

class UIConfig;

class AmpEnvelopeComponent final : public juce::Component
{
public:
    AmpEnvelopeComponent(PX3SynthAudioProcessor& processorIn, juce::Colour accentIn);

    void resized() override;
    void paint(juce::Graphics& g) override;

    void setPanelContentBounds(juce::Rectangle<int> panelContent);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);
    void refreshFromParameters();

private:
    PX3SynthAudioProcessor& processor;
    px3::ui::CardHost card;

    juce::ToggleButton enabledButton;
    juce::Label enabledLabel;
    juce::Label assignLabel;
    juce::ComboBox assignBox;

    std::unique_ptr<EnvelopeComponent> envelopeGraph;
};
