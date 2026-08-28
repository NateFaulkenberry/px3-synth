#pragma once

#include <JuceHeader.h>

#include "DelayComponent.h"
#include "MoodComponent.h"
#include "ReverbComponent.h"
#include "UIConfig.h"
#include "VibeComponent.h"

class FxPanel final : public juce::Component
{
public:
    FxPanel(juce::ToggleButton& vibeBypass,
            juce::Slider& vibeAmountKnob,
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
            juce::ToggleButton& moodBypass,
            juce::ToggleButton& moodFreeze,
            juce::Slider& moodMixKnob,
            juce::Label& moodMixLabel,
            juce::Slider& moodClockKnob,
            juce::Label& moodClockLabel,
            juce::Slider& moodWetTimeKnob,
            juce::Label& moodWetTimeLabel,
            juce::Slider& moodWetModifyKnob,
            juce::Label& moodWetModifyLabel,
            juce::Slider& moodLoopLengthKnob,
            juce::Label& moodLoopLengthLabel,
            juce::Slider& moodLoopModifyKnob,
            juce::Label& moodLoopModifyLabel,
            juce::Slider& moodFeedbackKnob,
            juce::Label& moodFeedbackLabel,
            juce::Slider& moodSpreadKnob,
            juce::Label& moodSpreadLabel,
            juce::Slider& moodDegradeKnob,
            juce::Label& moodDegradeLabel,
            juce::ComboBox& moodRoutingBox,
            juce::Label& moodRoutingLabel,
            juce::ComboBox& moodWetModeBox,
            juce::Label& moodWetModeLabel,
            juce::ComboBox& moodLoopModeBox,
            juce::Label& moodLoopModeLabel,
            juce::ToggleButton& reverbBypass,
            juce::Slider& reverbKnob,
            juce::Label& reverbLabel,
            juce::ComboBox& reverbTypeBox,
            juce::Label& reverbTypeLabel,
            juce::Colour panelAccent);

    void paint(juce::Graphics& g) override;

    void setSectionBounds(const juce::Rectangle<int>& vibeBounds,
                          const juce::Rectangle<int>& delayBounds,
                          const juce::Rectangle<int>& moodBounds,
                          const juce::Rectangle<int>& reverbBounds);

    void setActive(bool vibeEnabled,
                   bool delayEnabled,
                   bool granularModeSelectable,
                   bool moodEnabled,
                   bool reverbEnabled);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);

private:
    std::unique_ptr<VibeComponent> vibeUiComponent;
    std::unique_ptr<DelayComponent> delayPanelComponent;
    std::unique_ptr<MoodComponent> moodComponent;
    std::unique_ptr<ReverbComponent> reverbComponent;

    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;
};
