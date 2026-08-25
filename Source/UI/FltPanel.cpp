#include "FltPanel.h"

#include <array>

juce::PopupMenu::Options FltPanel::FilterComboLookAndFeel::getOptionsForComboBoxPopupMenu(juce::ComboBox& box,
                                                                                            juce::Label& label)
{
    auto options = juce::LookAndFeel_V4::getOptionsForComboBoxPopupMenu(box, label);
    return options.withParentComponent(box.getParentComponent())
                  .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::upwards);
}

FltPanel::FltPanel(std::array<juce::Slider*, kFilterInstanceCount> cutoffKnobsIn,
                   std::array<juce::Label*, kFilterInstanceCount> cutoffLabelsIn,
                   std::array<juce::Slider*, kFilterInstanceCount> resonanceKnobsIn,
                   std::array<juce::Label*, kFilterInstanceCount> resonanceLabelsIn,
                   std::array<juce::ComboBox*, kFilterInstanceCount> filterTypeBoxesIn,
                   std::array<juce::AudioParameterFloat*, kFilterInstanceCount> cutoffParams,
                   std::array<juce::AudioParameterFloat*, kFilterInstanceCount> resonanceParams,
                   std::array<juce::AudioParameterChoice*, kFilterInstanceCount> filterTypeParams,
                   juce::Colour panelAccent)
    : cutoffKnobs(cutoffKnobsIn),
      cutoffLabels(cutoffLabelsIn),
      resonanceKnobs(resonanceKnobsIn),
      resonanceLabels(resonanceLabelsIn),
      filterTypeBoxes(filterTypeBoxesIn),
      accent(panelAccent)
{
    for (int filterIndex = 0; filterIndex < kFilterInstanceCount; ++filterIndex)
    {
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
            "Filter " + juce::String(filterIndex + 1),
            panelAccent);
        addAndMakeVisible(*filterComponents[static_cast<std::size_t>(filterIndex)]);
    }
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

    auto contentArea = getLocalBounds().reduced(12, 10);
    contentArea.removeFromTop(26);

    constexpr int gap = 8;
    const auto totalGap = gap * (kFilterInstanceCount - 1);
    const auto columnWidth = juce::jmax(1, (contentArea.getWidth() - totalGap) / kFilterInstanceCount);

    for (int filterIndex = 0; filterIndex < kFilterInstanceCount; ++filterIndex)
    {
        const auto x = contentArea.getX() + filterIndex * (columnWidth + gap);
        const auto cardBounds = juce::Rectangle<int>(x, contentArea.getY(), columnWidth, contentArea.getHeight()).reduced(2, 0).toFloat();
        g.setColour(accent.withAlpha(0.10f));
        g.fillRoundedRectangle(cardBounds, 8.0f);
        g.setColour(juce::Colour::fromRGBA(220, 232, 252, 88));
        g.drawRoundedRectangle(cardBounds, 8.0f, 1.2f);
    }
}

void FltPanel::resized()
{
    auto panelArea = getLocalBounds().reduced(12, 10);
    panelArea.removeFromTop(26);

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
            filterComponent->setBounds(responseArea.withTrimmedLeft(8).withTrimmedRight(8).reduced(0, 4));
        }
    }
}

void FltPanel::refreshFromParameters()
{
    for (auto& filterComponent : filterComponents)
    {
        if (filterComponent != nullptr)
        {
            filterComponent->refreshFromParameters();
        }
    }
}
