#pragma once

#include "FxCardEditor.h"
#include "PluginProcessor.h"

class PX3LucyAudioProcessorEditor final : public px3::fx::FxCardEditor,
                                          private juce::Timer
{
public:
    explicit PX3LucyAudioProcessorEditor(PX3LucyAudioProcessor& processorIn);
    ~PX3LucyAudioProcessorEditor() override;

    // Tests: run one refresh now instead of waiting for the timer.
    void debugRefresh() { timerCallback(); }

private:
    // SPEED's dimming follows MODE, PACKETS and FREEZE however they change -
    // the card, automation, a preset - as PX3 Reverb's card follows its MODE.
    void timerCallback() override;

    PX3LucyAudioProcessor& lucyProcessor;
};
