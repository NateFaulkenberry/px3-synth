#pragma once

#include "FxCardEditor.h"
#include "PluginProcessor.h"

class PX3DoomAudioProcessorEditor final : public px3::fx::FxCardEditor
{
public:
    explicit PX3DoomAudioProcessorEditor(PX3DoomAudioProcessor& processorIn);
};
