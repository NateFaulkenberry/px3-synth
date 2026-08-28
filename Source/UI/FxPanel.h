#pragma once

#include <JuceHeader.h>

#include "FxSignalFlow.h"

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

    // The chain order to display. The panel lays its cards out in this order
    // and hands the same order to the signal-flow strip, so the two views
    // cannot disagree - they are the same list read twice.
    void setChainOrder(const std::array<int, 4>& order);
    void setSectionActive(int sectionId, bool active);

    // Raised when the user drags the strip into a new order. The panel does not
    // apply it: the editor writes it to the processor, which feeds it back
    // through setChainOrder.
    std::function<void(const std::array<int, 4>&)> onChainOrderChanged;

    void setActive(bool vibeEnabled,
                   bool delayEnabled,
                   bool granularModeSelectable,
                   bool moodEnabled,
                   bool reverbEnabled);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);

    void resized() override;

private:
    void refreshSignalFlowNodes();
    juce::Component* componentForSection(int sectionId) const;
    juce::Colour sectionAccent(int sectionId) const;
    static juce::String sectionName(int sectionId);

    std::unique_ptr<VibeComponent> vibeUiComponent;
    std::unique_ptr<DelayComponent> delayPanelComponent;
    std::unique_ptr<MoodComponent> moodComponent;
    std::unique_ptr<ReverbComponent> reverbComponent;

    juce::Colour accent;

    // The strip lives above the cards and never scrolls with them: it is the
    // ordering control, so it has to stay reachable however far the grid runs.
    px3::ui::FxSignalFlow signalFlow;
    juce::Viewport gridViewport;
    juce::Component gridContent;
    std::array<int, 4> chainOrder { { 0, 1, 3, 2 } };
    std::array<bool, 4> sectionActive { { true, true, true, true } };
    std::shared_ptr<const UIConfig> uiConfig;
};
