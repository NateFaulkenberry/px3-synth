#pragma once

#include <JuceHeader.h>

#include "ChipLabel.h"

#include <memory>

#include "EnvelopeComponent.h"
#include "PluginProcessor.h"

class UIConfig;

class AmpEnvelopeComponent final : public juce::Component
{
public:
    AmpEnvelopeComponent(PX3SynthAudioProcessor& processorIn, juce::Colour accentIn);

    void resized() override;

    void setPanelContentBounds(juce::Rectangle<int> panelContent);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);
    void refreshFromParameters();

    // What the user is actually looking at, for the UI tests.
    const EnvelopeComponent* debugGraph() const { return envelopeGraph.get(); }

    void setKnobLookAndFeel(juce::LookAndFeel* lookAndFeel)
    {
        if (envelopeGraph != nullptr) { envelopeGraph->setKnobLookAndFeel(lookAndFeel); }
    }

private:
    PX3SynthAudioProcessor& processor;

    juce::ToggleButton enabledButton;
    px3::ui::ChipLabel enabledLabel;
    px3::ui::ChipLabel assignLabel;
    juce::ComboBox assignBox;

    std::unique_ptr<EnvelopeComponent> envelopeGraph;
};
