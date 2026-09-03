#pragma once

#include "FxCardEditor.h"
#include "PluginProcessor.h"

class PX3ReverbAudioProcessorEditor final : public px3::fx::FxCardEditor
{
public:
    explicit PX3ReverbAudioProcessorEditor(PX3ReverbAudioProcessor& processorIn);
};
