#include "FltPanel.h"

#include "BypassButton.h"
#include "CardInner.h"
#include "ToggleChipButton.h"

#include "FilterMode.h"

#include "UIConfig.h"

#include <array>

juce::PopupMenu::Options FltPanel::FilterComboLookAndFeel::getOptionsForComboBoxPopupMenu(juce::ComboBox& box,
                                                                                            juce::Label& label)
{
    auto options = juce::LookAndFeel_V4::getOptionsForComboBoxPopupMenu(box, label);
    // Parented to the EDITOR, not to the combo's immediate parent.
    //
    // A menu hosted inside a component is clipped to that component's bounds.
    // The immediate parent here is one card, which is often shorter than the
    // menu - so the lower items were drawn clipped and could not be clicked,
    // which is why selecting an item sometimes appeared to do nothing and took
    // several attempts. The editor is large enough to hold the whole menu, and
    // keeping it in-window still suits hosts that dislike desktop-level popups.
    auto* host = box.getTopLevelComponent();
    return options.withParentComponent(host != nullptr ? host : box.getParentComponent())
                  .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::upwards);
}

FltPanel::FltPanel(std::array<juce::ToggleButton*, kFilterInstanceCount> enabledButtonsIn,
                                     std::array<juce::Slider*, kFilterInstanceCount> cutoffKnobsIn,
                   std::array<juce::Label*, kFilterInstanceCount> cutoffLabelsIn,
                   std::array<juce::Slider*, kFilterInstanceCount> resonanceKnobsIn,
                   std::array<juce::Label*, kFilterInstanceCount> resonanceLabelsIn,
                   std::array<juce::ComboBox*, kFilterInstanceCount> filterTypeBoxesIn,
                   std::array<juce::Label*, kFilterInstanceCount> filterTypeLabelsIn,
                   std::array<juce::Slider*, kFilterInstanceCount> combTuneKnobsIn,
                   std::array<juce::Slider*, kFilterInstanceCount> combDecayKnobsIn,
                   std::array<juce::Slider*, kFilterInstanceCount> combDampingKnobsIn,
                   std::array<juce::Slider*, kFilterInstanceCount> combDispersionKnobsIn,
                   std::array<juce::Slider*, kFilterInstanceCount> combDriveKnobsIn,
                   std::array<juce::Slider*, kFilterInstanceCount> combMixKnobsIn,
                   std::array<juce::Button*, kFilterInstanceCount> combInvertButtonsIn,
                   std::array<juce::Label*, kFilterInstanceCount> combTuneLabelsIn,
                   std::array<juce::Label*, kFilterInstanceCount> combDecayLabelsIn,
                   std::array<juce::Label*, kFilterInstanceCount> combDampingLabelsIn,
                   std::array<juce::Label*, kFilterInstanceCount> combDispersionLabelsIn,
                   std::array<juce::Label*, kFilterInstanceCount> combDriveLabelsIn,
                   std::array<juce::Label*, kFilterInstanceCount> combMixLabelsIn,
                                     std::array<juce::AudioParameterBool*, kFilterInstanceCount> enabledParams,
                   std::array<juce::AudioParameterFloat*, kFilterInstanceCount> cutoffParams,
                   std::array<juce::AudioParameterFloat*, kFilterInstanceCount> resonanceParams,
                   std::array<juce::AudioParameterChoice*, kFilterInstanceCount> filterTypeParams,
                   // The comb parameters the response graph draws from. The
                   // knobs alone cannot serve: the graph needs the values.
                   std::array<juce::AudioParameterFloat*, kFilterInstanceCount> combTuneParamsIn,
                   std::array<juce::AudioParameterFloat*, kFilterInstanceCount> combDecayParamsIn,
                   std::array<juce::AudioParameterFloat*, kFilterInstanceCount> combDampingParamsIn,
                   juce::Colour panelAccent)
        : enabledButtons(enabledButtonsIn),
            cutoffKnobs(cutoffKnobsIn),
      cutoffLabels(cutoffLabelsIn),
      resonanceKnobs(resonanceKnobsIn),
      resonanceLabels(resonanceLabelsIn),
      filterTypeBoxes(filterTypeBoxesIn),
      combTuneParams(combTuneParamsIn),
      combDecayParams(combDecayParamsIn),
      combDampingParams(combDampingParamsIn),
      combTuneKnobs(combTuneKnobsIn),
      combDecayKnobs(combDecayKnobsIn),
      combDampingKnobs(combDampingKnobsIn),
      combDispersionKnobs(combDispersionKnobsIn),
      combDriveKnobs(combDriveKnobsIn),
      combMixKnobs(combMixKnobsIn),
      combInvertButtons(combInvertButtonsIn),
      combTuneLabels(combTuneLabelsIn),
      combDecayLabels(combDecayLabelsIn),
      combDampingLabels(combDampingLabelsIn),
      combDispersionLabels(combDispersionLabelsIn),
      combDriveLabels(combDriveLabelsIn),
      combMixLabels(combMixLabelsIn),
      filterTypeLabels(filterTypeLabelsIn),
      accent(panelAccent)
{
    for (int filterIndex = 0; filterIndex < kFilterInstanceCount; ++filterIndex)
    {
                addAndMakeVisible(*enabledButtons[static_cast<std::size_t>(filterIndex)]);
        addAndMakeVisible(*cutoffKnobs[static_cast<std::size_t>(filterIndex)]);
        addAndMakeVisible(*cutoffLabels[static_cast<std::size_t>(filterIndex)]);
        addAndMakeVisible(*resonanceKnobs[static_cast<std::size_t>(filterIndex)]);
        addAndMakeVisible(*resonanceLabels[static_cast<std::size_t>(filterIndex)]);
        filterTypeBoxes[static_cast<std::size_t>(filterIndex)]->setLookAndFeel(&filterComboLookAndFeel);
        addAndMakeVisible(*filterTypeBoxes[static_cast<std::size_t>(filterIndex)]);
        addAndMakeVisible(*filterTypeLabels[static_cast<std::size_t>(filterIndex)]);

        for (auto* knob : { combTuneKnobs[static_cast<std::size_t>(filterIndex)],
                            combDecayKnobs[static_cast<std::size_t>(filterIndex)],
                            combDampingKnobs[static_cast<std::size_t>(filterIndex)],
                            combDispersionKnobs[static_cast<std::size_t>(filterIndex)],
                            combDriveKnobs[static_cast<std::size_t>(filterIndex)],
                            combMixKnobs[static_cast<std::size_t>(filterIndex)] })
        {
            addAndMakeVisible(*knob);
        }
        for (auto* label : { combTuneLabels[static_cast<std::size_t>(filterIndex)],
                             combDecayLabels[static_cast<std::size_t>(filterIndex)],
                             combDampingLabels[static_cast<std::size_t>(filterIndex)],
                             combDispersionLabels[static_cast<std::size_t>(filterIndex)],
                             combDriveLabels[static_cast<std::size_t>(filterIndex)],
                             combMixLabels[static_cast<std::size_t>(filterIndex)] })
        {
            addAndMakeVisible(*label);
        }
        addAndMakeVisible(*combInvertButtons[static_cast<std::size_t>(filterIndex)]);

                const auto idx = static_cast<std::size_t>(filterIndex);
                cutoffLabelBaseColours[idx] = cutoffLabels[idx]->findColour(juce::Label::textColourId);
                resonanceLabelBaseColours[idx] = resonanceLabels[idx]->findColour(juce::Label::textColourId);
                filterTypeBoxBaseBgColours[idx] = filterTypeBoxes[idx]->findColour(juce::ComboBox::backgroundColourId);
                filterTypeBoxBaseTextColours[idx] = filterTypeBoxes[idx]->findColour(juce::ComboBox::textColourId);
                filterTypeBoxBaseOutlineColours[idx] = filterTypeBoxes[idx]->findColour(juce::ComboBox::outlineColourId);

        filterComponents[static_cast<std::size_t>(filterIndex)] = std::make_unique<FilterComponent>(
            *cutoffParams[static_cast<std::size_t>(filterIndex)],
            *resonanceParams[static_cast<std::size_t>(filterIndex)],
            *filterTypeParams[static_cast<std::size_t>(filterIndex)],
            *enabledParams[static_cast<std::size_t>(filterIndex)],
            "Filter " + juce::String(filterIndex + 1),
            panelAccent);
        addAndMakeVisible(*filterComponents[static_cast<std::size_t>(filterIndex)]);
        filterComponents[static_cast<std::size_t>(filterIndex)]->setCombParameters(
            *combTuneParams[static_cast<std::size_t>(filterIndex)],
            *combDecayParams[static_cast<std::size_t>(filterIndex)],
            *combDampingParams[static_cast<std::size_t>(filterIndex)]);
        filterComponents[static_cast<std::size_t>(filterIndex)]->toBack();
        filterComponents[static_cast<std::size_t>(filterIndex)]->onBackgroundClick =
            [this, filterIndex]
            {
                auto* button = enabledButtons[static_cast<std::size_t>(filterIndex)];
                if (button != nullptr)
                {
                    button->setToggleState(! button->getToggleState(), juce::sendNotification);
                }
            };
    }

    refreshFromParameters();
}

FltPanel::~FltPanel()
{
    for (auto* filterTypeBox : filterTypeBoxes)
    {
        if (filterTypeBox != nullptr)
        {
            filterTypeBox->setLookAndFeel(nullptr);
        }
    }
}

void FltPanel::paint(juce::Graphics& g)
{
    const auto fillAlpha = uiConfig != nullptr ? uiConfig->getFloat("flt.panel.fillAlpha", 0.14f) : 0.14f;
    const auto strokeAlpha = uiConfig != nullptr ? uiConfig->getFloat("flt.panel.strokeAlpha", 0.75f) : 0.75f;
    const auto panelRadius = uiConfig != nullptr ? uiConfig->getFloat("flt.panel.cornerRadius", 10.0f) : 10.0f;

    const auto area = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(accent.withAlpha(fillAlpha));
    g.fillRoundedRectangle(area, panelRadius);

    g.setColour(accent.withAlpha(strokeAlpha));
    g.drawRoundedRectangle(area, panelRadius, 1.0f);

    // Card titles are drawn by the FilterComponents themselves - see
    // FilterComponent::paint. Drawing them here wrote into the children's
    // bounds, so a title was not tied to the component it named.
}

void FltPanel::resized()
{
    const auto panelPadX = uiConfig != nullptr ? uiConfig->getInt("flt.panel.layout.padX", 12) : 12;
    const auto panelPadY = uiConfig != nullptr ? uiConfig->getInt("flt.panel.layout.padY", 10) : 10;
    auto panelArea = getLocalBounds().reduced(panelPadX, panelPadY);

    constexpr int gap = 8;
    const auto totalGap = gap * (kFilterInstanceCount - 1);
    const auto columnWidth = juce::jmax(1, (panelArea.getWidth() - totalGap) / kFilterInstanceCount);

    for (int filterIndex = 0; filterIndex < kFilterInstanceCount; ++filterIndex)
    {
        auto filterArea = juce::Rectangle<int>(panelArea.getX() + filterIndex * (columnWidth + gap),
                                               panelArea.getY(),
                                               columnWidth,
                                               panelArea.getHeight()).reduced(2, 0);

        auto& filterComponent = filterComponents[static_cast<std::size_t>(filterIndex)];
        if (filterComponent == nullptr)
        {
            continue;
        }

        filterComponent->setInstanceIndex(filterIndex + 1);
        filterComponent->setPanelContentBounds(panelArea);
        filterComponent->setBounds(filterArea);

        // The panel used to lay out the card's interior itself - a 24px enabled
        // row, a 6px gap, a 24px type row, a 120px knob band - in parallel with
        // the card the component drew. The component owns that geometry now and
        // hands back row rectangles; the panel only translates them, because
        // these controls are its own children rather than the component's.
        filterComponent->layoutCardInner();

        const auto toPanel = [&filterArea](juce::Rectangle<int> r)
        {
            return r.translated(filterArea.getX(), filterArea.getY());
        };

        // Pinned to the cardInner corner, outside the flex flow. The panel owns
        // the button, so it does the placing; the component supplies the slot.
        const auto identity = filterComponent->cardAccentColour();

        if (auto* button = enabledButtons[static_cast<std::size_t>(filterIndex)])
        {
            button->setBounds(toPanel(filterComponent->powerBounds()));

            if (auto* power = dynamic_cast<px3::ui::BypassButton*>(button))
            {
                power->setAccentColour(identity);
            }
        }

        // The polarity switch gets its own red rather than the card's.
        //
        // Taking the card identity put a lit chip at the same brightness as the
        // card frame around it, so the two competed. A deeper red still reads
        // as part of the filter but sits behind its frame.
        if (auto* chip = dynamic_cast<px3::ui::ToggleChipButton*>(
                combInvertButtons[static_cast<std::size_t>(filterIndex)]))
        {
            const auto phaseColour = uiConfig != nullptr
                                         ? uiConfig->getColour("flt.combPhase.activeColour",
                                                               juce::Colour::fromRGB(0x8E, 0x24, 0x2C))
                                         : juce::Colour::fromRGB(0x8E, 0x24, 0x2C);
            chip->setAccentColour(phaseColour);
        }


        const auto idx = static_cast<std::size_t>(filterIndex);
        using px3::ui::ControlShape;

        // Row 1: the filter type dropdown. The power toggle used to share this
        // row and is pinned to the cardInner corner now.
        {
            auto flex = filterComponent->rowFlex(0);
            const auto gapMargin = filterComponent->rowGap(0);
            const auto row = toPanel(filterComponent->rowBounds(0));
            const auto cellHeight = static_cast<float>(juce::jmax(1, row.getHeight()));

            flex.items.add(juce::FlexItem(116.0f, cellHeight).withMargin(gapMargin));
            flex.performLayout(row.toFloat());

            const auto cell = [&flex](int i) { return flex.items.getReference(i).currentBounds.toNearestInt(); };
            px3::ui::layoutLabelledControl(cell(0),
                                       { filterTypeLabels[idx], filterTypeBoxes[idx], nullptr,
                                         ControlShape::stretch, 14, 0, 24 },
                                       filterComponent->rowControl(0));
        }

        // Row 2: whichever set of knobs the current mode uses.
        //
        // The comb is not a biquad response, so it does not have a cutoff or a
        // resonance - it has a pitch, a decay and a set of resonator controls.
        // Showing both sets at once would mean four dead knobs in every mode,
        // so the row carries one set or the other and the mode decides.
        {
            const auto combMode = px3::isCombMode(
                filterTypeBoxes[idx] != nullptr ? filterTypeBoxes[idx]->getSelectedItemIndex() : 0);

            std::vector<std::pair<juce::Label*, juce::Slider*>> knobs;
            if (combMode)
            {
                knobs = { { combTuneLabels[idx], combTuneKnobs[idx] },
                          { combDecayLabels[idx], combDecayKnobs[idx] },
                          { combDampingLabels[idx], combDampingKnobs[idx] },
                          { combDispersionLabels[idx], combDispersionKnobs[idx] },
                          { combDriveLabels[idx], combDriveKnobs[idx] },
                          { combMixLabels[idx], combMixKnobs[idx] } };
            }
            else
            {
                knobs = { { cutoffLabels[idx], cutoffKnobs[idx] },
                          { resonanceLabels[idx], resonanceKnobs[idx] } };
            }

            // Everything not in use is hidden rather than merely moved away:
            // a hidden control is skipped by the layout and cannot be tabbed to
            // or clicked through.
            const auto show = [](juce::Component* component, bool visible)
            {
                if (component != nullptr)
                {
                    component->setVisible(visible);
                }
            };
            show(cutoffKnobs[idx], ! combMode);
            show(cutoffLabels[idx], ! combMode);
            show(resonanceKnobs[idx], ! combMode);
            show(resonanceLabels[idx], ! combMode);
            for (auto* c : { static_cast<juce::Component*>(combTuneKnobs[idx]),
                             static_cast<juce::Component*>(combDecayKnobs[idx]),
                             static_cast<juce::Component*>(combDampingKnobs[idx]),
                             static_cast<juce::Component*>(combDispersionKnobs[idx]),
                             static_cast<juce::Component*>(combDriveKnobs[idx]),
                             static_cast<juce::Component*>(combMixKnobs[idx]),
                             static_cast<juce::Component*>(combTuneLabels[idx]),
                             static_cast<juce::Component*>(combDecayLabels[idx]),
                             static_cast<juce::Component*>(combDampingLabels[idx]),
                             static_cast<juce::Component*>(combDispersionLabels[idx]),
                             static_cast<juce::Component*>(combDriveLabels[idx]),
                             static_cast<juce::Component*>(combMixLabels[idx]),
                             static_cast<juce::Component*>(combInvertButtons[idx]) })
            {
                show(c, combMode);
            }

            const auto gapMargin = filterComponent->rowGap(1);
            auto row = toPanel(filterComponent->rowBounds(1));

            // The comb's six knobs go in two rows of three with the polarity
            // switch above them, centred. A single wrapping row would put the
            // switch wherever the wrap happened to leave it, which reads as an
            // afterthought rather than as the header it is.
            if (combMode && combInvertButtons[idx] != nullptr)
            {
                constexpr int switchHeight = 20;
                constexpr int switchWidth = 96;
                constexpr int switchGap = 6;

                auto switchRow = row.removeFromTop(switchHeight);
                combInvertButtons[idx]->setBounds(
                    juce::Rectangle<int>(switchWidth, switchHeight).withCentre(switchRow.getCentre()));
                row.removeFromTop(switchGap);
            }

            const auto perRow = combMode ? 3 : static_cast<int>(knobs.size());
            const auto rowCount = juce::jmax(1, (static_cast<int>(knobs.size()) + perRow - 1) / perRow);
            const auto knobRowHeight = juce::jmax(1, row.getHeight() / rowCount);
            const auto knobCap = combMode ? 52 : 84;

            for (int line = 0; line < rowCount; ++line)
            {
                auto lineArea = row.removeFromTop(knobRowHeight);
                const auto firstInLine = static_cast<std::size_t>(line * perRow);
                const auto countInLine = juce::jmin(static_cast<std::size_t>(perRow),
                                                    knobs.size() - firstInLine);

                auto flex = filterComponent->rowFlex(1);
                const std::vector<float> natural(countInLine, 84.0f);
                const auto widths = px3::ui::fitRowItemWidths(natural,
                                                              gapMargin.left + gapMargin.right,
                                                              static_cast<float>(juce::jmax(1, lineArea.getWidth())));
                for (const auto width : widths)
                {
                    flex.items.add(juce::FlexItem(width, static_cast<float>(lineArea.getHeight()))
                                       .withMargin(gapMargin));
                }
                flex.performLayout(lineArea.toFloat());

                for (std::size_t i = 0; i < countInLine; ++i)
                {
                    const auto& entry = knobs[firstInLine + i];
                    px3::ui::layoutLabelledControl(
                        flex.items.getReference(static_cast<int>(i)).currentBounds.toNearestInt(),
                        { entry.first, entry.second, nullptr, ControlShape::square, 16, 0, knobCap },
                        filterComponent->rowControl(1));
                }
            }
        }

        // Row 3 is the response graph, which the component draws.
    }
}

void FltPanel::refreshFromParameters()
{
    const auto disabledLabelColour = juce::Colour::fromRGB(176, 176, 176);
    const auto disabledComboBgColour = juce::Colour::fromRGBA(48, 48, 48, 210);
    const auto disabledComboTextColour = juce::Colour::fromRGB(176, 176, 176);
    const auto disabledComboOutlineColour = juce::Colour::fromRGBA(168, 168, 168, 94);

    for (int filterIndex = 0; filterIndex < kFilterInstanceCount; ++filterIndex)
    {
        const auto idx = static_cast<std::size_t>(filterIndex);
        const auto isEnabled = enabledButtons[idx]->getToggleState();

        // Relayout only when the mode actually changes. This runs at 30 Hz, and
        // laying the panel out on every tick to redo an identical arrangement
        // would be pure waste.
        const auto modeNow = filterTypeBoxes[idx] != nullptr
                                 ? filterTypeBoxes[idx]->getSelectedItemIndex()
                                 : 0;
        if (modeNow != lastLaidOutModes[idx])
        {
            lastLaidOutModes[idx] = modeNow;
            resized();
        }

        filterTypeBoxes[idx]->setEnabled(isEnabled);
        // The caption greys out with the control it names, like every other
        // label on this panel.
        filterTypeLabels[idx]->setEnabled(isEnabled);
        filterTypeBoxes[idx]->setColour(juce::ComboBox::backgroundColourId,
                                        isEnabled ? filterTypeBoxBaseBgColours[idx] : disabledComboBgColour);
        filterTypeBoxes[idx]->setColour(juce::ComboBox::textColourId,
                                        isEnabled ? filterTypeBoxBaseTextColours[idx] : disabledComboTextColour);
        filterTypeBoxes[idx]->setColour(juce::ComboBox::outlineColourId,
                                        isEnabled ? filterTypeBoxBaseOutlineColours[idx] : disabledComboOutlineColour);

        cutoffKnobs[idx]->setEnabled(isEnabled);
        cutoffKnobs[idx]->setInterceptsMouseClicks(isEnabled, isEnabled);
        cutoffKnobs[idx]->getProperties().set("knobBypassed", !isEnabled);
        cutoffKnobs[idx]->getProperties().set("psychedelicBypassGray", !isEnabled);
        cutoffLabels[idx]->setEnabled(isEnabled);
        cutoffLabels[idx]->setColour(juce::Label::textColourId,
                                     isEnabled ? cutoffLabelBaseColours[idx] : disabledLabelColour);

        resonanceKnobs[idx]->setEnabled(isEnabled);
        resonanceKnobs[idx]->setInterceptsMouseClicks(isEnabled, isEnabled);
        resonanceKnobs[idx]->getProperties().set("knobBypassed", !isEnabled);
        resonanceKnobs[idx]->getProperties().set("psychedelicBypassGray", !isEnabled);
        resonanceLabels[idx]->setEnabled(isEnabled);
        resonanceLabels[idx]->setColour(juce::Label::textColourId,
                                        isEnabled ? resonanceLabelBaseColours[idx] : disabledLabelColour);

        auto& filterComponent = filterComponents[idx];
        if (filterComponent != nullptr)
        {
            filterComponent->refreshFromParameters();
        }
    }
}

void FltPanel::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);

    // Re-read the type boxes' colours now that the config has styled them.
    //
    // These were captured once in the constructor - before any styling ran - and
    // then written back on every refresh, which pinned the two filter dropdowns
    // to JUCE's defaults and made them the only combos in the plugin that
    // ignored UIConfig.
    for (std::size_t idx = 0; idx < filterTypeBoxes.size(); ++idx)
    {
        if (auto* box = filterTypeBoxes[idx])
        {
            filterTypeBoxBaseBgColours[idx] = box->findColour(juce::ComboBox::backgroundColourId);
            filterTypeBoxBaseTextColours[idx] = box->findColour(juce::ComboBox::textColourId);
            filterTypeBoxBaseOutlineColours[idx] = box->findColour(juce::ComboBox::outlineColourId);
        }
    }

    for (auto& filterComponent : filterComponents)
    {
        if (filterComponent != nullptr)
        {
            filterComponent->setUIConfig(uiConfig);
        }
    }

    repaint();
}
