#include "PluginEditor.h"
#include "FxCardDeclarations.h"

PX3VibeAudioProcessorEditor::PX3VibeAudioProcessorEditor(PX3VibeAudioProcessor& processorIn)
    : px3::fx::FxCardEditor(processorIn, "vibe", "VIBE")
{
    // The same rows the Synth builds: one declaration, FxCardDeclarations.h.
    px3::ui::fxcards::declareVibeRows(rows(), processorIn.mode().choices, processorIn.stereo().choices);

    attachKnob("speed", processorIn.speed());
    attachKnob("intensity", processorIn.intensity());
    attachKnob("level", processorIn.level());
    attachChoice("mode", processorIn.mode());
    attachChoice("stereo", processorIn.stereo());
    attachBypass(processorIn.enabled());

    finishSetup();
}
