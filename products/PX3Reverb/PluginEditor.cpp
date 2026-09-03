#include "PluginEditor.h"

PX3ReverbAudioProcessorEditor::PX3ReverbAudioProcessorEditor(PX3ReverbAudioProcessor& processorIn)
    : px3::fx::FxCardEditor(processorIn, "reverb", "REVERB")
{
    // Unlike the other cards these rows have no counterpart in the Synth: the
    // Synth shows Reverb as a compact face with a mode and an amount, and the
    // nine controls below have never had a UI there at all. They are laid out
    // the way every other PX3 card is - a choice row, then knob rows, then the
    // feature knob - rather than in a new visual language.
    rows().addChoiceRow({ { "algorithm", "MODE", "Room, plate, hall or cloud",
                            processorIn.algorithm().choices } });

    rows().addKnobRow({ { "size", "SIZE", "Room size" },
                        { "decay", "DECAY", "How long the tail lasts" },
                        { "damping", "DAMPING", "How fast the top of the tail is lost" },
                        { "preDelay", "PRE", "Gap before the tail begins" } });

    rows().addKnobRow({ { "modDepth", "MOD DEPTH", "Movement in the tail" },
                        { "modRate", "MOD RATE", "How fast that movement is" },
                        { "width", "WIDTH", "Stereo spread of the tail" },
                        { "cloudFeedback", "CLOUD FB", "Cloud regeneration" },
                        { "cloudDiffusion", "CLOUD DIFF", "Cloud smearing" } });

    rows().addFeatureKnobRow({ "amount", "AMOUNT", "Dry against wet" });

    attachKnob("amount", processorIn.amount());
    attachKnob("size", processorIn.size());
    attachKnob("decay", processorIn.decay());
    attachKnob("damping", processorIn.damping());
    attachKnob("preDelay", processorIn.preDelay());
    attachKnob("modDepth", processorIn.modDepth());
    attachKnob("modRate", processorIn.modRate());
    attachKnob("width", processorIn.width());
    attachKnob("cloudFeedback", processorIn.cloudFeedback());
    attachKnob("cloudDiffusion", processorIn.cloudDiffusion());
    attachChoice("algorithm", processorIn.algorithm());
    attachBypass(processorIn.enabled());

    finishSetup(720, 320);
}
