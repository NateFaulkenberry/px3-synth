#include "SourceEnginePanel.h"

SourceEnginePanel::SourceEnginePanel(SynthProjectAudioProcessor& processorIn)
    : processor(processorIn),
      imagePanel(processorIn),
      audioPanel(processorIn)
{
    auto& sourceParam = processor.getSourceEngineParam();

    sourceLabel.setText("SOURCE", juce::dontSendNotification);
    sourceLabel.setJustificationType(juce::Justification::centredLeft);
    sourceLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(226, 236, 252));
    sourceLabel.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    sourceLabel.setInterceptsMouseClicks(false, false);

    imageButton.setButtonText("IMAGE");
    audioButton.setButtonText("AUDIO");

    const auto applyToggleTheme = [](juce::ToggleButton& button)
    {
        button.setClickingTogglesState(true);
        button.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(235, 235, 235));
        button.setColour(juce::ToggleButton::tickColourId, juce::Colour::fromRGB(110, 195, 255));
    };

    applyToggleTheme(imageButton);
    applyToggleTheme(audioButton);

    imageButton.onClick = [this, &sourceParam]()
    {
        if (!imageButton.getToggleState())
        {
            imageButton.setToggleState(true, juce::dontSendNotification);
            return;
        }

        audioButton.setToggleState(false, juce::dontSendNotification);
        sourceParam.setValueNotifyingHost(sourceParam.convertTo0to1(0.0f));
        refreshVisibilityFromParam();
    };

    audioButton.onClick = [this, &sourceParam]()
    {
        if (!audioButton.getToggleState())
        {
            audioButton.setToggleState(true, juce::dontSendNotification);
            return;
        }

        imageButton.setToggleState(false, juce::dontSendNotification);
        sourceParam.setValueNotifyingHost(sourceParam.convertTo0to1(1.0f));
        refreshVisibilityFromParam();
    };

    addAndMakeVisible(sourceLabel);
    addAndMakeVisible(imageButton);
    addAndMakeVisible(audioButton);
    addAndMakeVisible(imagePanel);
    addAndMakeVisible(audioPanel);

    refreshVisibilityFromParam();
    startTimerHz(20);
}

void SourceEnginePanel::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 22));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 10.0f, 1.0f);
}

void SourceEnginePanel::resized()
{
    auto area = getLocalBounds().reduced(4);

    auto top = area.removeFromTop(20);
    sourceLabel.setBounds(top.removeFromLeft(58));

    const auto buttonWidth = 74;
    imageButton.setBounds(top.removeFromLeft(buttonWidth));
    top.removeFromLeft(6);
    audioButton.setBounds(top.removeFromLeft(buttonWidth));

    area.removeFromTop(2);
    imagePanel.setBounds(area);
    audioPanel.setBounds(area);
}

void SourceEnginePanel::timerCallback()
{
    refreshVisibilityFromParam();
}

void SourceEnginePanel::refreshVisibilityFromParam()
{
    const auto sourceIndex = processor.getSourceEngineParam().getIndex();
    const auto imageMode = sourceIndex == 0;

    imageButton.setToggleState(imageMode, juce::dontSendNotification);
    audioButton.setToggleState(!imageMode, juce::dontSendNotification);

    imagePanel.setVisible(imageMode);
    audioPanel.setVisible(!imageMode);
}
