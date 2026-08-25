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
    reverbUiComponent = std::make_unique<ReverbUiComponent>(reverbBypass,
                                                            reverbKnob,
                                                            reverbLabel,
                                                            reverbTypeBox,
                                                            reverbTypeLabel,
                                                            juce::Colour::fromRGB(128, 208, 255));

    addAndMakeVisible(*vibeUiComponent);
    addAndMakeVisible(*delayPanelComponent);
    addAndMakeVisible(*reverbUiComponent);
}

void FxPanel::paint(juce::Graphics& g)
{
    const auto fillAlpha = uiConfig != nullptr ? uiConfig->getFloat("fx.panel.fillAlpha", 0.14f) : 0.14f;
    const auto topFillAlpha = uiConfig != nullptr ? uiConfig->getFloat("fx.panel.topFillAlpha", 0.10f) : 0.10f;
    const auto strokeAlpha = uiConfig != nullptr ? uiConfig->getFloat("fx.panel.strokeAlpha", 0.75f) : 0.75f;
    const auto radius = uiConfig != nullptr ? uiConfig->getFloat("fx.panel.cornerRadius", 10.0f) : 10.0f;

    const auto area = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(accent.withAlpha(fillAlpha));
    g.fillRoundedRectangle(area, radius);

    g.setColour(accent.withAlpha(topFillAlpha));
    g.fillRoundedRectangle(area.withTrimmedBottom(area.getHeight() * 0.5f), radius);

    g.setColour(accent.withAlpha(strokeAlpha));
    g.drawRoundedRectangle(area, radius, 1.0f);

}

void FxPanel::setSectionBounds(const juce::Rectangle<int>& vibeBounds,
                               const juce::Rectangle<int>& delayBounds,
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

    if (reverbUiComponent != nullptr)
    {
        reverbUiComponent->setBounds(reverbBounds);
    }
}

void FxPanel::setActive(bool vibeEnabled, bool delayEnabled, bool granularModeSelectable, bool reverbEnabled)
{
    if (vibeUiComponent != nullptr)
    {
        vibeUiComponent->setActive(vibeEnabled);
    }

    if (delayPanelComponent != nullptr)
    {
        delayPanelComponent->setActive(delayEnabled, granularModeSelectable);
    }

    if (reverbUiComponent != nullptr)
    {
        reverbUiComponent->setActive(reverbEnabled);
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
    if (reverbUiComponent != nullptr)
    {
        reverbUiComponent->setUIConfig(uiConfig);
    }

    repaint();
}
