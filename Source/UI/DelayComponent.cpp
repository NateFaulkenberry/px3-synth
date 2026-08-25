#include "DelayComponent.h"

#include "UIConfig.h"

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

void DelayComponent::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);
    repaint();
}

void DelayComponent::resized()
{
    const auto padX = uiConfig != nullptr ? uiConfig->getInt("fx.delay.layout.padX", 10) : 10;
    const auto padY = uiConfig != nullptr ? uiConfig->getInt("fx.delay.layout.padY", 8) : 8;
    auto area = getLocalBounds().reduced(padX, padY);

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
    const auto borderPad = uiConfig != nullptr ? uiConfig->getFloat("fx.delay.visual.borderPadding", 6.0f) : 6.0f;
    const auto radius = uiConfig != nullptr ? uiConfig->getFloat("fx.delay.visual.cornerRadius", 8.0f) : 8.0f;
    auto bounds = getLocalBounds().toFloat().reduced(borderPad);
    g.setColour(isActive ? accent.withAlpha(0.08f) : juce::Colour::fromRGBA(120, 120, 120, 30));
    g.fillRoundedRectangle(bounds, radius);

    const auto textColour = uiConfig != nullptr
                                ? uiConfig->getColour("fx.delay.visual.onLabel.textColour", juce::Colour::fromRGB(232, 232, 232))
                                : juce::Colour::fromRGB(232, 232, 232);
    const auto fontSize = uiConfig != nullptr ? uiConfig->getFloat("fx.delay.visual.onLabel.fontSize", 11.5f) : 11.5f;
    const auto textBounds = uiConfig != nullptr
                                ? uiConfig->getRect("fx.delay.visual.onLabel.bounds", getLocalBounds(), { 36, 11, 24, 14 })
                                : juce::Rectangle<int>(36, 11, 24, 14);
    const auto text = uiConfig != nullptr ? uiConfig->getString("fx.delay.visual.onLabel.text", "ON") : juce::String("ON");

    g.setColour(textColour.withAlpha(isActive ? 1.0f : 0.6f));
    g.setFont(juce::FontOptions(fontSize));
    g.drawText(text, textBounds, juce::Justification::centredLeft, false);
}
