#include "MoodComponent.h"

#include "UIConfig.h"

MoodComponent::MoodComponent(juce::ToggleButton& enabledButtonIn,
                             juce::ToggleButton& freezeButtonIn,
                             juce::Slider& mixKnobIn,
                             juce::Label& mixLabelIn,
                             juce::Slider& clockKnobIn,
                             juce::Label& clockLabelIn,
                             juce::Slider& wetTimeKnobIn,
                             juce::Label& wetTimeLabelIn,
                             juce::Slider& wetModifyKnobIn,
                             juce::Label& wetModifyLabelIn,
                             juce::Slider& loopLengthKnobIn,
                             juce::Label& loopLengthLabelIn,
                             juce::Slider& loopModifyKnobIn,
                             juce::Label& loopModifyLabelIn,
                             juce::Slider& feedbackKnobIn,
                             juce::Label& feedbackLabelIn,
                             juce::Slider& spreadKnobIn,
                             juce::Label& spreadLabelIn,
                             juce::Slider& degradeKnobIn,
                             juce::Label& degradeLabelIn,
                             juce::ComboBox& routingBoxIn,
                             juce::Label& routingLabelIn,
                             juce::ComboBox& wetModeBoxIn,
                             juce::Label& wetModeLabelIn,
                             juce::ComboBox& loopModeBoxIn,
                             juce::Label& loopModeLabelIn,
                             juce::Colour accentIn)
    : enabledButton(enabledButtonIn),
      freezeButton(freezeButtonIn),
      mixKnob(mixKnobIn),
      mixLabel(mixLabelIn),
      clockKnob(clockKnobIn),
      clockLabel(clockLabelIn),
      wetTimeKnob(wetTimeKnobIn),
      wetTimeLabel(wetTimeLabelIn),
      wetModifyKnob(wetModifyKnobIn),
      wetModifyLabel(wetModifyLabelIn),
      loopLengthKnob(loopLengthKnobIn),
      loopLengthLabel(loopLengthLabelIn),
      loopModifyKnob(loopModifyKnobIn),
      loopModifyLabel(loopModifyLabelIn),
      feedbackKnob(feedbackKnobIn),
      feedbackLabel(feedbackLabelIn),
      spreadKnob(spreadKnobIn),
      spreadLabel(spreadLabelIn),
      degradeKnob(degradeKnobIn),
      degradeLabel(degradeLabelIn),
      routingBox(routingBoxIn),
      routingLabel(routingLabelIn),
      wetModeBox(wetModeBoxIn),
      wetModeLabel(wetModeLabelIn),
      loopModeBox(loopModeBoxIn),
      loopModeLabel(loopModeLabelIn),
      accent(accentIn)
{
    addAndMakeVisible(enabledButton);
    addAndMakeVisible(freezeButton);

    addAndMakeVisible(mixKnob);
    addAndMakeVisible(mixLabel);
    addAndMakeVisible(clockKnob);
    addAndMakeVisible(clockLabel);
    addAndMakeVisible(wetTimeKnob);
    addAndMakeVisible(wetTimeLabel);
    addAndMakeVisible(wetModifyKnob);
    addAndMakeVisible(wetModifyLabel);
    addAndMakeVisible(loopLengthKnob);
    addAndMakeVisible(loopLengthLabel);
    addAndMakeVisible(loopModifyKnob);
    addAndMakeVisible(loopModifyLabel);
    addAndMakeVisible(feedbackKnob);
    addAndMakeVisible(feedbackLabel);
    addAndMakeVisible(spreadKnob);
    addAndMakeVisible(spreadLabel);
    addAndMakeVisible(degradeKnob);
    addAndMakeVisible(degradeLabel);

    addAndMakeVisible(routingBox);
    addAndMakeVisible(routingLabel);
    addAndMakeVisible(wetModeBox);
    addAndMakeVisible(wetModeLabel);
    addAndMakeVisible(loopModeBox);
    addAndMakeVisible(loopModeLabel);
}

void MoodComponent::setAccentColour(juce::Colour accentIn)
{
    accent = accentIn;
    repaint();
}

void MoodComponent::setActive(bool enabled)
{
    isActive = enabled;

    mixKnob.setEnabled(enabled);
    clockKnob.setEnabled(enabled);
    wetTimeKnob.setEnabled(enabled);
    wetModifyKnob.setEnabled(enabled);
    loopLengthKnob.setEnabled(enabled);
    loopModifyKnob.setEnabled(enabled);
    feedbackKnob.setEnabled(enabled);
    spreadKnob.setEnabled(enabled);
    degradeKnob.setEnabled(enabled);

    routingBox.setEnabled(enabled);
    wetModeBox.setEnabled(enabled);
    loopModeBox.setEnabled(enabled);

    mixLabel.setEnabled(enabled);
    clockLabel.setEnabled(enabled);
    wetTimeLabel.setEnabled(enabled);
    wetModifyLabel.setEnabled(enabled);
    loopLengthLabel.setEnabled(enabled);
    loopModifyLabel.setEnabled(enabled);
    feedbackLabel.setEnabled(enabled);
    spreadLabel.setEnabled(enabled);
    degradeLabel.setEnabled(enabled);
    routingLabel.setEnabled(enabled);
    wetModeLabel.setEnabled(enabled);
    loopModeLabel.setEnabled(enabled);

    mixKnob.getProperties().set("psychedelicBypassGray", !enabled);
    clockKnob.getProperties().set("psychedelicBypassGray", !enabled);
    wetTimeKnob.getProperties().set("psychedelicBypassGray", !enabled);
    wetModifyKnob.getProperties().set("psychedelicBypassGray", !enabled);
    loopLengthKnob.getProperties().set("psychedelicBypassGray", !enabled);
    loopModifyKnob.getProperties().set("psychedelicBypassGray", !enabled);
    feedbackKnob.getProperties().set("psychedelicBypassGray", !enabled);
    spreadKnob.getProperties().set("psychedelicBypassGray", !enabled);
    degradeKnob.getProperties().set("psychedelicBypassGray", !enabled);

    repaint();
}

void MoodComponent::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);
    repaint();
}

void MoodComponent::resized()
{
    const auto padX = uiConfig != nullptr ? uiConfig->getInt("fx.mood.layout.padX", 10) : 10;
    const auto padY = uiConfig != nullptr ? uiConfig->getInt("fx.mood.layout.padY", 8) : 8;
    auto area = getLocalBounds().reduced(padX, padY);

    auto header = area.removeFromTop(24);
    enabledButton.setBounds(header.removeFromLeft(22));
    freezeButton.setBounds(header.removeFromLeft(48));

    area.removeFromTop(2);

    auto modeRow = area.removeFromTop(24);
    auto modeLeft = modeRow.removeFromLeft(modeRow.getWidth() / 2);
    auto modeRight = modeRow;

    wetModeLabel.setBounds(modeLeft.removeFromLeft(42));
    wetModeBox.setBounds(modeLeft.reduced(2, 1));

    loopModeLabel.setBounds(modeRight.removeFromLeft(42));
    loopModeBox.setBounds(modeRight.reduced(2, 1));

    area.removeFromTop(4);

    auto routingRow = area.removeFromTop(24);
    routingLabel.setBounds(routingRow.removeFromLeft(58));
    routingBox.setBounds(routingRow.reduced(2, 1));

    area.removeFromTop(6);

    auto knobsTop = area.removeFromTop(74);
    auto knobsBottom = area.removeFromTop(74);

    auto placeKnob = [](juce::Rectangle<int>& row, juce::Slider& knob, juce::Label& label)
    {
        auto cell = row.removeFromLeft(row.getWidth() / juce::jmax(1, 3));
        auto kArea = cell.removeFromTop(56);
        const auto size = juce::jlimit(30, 44, juce::jmin(kArea.getWidth(), kArea.getHeight()));
        knob.setBounds(juce::Rectangle<int>(size, size).withCentre(kArea.getCentre()));
        label.setBounds(cell.withTrimmedTop(2).withHeight(16));
    };

    auto topA = knobsTop;
    placeKnob(topA, wetTimeKnob, wetTimeLabel);
    placeKnob(topA, wetModifyKnob, wetModifyLabel);
    placeKnob(topA, loopLengthKnob, loopLengthLabel);

    auto botA = knobsBottom;
    placeKnob(botA, loopModifyKnob, loopModifyLabel);
    placeKnob(botA, feedbackKnob, feedbackLabel);
    placeKnob(botA, spreadKnob, spreadLabel);

    auto sharedRow = area.removeFromTop(74);
    auto left = sharedRow.removeFromLeft(sharedRow.getWidth() / 2);
    auto right = sharedRow;

    const auto placeShared = [](juce::Rectangle<int> cell, juce::Slider& knob, juce::Label& label)
    {
        auto kArea = cell.removeFromTop(56);
        const auto size = juce::jlimit(30, 44, juce::jmin(kArea.getWidth(), kArea.getHeight()));
        knob.setBounds(juce::Rectangle<int>(size, size).withCentre(kArea.getCentre()));
        label.setBounds(cell.withTrimmedTop(2).withHeight(16));
    };

    placeShared(left, clockKnob, clockLabel);
    placeShared(right, mixKnob, mixLabel);

    auto degradeRow = area.removeFromTop(58);
    placeShared(degradeRow.withSizeKeepingCentre(86, degradeRow.getHeight()), degradeKnob, degradeLabel);
}

void MoodComponent::paint(juce::Graphics& g)
{
    const auto borderPad = uiConfig != nullptr ? uiConfig->getFloat("fx.mood.visual.borderPadding", 6.0f) : 6.0f;
    const auto radius = uiConfig != nullptr ? uiConfig->getFloat("fx.mood.visual.cornerRadius", 8.0f) : 8.0f;
    const auto bgTintAlpha = uiConfig != nullptr ? uiConfig->getFloat("fx.mood.visual.bgTintAlpha", 0.08f) : 0.08f;
    const auto topFillAlpha = uiConfig != nullptr ? uiConfig->getFloat("fx.mood.visual.topFillAlpha", 0.10f) : 0.10f;

    auto bounds = getLocalBounds().toFloat().reduced(borderPad);
    const auto fillColour = isActive ? accent : juce::Colour::fromRGBA(120, 120, 120, 180);
    const auto bgTintColour = uiConfig != nullptr ? uiConfig->getColour("fx.mood.visual.bgTintColour", fillColour)
                                                  : fillColour;
    const auto topFillColour = uiConfig != nullptr ? uiConfig->getColour("fx.mood.visual.topFillColour", fillColour)
                                                   : fillColour;

    g.setColour(bgTintColour.withAlpha(bgTintAlpha));
    g.fillRoundedRectangle(bounds, radius);

    g.setColour(topFillColour.withAlpha(topFillAlpha));
    juce::Path topFill;
    const auto topHalf = bounds.withTrimmedBottom(bounds.getHeight() * 0.52f);
    topFill.addRoundedRectangle(topHalf.getX(),
                                topHalf.getY(),
                                topHalf.getWidth(),
                                topHalf.getHeight(),
                                radius,
                                radius,
                                true,
                                true,
                                false,
                                false);
    g.fillPath(topFill);

    const auto textColour = uiConfig != nullptr
                                ? uiConfig->getColour("fx.mood.visual.onLabel.textColour", juce::Colour::fromRGB(232, 232, 232))
                                : juce::Colour::fromRGB(232, 232, 232);
    const auto fontSize = uiConfig != nullptr ? uiConfig->getFloat("fx.mood.visual.onLabel.fontSize", 11.5f) : 11.5f;
    const auto textBounds = uiConfig != nullptr
                                ? uiConfig->getRect("fx.mood.visual.onLabel.bounds", getLocalBounds(), { 36, 11, 24, 14 })
                                : juce::Rectangle<int>(36, 11, 24, 14);
    const auto text = uiConfig != nullptr ? uiConfig->getString("fx.mood.visual.onLabel.text", "ON") : juce::String("ON");

    g.setColour(textColour.withAlpha(isActive ? 1.0f : 0.6f));
    g.setFont(juce::FontOptions(fontSize));
    g.drawText(text, textBounds, juce::Justification::centredLeft, false);
}
