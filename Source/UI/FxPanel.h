#pragma once

#include <JuceHeader.h>

#include "DelayUiComponent.h"
#include "ReverbUiComponent.h"
#include "VibeUiComponent.h"

class FxPanel final : public juce::Component
{
public:
    FxPanel(juce::ToggleButton& vibeBypass,
            juce::Slider& vibeAmountKnob,
            juce::Label& vibeAmountLabel,
            juce::ComboBox& vibeTypeBox,
            juce::Label& vibeTypeLabel,
            juce::ToggleButton& delayBypass,
            juce::Slider& delayAmountKnob,
            juce::Label& delayAmountLabel,
            juce::ComboBox& delayAlgoBox,
            juce::Label& delayAlgoLabel,
            juce::ComboBox& granularSyncBox,
            juce::Label& granularSyncLabel,
            juce::ComboBox& granularModeBox,
            juce::Label& granularModeLabel,
            juce::Slider& delayTimeKnob,
            juce::Label& delayTimeLabel,
            juce::Slider& delayFeedbackKnob,
            juce::Label& delayFeedbackLabel,
            juce::ToggleButton& reverbBypass,
            juce::Slider& reverbKnob,
            juce::Label& reverbLabel,
            juce::ComboBox& reverbTypeBox,
            juce::Label& reverbTypeLabel,
            juce::Colour panelAccent);

    void paint(juce::Graphics& g) override;

    void setSectionBounds(const juce::Rectangle<int>& vibeBounds,
                          const juce::Rectangle<int>& delayBounds,
                          const juce::Rectangle<int>& reverbBounds);

    void setActive(bool vibeEnabled, bool delayEnabled, bool granularModeSelectable, bool reverbEnabled);

private:
    std::unique_ptr<VibeUiComponent> vibeUiComponent;
    std::unique_ptr<DelayUiComponent> delayUiComponent;
    std::unique_ptr<ReverbUiComponent> reverbUiComponent;

    juce::String title { "FX" };
    juce::Colour accent;
};
