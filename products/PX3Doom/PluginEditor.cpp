#include "PluginEditor.h"

#include "DoomCardLayout.h"

PX3DoomAudioProcessorEditor::PX3DoomAudioProcessorEditor(PX3DoomAudioProcessor& processorIn)
    : px3::fx::FxCardEditor(processorIn, "doom", "DOOM")
{
    // ONE declaration, shared with the Synth's card. See DoomCardLayout.h.
    px3::ui::doomLayout::declareRows(rows(),
                                     processorIn.wetMode().choices,
                                     processorIn.loopMode().choices,
                                     processorIn.routing().choices);

    // The six primaries.
    attachKnob("wetTime", processorIn.wetTime());
    attachKnob("wetModify", processorIn.wetModify());
    attachKnob("loopLength", processorIn.loopLength());
    attachKnob("loopModify", processorIn.loopModify());
    attachKnob("clock", processorIn.clock());
    attachKnob("mix", processorIn.mix());

    // Their alternates, attached exactly the same way: each is a real
    // parameter with its own automation lane, and which of a pair the panel is
    // showing has no bearing on it.
    attachKnob("cross", processorIn.cross());
    attachKnob("eq", processorIn.eq());
    attachKnob("fade", processorIn.fade());
    attachKnob("blend", processorIn.blend());
    attachKnob("glue", processorIn.glue());
    attachKnob("balance", processorIn.balance());

    attachKnob("overdub", processorIn.overdub());
    attachKnob("spread", processorIn.spread());

    attachChoice("wetMode", processorIn.wetMode());
    attachChoice("loopMode", processorIn.loopMode());
    attachChoice("routing", processorIn.routing());

    attachToggle("crossSource", processorIn.crossSource());
    attachToggle("loopActive", processorIn.loopActive());
    attachToggle("wetActive", processorIn.wetActive());
    attachToggle("freeze", processorIn.freeze());
    attachToggle("loopHalf", processorIn.loopHalf());
    attachToggle("clockSmooth", processorIn.clockSmooth());

    attachBypass(processorIn.enabled());

    // ALT is deliberately NOT a parameter: it selects which function the six
    // paired knobs display, which is a property of the panel rather than of
    // the sound.
    px3::ui::doomLayout::wireAltSwitch(rows());

    finishSetup();
}
