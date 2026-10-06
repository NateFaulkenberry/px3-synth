#pragma once

#include "FxChain.h"
#include "FxCardComponent.h"

#include <map>

#include <JuceHeader.h>

#include "FxRack.h"

#include "DelayComponent.h"
#include "MoodComponent.h"
#include "UIConfig.h"

class FxPanel final : public juce::Component
{
public:
    FxPanel(juce::ToggleButton& delayBypass,
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
            juce::Colour panelAccent);

    void paint(juce::Graphics& g) override;

    // The chain order to display. The send section lays its cards out in this
    // order; the INSTRUMENT and MASTER sections are fixed and ignore it.
    void setChainOrder(const px3::FxOrder& order);

    // Cards that own their own controls are handed over whole, rather than
    // having every knob passed through this constructor. The panel parents them
    // into the scrolling rack and places them by domain and chain order.
    void addCard(int sectionId, std::unique_ptr<px3::ui::FxCardComponent> card);
    px3::ui::FxCardComponent* cardForSection(int sectionId) const;
    // The component a stage shows, card or not, so a test can hold the Synth's
    // Delay and Mood panels against the standalone products' copies.
    juce::Component* debugComponentForSection(int sectionId) const
    { return componentForSection(sectionId); }
    void setSectionActive(int sectionId, bool active);

    // The delay's algorithm-specific controls and the algorithm that picks
    // which of them show.
    void setDelayAlgorithmControls(const DelayComponent::AlgorithmControls& controls);
    void setDelayAlgorithm(int algorithmIndex);

    // The page is three sections, one per processing domain (FxRack.h):
    // INSTRUMENT (ANALOG inside the voices, then VIBE on the whole instrument),
    // SEND FX (the reorderable FX bus chain) and MASTER (LUCY then SPREAD on
    // the finished mix). Only the send section can be reordered.
    static bool isReorderable(int sectionId) noexcept { return px3::isSendChainFxStage(sectionId); }
    static bool isUpstreamOfChain(int sectionId) noexcept { return px3::isUpstreamFxStage(sectionId); }
    static px3::ui::FxDomain domainOf(int sectionId) noexcept
    {
        if (px3::isUpstreamFxStage(sectionId)) return px3::ui::FxDomain::instrument;
        if (px3::isMasterFxStage(sectionId)) return px3::ui::FxDomain::master;
        return px3::ui::FxDomain::send;
    }
    static juce::String debugSectionName(int sectionId) { return sectionName(sectionId); }

    // The single bus send in the SEND FX header (mix.send.fx.level).
    juce::Slider& busSendKnob() noexcept { return rack.busSendKnob(); }
    px3::ui::FxRackCanvas& debugRack() noexcept { return rack; }
    juce::Viewport& debugViewport() noexcept { return viewport; }

    // Raised when the user drags a send card into a new place. The panel does
    // not apply it: the editor writes it to the processor, which feeds it
    // back through setChainOrder.
    std::function<void(const px3::FxOrder&)> onChainOrderChanged;

    void setActive(bool delayEnabled,
                   bool granularModeSelectable,
                   bool moodEnabled,
                   bool reverbEnabled);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);

    void resized() override;

private:
    void refreshSections();
    void layoutRack(bool animate);
    juce::Component* componentForSection(int sectionId) const;
    static juce::String sectionName(int sectionId);
    static juce::String styleKeyFor(int sectionId);

    // Declared before the rack: the rack is destroyed first and unhooks its
    // hover listener from cards that still exist.
    std::unique_ptr<DelayComponent> delayPanelComponent;
    std::unique_ptr<MoodComponent> moodComponent;
    std::map<int, std::unique_ptr<px3::ui::FxCardComponent>> ownedCards;

    juce::Colour accent;

    px3::ui::FxRackCanvas rack;
    juce::Viewport viewport;
    px3::FxOrder chainOrder { px3::kDefaultFxOrder };
    std::array<bool, px3::kFxStageCount> sectionActive { {} };
    std::shared_ptr<const UIConfig> uiConfig;
};
