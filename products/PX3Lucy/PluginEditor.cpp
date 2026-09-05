#include "PluginEditor.h"

PX3LucyAudioProcessorEditor::PX3LucyAudioProcessorEditor(PX3LucyAudioProcessor& processorIn)
    : px3::fx::FxCardEditor(processorIn, "lucy", "LUCY")
{
    // The same rows, in the same order and wording, as buildLucyCard.
    rows().addToggleRow({
        { "freeze", "FROZEN", "FREEZE", "Freeze the spectrum" },
        { "freezeSlushy", "SLUSHY", "SOLID", "How the frozen spectrum behaves" },
        { "gate", "GATE ON", "GATE OFF", "Silence anything below the cutoff" },
        { "verbPost", "V-POST", "V-PRE", "Reverb after or before the loss" },
        { "filterInvert", "REJECT", "PASS", "Invert the filter" },
        { "slow", "SLOW ON", "SLOW OFF", "Slow the update rate" } });

    rows().addChoiceRow({
        { "mode", "MODE", "Loss mode", processorIn.mode().choices },
        { "slope", "SLOPE", "Filter slope", processorIn.slope().choices },
        { "packets", "PACKETS", "Packet corruption mode", processorIn.packets().choices } });

    rows().addKnobRow({
        { "loss", "LOSS", "Depth of the loss and packet effects, and which frequencies they reach" },
        { "speed", "SPEED", "How fast the loss, packets and freeze update" },
        { "filter", "FILTER", "Filter width; fully down is no filtering" },
        { "filterFreq", "FREQ", "Filter centre frequency" },
        { "verb", "VERB", "Reverb mix" },
        { "verbDecay", "DECAY", "Reverb size and length" } });

    rows().addKnobRow({
        { "freezer", "FREEZER", "Live against frozen" },
        { "gateCutoff", "CUTOFF", "Gate threshold" },
        { "threshold", "LIMIT", "Limiter threshold; lower means more limiting" },
        { "autoGain", "AUTO GAIN", "Gain compensation for the loss modes" },
        { "weighting", "WEIGHT", "Dark, psychoacoustic, or bright frequency weighting" },
        { "gain", "GAIN", "Wet gain, plus or minus 36 dB" },
        { "spread", "SPREAD", "Packet alternation and reverb width" } });

    rows().addFeatureKnobRow({ "global", "GLOBAL", "Overall intensity" });

    for (const auto& pair : { std::pair<const char*, juce::AudioParameterFloat*>
                              { "global", &processorIn.global() },
                              { "loss", &processorIn.loss() },
                              { "speed", &processorIn.speed() },
                              { "filter", &processorIn.filter() },
                              { "filterFreq", &processorIn.filterFreq() },
                              { "verb", &processorIn.verb() },
                              { "verbDecay", &processorIn.verbDecay() },
                              { "freezer", &processorIn.freezer() },
                              { "gateCutoff", &processorIn.gateCutoff() },
                              { "threshold", &processorIn.threshold() },
                              { "autoGain", &processorIn.autoGain() },
                              { "weighting", &processorIn.weighting() },
                              { "gain", &processorIn.gain() },
                              { "spread", &processorIn.spread() } })
    {
        attachKnob(pair.first, *pair.second);
    }

    attachChoice("mode", processorIn.mode());
    attachChoice("slope", processorIn.slope());
    attachChoice("packets", processorIn.packets());

    attachToggle("freeze", processorIn.freeze());
    attachToggle("freezeSlushy", processorIn.freezeSlushy());
    attachToggle("gate", processorIn.gate());
    attachToggle("verbPost", processorIn.verbPost());
    attachToggle("filterInvert", processorIn.filterInvert());
    attachToggle("slow", processorIn.slow());

    attachBypass(processorIn.enabled());

    finishSetup();
}
