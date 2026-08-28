#include "FxPanel.h"

FxPanel::FxPanel(juce::ToggleButton& vibeBypass,
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
                 juce::Colour panelAccent)
    : accent(panelAccent)
{
    vibeUiComponent = std::make_unique<VibeComponent>(vibeBypass,
                                                        vibeAmountKnob,
                                                        vibeAmountLabel,
                                                        vibeTypeBox,
                                                        vibeTypeLabel,
                                                        juce::Colour::fromRGB(236, 182, 92));
    delayPanelComponent = std::make_unique<DelayComponent>(delayBypass,
                                                                delayAmountKnob,
                                                                delayAmountLabel,
                                                                delayAlgoBox,
                                                                delayAlgoLabel,
                                                                granularSyncBox,
                                                                granularSyncLabel,
                                                                granularModeBox,
                                                                granularModeLabel,
                                                                delayTimeKnob,
                                                                delayTimeLabel,
                                                                delayFeedbackKnob,
                                                                delayFeedbackLabel,
                                                                juce::Colour::fromRGB(132, 210, 255));
    moodComponent = std::make_unique<MoodComponent>(moodBypass,
                                                    moodFreeze,
                                                    moodMixKnob,
                                                    moodMixLabel,
                                                    moodClockKnob,
                                                    moodClockLabel,
                                                    moodWetTimeKnob,
                                                    moodWetTimeLabel,
                                                    moodWetModifyKnob,
                                                    moodWetModifyLabel,
                                                    moodLoopLengthKnob,
                                                    moodLoopLengthLabel,
                                                    moodLoopModifyKnob,
                                                    moodLoopModifyLabel,
                                                    moodFeedbackKnob,
                                                    moodFeedbackLabel,
                                                    moodSpreadKnob,
                                                    moodSpreadLabel,
                                                    moodDegradeKnob,
                                                    moodDegradeLabel,
                                                    moodRoutingBox,
                                                    moodRoutingLabel,
                                                    moodWetModeBox,
                                                    moodWetModeLabel,
                                                    moodLoopModeBox,
                                                    moodLoopModeLabel,
                                                    juce::Colour::fromRGB(202, 150, 98));
    reverbComponent = std::make_unique<ReverbComponent>(reverbBypass,
                                                        reverbKnob,
                                                        reverbLabel,
                                                        reverbTypeBox,
                                                        reverbTypeLabel,
                                                        juce::Colour::fromRGB(128, 208, 255));

    addAndMakeVisible(*vibeUiComponent);
    addAndMakeVisible(*delayPanelComponent);
    addAndMakeVisible(*moodComponent);
    addAndMakeVisible(*reverbComponent);
}

// The four FX section cards are drawn by the components themselves - see
// VibeComponent::paint and its siblings. They were briefly drawn here, moved out
// of the editor; owning them in each component is a step further and is what
// makes drag-and-drop reordering free, because a card that follows its own
// component's bounds needs no separate bookkeeping when the order changes.
void FxPanel::paint(juce::Graphics& g)
{
    const auto fillAlpha = uiConfig != nullptr ? uiConfig->getFloat("fx.panel.fillAlpha", 0.14f) : 0.14f;
    const auto strokeAlpha = uiConfig != nullptr ? uiConfig->getFloat("fx.panel.strokeAlpha", 0.75f) : 0.75f;
    const auto radius = uiConfig != nullptr ? uiConfig->getFloat("fx.panel.cornerRadius", 10.0f) : 10.0f;

    const auto area = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(accent.withAlpha(fillAlpha));
    g.fillRoundedRectangle(area, radius);

    g.setColour(accent.withAlpha(strokeAlpha));
    g.drawRoundedRectangle(area, radius, 1.0f);
}

void FxPanel::setSectionBounds(const juce::Rectangle<int>& vibeBounds,
                               const juce::Rectangle<int>& delayBounds,
                               const juce::Rectangle<int>& moodBounds,
                               const juce::Rectangle<int>& reverbBounds)
{
    if (vibeUiComponent != nullptr)
    {
        vibeUiComponent->setBounds(vibeBounds);
    }

    if (delayPanelComponent != nullptr)
    {
        delayPanelComponent->setBounds(delayBounds);
    }

    if (moodComponent != nullptr)
    {
        moodComponent->setBounds(moodBounds);
    }

    if (reverbComponent != nullptr)
    {
        reverbComponent->setBounds(reverbBounds);
    }
}

void FxPanel::setActive(bool vibeEnabled,
                        bool delayEnabled,
                        bool granularModeSelectable,
                        bool moodEnabled,
                        bool reverbEnabled)
{
    if (vibeUiComponent != nullptr)
    {
        vibeUiComponent->setActive(vibeEnabled);
    }

    if (delayPanelComponent != nullptr)
    {
        delayPanelComponent->setActive(delayEnabled, granularModeSelectable);
    }

    if (moodComponent != nullptr)
    {
        moodComponent->setActive(moodEnabled);
    }

    if (reverbComponent != nullptr)
    {
        reverbComponent->setActive(reverbEnabled);
    }
}

void FxPanel::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);

    if (vibeUiComponent != nullptr)
    {
        vibeUiComponent->setUIConfig(uiConfig);
    }
    if (delayPanelComponent != nullptr)
    {
        delayPanelComponent->setUIConfig(uiConfig);
    }
    if (moodComponent != nullptr)
    {
        moodComponent->setUIConfig(uiConfig);
    }
    if (reverbComponent != nullptr)
    {
        reverbComponent->setUIConfig(uiConfig);
    }

    repaint();
}
