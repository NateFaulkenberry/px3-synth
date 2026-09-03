#include "PluginEditor.h"

PX3ChorusAudioProcessorEditor::PX3ChorusAudioProcessorEditor(PX3ChorusAudioProcessor& processorIn)
    : px3::fx::FxCardEditor(processorIn, "chorus", "CHORUS")
{
    // The same rows, in the same order, as buildChorusCard in the Synth.
    rows().addChoiceRow({ { "mode", "MODE", "Dimension mode, ensemble, or CE-style",
                            processorIn.mode().choices } });

    rows().addKnobRow({ { "rate", "RATE", "Modulation rate" },
                        { "depth", "DEPTH", "Modulation excursion" },
                        { "width", "WIDTH", "Stereo expansion of the wet pair" },
                        { "spread", "SPREAD", "Phase offset between the two delay paths" } });

    rows().addKnobRow({ { "tone", "TONE", "Warm against clear, on the wet path only" },
                        { "lowCut", "LOW CUT", "Wet-path high-pass: what anchors the bass" },
                        { "feedback", "FEEDBACK", "Colour; capped short of flanging" },
                        { "character", "CHARACTER", "BBD emphasis, companding and bandwidth" },
                        { "mix", "MIX", "Final dry against wet" } });

    rows().addFeatureKnobRow({ "amount", "AMOUNT", "Overall intensity" });

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

    finishSetup(720, 320);
}
