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
    vibeUiComponent = std::make_unique<VibeUiComponent>(vibeBypass,
                                                        vibeAmountKnob,
                                                        vibeAmountLabel,
                                                        vibeTypeBox,
                                                        vibeTypeLabel,
                                                        juce::Colour::fromRGB(236, 182, 92));
    delayUiComponent = std::make_unique<DelayUiComponent>(delayBypass,
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
    addAndMakeVisible(*delayUiComponent);
    addAndMakeVisible(*reverbUiComponent);
}

void FxPanel::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(accent.withAlpha(0.14f));
    g.fillRoundedRectangle(area, 10.0f);

    g.setColour(accent.withAlpha(0.10f));
    g.fillRoundedRectangle(area.withTrimmedBottom(area.getHeight() * 0.5f), 10.0f);

    g.setColour(accent.withAlpha(0.75f));
    g.drawRoundedRectangle(area, 10.0f, 1.0f);

    g.setColour(accent.brighter(0.30f));
    g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    g.drawText(title, getLocalBounds().removeFromTop(24), juce::Justification::centred);
}

void FxPanel::setSectionBounds(const juce::Rectangle<int>& vibeBounds,
                               const juce::Rectangle<int>& delayBounds,
                               const juce::Rectangle<int>& reverbBounds)
{
    if (vibeUiComponent != nullptr)
    {
        vibeUiComponent->setBounds(vibeBounds);
    }

    if (delayUiComponent != nullptr)
    {
        delayUiComponent->setBounds(delayBounds);
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

    if (delayUiComponent != nullptr)
    {
        delayUiComponent->setActive(delayEnabled, granularModeSelectable);
    }

    if (reverbUiComponent != nullptr)
    {
        reverbUiComponent->setActive(reverbEnabled);
    }
}
