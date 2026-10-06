#include "FxPanel.h"


#include <algorithm>

FxPanel::FxPanel(juce::ToggleButton& delayBypass,
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
                 juce::Colour panelAccent)
    : accent(panelAccent)
{
    sectionActive.fill(true);

    viewport.setViewedComponent(&rack, false);
    viewport.setScrollBarsShown(true, false);
    viewport.setScrollBarThickness(10);
    viewport.setSingleStepSizes(16, 24);
    addAndMakeVisible(viewport);

    // Reported upward. The rack only knows the send stages; the processor's
    // order is a permutation of every stage, so the fixed stages are put back
    // where they were and the send stages fill the send slots in their new
    // order. The panel never writes the order itself - it is handed one.
    rack.onSendOrderChanged = [this](const std::vector<int>& sendOrder)
    {
        if (onChainOrderChanged == nullptr)
        {
            return;
        }

        auto next = chainOrder;
        auto source = sendOrder.begin();
        for (auto& stage : next)
        {
            if (! isReorderable(stage) || componentForSection(stage) == nullptr) { continue; }
            if (source == sendOrder.end()) { return; }
            stage = *source++;
        }
        if (source != sendOrder.end()) { return; }

        auto sorted = next;
        auto original = chainOrder;
        std::sort(sorted.begin(), sorted.end());
        std::sort(original.begin(), original.end());
        if (sorted != original) { return; }

        onChainOrderChanged(next);
    };

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

    refreshSections();
}

// The page furniture (section headers, arrows, rails) is painted by the rack,
// and every card draws its own faceplate.
void FxPanel::paint(juce::Graphics& g)
{
    const auto fillAlpha = uiConfig != nullptr ? uiConfig->getFloat("fx.panel.fillAlpha", 0.14f) : 0.14f;
    const auto strokeAlpha = uiConfig != nullptr ? uiConfig->getFloat("fx.panel.strokeAlpha", 0.75f) : 0.75f;
    const auto radius = uiConfig != nullptr ? uiConfig->getFloat("fx.panel.cornerRadius", 10.0f) : 10.0f;
    if (fillAlpha <= 0.0f && strokeAlpha <= 0.0f)
    {
        return;
    }

    const auto area = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(accent.withAlpha(fillAlpha));
    g.fillRoundedRectangle(area, radius);

    g.setColour(accent.withAlpha(strokeAlpha));
    g.drawRoundedRectangle(area, radius, 1.0f);
}

void FxPanel::setChainOrder(const px3::FxOrder& order)
{
    const auto changed = order != chainOrder;
    chainOrder = order;
    refreshSections();
    // A new order slides the cards into place; anything else is a re-layout.
    layoutRack(changed && isShowing());
}

void FxPanel::addCard(int sectionId, std::unique_ptr<px3::ui::FxCardComponent> card)
{
    if (card == nullptr)
    {
        return;
    }

    ownedCards[sectionId] = std::move(card);
    refreshSections();
    layoutRack(false);
}

px3::ui::FxCardComponent* FxPanel::cardForSection(int sectionId) const
{
    const auto it = ownedCards.find(sectionId);
    return it != ownedCards.end() ? it->second.get() : nullptr;
}

void FxPanel::setDelayAlgorithmControls(const DelayComponent::AlgorithmControls& controls)
{
    if (delayPanelComponent != nullptr) { delayPanelComponent->setAlgorithmControls(controls); }
}

void FxPanel::setDelayAlgorithm(int algorithmIndex)
{
    if (delayPanelComponent != nullptr) { delayPanelComponent->setAlgorithm(algorithmIndex); }
}

void FxPanel::setSectionActive(int sectionId, bool active)
{
    const auto slot = static_cast<std::size_t>(
        juce::jlimit(0, static_cast<int>(sectionActive.size()) - 1, sectionId));
    if (sectionActive[slot] == active)
    {
        return;
    }

    sectionActive[slot] = active;
    rack.setStageActive(sectionId, active);
}

void FxPanel::refreshSections()
{
    // Built from the stage lists in FxChain.h, not from a list of cards: a new
    // stage lands in the right section by being classified there, and a
    // section grows and wraps without this changing.
    const auto stageFor = [this](int sectionId)
    {
        const auto slot = static_cast<std::size_t>(
            juce::jlimit(0, static_cast<int>(sectionActive.size()) - 1, sectionId));
        return px3::ui::FxRackCanvas::Stage { sectionId, componentForSection(sectionId), sectionName(sectionId),
                                              styleKeyFor(sectionId), sectionActive[slot] };
    };

    std::vector<px3::ui::FxRackCanvas::Stage> instrument;
    std::vector<px3::ui::FxRackCanvas::Stage> send;
    std::vector<px3::ui::FxRackCanvas::Stage> master;

    for (const auto sectionId : px3::kUpstreamFxStageOrder)
    {
        if (componentForSection(sectionId) != nullptr) { instrument.push_back(stageFor(sectionId)); }
    }
    // The send chain in the processor's order. A stage with no component yet
    // still processes; it simply has nothing to show.
    for (const auto sectionId : chainOrder)
    {
        if (isReorderable(sectionId) && componentForSection(sectionId) != nullptr) { send.push_back(stageFor(sectionId)); }
    }
    // The master stages in the order they process, whatever slot a saved
    // order happens to give them.
    for (const auto sectionId : px3::kMasterFxStageOrder)
    {
        if (componentForSection(sectionId) != nullptr) { master.push_back(stageFor(sectionId)); }
    }

    rack.setSections(std::move(instrument), std::move(send), std::move(master));
}

juce::String FxPanel::sectionName(int sectionId)
{
    switch (sectionId)
    {
        case px3::fxStageVibe:         return "VIBE";
        case px3::fxStageDelay:        return "DELAY";
        case px3::fxStageReverb:       return "REVERB";
        case px3::fxStageMood:         return "MOOD";
        case px3::fxStageDoom:         return "DOOM";
        case px3::fxStageLucy:         return "LUCY";
        case px3::fxStageChorus:       return "CHORUS";
        case px3::fxStageStereoSpread: return "SPREAD";
        case px3::fxStageDistortion:   return "DRIVE";
        case px3::fxStageAnalog:       return "ANALOG";
        default: break;
    }
    return "FX";
}

juce::String FxPanel::styleKeyFor(int sectionId)
{
    // The cards.<key> block each stage's card is styled from, which is also
    // the key of its height in fx.rack.cardHeight.
    switch (sectionId)
    {
        case px3::fxStageVibe:         return "vibe";
        case px3::fxStageDelay:        return "delay";
        case px3::fxStageReverb:       return "reverb";
        case px3::fxStageMood:         return "mood";
        case px3::fxStageDoom:         return "doom";
        case px3::fxStageLucy:         return "lucy";
        case px3::fxStageChorus:       return "chorus";
        case px3::fxStageStereoSpread: return "stereoSpread";
        case px3::fxStageDistortion:   return "drive";
        case px3::fxStageAnalog:       return "analog";
        default: break;
    }
    return {};
}

juce::Component* FxPanel::componentForSection(int sectionId) const
{
    if (auto* owned = cardForSection(sectionId))
    {
        return owned;
    }

    switch (sectionId)
    {
        case px3::fxStageDelay:  return delayPanelComponent.get();
        case px3::fxStageMood:   return moodComponent.get();
        default: break;
    }
    return nullptr;
}

void FxPanel::layoutRack(bool animate)
{
    const auto viewWidth = viewport.getWidth();
    const auto viewHeight = viewport.getHeight();
    if (viewWidth <= 0 || viewHeight <= 0)
    {
        return;
    }

    // The scrollbar takes width from the page, so whether it is needed has to
    // be decided before the cards are measured - otherwise the first layout
    // sizes the rack for a bar that then appears and overlaps it. Laid out at
    // the width the last layout settled on; only if that guess turns out wrong
    // is it laid out again at the other width (placed, not slid).
    const auto narrow = juce::jmax(1, viewWidth - viewport.getScrollBarThickness() - 2);
    const auto guessScroll = rack.getWidth() == narrow || rack.getWidth() == 0;
    auto width = guessScroll ? narrow : viewWidth;
    auto height = rack.layoutForWidth(width, animate);
    const auto needsScroll = height > viewHeight;
    if (needsScroll != guessScroll)
    {
        width = needsScroll ? narrow : viewWidth;
        height = rack.layoutForWidth(width, false);
    }
    rack.setSize(width, juce::jmax(height, viewHeight));
}

void FxPanel::resized()
{
    const auto padX = uiConfig != nullptr ? uiConfig->getInt("fx.panel.layout.padX", 0) : 0;
    const auto padY = uiConfig != nullptr ? uiConfig->getInt("fx.panel.layout.padY", 0) : 0;
    viewport.setBounds(getLocalBounds().reduced(padX, padY));
    layoutRack(false);
}

void FxPanel::setActive(bool delayEnabled,
                        bool granularModeSelectable,
                        bool moodEnabled,
                        bool reverbEnabled)
{
    if (delayPanelComponent != nullptr)
    {
        delayPanelComponent->setActive(delayEnabled, granularModeSelectable);
    }

    if (moodComponent != nullptr)
    {
        moodComponent->setActive(moodEnabled);
    }

    setSectionActive(px3::fxStageDelay, delayEnabled);
    setSectionActive(px3::fxStageReverb, reverbEnabled);
    setSectionActive(px3::fxStageMood, moodEnabled);
}

void FxPanel::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);
    rack.setUIConfig(uiConfig);

    for (auto& entry : ownedCards)
    {
        entry.second->setUIConfig(uiConfig);
    }

    if (delayPanelComponent != nullptr)
    {
        delayPanelComponent->setUIConfig(uiConfig);
    }
    if (moodComponent != nullptr)
    {
        moodComponent->setUIConfig(uiConfig);
    }

    // The rack's geometry and every card height come from the config, so a
    // live reload re-lays the page out rather than only repainting it.
    resized();
    repaint();
}
