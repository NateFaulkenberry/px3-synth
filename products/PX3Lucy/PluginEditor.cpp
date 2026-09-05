#include "PluginEditor.h"

#include "LucyCardLayout.h"

PX3LucyAudioProcessorEditor::PX3LucyAudioProcessorEditor(PX3LucyAudioProcessor& processorIn)
    : px3::fx::FxCardEditor(processorIn, "lucy", "LUCY")
{
    // The same rows, in the same order and wording, as buildLucyCard. The two
    // are compared control-by-control by
    // FxProducts_AStandaloneCardMatchesTheSynthsCardExactly, so a change here
    // that is not also made there is a test failure rather than a drift.
    px3::ui::lucyLayout::declareRows(rows(),
                                     processorIn.mode().choices,
                                     processorIn.slope().choices,
                                     processorIn.packets().choices,
                                     processorIn.weighting().choices,
                                     processorIn.freeze().choices);

    // ---- the six primary knobs -------------------------------------------
    attachKnob("filter", processorIn.filter());
    attachKnob("global", processorIn.global());
    attachKnob("verb", processorIn.verb());
    attachKnob("freq", processorIn.filterFreq());
    attachKnob("speed", processorIn.speed());
    attachKnob("loss", processorIn.loss());

    // ---- and their alternates, attached exactly the same way --------------
    //
    // Every one is a real parameter with its own automation lane. Which of a
    // pair the panel is showing is a display state and has no bearing on this.
    attachKnob("gate", processorIn.gateThreshold());
    attachKnob("freezer", processorIn.freezer());
    attachKnob("decay", processorIn.verbDecay());
    attachKnob("limiterThreshold", processorIn.limiterThreshold());
    attachKnob("autoGain", processorIn.autoGain());
    attachKnob("lossGain", processorIn.lossGain());

    attachKnob("spread", processorIn.spread());

    attachChoice("mode", processorIn.mode());
    attachChoice("slope", processorIn.slope());
    attachChoice("packets", processorIn.packets());
    attachChoice("weighting", processorIn.weighting());
    attachChoice("freeze", processorIn.freeze());

    attachToggle("gateOn", processorIn.gate());
    attachToggle("verbPost", processorIn.verbPost());
    attachToggle("filterInvert", processorIn.filterInvert());
    attachToggle("slow", processorIn.slow());

    attachBypass(processorIn.enabled());

    px3::ui::lucyLayout::wireAltSwitch(rows());

    finishSetup();
}
