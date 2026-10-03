#include "PluginEditor.h"
#include "FxCardDeclarations.h"

PX3SpreadAudioProcessorEditor::PX3SpreadAudioProcessorEditor(PX3SpreadAudioProcessor& processorIn)
    // "stereoSpread", not "spread": the style key indexes cards.<key> in
    // UIConfig.json, and the Synth's card is built with stereoSpread. Spelled
    // the short way this found nothing and fell back to code defaults, which is
    // why the standalone was the one effect with a pale border instead of its
    // own green.
    : px3::fx::FxCardEditor(processorIn, "stereoSpread", "SPREAD")
{
    // The same rows the Synth builds: one declaration, FxCardDeclarations.h.
    px3::ui::fxcards::declareSpreadRows(rows(), processorIn.mode().choices);
    px3::ui::fxcards::wireAdvancedSwitch(rows());

    attachKnob("amount", processorIn.amount());
    attachKnob("width", processorIn.width());
    attachKnob("depth", processorIn.depth());
    attachKnob("center", processorIn.center());
    attachKnob("tone", processorIn.tone());
    attachKnob("lowWidth", processorIn.lowWidth());
    attachKnob("highWidth", processorIn.highWidth());
    attachKnob("lowFreq", processorIn.lowFreq());
    attachKnob("highFreq", processorIn.highFreq());
    attachKnob("mix", processorIn.mix());
    attachChoice("mode", processorIn.mode());
    attachBypass(processorIn.enabled());

    finishSetup();
}
