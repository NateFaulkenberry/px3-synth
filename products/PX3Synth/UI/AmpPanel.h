#pragma once

#include <JuceHeader.h>

#include <memory>

#include "AmpEnvelopeComponent.h"
#include "PluginProcessor.h"

class UIConfig;

class AmpPanel final : public juce::Component
{
public:
    // The editor's shared rotary look-and-feel, for the ADSR knobs under the
    // graph. Handed down rather than constructed here, so every knob in the
    // plugin is drawn by one of these.
    void setKnobLookAndFeel(juce::LookAndFeel* lookAndFeel);

    AmpPanel(PX3SynthAudioProcessor& processorIn, juce::Colour panelAccent);

    void paint(juce::Graphics& g) override;
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
