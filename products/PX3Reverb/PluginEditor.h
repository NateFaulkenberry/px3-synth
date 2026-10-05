#pragma once

#include "FxCardEditor.h"
#include "PluginProcessor.h"

class PX3ReverbAudioProcessorEditor final : public px3::fx::FxCardEditor,
                                            private juce::Timer
{
public:
    explicit PX3ReverbAudioProcessorEditor(PX3ReverbAudioProcessor& processorIn);
    ~PX3ReverbAudioProcessorEditor() override;

    // Tests: run one refresh now instead of waiting for the timer.
    void debugRefresh() { timerCallback(); }

private:
    void timerCallback() override;

    PX3ReverbAudioProcessor& reverbProcessor;
};
