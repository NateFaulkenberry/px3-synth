#include "FactoryPresets.h"

namespace px3::presets
{
namespace
{
// Choice indices, named. A preset full of bare numbers is unreadable and a
// preset full of wrong bare numbers is undebuggable.
enum OscMode
{
    sine = 0, saw, square, triangle, noise, pinkNoise, superSaw, pwm, wavetable,
    additive, formant, fm, hardSync, karplus, organ, digital, physical, rob, isaac, px3
};

enum FilterType { lp12 = 0, lp24, hp12, hp24, bandPass, notch, allPass, comb };
enum DelayAlgo  { granular = 0, tape, analogBbd, pingPong, stereoDelay, modulated, diffusion };
enum ReverbAlgo { room = 0, plate, hall, cloud };
enum VibeType   { warm = 0, hot, cool, vintage, clean, loFi };

// DIM 1 is the softest and has the LONGEST delay; DIM 4 is the strongest.
enum ChorusMode { dim1 = 0, dim2, dim3, dim4, dim1plus4, dim2plus4, dim3plus4, ensemble, ceWarm };
enum SpreadMode { classic = 0, wide, deep, monoSafe };

enum DoomLoopMode { burst = 0, radio, mask };
enum DoomWetMode  { soup = 0, relay, flip };
enum DoomRouting  { inputOnly = 0, inputPlusLoop, loopOnly };

enum LucyMode    { standard = 0, inverse, jitter };
enum LucyPackets { cleanPackets = 0, packetLoss, packetRepeat };
enum LucySlope   { slope6 = 0, slope24, slope96 };

enum SubOctave   { oct0 = 0, octMinus1, octMinus2 };
enum SubWave     { subSine = 0, subSquare };

enum MoodWetMode  { moodReverb = 0, moodDelay, moodSlip };
enum MoodLoopMode { moodEnv = 0, moodTape, moodStretch };
} // namespace

std::vector<FactoryPreset> factoryPresets()
{
    return {

    // =======================================================================
    // BASS
    // =======================================================================

    { "Reese Undertow", "BASS", "P(X3)",
      "Two saws pulled apart until they beat against each other, anchored by a square sub. "
      "CHORUS widens the harmonics while its low cut keeps the fundamental where you left it.",
      { { "osc1Mode", saw }, { "osc1MacroA", 0.30f },
        { "osc2Enabled", 1 }, { "osc2Mode", saw }, { "osc2Fine", -13.0f }, { "osc2Coarse", 0.0f },
        { "subOscEnabled", 1 }, { "subOscOctave", octMinus1 }, { "subOscWaveform", subSquare },
        { "mix.sub.level", 0.72f }, { "mix.osc1.level", 0.58f }, { "mix.osc2.level", 0.58f },
        { "filter1Enabled", 1 }, { "filter1Type", lp24 }, { "filter1Cutoff", 420.0f }, { "filter1Resonance", 0.85f },
        { "ampAttack", 0.004f }, { "ampDecay", 0.40f }, { "ampSustain", 0.88f }, { "ampRelease", 0.22f },
        // Mode 4 is the strongest and the shortest-delay Dimension mode - the one
        // that moves harmonics without smearing a bass note.
        { "chorusAmount", 0.34f }, { "chorusMode", dim4 }, { "chorusLowCut", 0.72f }, { "chorusDepth", 0.42f },
        { "spreadAmount", 0.28f }, { "spreadMode", monoSafe },
        { "masterGain", 0.55f } } },

    { "Dial Tone", "BASS", "P(X3)",
      "A hard FM bass with LUCY set low and slow behind it. The loss is barely a texture at "
      "this depth - just enough to make it sound like it arrived over a wire.",
      { { "osc1Mode", fm }, { "osc1MacroA", 0.34f }, { "osc1MacroB", 0.58f }, { "osc1MacroC", 0.24f },
        { "subOscEnabled", 1 }, { "subOscOctave", octMinus1 }, { "subOscWaveform", subSine },
        { "mix.sub.level", 0.55f },
        { "filter1Enabled", 1 }, { "filter1Type", lp24 }, { "filter1Cutoff", 900.0f }, { "filter1Resonance", 0.60f },
        { "ampAttack", 0.002f }, { "ampDecay", 0.26f }, { "ampSustain", 0.55f }, { "ampRelease", 0.14f },
        { "lucyGlobal", 0.26f }, { "lucyMode", standard }, { "lucyLoss", 0.44f }, { "lucySpeed", 0.62f },
        { "lucyFilter", 0.22f }, { "lucyFilterFreq", 0.62f }, { "lucySlope", slope24 },
        { "lucyWeighting", -0.35f }, { "lucyGain", 3.0f },
        { "vibeAmount", 0.22f }, { "vibeType", loFi },
        { "masterGain", 0.60f } } },

    { "Tar Kiln", "BASS", "P(X3)",
      "Square and sub run into DOOM's RELAY at its shortest time, then straight into GLUE. "
      "The repeats do not decay, so the note thickens instead of echoing.",
      { { "osc1Mode", square }, { "osc1MacroA", 0.42f },
        { "subOscEnabled", 1 }, { "subOscOctave", octMinus1 }, { "subOscWaveform", subSquare },
        { "mix.sub.level", 0.78f }, { "mix.osc1.level", 0.62f },
        { "filter1Enabled", 1 }, { "filter1Type", lp12 }, { "filter1Cutoff", 640.0f }, { "filter1Resonance", 1.05f },
        { "ampAttack", 0.003f }, { "ampDecay", 0.30f }, { "ampSustain", 0.80f }, { "ampRelease", 0.18f },
        { "doomMix", 0.30f }, { "doomWetMode", relay }, { "doomWetTime", 0.06f }, { "doomWetModify", 0.20f },
        { "doomBalance", 1.0f }, { "doomRouting", inputOnly },
        // GLUE past halfway starts folding rather than only saturating.
        { "doomGlue", 0.62f }, { "doomEq", -0.30f }, { "doomClock", 0.80f }, { "doomSpread", 0.20f },
        { "vibeAmount", 0.30f }, { "vibeType", hot },
        { "masterGain", 0.49f } } },

    { "Sunken Bell", "BASS", "P(X3)",
      "The comb filter tuned to a low pitch and given a long decay, so every note rings the "
      "filter rather than passing through it. Play short - the tail is the instrument.",
      { { "osc1Mode", triangle }, { "osc1MacroA", 0.50f },
        { "subOscEnabled", 1 }, { "subOscOctave", octMinus1 }, { "subOscWaveform", subSine },
        { "mix.sub.level", 0.62f },
        { "filter1Enabled", 1 }, { "filter1Type", comb },
        { "filter1CombTune", 82.0f }, { "filter1CombDecay", 4.5f }, { "filter1CombDamping", 0.42f },
        { "filter1CombDispersion", 0.30f }, { "filter1CombDrive", 0.25f }, { "filter1CombMix", 0.85f },
        { "ampAttack", 0.002f }, { "ampDecay", 0.60f }, { "ampSustain", 0.30f }, { "ampRelease", 0.90f },
        { "reverbAmount", 0.18f }, { "reverbAlgorithm", room }, { "reverbSize", 0.35f }, { "reverbDecay", 0.30f },
        { "spreadAmount", 0.22f }, { "spreadMode", monoSafe },
        { "masterGain", 0.72f } } },

    // =======================================================================
    // LEADS
    // =======================================================================

    { "Neon Arterial", "LEADS", "P(X3)",
      "The PX3 oscillator pushed bright, through an ENSEMBLE chorus and a ping-pong delay. "
      "SPREAD sizes the whole thing last, so the delays are widened rather than re-imaged.",
      { { "osc1Mode", px3 }, { "osc1MacroA", 0.64f }, { "osc1MacroB", 0.52f }, { "osc1MacroC", 0.60f },
        { "filter1Enabled", 1 }, { "filter1Type", lp24 }, { "filter1Cutoff", 6200.0f }, { "filter1Resonance", 0.72f },
        { "ampAttack", 0.008f }, { "ampDecay", 0.35f }, { "ampSustain", 0.72f }, { "ampRelease", 0.30f },
        { "chorusAmount", 0.52f }, { "chorusMode", ensemble }, { "chorusRate", 0.28f }, { "chorusWidth", 0.85f },
        { "delayAmount", 0.32f }, { "delayAlgorithm", pingPong }, { "delayTime", 0.38f }, { "delayFeedback", 0.42f },
        { "reverbAmount", 0.20f }, { "reverbAlgorithm", plate }, { "reverbDecay", 0.42f },
        { "spreadAmount", 0.45f }, { "spreadMode", wide },
        { "masterGain", 0.58f } } },

    { "Hollow Siren", "LEADS", "P(X3)",
      "Hard sync with the sync pitch pushed up until the tone tears. Modulated delay keeps it "
      "moving; the reverb is short so the edge survives.",
      { { "osc1Mode", hardSync }, { "osc1MacroA", 0.72f }, { "osc1MacroB", 0.46f }, { "osc1MacroC", 0.55f },
        { "filter1Enabled", 1 }, { "filter1Type", lp12 }, { "filter1Cutoff", 4800.0f }, { "filter1Resonance", 1.15f },
        { "ampAttack", 0.012f }, { "ampDecay", 0.24f }, { "ampSustain", 0.78f }, { "ampRelease", 0.26f },
        { "delayAmount", 0.28f }, { "delayAlgorithm", modulated }, { "delayTime", 0.30f }, { "delayFeedback", 0.36f },
        { "reverbAmount", 0.16f }, { "reverbAlgorithm", room }, { "reverbSize", 0.40f }, { "reverbDecay", 0.28f },
        { "vibeAmount", 0.26f }, { "vibeType", hot },
        { "chorusAmount", 0.20f }, { "chorusMode", dim2 },
        { "masterGain", 0.52f } } },

    { "Glass Filament", "LEADS", "P(X3)",
      "An FM bell stretched into a lead, with LUCY in INVERSE - which plays back only what "
      "STANDARD would have thrown away. Thin, bright, and constantly moving.",
      { { "osc1Mode", fm }, { "osc1MacroA", 0.68f }, { "osc1MacroB", 0.30f }, { "osc1MacroC", 0.72f },
        { "filter1Enabled", 1 }, { "filter1Type", lp24 }, { "filter1Cutoff", 9000.0f }, { "filter1Resonance", 0.45f },
        { "ampAttack", 0.006f }, { "ampDecay", 0.50f }, { "ampSustain", 0.60f }, { "ampRelease", 0.55f },
        { "lucyGlobal", 0.42f }, { "lucyMode", inverse }, { "lucyLoss", 0.62f }, { "lucySpeed", 0.44f },
        { "lucyFilter", 0.34f }, { "lucyFilterFreq", 0.72f }, { "lucySlope", slope24 },
        { "lucyVerb", 0.30f }, { "lucyVerbDecay", 0.50f }, { "lucyWeighting", 0.40f },
        { "lucyThreshold", 0.70f }, { "lucyGain", 6.0f },
        { "spreadAmount", 0.35f }, { "spreadMode", classic },
        { "masterGain", 0.56f } } },

    { "Vowel Machine", "LEADS", "P(X3)",
      "A formant lead parked on a vowel, run through the CE-style single-path chorus for "
      "warmth rather than width. It talks.",
      { { "osc1Mode", formant }, { "osc1Vowel", 2 }, { "osc1MacroA", 0.55f }, { "osc1MacroB", 0.62f },
        { "osc2Enabled", 1 }, { "osc2Mode", saw }, { "osc2Coarse", -12.0f }, { "mix.osc2.level", 0.38f },
        { "filter1Enabled", 1 }, { "filter1Type", bandPass }, { "filter1Cutoff", 1400.0f }, { "filter1Resonance", 0.95f },
        { "ampAttack", 0.030f }, { "ampDecay", 0.30f }, { "ampSustain", 0.82f }, { "ampRelease", 0.34f },
        { "chorusAmount", 0.48f }, { "chorusMode", ceWarm }, { "chorusCharacter", 0.72f }, { "chorusTone", -0.25f },
        { "delayAmount", 0.22f }, { "delayAlgorithm", analogBbd }, { "delayTime", 0.42f }, { "delayFeedback", 0.30f },
        { "reverbAmount", 0.22f }, { "reverbAlgorithm", plate },
        { "masterGain", 0.74f } } },

    // =======================================================================
    // PADS
    // =======================================================================

    { "Slow Weather", "PADS", "P(X3)",
      "A supersaw pad under a stacked Dimension chorus - two pairs at different rates, which "
      "is denser and less periodic than either alone. CLOUD reverb behind it.",
      { { "osc1Mode", superSaw }, { "osc1MacroA", 0.46f }, { "osc1MacroB", 0.70f }, { "osc1MacroC", 0.40f },
        { "filter1Enabled", 1 }, { "filter1Type", lp24 }, { "filter1Cutoff", 3400.0f }, { "filter1Resonance", 0.42f },
        { "ampAttack", 0.85f }, { "ampDecay", 1.20f }, { "ampSustain", 0.85f }, { "ampRelease", 2.20f },
        { "chorusAmount", 0.55f }, { "chorusMode", dim2plus4 }, { "chorusRate", 0.22f }, { "chorusWidth", 0.90f },
        { "reverbAmount", 0.52f }, { "reverbAlgorithm", cloud }, { "reverbSize", 0.72f }, { "reverbDecay", 0.68f },
        { "reverbWidth", 0.90f },
        { "spreadAmount", 0.42f }, { "spreadMode", deep },
        { "masterGain", 0.56f } } },

    { "Frozen Transmission", "PADS", "P(X3)",
      "LUCY's spectral freeze in its SLUSHY state - it keeps updating from whatever you play, "
      "so the pad is a shifting copy of your own chords rather than a held snapshot.",
      { { "osc1Mode", wavetable }, { "osc1MacroA", 0.38f }, { "osc1MacroB", 0.55f }, { "osc1MacroC", 0.48f },
        { "filter1Enabled", 1 }, { "filter1Type", lp24 }, { "filter1Cutoff", 5200.0f }, { "filter1Resonance", 0.38f },
        { "ampAttack", 0.35f }, { "ampDecay", 0.90f }, { "ampSustain", 0.88f }, { "ampRelease", 1.60f },
        { "lucyGlobal", 0.62f }, { "lucyFreeze", 1 }, { "lucyFreezeSlushy", 1 }, { "lucyFreezer", 0.72f },
        { "lucySpeed", 0.30f }, { "lucyLoss", 0.40f }, { "lucyMode", standard },
        { "lucyVerb", 0.55f }, { "lucyVerbDecay", 0.72f }, { "lucyThreshold", 0.72f },
        { "lucySpread", 0.75f }, { "lucyGain", 4.0f },
        { "reverbAmount", 0.30f }, { "reverbAlgorithm", cloud }, { "reverbDecay", 0.60f },
        { "masterGain", 0.54f } } },

    { "Ghost Ensemble", "PADS", "P(X3)",
      "An additive stack fed into DOOM's SOUP - a spectral reverb that resynthesises what "
      "passes through it. MODIFY is up, so it remembers your instrument rather than reflecting it.",
      { { "osc1Mode", additive }, { "osc1H1", 1.0f }, { "osc1H2", 0.55f }, { "osc1H3", 0.62f },
        { "osc1H4", 0.28f }, { "osc1H5", 0.34f }, { "osc1H6", 0.16f }, { "osc1H7", 0.20f }, { "osc1H8", 0.10f },
        { "filter1Enabled", 1 }, { "filter1Type", lp24 }, { "filter1Cutoff", 6000.0f }, { "filter1Resonance", 0.35f },
        { "ampAttack", 0.55f }, { "ampDecay", 1.00f }, { "ampSustain", 0.80f }, { "ampRelease", 1.80f },
        { "doomMix", 0.52f }, { "doomWetMode", soup }, { "doomWetTime", 0.72f }, { "doomWetModify", 0.68f },
        { "doomBalance", 1.0f }, { "doomRouting", inputOnly },
        // A low clock darkens and slows SOUP; a high one is where the sparkle is.
        { "doomClock", 0.55f }, { "doomGlue", 0.12f }, { "doomEq", -0.15f }, { "doomSpread", 0.80f },
        { "spreadAmount", 0.38f }, { "spreadMode", deep },
        { "masterGain", 0.54f } } },

    { "Tidal Organ", "PADS", "P(X3)",
      "Drawbar organ tone through the string-machine ensemble chorus, with MOOD holding a "
      "slow reverb underneath. Sits still and moves at the same time.",
      { { "osc1Mode", organ }, { "osc1MacroA", 0.60f }, { "osc1MacroB", 0.45f }, { "osc1MacroC", 0.52f },
        { "osc2Enabled", 1 }, { "osc2Mode", sine }, { "osc2Coarse", 12.0f }, { "mix.osc2.level", 0.34f },
        { "filter1Enabled", 1 }, { "filter1Type", lp12 }, { "filter1Cutoff", 4200.0f }, { "filter1Resonance", 0.30f },
        { "ampAttack", 0.12f }, { "ampDecay", 0.60f }, { "ampSustain", 0.92f }, { "ampRelease", 0.85f },
        { "chorusAmount", 0.62f }, { "chorusMode", ensemble }, { "chorusRate", 0.18f }, { "chorusDepth", 0.62f },
        { "moodMix", 0.28f }, { "moodWetMode", moodReverb }, { "moodWetTime", 0.62f }, { "moodWetModify", 0.40f },
        { "moodClock", 0.70f }, { "moodSpread", 0.70f },
        { "masterGain", 0.50f } } },

    // =======================================================================
    // PLUCKS
    // =======================================================================

    { "Porcelain", "PLUCKS", "P(X3)",
      "Karplus-Strong into the comb filter - a plucked string played through a second one. "
      "The diffusion delay smears the tails without repeating them.",
      { { "osc1Mode", karplus }, { "osc1MacroA", 0.55f }, { "osc1MacroB", 0.68f }, { "osc1MacroC", 0.40f },
        { "filter1Enabled", 1 }, { "filter1Type", comb },
        { "filter1CombTune", 660.0f }, { "filter1CombDecay", 1.40f }, { "filter1CombDamping", 0.35f },
        { "filter1CombDispersion", 0.45f }, { "filter1CombDrive", 0.15f }, { "filter1CombMix", 0.68f },
        { "ampAttack", 0.001f }, { "ampDecay", 0.42f }, { "ampSustain", 0.24f }, { "ampRelease", 0.55f },
        { "delayAmount", 0.30f }, { "delayAlgorithm", diffusion }, { "delayTime", 0.26f }, { "delayFeedback", 0.34f },
        { "reverbAmount", 0.26f }, { "reverbAlgorithm", plate }, { "reverbDecay", 0.40f },
        { "chorusAmount", 0.22f }, { "chorusMode", dim1 },
        { "masterGain", 0.86f } } },

    { "Rain on Copper", "PLUCKS", "P(X3)",
      "A short digital pluck with LUCY set to PACKET REPEAT. Dropped frames are filled with "
      "the last good one, phase advanced - so the glitches smear instead of stuttering.",
      { { "osc1Mode", digital }, { "osc1MacroA", 0.62f }, { "osc1MacroB", 0.40f }, { "osc1MacroC", 0.58f },
        { "filter1Enabled", 1 }, { "filter1Type", lp24 }, { "filter1Cutoff", 7200.0f }, { "filter1Resonance", 0.68f },
        { "ampAttack", 0.001f }, { "ampDecay", 0.20f }, { "ampSustain", 0.08f }, { "ampRelease", 0.22f },
        { "lucyGlobal", 0.50f }, { "lucyPackets", packetRepeat }, { "lucyMode", standard },
        { "lucyLoss", 0.58f }, { "lucySpeed", 0.68f }, { "lucySpread", 0.85f },
        { "lucyFilter", 0.26f }, { "lucyFilterFreq", 0.66f }, { "lucyThreshold", 0.68f }, { "lucyGain", 9.0f },
        { "delayAmount", 0.26f }, { "delayAlgorithm", stereoDelay }, { "delayTime", 0.22f }, { "delayFeedback", 0.30f },
        { "reverbAmount", 0.24f }, { "reverbAlgorithm", room },
        { "masterGain", 0.74f } } },

    { "Music Box", "PLUCKS", "P(X3)",
      "A clean FM bell with a tape delay behind it. Nothing exotic - it is here because a "
      "preset library needs something you can just play.",
      { { "osc1Mode", fm }, { "osc1MacroA", 0.52f }, { "osc1MacroB", 0.22f }, { "osc1MacroC", 0.66f },
        { "filter1Enabled", 1 }, { "filter1Type", lp12 }, { "filter1Cutoff", 8000.0f }, { "filter1Resonance", 0.30f },
        { "ampAttack", 0.001f }, { "ampDecay", 0.55f }, { "ampSustain", 0.05f }, { "ampRelease", 0.70f },
        { "delayAmount", 0.28f }, { "delayAlgorithm", tape }, { "delayTime", 0.34f }, { "delayFeedback", 0.32f },
        { "reverbAmount", 0.30f }, { "reverbAlgorithm", hall }, { "reverbSize", 0.60f }, { "reverbDecay", 0.45f },
        { "chorusAmount", 0.24f }, { "chorusMode", dim1 }, { "chorusRate", 0.20f },
        { "masterGain", 0.72f } } },

    // =======================================================================
    // EXPERIMENTAL
    // =======================================================================

    { "Bad Signal", "EXPERIMENTAL", "P(X3)",
      "LUCY with PACKET LOSS, JITTER and the gate open. Losses arrive in bursts rather than "
      "evenly, which is why it sounds like a failing connection and not like tremolo.",
      { { "osc1Mode", superSaw }, { "osc1MacroA", 0.55f }, { "osc1MacroB", 0.60f },
        { "filter1Enabled", 1 }, { "filter1Type", lp24 }, { "filter1Cutoff", 5000.0f }, { "filter1Resonance", 0.55f },
        { "ampAttack", 0.02f }, { "ampDecay", 0.40f }, { "ampSustain", 0.80f }, { "ampRelease", 0.40f },
        { "lucyGlobal", 0.78f }, { "lucyMode", jitter }, { "lucyPackets", packetLoss },
        { "lucyLoss", 0.72f }, { "lucySpeed", 0.42f },
        { "lucyGate", 1 }, { "lucyGateCutoff", 0.30f },
        { "lucyFilter", 0.40f }, { "lucyFilterFreq", 0.55f }, { "lucySlope", slope96 },
        { "lucyVerb", 0.38f }, { "lucyVerbDecay", 0.55f }, { "lucySpread", 0.90f },
        { "lucyThreshold", 0.62f }, { "lucyGain", 11.0f },
        { "masterGain", 0.72f } } },

    { "Splinter Choir", "EXPERIMENTAL", "P(X3)",
      "DOOM's FLIP mode - fourths, fifths and octaves stacked on what you play and spread "
      "across time, so the chord arrives one note at a time.",
      { { "osc1Mode", physical }, { "osc1MacroA", 0.48f }, { "osc1MacroB", 0.62f }, { "osc1MacroC", 0.40f },
        { "filter1Enabled", 1 }, { "filter1Type", lp24 }, { "filter1Cutoff", 5600.0f }, { "filter1Resonance", 0.48f },
        { "ampAttack", 0.05f }, { "ampDecay", 0.70f }, { "ampSustain", 0.60f }, { "ampRelease", 1.10f },
        { "doomMix", 0.58f }, { "doomWetMode", flip }, { "doomWetTime", 0.42f }, { "doomWetModify", 0.88f },
        { "doomBalance", 1.0f }, { "doomRouting", inputOnly }, { "doomClock", 0.85f },
        { "doomGlue", 0.20f }, { "doomSpread", 0.85f }, { "doomEq", 0.10f },
        { "reverbAmount", 0.34f }, { "reverbAlgorithm", hall }, { "reverbDecay", 0.55f },
        { "masterGain", 0.54f } } },

    { "Radio Ghost", "EXPERIMENTAL", "P(X3)",
      "DOOM primed for its micro-looper: play a phrase, then engage LOOPER to catch what you "
      "already played. RADIO scans five loopers with interference between the stations.",
      { { "osc1Mode", isaac }, { "osc1MacroA", 0.58f }, { "osc1MacroB", 0.66f }, { "osc1MacroC", 0.44f },
        { "filter1Enabled", 1 }, { "filter1Type", lp12 }, { "filter1Cutoff", 4400.0f }, { "filter1Resonance", 0.62f },
        { "ampAttack", 0.02f }, { "ampDecay", 0.55f }, { "ampSustain", 0.70f }, { "ampRelease", 0.60f },
        { "doomMix", 0.50f }, { "doomWetMode", soup }, { "doomWetTime", 0.55f }, { "doomWetModify", 0.50f },
        // The looper is left OFF on purpose. It is always listening, so engaging
        // it captures what you already played - loading a preset with it on
        // would capture the silence before you touched a key.
        { "doomLoopActive", 0 }, { "doomLoopMode", radio }, { "doomLoopLength", 0.52f },
        { "doomLoopModify", 0.30f }, { "doomRouting", inputPlusLoop }, { "doomBalance", 0.55f },
        { "doomClock", 0.42f }, { "doomGlue", 0.28f }, { "doomSpread", 0.70f },
        { "doomCross", 0.35f }, { "doomCrossSource", 0 },
        { "reverbAmount", 0.26f }, { "reverbAlgorithm", cloud },
        { "masterGain", 0.52f } } },

    { "Comb Reactor", "EXPERIMENTAL", "P(X3)",
      "The comb filter driven near self-oscillation and fed with ROB, then DOOM's CROSS "
      "modulating pitch and loudness from the signal itself. Unstable on purpose.",
      { { "osc1Mode", rob }, { "osc1MacroA", 0.76f }, { "osc1MacroB", 0.68f }, { "osc1MacroC", 0.82f },
        { "filter1Enabled", 1 }, { "filter1Type", comb },
        { "filter1CombTune", 220.0f }, { "filter1CombDecay", 8.0f }, { "filter1CombDamping", 0.18f },
        { "filter1CombDispersion", 0.62f }, { "filter1CombDrive", 0.55f }, { "filter1CombMix", 0.75f },
        { "ampAttack", 0.008f }, { "ampDecay", 0.45f }, { "ampSustain", 0.55f }, { "ampRelease", 0.80f },
        { "doomMix", 0.42f }, { "doomWetMode", relay }, { "doomWetTime", 0.30f }, { "doomWetModify", 0.72f },
        { "doomBalance", 1.0f }, { "doomCross", 0.78f }, { "doomCrossSource", 1 },
        { "doomGlue", 0.45f }, { "doomClock", 0.62f }, { "doomEq", -0.20f }, { "doomSpread", 0.65f },
        { "spreadAmount", 0.30f }, { "spreadMode", classic },
        { "masterGain", 0.44f } } },

    { "Dimension Drift", "EXPERIMENTAL", "P(X3)",
      "A demonstration of the two spatial effects with nothing else in the way: the stacked "
      "Dimension chorus into SPREAD on WIDE. Mono-compatible at every setting - check it.",
      { { "osc1Mode", wavetable }, { "osc1MacroA", 0.50f }, { "osc1MacroB", 0.62f }, { "osc1MacroC", 0.38f },
        { "osc2Enabled", 1 }, { "osc2Mode", triangle }, { "osc2Coarse", 7.0f }, { "osc2Fine", 6.0f },
        { "mix.osc2.level", 0.42f },
        { "filter1Enabled", 1 }, { "filter1Type", lp24 }, { "filter1Cutoff", 6400.0f }, { "filter1Resonance", 0.36f },
        { "ampAttack", 0.28f }, { "ampDecay", 0.80f }, { "ampSustain", 0.85f }, { "ampRelease", 1.40f },
        { "chorusAmount", 0.68f }, { "chorusMode", dim3plus4 }, { "chorusRate", 0.26f },
        { "chorusWidth", 0.95f }, { "chorusDepth", 0.58f }, { "chorusCharacter", 0.45f },
        { "spreadAmount", 0.70f }, { "spreadMode", wide }, { "spreadWidth", 0.85f },
        { "spreadDepth", 0.60f }, { "spreadHighWidth", 0.90f },
        { "reverbAmount", 0.22f }, { "reverbAlgorithm", plate },
        { "masterGain", 0.54f } } },

    };
}

} // namespace px3::presets
