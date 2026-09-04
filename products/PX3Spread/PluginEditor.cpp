#include "PluginEditor.h"

PX3SpreadAudioProcessorEditor::PX3SpreadAudioProcessorEditor(PX3SpreadAudioProcessor& processorIn)
    // "stereoSpread", not "spread": the style key indexes cards.<key> in
    // UIConfig.json, and the Synth's card is built with stereoSpread. Spelled
    // the short way this found nothing and fell back to code defaults, which is
    // why the standalone was the one effect with a pale border instead of its
    // own green.
    : px3::fx::FxCardEditor(processorIn, "stereoSpread", "SPREAD")
{
    // The same rows, in the same order, as buildStereoSpreadCard in the Synth.
    rows().addChoiceRow({ { "mode", "MODE", "Widening strategy", processorIn.mode().choices } });

    rows().addKnobRow({ { "width", "WIDTH", "Overall stereo expansion" },
                        { "depth", "DEPTH", "Decorrelation depth" },
                        { "center", "CENTER", "How strongly the middle is anchored" },
                        { "tone", "TONE", "Tilt on the side signal only" } });

    rows().addKnobRow({ { "lowWidth", "LOW W", "Width permitted below the low crossover" },
                        { "highWidth", "HIGH W", "Width in the top band" },
                        { "lowFreq", "LOW XO", "Low crossover: below it, mono" },
                        { "highFreq", "HIGH XO", "High crossover: above it, level rather than phase" },
                        { "mix", "MIX", "Final dry against wet" } });

    rows().addFeatureKnobRow({ "amount", "AMOUNT", "Overall amount of spatial processing" });

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
