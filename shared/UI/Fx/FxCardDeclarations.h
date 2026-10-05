#pragma once

#include "FxCardComponent.h"

// The rows of the Chorus, Reverb, Spread and Drive cards, declared ONCE and
// shared by the Synth's FX page and the standalone effect products - the same
// arrangement DoomCardLayout.h and LucyCardLayout.h use. A label or a tooltip
// changed here changes in both, so the parity tests keep holding.
//
// Labels say what the control does in the fewest words; tooltips say how it
// behaves. Parameter IDs are not involved here: the card ids below are the
// card's own names, attached to parameters by each product.
namespace px3::ui::fxcards
{

inline void wireAdvancedSwitch(FxCardComponent& card)
{
    if (auto* alt = card.toggle("alt"))
    {
        alt->onClick = [&card, alt] { card.setAltMode(alt->getToggleState()); };
    }
}

inline void declareChorusRows(FxCardComponent& card, const juce::StringArray& modeChoices)
{
    card.setDescription("Bucket-brigade chorus models: Dimension D, string ensemble, CE-1 and Juno-60. "
                        "At the default knob positions each mode runs at its hardware's own settings.");
    card.addChoiceRow({ { "mode", "MODE",
                          "DIM: Dimension D anti-phase pair with compander; 1+4 / 2+4 / 3+4 are both buttons on "
                          "the same pair. ENSEMBLE: string-machine ensemble, three lines on slow and fast "
                          "three-phase sweeps. CE-1: one line - chorus on the left, direct on the right. "
                          "JUNO-60 I / II: two lines swept in opposition (II faster). JUNO-60 I+II: fast, "
                          "narrow and in phase - near mono.",
                          modeChoices } });

    card.addKnobRow({ { "rate", "RATE", "LFO speed; the default is the hardware's rate" },
                      { "depth", "DEPTH", "Delay sweep; the default is the hardware's swing, full is twice it" },
                      { "width", "WIDTH",
                        "Stereo spread of the wet; the default is the hardware's routing (CE-1: 0 is its mono "
                        "output; ENSEMBLE: 0 is the original mono)" },
                      { "spread", "PHASE", "LFO phase between the lines; the default is the hardware's" } });

    card.addKnobRow({ { "tone", "TONE", "Warm to clear, on the wet path only" },
                      { "lowCut", "LOW CUT", "Extra high-pass on the wet only (no original had one); off at zero" },
                      { "feedback", "FEEDBACK", "Extra resonant colour (no original had it); capped short of flanging" },
                      { "character", "VINTAGE",
                        "Bucket-brigade drive: 0 is linear, the default the hardware's, full is hot" },
                      { "mix", "DRY/WET", "Final balance of dry against chorus" } });

    card.addFeatureKnobRow({ "amount", "INTENSITY",
                             "Fades from bypass to the hardware's own wet/dry balance and stereo routing" });
}

inline void declareReverbRows(FxCardComponent& card, const juce::StringArray& algorithmChoices)
{
    card.setDescription("Space: room, plate, hall, or CLOUD (an endless granular wash with optional shimmer).");
    card.addChoiceRow({ { "algorithm", "MODE", "ROOM, PLATE, HALL, or CLOUD (REGEN, SMEAR and SHIMMER apply to CLOUD)",
                          algorithmChoices } });

    card.addKnobRow({ { "size", "SIZE", "Size of the space" },
                      { "decay", "DECAY", "How long the tail lasts" },
                      { "damping", "DAMPING", "How fast the top end of the tail dies away" },
                      { "preDelay", "PRE-DELAY", "Gap before the tail begins" } });

    card.addKnobRow({ { "modDepth", "MOD DEPTH", "How much the tail's delay lines are modulated (chorused movement)" },
                      { "modRate", "MOD RATE", "Speed of that modulation" },
                      { "width", "WIDTH", "Stereo width of the tail" } });

    card.addKnobRow({ { "cloudFeedback", "REGEN", "CLOUD: how much of the tail is fed back in (longer, denser)" },
                      { "cloudDiffusion", "SMEAR", "CLOUD: how much the grains are blurred together" },
                      { "shimmer", "SHIMMER", "CLOUD: an octave-up copy fed back into the tail" } });

    card.addFeatureKnobRow({ "amount", "MIX", "Dry against reverb" });
}

// Spread sits on the MASTER bus, after everything: it is not part of the
// reorderable send chain. The basic view is the essentials; ADVANCED unfolds
// the rest.
inline void declareSpreadRows(FxCardComponent& card, const juce::StringArray& modeChoices)
{
    card.setDescription("Master stereo width, after everything else. Keeps the low end mono and the mix mono-safe.");
    card.addToggleRow({ { "alt", "BASIC", "ADVANCED", "Show or hide the advanced width controls" } });

    card.addKnobRow({ { "width", "WIDTH", "How wide the stereo image becomes" },
                      { "lowFreq", "LOW MONO", "Everything below this frequency stays mono" },
                      { "mix", "MIX", "Dry against widened" } });

    card.addChoiceRow({ { "mode", "MODE", "Widening strategy", modeChoices } });
    card.markLastRowAdvanced();
    card.addKnobRow({ { "depth", "DECORR", "Decorrelation depth: how different left and right become" },
                      { "center", "CENTER", "How firmly the middle of the image is held in place" },
                      { "tone", "SIDE TONE", "Tilt EQ on the side (stereo) signal only" } });
    card.markLastRowAdvanced();
    card.addKnobRow({ { "lowWidth", "LOW WIDTH", "Width allowed below the LOW MONO crossover" },
                      { "highWidth", "HIGH WIDTH", "Width of the top band" },
                      { "highFreq", "HIGH XO", "Where the top band starts (widened by level, not phase)" } });
    card.markLastRowAdvanced();

    card.addFeatureKnobRow({ "amount", "AMOUNT", "Master amount of stereo processing (0 = off)" });
}

inline void declareDriveRows(FxCardComponent& card, const juce::StringArray& typeChoices)
{
    card.setDescription("Overdrive and distortion before the modulation and time effects. Level-matched, so DRIVE changes tone, not volume.");
    card.addChoiceRow({ { "type", "CLIP",
                          "SOFT: smooth diode-style overdrive. HARD: op-amp style clipping. "
                          "ASYM: asymmetric, adds even harmonics",
                          typeChoices } });

    card.addKnobRow({ { "drive", "DRIVE", "How hard the signal is pushed into the clipper (0 to 40 dB)" },
                      { "tight", "TIGHT", "Cuts the lows before clipping for a tighter, mid-forward drive" },
                      { "tone", "TONE", "Dark to bright, after the clipper" } });

    card.addKnobRow({ { "level", "LEVEL", "Output trim, plus or minus 12 dB (centre is unity)" },
                      { "mix", "MIX", "Dry against driven: blend for parallel distortion" } });
}

inline void declareVibeRows(FxCardComponent& card,
                            const juce::StringArray& vibeModeChoices,
                            const juce::StringArray& driftTypeChoices)
{
    card.setDescription("UNI-VIBE: a photocell phaser/vibrato in the FX chain. ANALOG DRIFT: per-voice pitch and "
                        "filter wander, saturation and noise.");
    card.addChoiceRow({ { "mode", "MODE", "CHORUS: the throb mixed with the dry signal. VIBRATO: wet only, pitch wobble",
                          vibeModeChoices } });
    card.addKnobRow({ { "speed", "SPEED", "Rate of the lamp sweep, 0.5 to 10 Hz" },
                      { "intensity", "INTENSITY", "Depth of the Uni-Vibe effect (0 = off)" } });

    card.addHeadingRow("ANALOG DRIFT", "Per-voice analog imperfection");
    card.addChoiceRow({ { "type", "STYLE", "Which kind of instability each voice gets", driftTypeChoices } });
    card.addKnobRow({ { "amount", "DRIFT", "How much each voice wanders: pitch, cutoff, saturation and noise" } });
}

} // namespace px3::ui::fxcards
