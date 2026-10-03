#include "PluginEditor.h"
#include "FxCardDeclarations.h"

PX3ChorusAudioProcessorEditor::PX3ChorusAudioProcessorEditor(PX3ChorusAudioProcessor& processorIn)
    : px3::fx::FxCardEditor(processorIn, "chorus", "CHORUS")
{
    // The same rows, in the same order, as buildChorusCard in the Synth.
    // The same rows the Synth builds: one declaration, FxCardDeclarations.h.
    px3::ui::fxcards::declareChorusRows(rows(), processorIn.mode().choices);

    attachKnob("amount", processorIn.amount());
    attachKnob("rate", processorIn.rate());
    attachKnob("depth", processorIn.depth());
    attachKnob("width", processorIn.width());
    attachKnob("spread", processorIn.spread());
    attachKnob("tone", processorIn.tone());
    attachKnob("lowCut", processorIn.lowCut());
    attachKnob("feedback", processorIn.feedback());
    attachKnob("character", processorIn.character());
    attachKnob("mix", processorIn.mix());
    attachChoice("mode", processorIn.mode());
    attachBypass(processorIn.enabled());

    finishSetup();
}
