#include "DelayComponent.h"

DelayComponent::DelayComponent(juce::ToggleButton& enabledButtonIn,
                                         juce::Slider& amountKnobIn,
                                         juce::Label& amountLabelIn,
                                         juce::ComboBox& algorithmBoxIn,
                                         juce::Label& algorithmLabelIn,
                                         juce::ComboBox& syncBoxIn,
                                         juce::Label& syncLabelIn,
                                         juce::ComboBox& modeBoxIn,
                                         juce::Label& modeLabelIn,
                                         juce::Slider& timeKnobIn,
                                         juce::Label& timeLabelIn,
                                         juce::Slider& feedbackKnobIn,
                                         juce::Label& feedbackLabelIn,
                                         juce::Colour accentIn)
    : enabledButton(enabledButtonIn),
      amountKnob(amountKnobIn),
      amountLabel(amountLabelIn),
      algorithmBox(algorithmBoxIn),
      algorithmLabel(algorithmLabelIn),
      syncBox(syncBoxIn),
      syncLabel(syncLabelIn),
      modeBox(modeBoxIn),
      modeLabel(modeLabelIn),
      timeKnob(timeKnobIn),
      timeLabel(timeLabelIn),
      feedbackKnob(feedbackKnobIn),
      feedbackLabel(feedbackLabelIn),
      accent(accentIn)
{
    addAndMakeVisible(enabledButton);
    addAndMakeVisible(amountKnob);
    addAndMakeVisible(amountLabel);
    addAndMakeVisible(algorithmBox);
    addAndMakeVisible(algorithmLabel);
    addAndMakeVisible(syncBox);
    addAndMakeVisible(syncLabel);
    addAndMakeVisible(modeBox);
    addAndMakeVisible(modeLabel);
    addAndMakeVisible(timeKnob);
    addAndMakeVisible(timeLabel);
    addAndMakeVisible(feedbackKnob);
    addAndMakeVisible(feedbackLabel);
}

void DelayComponent::setAccentColour(juce::Colour accentIn)
{
    accent = accentIn;
    repaint();
}

void DelayComponent::setActive(bool enabled, bool granularModeSelectable)
{
    isActive = enabled;

    amountKnob.setEnabled(isActive);
    amountLabel.setEnabled(isActive);
    algorithmBox.setEnabled(isActive);
    algorithmLabel.setEnabled(isActive);
    timeKnob.setEnabled(isActive);
    timeLabel.setEnabled(isActive);
    feedbackKnob.setEnabled(isActive);
    feedbackLabel.setEnabled(isActive);
    syncBox.setEnabled(isActive);
    syncLabel.setEnabled(isActive);
    modeBox.setEnabled(granularModeSelectable);
    modeLabel.setEnabled(granularModeSelectable);

    amountKnob.getProperties().set("psychedelicBypassGray", !isActive);
    timeKnob.getProperties().set("psychedelicBypassGray", !isActive);
    feedbackKnob.getProperties().set("psychedelicBypassGray", !isActive);

    repaint();
}

void DelayComponent::resized()
{
    auto area = getLocalBounds().reduced(10, 8);

    auto top = area.removeFromTop(24);
    enabledButton.setBounds(top.removeFromLeft(22));

    auto controls = area.removeFromBottom(120);
    auto rowMode = controls.removeFromBottom(22);
    controls.removeFromBottom(2);
    auto rowAlgo = controls.removeFromBottom(22);
    auto rowSync = controls.removeFromBottom(22);
    controls.removeFromBottom(2);
    auto miniArea = controls;

    auto leftMini = miniArea.removeFromLeft(miniArea.getWidth() / 2).reduced(2, 0);
    auto rightMini = miniArea.reduced(2, 0);

    auto leftLabelArea = leftMini.removeFromBottom(16);
    auto rightLabelArea = rightMini.removeFromBottom(16);

    const auto miniKnobSize = juce::jlimit(30,
                                           44,
                                           juce::jmin(leftMini.getWidth(), juce::jmin(leftMini.getHeight(), rightMini.getHeight())));
    const auto leftKnobBounds = juce::Rectangle<int>(miniKnobSize, miniKnobSize).withCentre(leftMini.getCentre());
    const auto rightKnobBounds = juce::Rectangle<int>(miniKnobSize, miniKnobSize).withCentre(rightMini.getCentre());
    timeKnob.setBounds(leftKnobBounds);
    feedbackKnob.setBounds(rightKnobBounds);

    constexpr int miniLabelHeight = 16;
    const auto leftLabelWidth = leftMini.getWidth();
    const auto rightLabelWidth = rightMini.getWidth();
    timeLabel.setBounds(juce::Rectangle<int>(leftLabelWidth, miniLabelHeight)
                            .withCentre({ leftKnobBounds.getCentreX(), leftLabelArea.getCentreY() }));
    feedbackLabel.setBounds(juce::Rectangle<int>(rightLabelWidth, miniLabelHeight)
                                .withCentre({ rightKnobBounds.getCentreX(), rightLabelArea.getCentreY() }));

    auto algoLabelArea = rowAlgo.removeFromLeft(56);
    algorithmLabel.setBounds(algoLabelArea);
    algorithmBox.setBounds(rowAlgo.reduced(2, 1));

    auto syncLabelArea = rowSync.removeFromLeft(56);
    syncLabel.setBounds(syncLabelArea);
    syncBox.setBounds(rowSync.reduced(2, 1));

    auto modeLabelArea = rowMode.removeFromLeft(56);
    modeLabel.setBounds(modeLabelArea);
    modeBox.setBounds(rowMode.reduced(2, 1));

    const auto knobSize = juce::jmin(80, juce::jmin(area.getWidth(), area.getHeight()));
    amountKnob.setBounds(juce::Rectangle<int>(knobSize, knobSize).withCentre(area.getCentre()));
    amountLabel.setBounds(juce::Rectangle<int>(area.getX(), area.getBottom() - 18, area.getWidth(), 16));
}

void DelayComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(6.0f);
    g.setColour(isActive ? accent.withAlpha(0.08f) : juce::Colour::fromRGBA(120, 120, 120, 30));
    g.fillRoundedRectangle(bounds, 8.0f);

    g.setColour(juce::Colour::fromRGB(232, 232, 232).withAlpha(isActive ? 1.0f : 0.6f));
    g.setFont(juce::FontOptions(11.5f));
    g.drawText("ON", 36, 11, 24, 14, juce::Justification::centredLeft, false);
}
