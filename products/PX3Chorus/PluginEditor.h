#pragma once

#include "FxCardEditor.h"
#include "PluginProcessor.h"

// PX3 Chorus's window: the same card the Synth's FX chain shows, declared with
// the same rows and styled from the same UIConfig key.
class PX3ChorusAudioProcessorEditor final : public px3::fx::FxCardEditor
{
public:
    explicit PX3ChorusAudioProcessorEditor(PX3ChorusAudioProcessor& processorIn);
};
