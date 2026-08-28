#include "FltPanel.h"

#include "CardInner.h"

#include "UIConfig.h"

#include <array>

juce::PopupMenu::Options FltPanel::FilterComboLookAndFeel::getOptionsForComboBoxPopupMenu(juce::ComboBox& box,
                                                                                            juce::Label& label)
{
    auto options = juce::LookAndFeel_V4::getOptionsForComboBoxPopupMenu(box, label);
    return options.withParentComponent(box.getParentComponent())
                  .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::upwards);
}

FltPanel::FltPanel(std::array<juce::ToggleButton*, kFilterInstanceCount> enabledButtonsIn,
                                     std::array<juce::Label*, kFilterInstanceCount> enabledLabelsIn,
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
            enabledLabels(enabledLabelsIn),
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
                addAndMakeVisible(*enabledLabels[static_cast<std::size_t>(filterIndex)]);
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

        const auto idx = static_cast<std::size_t>(filterIndex);
        using px3::ui::ControlShape;

        // Row 1: bypass and filter type.
        {
            auto flex = filterComponent->rowFlex(0);
            const auto gapMargin = filterComponent->rowGap(0);
            const auto row = toPanel(filterComponent->rowBounds(0));
            const auto cellHeight = static_cast<float>(juce::jmax(1, row.getHeight()));

            flex.items.add(juce::FlexItem(44.0f, cellHeight).withMargin(gapMargin));
            flex.items.add(juce::FlexItem(116.0f, cellHeight).withMargin(gapMargin));
            flex.performLayout(row.toFloat());

            const auto cell = [&flex](int i) { return flex.items.getReference(i).currentBounds.toNearestInt(); };
            px3::ui::layoutLabelledControl(cell(0),
                                       { enabledLabels[idx], enabledButtons[idx], nullptr,
                                         ControlShape::square, 14, 0, 22 },
                                       filterComponent->rowControl(0));
            px3::ui::layoutLabelledControl(cell(1),
                                       { filterTypeLabels[idx], filterTypeBoxes[idx], nullptr,
                                         ControlShape::stretch, 14, 0, 24 },
                                       filterComponent->rowControl(0));
        }

        // Row 2: cutoff and resonance, each with its existing label above.
        {
            auto flex = filterComponent->rowFlex(1);
            const auto gapMargin = filterComponent->rowGap(1);
            const auto row = toPanel(filterComponent->rowBounds(1));
            const auto cellHeight = static_cast<float>(juce::jmax(1, row.getHeight()));

            for (int i = 0; i < 2; ++i)
            {
                auto item = juce::FlexItem(110.0f, cellHeight).withMargin(gapMargin);
                item.flexGrow = 1.0f;
                flex.items.add(item);
            }
            flex.performLayout(row.toFloat());

            const auto cell = [&flex](int i) { return flex.items.getReference(i).currentBounds.toNearestInt(); };
            px3::ui::layoutLabelledControl(cell(0),
                                       { cutoffLabels[idx], cutoffKnobs[idx], nullptr,
                                         ControlShape::square, 18, 0, 110 },
                                       filterComponent->rowControl(1));
            px3::ui::layoutLabelledControl(cell(1),
                                       { resonanceLabels[idx], resonanceKnobs[idx], nullptr,
                                         ControlShape::square, 18, 0, 110 },
                                       filterComponent->rowControl(1));
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

    for (auto& filterComponent : filterComponents)
    {
        if (filterComponent != nullptr)
        {
            filterComponent->setUIConfig(uiConfig);
        }
    }

    repaint();
}
