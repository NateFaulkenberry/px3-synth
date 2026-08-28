#include "FltPanel.h"

#include "BypassButton.h"
#include "CardInner.h"

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
                                     std::array<juce::AudioParameterBool*, kFilterInstanceCount> enabledParams,
                   std::array<juce::AudioParameterFloat*, kFilterInstanceCount> cutoffParams,
                   std::array<juce::AudioParameterFloat*, kFilterInstanceCount> resonanceParams,
                   std::array<juce::AudioParameterChoice*, kFilterInstanceCount> filterTypeParams,
                   juce::Colour panelAccent)
        : enabledButtons(enabledButtonsIn),
            cutoffKnobs(cutoffKnobsIn),
      cutoffLabels(cutoffLabelsIn),
      resonanceKnobs(resonanceKnobsIn),
      resonanceLabels(resonanceLabelsIn),
      filterTypeBoxes(filterTypeBoxesIn),
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

        // Row 2: the filter's knobs. Driven off a list rather than two hard
        // coded cells, so adding a parameter later is one entry here and not a
        // layout rewrite - the widths scale to fit however many there are.
        {
            const std::array<std::pair<juce::Label*, juce::Slider*>, 2> knobs { {
                { cutoffLabels[idx], cutoffKnobs[idx] },
                { resonanceLabels[idx], resonanceKnobs[idx] },
            } };

            auto flex = filterComponent->rowFlex(1);
            const auto gapMargin = filterComponent->rowGap(1);
            const auto row = toPanel(filterComponent->rowBounds(1));
            const auto cellHeight = static_cast<float>(juce::jmax(1, row.getHeight()));

            // Same natural width and cap as the LFO knobs, so the two cards
            // read as one family.
            const std::vector<float> natural(knobs.size(), 84.0f);
            const auto widths = px3::ui::fitRowItemWidths(natural,
                                                          gapMargin.left + gapMargin.right,
                                                          static_cast<float>(juce::jmax(1, row.getWidth())));
            for (const auto width : widths)
            {
                flex.items.add(juce::FlexItem(width, cellHeight).withMargin(gapMargin));
            }
            flex.performLayout(row.toFloat());

            for (std::size_t i = 0; i < knobs.size(); ++i)
            {
                px3::ui::layoutLabelledControl(flex.items.getReference(static_cast<int>(i)).currentBounds.toNearestInt(),
                                               { knobs[i].first, knobs[i].second, nullptr,
                                                 ControlShape::square, 16, 0, 84 },
                                               filterComponent->rowControl(1));
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
