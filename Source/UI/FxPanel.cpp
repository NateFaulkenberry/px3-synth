#include "FxPanel.h"

#include "FxChainLayout.h"

FxPanel::FxPanel(juce::ToggleButton& vibeBypass,
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
                 juce::Colour panelAccent)
    : accent(panelAccent)
{
    sectionActive.fill(true);

    addAndMakeVisible(signalFlow);
    gridViewport.setViewedComponent(&gridContent, false);
    gridViewport.setScrollBarsShown(true, false);
    gridViewport.setScrollBarThickness(10);
    addAndMakeVisible(gridViewport);

    // Reported upward. The panel never writes the order itself - it is handed
    // one and displays it.
    signalFlow.onOrderChanged = [this](const std::vector<int>& order)
    {
        if (order.size() != chainOrder.size() || onChainOrderChanged == nullptr)
        {
            return;
        }

        px3::FxOrder next {};
        std::copy(order.begin(), order.end(), next.begin());
        onChainOrderChanged(next);
    };

    vibeUiComponent = std::make_unique<VibeComponent>(vibeBypass,
                                                        vibeAmountKnob,
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

    // The cards belong to the scrolling content, not to the panel: the strip
    // above them must stay put while the grid scrolls.
    gridContent.addAndMakeVisible(*vibeUiComponent);
    gridContent.addAndMakeVisible(*delayPanelComponent);
    gridContent.addAndMakeVisible(*moodComponent);
    gridContent.addAndMakeVisible(*reverbComponent);

    refreshSignalFlowNodes();
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

void FxPanel::setChainOrder(const px3::FxOrder& order)
{
    chainOrder = order;
    refreshSignalFlowNodes();
    resized();
}

void FxPanel::addCard(int sectionId, std::unique_ptr<px3::ui::FxCardComponent> card)
{
    if (card == nullptr)
    {
        return;
    }

    gridContent.addAndMakeVisible(*card);
    ownedCards[sectionId] = std::move(card);
    refreshSignalFlowNodes();
    resized();
}

px3::ui::FxCardComponent* FxPanel::cardForSection(int sectionId) const
{
    const auto it = ownedCards.find(sectionId);
    return it != ownedCards.end() ? it->second.get() : nullptr;
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
    signalFlow.setNodeActive(sectionId, active);
}

void FxPanel::refreshSignalFlowNodes()
{
    // Built from the order the panel was given, so the strip and the grid are
    // two readings of one list rather than two lists that have to be kept in
    // step.
    std::vector<px3::ui::FxSignalFlow::Node> flowNodes;
    flowNodes.reserve(chainOrder.size());

    for (const auto sectionId : chainOrder)
    {
        // A stage with no card yet is not in the chain the user can see, so it
        // does not get a node. Reordering still carries it - the processor owns
        // the order, and it is a permutation of every stage either way.
        if (componentForSection(sectionId) == nullptr)
        {
            continue;
        }

        const auto slot = static_cast<std::size_t>(
            juce::jlimit(0, static_cast<int>(sectionActive.size()) - 1, sectionId));
        flowNodes.push_back({ sectionId,
                              sectionName(sectionId),
                              sectionAccent(sectionId),
                              sectionActive[slot] });
    }

    signalFlow.setNodes(std::move(flowNodes));
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
        default: break;
    }
    return "FX";
}

juce::Colour FxPanel::sectionAccent(int sectionId) const
{
    // Read from the same card blocks the FX cards use, so a node and its card
    // are the same colour without either being told about the other.
    if (uiConfig == nullptr)
    {
        return accent;
    }

    switch (sectionId)
    {
        case px3::fxStageVibe:         return uiConfig->getColour("cards.vibe.border.color", accent);
        case px3::fxStageDelay:        return uiConfig->getColour("cards.delay.border.color", accent);
        case px3::fxStageReverb:       return uiConfig->getColour("cards.reverb.border.color", accent);
        case px3::fxStageMood:         return uiConfig->getColour("cards.mood.border.color", accent);
        case px3::fxStageDoom:         return uiConfig->getColour("cards.doom.border.color", accent);
        case px3::fxStageLucy:         return uiConfig->getColour("cards.lucy.border.color", accent);
        case px3::fxStageChorus:       return uiConfig->getColour("cards.chorus.border.color", accent);
        case px3::fxStageStereoSpread: return uiConfig->getColour("cards.stereoSpread.border.color", accent);
        default: break;
    }
    return accent;
}

juce::Component* FxPanel::componentForSection(int sectionId) const
{
    if (auto* owned = cardForSection(sectionId))
    {
        return owned;
    }

    switch (sectionId)
    {
        case px3::fxStageVibe:   return vibeUiComponent.get();
        case px3::fxStageDelay:  return delayPanelComponent.get();
        case px3::fxStageReverb: return reverbComponent.get();
        case px3::fxStageMood:   return moodComponent.get();
        default: break;
    }
    return nullptr;
}

void FxPanel::resized()
{
    const auto padX = uiConfig != nullptr ? uiConfig->getInt("fx.panel.layout.padX", 0) : 0;
    const auto padY = uiConfig != nullptr ? uiConfig->getInt("fx.panel.layout.padY", 0) : 0;
    auto area = getLocalBounds().reduced(padX, padY);

    // The strip is fixed at the top and outside the viewport: it is how the
    // chain is reordered, so scrolling through a long grid must not take it
    // away.
    const auto stripHeight = uiConfig != nullptr ? uiConfig->getInt("fx.signalFlow.height", 46) : 46;
    signalFlow.setBounds(area.removeFromTop(stripHeight));

    const auto stripGap = uiConfig != nullptr ? uiConfig->getInt("fx.signalFlow.gapBelow", 8) : 8;
    area.removeFromTop(stripGap);

    gridViewport.setBounds(area);

    // ---- the grid ---------------------------------------------------------
    // A wrapping grid whose cell order IS the chain order, so the grid is a
    // second reading of the strip rather than a second list to keep in step.
    const auto columns = uiConfig != nullptr ? uiConfig->getInt("fx.grid.columns", 4) : 4;
    const auto gap = uiConfig != nullptr ? uiConfig->getInt("fx.grid.gap", 8) : 8;
    const auto rowHeight = uiConfig != nullptr ? uiConfig->getInt("fx.grid.rowHeight", 400) : 400;

    // Only the stages that have a card take a cell. A stage without one is
    // still in the chain and still processes; it simply has nothing to show.
    std::vector<juce::Component*> cards;
    cards.reserve(chainOrder.size());
    for (const auto sectionId : chainOrder)
    {
        if (auto* component = componentForSection(sectionId))
        {
            cards.push_back(component);
        }
    }

    const auto count = static_cast<int>(cards.size());
    const auto neededHeight = px3::ui::fxGridContentHeight(count, columns, gap, rowHeight);

    // The scrollbar takes width from the cells, so whether it is needed has to
    // be decided before they are measured - otherwise the first layout sizes
    // cells for a bar that then appears and overlaps them.
    const auto needsScrollBar = neededHeight > gridViewport.getHeight();
    const auto gutter = needsScrollBar ? gridViewport.getScrollBarThickness() + 4 : 0;
    const auto contentWidth = juce::jmax(1, gridViewport.getWidth() - gutter);

    gridContent.setBounds(0, 0, contentWidth, juce::jmax(gridViewport.getHeight(), neededHeight));

    const auto cells = px3::ui::fxGridCells(contentWidth, count, columns, gap, rowHeight);

    for (int i = 0; i < count && i < static_cast<int>(cells.size()); ++i)
    {
        cards[static_cast<std::size_t>(i)]->setBounds(cells[static_cast<std::size_t>(i)]);
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

    setSectionActive(0, vibeEnabled);
    setSectionActive(1, delayEnabled);
    setSectionActive(2, reverbEnabled);
    setSectionActive(3, moodEnabled);
}

void FxPanel::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);
    signalFlow.setUIConfig(uiConfig);

    for (auto& entry : ownedCards)
    {
        entry.second->setUIConfig(uiConfig);
    }

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
