#include "PluginEditor.h"

PX3SpreadAudioProcessorEditor::PX3SpreadAudioProcessorEditor(PX3SpreadAudioProcessor& processorIn)
    : px3::fx::FxCardEditor(processorIn, "spread", "SPREAD")
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

    finishSetup(720, 320);
}
