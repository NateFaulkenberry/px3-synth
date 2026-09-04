#include "PluginEditor.h"

PX3DoomAudioProcessorEditor::PX3DoomAudioProcessorEditor(PX3DoomAudioProcessor& processorIn)
    : px3::fx::FxCardEditor(processorIn, "doom", "DOOM")
{
    // The same rows, in the same order and with the same wording, as
    // buildDoomCard in the Synth.
    rows().addToggleRow({
        { "loopActive", "LOOPER", "LISTEN", "Play the captured micro-loop, or keep listening" },
        { "wetActive", "WET ON", "WET OFF", "Engage the wet channel" },
        { "freeze", "FROZEN", "FREEZE", "Freeze the wet channel and repeat it" },
        { "loopHalf", "HALF", "FULL", "Halve the micro-loop length" },
        { "clockSmooth", "SMOOTH", "STEPPED", "Bypass the harmonised clock quantiser" },
        // A toggle, not a dropdown - the Synth draws this two-value choice as a
        // chip, and this card is meant to be that card. Same wording too.
        { "crossSource", "CROSS: CHAN", "CROSS: INPUT",
          "Modulate from your playing, or let each channel modulate the other" } });

    rows().addChoiceRow({
        { "loopMode", "LOOP", "Micro-looper mode", processorIn.loopMode().choices },
        { "routing", "ROUTE", "What the wet channel processes", processorIn.routing().choices },
        { "wetMode", "WET", "Wet channel mode", processorIn.wetMode().choices } });

    rows().addKnobRow({
        { "clock", "CLOCK", "Engine sample rate: loop length, pitch and wet time at once" },
        { "loopLength", "LENGTH", "Micro-looper length (mode dependent)" },
        { "loopModify", "MODIFY", "Micro-looper character (mode dependent)" },
        { "wetTime", "TIME", "Wet channel time (mode dependent)" },
        { "wetModify", "SHAPE", "Wet channel character (mode dependent)" },
        { "balance", "BALANCE", "Micro-looper against wet channel" } });

    rows().addKnobRow({
        { "cross", "CROSS", "Signal-dependent interference in pitch and loudness" },
        { "glue", "GLUE", "End of chain saturator, then destroyer" },
        { "eq", "EQ", "Tilt: left removes highs, right removes lows" },
        { "overdub", "OVERDUB", "Record onto the micro-loop" },
        { "fade", "FADE", "How much of the loop survives each lap while overdubbing" },
        { "blend", "BLEND", "Clean micro-loop blended past the wet channel" },
        { "spread", "SPREAD", "Stereo processing depth" } });

    rows().addFeatureKnobRow({ "mix", "MIX", "Dry against the whole processed signal" });

    attachKnob("mix", processorIn.mix());
    attachKnob("clock", processorIn.clock());
    attachKnob("loopLength", processorIn.loopLength());
    attachKnob("loopModify", processorIn.loopModify());
    attachKnob("wetTime", processorIn.wetTime());
    attachKnob("wetModify", processorIn.wetModify());
    attachKnob("balance", processorIn.balance());
    attachKnob("cross", processorIn.cross());
    attachKnob("glue", processorIn.glue());
    attachKnob("eq", processorIn.eq());
    attachKnob("overdub", processorIn.overdub());
    attachKnob("fade", processorIn.fade());
    attachKnob("blend", processorIn.blend());
    attachKnob("spread", processorIn.spread());

    attachChoice("loopMode", processorIn.loopMode());
    attachChoice("routing", processorIn.routing());
    attachChoice("wetMode", processorIn.wetMode());


    attachToggle("crossSource", processorIn.crossSource());
    attachToggle("loopActive", processorIn.loopActive());
    attachToggle("wetActive", processorIn.wetActive());
    attachToggle("freeze", processorIn.freeze());
    attachToggle("loopHalf", processorIn.loopHalf());
    attachToggle("clockSmooth", processorIn.clockSmooth());

    attachBypass(processorIn.enabled());

    finishSetup();
}
