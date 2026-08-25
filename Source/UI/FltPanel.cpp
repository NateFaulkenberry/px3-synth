#include "FltPanel.h"

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
    const auto cardTitleFontSize = uiConfig != nullptr ? uiConfig->getFloat("flt.panel.card.title.fontSize", 11.0f) : 11.0f;

    const auto area = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(accent.withAlpha(fillAlpha));
    g.fillRoundedRectangle(area, panelRadius);

    g.setColour(accent.withAlpha(strokeAlpha));
    g.drawRoundedRectangle(area, panelRadius, 1.0f);

    const auto panelPadX = uiConfig != nullptr ? uiConfig->getInt("flt.panel.layout.padX", 12) : 12;
    const auto panelPadY = uiConfig != nullptr ? uiConfig->getInt("flt.panel.layout.padY", 10) : 10;
    auto contentArea = getLocalBounds().reduced(panelPadX, panelPadY);

    constexpr int gap = 8;
    const auto totalGap = gap * (kFilterInstanceCount - 1);
    const auto columnWidth = juce::jmax(1, (contentArea.getWidth() - totalGap) / kFilterInstanceCount);

    const auto drawCardTitle = [&g, cardTitleFontSize](const juce::String& text, juce::Rectangle<int> bounds, juce::Colour colour)
    {
        g.setColour(colour.brighter(0.2f));
        g.setFont(juce::FontOptions(cardTitleFontSize, juce::Font::bold));
        g.drawText(text, bounds.removeFromTop(14), juce::Justification::centredTop, true);
    };

    for (int filterIndex = 0; filterIndex < kFilterInstanceCount; ++filterIndex)
    {
        const auto x = contentArea.getX() + filterIndex * (columnWidth + gap);
        const auto cardBoundsInt = juce::Rectangle<int>(x, contentArea.getY(), columnWidth, contentArea.getHeight()).reduced(2, 0);
        drawCardTitle("Filter " + juce::String(filterIndex + 1), cardBoundsInt, accent);
    }
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

        auto contentArea = filterArea.reduced(8, 8);

        auto enabledRow = contentArea.removeFromTop(24);
        enabledLabels[static_cast<std::size_t>(filterIndex)]->setBounds(enabledRow.removeFromLeft(56));
        enabledButtons[static_cast<std::size_t>(filterIndex)]->setBounds(enabledRow.removeFromLeft(40).reduced(2, 2));

        contentArea.removeFromTop(6);

        auto row = contentArea.removeFromTop(24);
        filterTypeBoxes[static_cast<std::size_t>(filterIndex)]->setBounds(row.reduced(2, 1));
        contentArea.removeFromTop(8);

        auto responseArea = contentArea;
        auto knobBand = responseArea.removeFromTop(120);
        const auto knobSize = juce::jlimit(56, 110, juce::jmin((knobBand.getWidth() - 24) / 2, knobBand.getHeight() - 24));

        auto leftKnob = juce::Rectangle<int>(knobSize, knobSize)
                            .withCentre({ knobBand.getX() + knobBand.getWidth() / 4, knobBand.getCentreY() + 8 });
        auto rightKnob = juce::Rectangle<int>(knobSize, knobSize)
                             .withCentre({ knobBand.getX() + (knobBand.getWidth() * 3) / 4, knobBand.getCentreY() + 8 });

        cutoffLabels[static_cast<std::size_t>(filterIndex)]->setBounds(juce::Rectangle<int>(leftKnob.getX(), leftKnob.getY() - 20, leftKnob.getWidth(), 18));
        cutoffKnobs[static_cast<std::size_t>(filterIndex)]->setBounds(leftKnob);
        resonanceLabels[static_cast<std::size_t>(filterIndex)]->setBounds(juce::Rectangle<int>(rightKnob.getX(), rightKnob.getY() - 20, rightKnob.getWidth(), 18));
        resonanceKnobs[static_cast<std::size_t>(filterIndex)]->setBounds(rightKnob);

        auto& filterComponent = filterComponents[static_cast<std::size_t>(filterIndex)];
        if (filterComponent != nullptr)
        {
            filterComponent->setBounds(filterArea);
            filterComponent->setGraphBounds(responseArea.withTrimmedLeft(8).withTrimmedRight(8).reduced(0, 4)
                                                .translated(-filterArea.getX(), -filterArea.getY()));
        }
    }
}

void FltPanel::refreshFromParameters()
{
    for (int filterIndex = 0; filterIndex < kFilterInstanceCount; ++filterIndex)
    {
        const auto idx = static_cast<std::size_t>(filterIndex);
        const auto isEnabled = enabledButtons[idx]->getToggleState();

        filterTypeBoxes[idx]->setEnabled(isEnabled);
        cutoffKnobs[idx]->setEnabled(isEnabled);
        cutoffLabels[idx]->setEnabled(isEnabled);
        resonanceKnobs[idx]->setEnabled(isEnabled);
        resonanceLabels[idx]->setEnabled(isEnabled);

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
