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

// Draws the four FX section cards.
//
// These used to be painted by the editor, into this panel's rectangle. Six
// panels are stacked in that rectangle and swapped by visibility, so decoration
// painted by the parent is not tied to any child's lifetime: hiding this panel
// invalidates this panel's bounds, but nothing guarantees the parent's own
// pixels underneath are redrawn. Owning the drawing here makes a leftover card
// outline impossible rather than merely unlikely.
void FxPanel::paintSectionCards(juce::Graphics& g) const
{
    struct Card
    {
        const juce::Rectangle<int>& area;
        bool enabled;
        juce::Colour fill;
        juce::Colour outline;
        juce::Colour text;
        const char* label;
    };

    const Card cards[] = {
        { vibeSectionArea,   vibeSectionEnabled,
          juce::Colour::fromRGBA(104, 194, 255, 35), juce::Colour::fromRGBA(104, 194, 255, 180),
          juce::Colour::fromRGB(240, 245, 255), "VIBE" },
        { delaySectionArea,  delaySectionEnabled,
          juce::Colour::fromRGBA(255, 198, 110, 35), juce::Colour::fromRGBA(255, 198, 110, 180),
          juce::Colour::fromRGB(250, 244, 224), "DELAY" },
        { moodSectionArea,   moodSectionEnabled,
          juce::Colour::fromRGBA(238, 182, 120, 35), juce::Colour::fromRGBA(238, 182, 120, 180),
          juce::Colour::fromRGB(255, 240, 214), "MOOD" },
        { reverbSectionArea, reverbSectionEnabled,
          juce::Colour::fromRGBA(128, 208, 255, 30), juce::Colour::fromRGBA(128, 208, 255, 150),
          juce::Colour::fromRGB(224, 245, 255), "REVERB" },
    };

    static const auto disabledFill = juce::Colour::fromRGBA(120, 120, 120, 30);
    static const auto disabledOutline = juce::Colour::fromRGBA(150, 150, 150, 130);
    static const auto disabledText = juce::Colour::fromRGB(170, 170, 170);

    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));

    for (const auto& card : cards)
    {
        if (card.area.isEmpty())
        {
            continue;
        }

        const auto bounds = card.area.toFloat();
        g.setColour(card.enabled ? card.fill : disabledFill);
        g.fillRoundedRectangle(bounds, 10.0f);
        g.setColour(card.enabled ? card.outline : disabledOutline);
        g.drawRoundedRectangle(bounds, 10.0f, 1.0f);

        g.setColour(card.enabled ? card.text : disabledText);
        g.drawText(card.label, card.area.withTrimmedTop(5).withHeight(18),
                   juce::Justification::centred);
    }
}

void FxPanel::paint(juce::Graphics& g)
{
    const auto fillAlpha = uiConfig != nullptr ? uiConfig->getFloat("fx.panel.fillAlpha", 0.14f) : 0.14f;
    const auto strokeAlpha = uiConfig != nullptr ? uiConfig->getFloat("fx.panel.strokeAlpha", 0.75f) : 0.75f;
    const auto radius = uiConfig != nullptr ? uiConfig->getFloat("fx.panel.cornerRadius", 10.0f) : 10.0f;

    // Cards first, then the panel wash over them - the same order, and so the
    // same appearance, as when the editor painted the cards and this panel's
    // translucent background then tinted them.
    paintSectionCards(g);

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
    // Kept so paint() can draw the cards behind these components. They arrive
    // in this panel's coordinates, which is what makes owning the drawing here
    // straightforward.
    if (vibeSectionArea != vibeBounds || delaySectionArea != delayBounds
        || moodSectionArea != moodBounds || reverbSectionArea != reverbBounds)
    {
        vibeSectionArea = vibeBounds;
        delaySectionArea = delayBounds;
        moodSectionArea = moodBounds;
        reverbSectionArea = reverbBounds;
        repaint();
    }

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
    if (vibeSectionEnabled != vibeEnabled || delaySectionEnabled != delayEnabled
        || moodSectionEnabled != moodEnabled || reverbSectionEnabled != reverbEnabled)
    {
        vibeSectionEnabled = vibeEnabled;
        delaySectionEnabled = delayEnabled;
        moodSectionEnabled = moodEnabled;
        reverbSectionEnabled = reverbEnabled;
        repaint();
    }

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
