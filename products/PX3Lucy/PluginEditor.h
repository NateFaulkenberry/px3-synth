#pragma once

#include "FxCardEditor.h"
#include "PluginProcessor.h"

class PX3LucyAudioProcessorEditor final : public px3::fx::FxCardEditor
{
public:
    explicit PX3LucyAudioProcessorEditor(PX3LucyAudioProcessor& processorIn);
};
