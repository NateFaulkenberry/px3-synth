#include "FltPanel.h"

juce::PopupMenu::Options FltPanel::FilterComboLookAndFeel::getOptionsForComboBoxPopupMenu(juce::ComboBox& box,
                                                                                            juce::Label& label)
{
    auto options = juce::LookAndFeel_V4::getOptionsForComboBoxPopupMenu(box, label);
    return options.withParentComponent(box.getParentComponent())
                  .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::upwards);
}

FltPanel::FltPanel(juce::Slider& cutoffKnobIn,
                   juce::Label& cutoffLabelIn,
                   juce::Slider& resonanceKnobIn,
                   juce::Label& resonanceLabelIn,
                   juce::ComboBox& filterTypeBoxIn,
                   juce::AudioParameterFloat& cutoffParam,
                   juce::AudioParameterFloat& resonanceParam,
                   juce::AudioParameterChoice& filterTypeParam,
                   juce::Colour panelAccent)
    : cutoffKnob(cutoffKnobIn),
      cutoffLabel(cutoffLabelIn),
      resonanceKnob(resonanceKnobIn),
      resonanceLabel(resonanceLabelIn),
      filterTypeBox(filterTypeBoxIn),
      accent(panelAccent)
{
    addAndMakeVisible(cutoffKnob);
    addAndMakeVisible(cutoffLabel);
    addAndMakeVisible(resonanceKnob);
    addAndMakeVisible(resonanceLabel);
    filterTypeBox.setLookAndFeel(&filterComboLookAndFeel);
    addAndMakeVisible(filterTypeBox);

    filterComponent = std::make_unique<FilterComponent>(cutoffParam,
                                                        resonanceParam,
                                                        filterTypeParam,
                                                        panelAccent);
    addAndMakeVisible(*filterComponent);
}

FltPanel::~FltPanel()
{
    filterTypeBox.setLookAndFeel(nullptr);
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

    auto cardArea = getLocalBounds().reduced(12, 10);
    cardArea.removeFromTop(26);
    constexpr int targetCardWidth = 300;
    const auto cardWidth = juce::jmin(targetCardWidth, cardArea.getWidth());
    cardArea = cardArea.withSizeKeepingCentre(cardWidth, cardArea.getHeight());

    const auto cardBounds = cardArea.toFloat().reduced(2.0f);
    g.setColour(accent.withAlpha(0.10f));
    g.fillRoundedRectangle(cardBounds, 8.0f);
    g.setColour(juce::Colour::fromRGBA(220, 232, 252, 88));
    g.drawRoundedRectangle(cardBounds, 8.0f, 1.2f);
}

void FltPanel::resized()
{
    auto panelArea = getLocalBounds().reduced(12, 10);
    panelArea.removeFromTop(26);

    auto filterArea = panelArea.reduced(4, 0);
    constexpr int targetCardWidth = 300;
    const auto cardWidth = juce::jmin(targetCardWidth, filterArea.getWidth());
    filterArea = filterArea.withSizeKeepingCentre(cardWidth, filterArea.getHeight());

    auto contentArea = filterArea.reduced(8, 8);

    auto row = contentArea.removeFromTop(24);
    filterTypeBox.setBounds(row.reduced(2, 1));
    contentArea.removeFromTop(8);

    auto responseArea = contentArea;
    auto knobBand = responseArea.removeFromTop(120);
    const auto knobSize = juce::jlimit(56, 110, juce::jmin((knobBand.getWidth() - 24) / 2, knobBand.getHeight() - 24));

    auto leftKnob = juce::Rectangle<int>(knobSize, knobSize)
                        .withCentre({ knobBand.getX() + knobBand.getWidth() / 4, knobBand.getCentreY() + 8 });
    auto rightKnob = juce::Rectangle<int>(knobSize, knobSize)
                         .withCentre({ knobBand.getX() + (knobBand.getWidth() * 3) / 4, knobBand.getCentreY() + 8 });

    cutoffLabel.setBounds(juce::Rectangle<int>(leftKnob.getX(), leftKnob.getY() - 20, leftKnob.getWidth(), 18));
    cutoffKnob.setBounds(leftKnob);
    resonanceLabel.setBounds(juce::Rectangle<int>(rightKnob.getX(), rightKnob.getY() - 20, rightKnob.getWidth(), 18));
    resonanceKnob.setBounds(rightKnob);

    if (filterComponent != nullptr)
    {
        filterComponent->setBounds(responseArea.withTrimmedLeft(8).withTrimmedRight(8).reduced(0, 4));
    }
}

void FltPanel::refreshFromParameters()
{
    if (filterComponent != nullptr)
    {
        filterComponent->refreshFromParameters();
    }
}
