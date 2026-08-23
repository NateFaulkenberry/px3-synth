#pragma once

#include <JuceHeader.h>

#include "AudioEnginePanel.h"
#include "ImageEnginePanel.h"
#include "PluginProcessor.h"

class SourceEnginePanel final : public juce::Component,
                                private juce::Timer
{
public:
    explicit SourceEnginePanel(SynthProjectAudioProcessor& processorIn);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshVisibilityFromParam();

    SynthProjectAudioProcessor& processor;
    ImageEnginePanel imagePanel;
    AudioEnginePanel audioPanel;

    juce::ToggleButton imageButton;
    juce::ToggleButton audioButton;
    juce::Label sourceLabel;
};
