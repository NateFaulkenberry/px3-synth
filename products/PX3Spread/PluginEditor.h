#pragma once

#include "FxCardEditor.h"
#include "PluginProcessor.h"

class PX3SpreadAudioProcessorEditor final : public px3::fx::FxCardEditor
{
public:
    explicit PX3SpreadAudioProcessorEditor(PX3SpreadAudioProcessor& processorIn);
};
