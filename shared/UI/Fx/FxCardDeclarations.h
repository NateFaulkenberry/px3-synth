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
    card.setDescription("Thickens and widens with modulated short delays: dimension, ensemble and Juno-style modes.");
    card.addChoiceRow({ { "mode", "MODE",
                          "DIM: Dimension-style anti-phase pair. ENSEMBLE: three-phase string ensemble. "
                          "JUNO I / II / I+II: one BBD line per side on a triangle LFO.",
                          modeChoices } });

    card.addKnobRow({ { "rate", "RATE", "Modulation speed" },
                      { "depth", "DEPTH", "How far the delay lines are swept (pitch movement)" },
                      { "width", "WIDTH", "Stereo width of the wet signal" },
                      { "spread", "PHASE", "Phase offset between the left and right sweeps" } });

    card.addKnobRow({ { "tone", "TONE", "Warm to clear, on the wet path only" },
                      { "lowCut", "LOW CUT", "Keeps the bass out of the chorus so it stays anchored" },
                      { "feedback", "FEEDBACK", "Resonant colour; capped short of flanging" },
                      { "character", "VINTAGE",
                        "Bucket-brigade character: emphasis, companding noise and bandwidth. 0 is clean" },
                      { "mix", "DRY/WET", "Final balance of dry against chorus" } });

    card.addFeatureKnobRow({ "amount", "INTENSITY", "Scales depth, width and colour together" });
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
    card.addChoiceRow({ { "type", "CHARACTER", "Which kind of instability each voice gets", driftTypeChoices } });
    card.addKnobRow({ { "amount", "DRIFT", "How much each voice wanders: pitch, cutoff, saturation and noise" } });
}

} // namespace px3::ui::fxcards
