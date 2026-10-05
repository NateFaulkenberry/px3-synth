#include "FactoryPresets.h"

#include "ReverbPresets.h"

namespace px3::presets
{
namespace
{
// Choice indices, named. A preset full of bare numbers is unreadable and a
// preset full of wrong bare numbers is undebuggable.
enum OscMode
{
    sine = 0, saw, square, triangle, noise, pinkNoise, superSaw, pwm, wavetable,
    additive, formant, fm, hardSync, organ, digital, physical, rob, isaac, px3
};

enum FilterType { lp12 = 0, lp24, hp12, hp24, bandPass, notch, allPass, comb };
enum DelayAlgo  { granular = 0, tape, analogBbd, pingPong, stereoDelay, modulated, diffusion };
enum ReverbAlgo { room = 0, plate, hall, cloud, spring, gated };
enum AnalogType { warm = 0, hot, cool, vintage, clean, loFi };
enum VibeMode   { vibeChorus = 0, vibeVibrato };

// DIM 1 is the softest and has the LONGEST delay; DIM 4 is the strongest.
enum ChorusMode { dim1 = 0, dim2, dim3, dim4, dim1plus4, dim2plus4, dim3plus4, ensemble, ce1, juno60I, juno60II, juno60Both };
enum SpreadMode { classic = 0, wide, deep, monoSafe };

enum DoomLoopMode { burst = 0, radio, mask };
enum DoomWetMode  { soup = 0, relay, flip };
enum DoomRouting  { inputOnly = 0, inputPlusLoop, loopOnly };

enum LucyMode    { standard = 0, inverse, jitter };
enum LucyPackets { cleanPackets = 0, packetLoss, packetRepeat };
enum LucySlope   { slope6 = 0, slope24, slope96 };
// Both were something else before the control refresh: WEIGHTING was a bipolar
// float and FREEZE was a pair of booleans. See docs/LUCY_DSP_DESIGN.md.
enum LucyWeighting { weightDark = 0, weightNeutral, weightBright };
enum LucyFreeze    { freezeOff = 0, freezeSolid, freezeSlushy };

enum SubWave     { subSine = 0, subSquare };

enum MoodWetMode  { moodReverb = 0, moodDelay, moodSlip };
enum MoodLoopMode { moodEnv = 0, moodTape, moodStretch };
} // namespace

// A patch's reverb is one of the Reverb card's own presets (ReverbPresets.h):
// its controls are written into the patch, and the patch names it, so the
// card's PRESET menu shows which one.
void useReverbPreset(std::vector<FactoryPreset>& presets, const char* patch, int type, const char* reverbPreset)
{
    const auto* preset = px3::reverb::findPreset(type, reverbPreset);
    jassert(preset != nullptr);
    if (preset == nullptr) { return; }
    for (auto& p : presets)
    {
        if (juce::String(p.name) != patch) { continue; }
        auto& params = p.params;
        params.erase(std::remove_if(params.begin(), params.end(), [](const auto& entry)
        {
            const juce::String id(entry.first);
            return id.startsWith("fx.reverb.") && id != "fx.reverb.enabled" && id != "fx.reverb.amount";
        }), params.end());
        params.push_back({ "fx.reverb.algorithm", static_cast<float>(type) });
        for (const auto& spec : px3::reverb::kParameterSpecs)
            params.push_back({ spec.id, px3::reverb::presetValue(*preset, spec.control) });
        p.reverbPreset = std::string(px3::reverb::kTypeNames[type]) + "/" + reverbPreset;
        return;
    }
    jassertfalse;
}

std::vector<FactoryPreset> factoryPresets()
{
    auto presets = std::vector<FactoryPreset> {

    // =======================================================================
    // BASS
    // =======================================================================

    { "Reese Undertow", "BASS", "P(X3)",
      "Two saws pulled apart until they beat against each other, anchored by a square sub. "
      "CHORUS widens the harmonics while its low cut keeps the fundamental where you left it.",
      { { "voice.osc1.mode", saw }, { "voice.osc1.macro.a", 0.30f },
        { "fx.analog.enabled", 1 }, { "fx.delay.enabled", 0 }, { "fx.reverb.enabled", 0 }, { "fx.mood.enabled", 0 },
        { "fx.doom.enabled", 0 }, { "fx.lucy.enabled", 0 }, { "fx.chorus.enabled", 1 }, { "fx.spread.enabled", 1 },
        { "voice.filter2.enabled", 0 }, { "global.character.enabled", 1 }, { "global.character.profile", 1 },
        { "voice.osc2.enabled", 1 }, { "voice.osc2.mode", saw }, { "voice.osc2.tuning.cents", -13.0f }, { "voice.osc2.tuning.octave", 0.0f },
        { "voice.sub.enabled", 1 }, { "voice.sub.tuning.octave", -1.0f }, { "voice.sub.waveform", subSquare },
        { "mix.sub.level", 0.72f }, { "mix.osc1.level", 0.58f }, { "mix.osc2.level", 0.58f },
        { "voice.filter1.enabled", 1 }, { "voice.filter1.type", lp24 }, { "voice.filter1.cutoff", 420.0f }, { "voice.filter1.resonance", 0.85f },
        { "voice.amp.attack", 0.004f }, { "voice.amp.decay", 0.40f }, { "voice.amp.sustain", 0.88f }, { "voice.amp.release", 0.22f },
        // Mode 4 is the strongest and the shortest-delay Dimension mode - the one
        // that moves harmonics without smearing a bass note.
        { "fx.chorus.amount", 0.34f }, { "fx.chorus.mode", dim4 }, { "fx.chorus.low.cut", 0.72f }, { "fx.chorus.depth", 0.42f },
        { "fx.spread.amount", 0.28f }, { "fx.spread.mode", monoSafe },
        { "mix.master.level", 0.429f } } },

    { "Dial Tone", "BASS", "P(X3)",
      "A hard FM bass with LUCY set low and slow behind it. The loss is barely a texture at "
      "this depth - just enough to make it sound like it arrived over a wire.",
      { { "voice.osc1.mode", fm }, { "voice.osc1.macro.a", 0.34f }, { "voice.osc1.macro.b", 0.58f }, { "voice.osc1.macro.c", 0.24f },
        { "fx.analog.enabled", 0 }, { "fx.delay.enabled", 0 }, { "fx.reverb.enabled", 0 }, { "fx.mood.enabled", 0 },
        { "fx.doom.enabled", 0 }, { "fx.lucy.enabled", 1 }, { "fx.chorus.enabled", 0 }, { "fx.spread.enabled", 0 },
        { "voice.filter2.enabled", 0 },
        { "voice.sub.enabled", 1 }, { "voice.sub.tuning.octave", -1.0f }, { "voice.sub.waveform", subSine },
        { "mix.sub.level", 0.55f },
        { "voice.filter1.enabled", 1 }, { "voice.filter1.type", lp24 }, { "voice.filter1.cutoff", 900.0f }, { "voice.filter1.resonance", 0.60f },
        { "voice.amp.attack", 0.002f }, { "voice.amp.decay", 0.26f }, { "voice.amp.sustain", 0.55f }, { "voice.amp.release", 0.14f },
        { "fx.lucy.global", 0.26f }, { "fx.lucy.mode", standard }, { "fx.lucy.loss", 0.44f }, { "fx.lucy.speed", 0.62f },
        { "fx.lucy.filter", 0.22f }, { "fx.lucy.freq", 0.62f }, { "fx.lucy.slope", slope24 },
        { "fx.lucy.weighting", weightDark }, { "fx.lucy.loss.gain", 3.0f },
        { "mix.master.level", 0.60f } } },

    { "Tar Kiln", "BASS", "P(X3)",
      "Square and sub run into DOOM's RELAY at its shortest time, then straight into GLUE. "
      "The repeats do not decay, so the note thickens instead of echoing.",
      { { "voice.osc1.mode", square }, { "voice.osc1.macro.a", 0.42f },
        { "fx.analog.enabled", 0 }, { "fx.delay.enabled", 0 }, { "fx.reverb.enabled", 0 }, { "fx.mood.enabled", 0 },
        { "fx.doom.enabled", 1 }, { "fx.lucy.enabled", 0 }, { "fx.chorus.enabled", 0 }, { "fx.spread.enabled", 0 },
        { "voice.filter2.enabled", 0 }, { "global.character.enabled", 1 }, { "global.character.profile", 3 },
        { "voice.sub.enabled", 1 }, { "voice.sub.tuning.octave", -1.0f }, { "voice.sub.waveform", subSquare },
        { "mix.sub.level", 0.78f }, { "mix.osc1.level", 0.62f },
        { "voice.filter1.enabled", 1 }, { "voice.filter1.type", lp12 }, { "voice.filter1.cutoff", 640.0f }, { "voice.filter1.resonance", 1.05f },
        { "voice.amp.attack", 0.003f }, { "voice.amp.decay", 0.30f }, { "voice.amp.sustain", 0.80f }, { "voice.amp.release", 0.18f },
        { "fx.doom.mix", 0.30f }, { "fx.doom.wet.mode", relay }, { "fx.doom.wet.time", 0.06f }, { "fx.doom.wet.modify", 0.20f },
        { "fx.doom.balance", 1.0f }, { "fx.doom.routing", inputOnly },
        // GLUE past halfway starts folding rather than only saturating.
        { "fx.doom.glue", 0.62f }, { "fx.doom.eq", -0.30f }, { "fx.doom.clock", 0.80f }, { "fx.doom.spread", 0.20f },
        { "mix.master.level", 0.304f } } },

    { "Sunken Bell", "BASS", "P(X3)",
      "The comb filter tuned to a low pitch and given a long decay, so every note rings the "
      "filter rather than passing through it. Play short - the tail is the instrument.",
      { { "voice.osc1.mode", triangle }, { "voice.osc1.macro.a", 0.50f },
        { "fx.analog.enabled", 0 }, { "fx.delay.enabled", 0 }, { "fx.reverb.enabled", 1 }, { "fx.mood.enabled", 0 },
        { "fx.doom.enabled", 0 }, { "fx.lucy.enabled", 0 }, { "fx.chorus.enabled", 0 }, { "fx.spread.enabled", 0 },
        { "voice.filter2.enabled", 0 },
        { "voice.sub.enabled", 1 }, { "voice.sub.tuning.octave", -1.0f }, { "voice.sub.waveform", subSine },
        { "mix.sub.level", 0.62f },
        { "voice.filter1.enabled", 1 }, { "voice.filter1.type", comb },
        { "voice.filter1.comb.tuning", 82.0f }, { "voice.filter1.comb.decay", 4.5f }, { "voice.filter1.comb.damping", 0.42f },
        { "voice.filter1.comb.dispersion", 0.30f }, { "voice.filter1.comb.drive", 0.25f }, { "voice.filter1.comb.mix", 0.85f },
        { "voice.amp.attack", 0.002f }, { "voice.amp.decay", 0.60f }, { "voice.amp.sustain", 0.30f }, { "voice.amp.release", 0.90f },
        { "fx.reverb.amount", 0.05f }, { "fx.reverb.algorithm", room }, { "fx.reverb.size", 0.35f }, { "fx.reverb.decay", 0.30f },
        { "fx.spread.amount", 0.22f }, { "fx.spread.mode", monoSafe },
        { "mix.master.level", 0.72f } } },

    // =======================================================================
    // LEADS
    // =======================================================================

    { "Neon Arterial", "LEADS", "P(X3)",
      "The PX3 oscillator pushed bright, through an ENSEMBLE chorus and a ping-pong delay. "
      "SPREAD sizes the whole thing last, so the delays are widened rather than re-imaged.",
      { { "voice.osc1.mode", px3 }, { "voice.osc1.macro.a", 0.64f }, { "voice.osc1.macro.b", 0.52f }, { "voice.osc1.macro.c", 0.60f },
        { "fx.analog.enabled", 0 }, { "fx.delay.enabled", 1 }, { "fx.reverb.enabled", 0 }, { "fx.mood.enabled", 0 },
        { "fx.doom.enabled", 0 }, { "fx.lucy.enabled", 0 }, { "fx.chorus.enabled", 1 }, { "fx.spread.enabled", 1 },
        { "voice.filter2.enabled", 1 },
        { "voice.filter1.enabled", 1 }, { "voice.filter1.type", lp24 }, { "voice.filter1.cutoff", 6200.0f }, { "voice.filter1.resonance", 0.72f },
        { "voice.amp.attack", 0.008f }, { "voice.amp.decay", 0.35f }, { "voice.amp.sustain", 0.72f }, { "voice.amp.release", 0.30f },
        { "fx.chorus.amount", 0.52f }, { "fx.chorus.mode", ensemble }, { "fx.chorus.rate", 0.28f }, { "fx.chorus.width", 0.85f },
        { "fx.delay.amount", 0.32f }, { "fx.delay.algorithm", pingPong }, { "fx.delay.time", 0.38f }, { "fx.delay.feedback", 0.42f },
        { "fx.reverb.amount", 0.07f }, { "fx.reverb.algorithm", plate }, { "fx.reverb.decay", 0.42f },
        { "fx.spread.amount", 0.45f }, { "fx.spread.mode", wide },
        { "mix.master.level", 0.58f } } },

    { "Hollow Siren", "LEADS", "P(X3)",
      "Hard sync with the sync pitch pushed up until the tone tears. Modulated delay keeps it "
      "moving; the reverb is short so the edge survives.",
      { { "voice.osc1.mode", hardSync }, { "voice.osc1.macro.a", 0.72f }, { "voice.osc1.macro.b", 0.46f }, { "voice.osc1.macro.c", 0.55f },
        { "fx.analog.enabled", 1 }, { "fx.delay.enabled", 1 }, { "fx.reverb.enabled", 1 }, { "fx.mood.enabled", 0 },
        { "fx.doom.enabled", 0 }, { "fx.lucy.enabled", 0 }, { "fx.chorus.enabled", 0 }, { "fx.spread.enabled", 0 },
        { "voice.filter2.enabled", 0 },
        { "voice.filter1.enabled", 1 }, { "voice.filter1.type", lp12 }, { "voice.filter1.cutoff", 4800.0f }, { "voice.filter1.resonance", 1.15f },
        { "voice.amp.attack", 0.012f }, { "voice.amp.decay", 0.24f }, { "voice.amp.sustain", 0.78f }, { "voice.amp.release", 0.26f },
        { "fx.delay.amount", 0.28f }, { "fx.delay.algorithm", modulated }, { "fx.delay.time", 0.30f }, { "fx.delay.feedback", 0.36f },
        { "fx.reverb.amount", 0.05f }, { "fx.reverb.algorithm", room }, { "fx.reverb.size", 0.40f }, { "fx.reverb.decay", 0.28f },
        { "fx.analog.amount", 0.26f }, { "fx.analog.type", hot },
        { "fx.chorus.amount", 0.20f }, { "fx.chorus.mode", dim2 },
        { "mix.master.level", 0.52f } } },

    { "Glass Filament", "LEADS", "P(X3)",
      "An FM bell stretched into a lead, with LUCY in INVERSE - which plays back only what "
      "STANDARD would have thrown away. Thin, bright, and constantly moving.",
      { { "voice.osc1.mode", fm }, { "voice.osc1.macro.a", 0.68f }, { "voice.osc1.macro.b", 0.30f }, { "voice.osc1.macro.c", 0.72f },
        { "fx.analog.enabled", 0 }, { "fx.delay.enabled", 0 }, { "fx.reverb.enabled", 1 }, { "fx.mood.enabled", 0 },
        { "fx.doom.enabled", 0 }, { "fx.lucy.enabled", 1 }, { "fx.chorus.enabled", 0 }, { "fx.spread.enabled", 0 },
        { "voice.filter2.enabled", 0 },
        { "voice.filter1.enabled", 1 }, { "voice.filter1.type", lp24 }, { "voice.filter1.cutoff", 9000.0f }, { "voice.filter1.resonance", 0.45f },
        { "voice.amp.attack", 0.006f }, { "voice.amp.decay", 0.50f }, { "voice.amp.sustain", 0.60f }, { "voice.amp.release", 0.55f },
        { "fx.lucy.global", 0.42f }, { "fx.lucy.mode", inverse }, { "fx.lucy.loss", 0.62f }, { "fx.lucy.speed", 0.44f },
        { "fx.lucy.filter", 0.34f }, { "fx.lucy.freq", 0.72f }, { "fx.lucy.slope", slope24 },
        { "fx.lucy.verb", 0.30f }, { "fx.lucy.decay", 0.50f }, { "fx.lucy.weighting", weightBright },
        { "fx.lucy.limiter.threshold", 0.70f }, { "fx.lucy.loss.gain", 6.0f },
        { "fx.spread.amount", 0.35f }, { "fx.spread.mode", classic },
        { "mix.master.level", 0.56f } } },

    { "Vowel Machine", "LEADS", "P(X3)",
      "A formant lead parked on a vowel, run through the CE-1 chorus on its mono output (WIDTH 0: "
      "direct and chorus on both sides) for warmth rather than width. It talks.",
      { { "voice.osc1.mode", formant }, { "voice.osc1.vowel", 2 }, { "voice.osc1.macro.a", 0.55f }, { "voice.osc1.macro.b", 0.62f },
        { "fx.analog.enabled", 0 }, { "fx.delay.enabled", 1 }, { "fx.reverb.enabled", 0 }, { "fx.mood.enabled", 0 },
        { "fx.doom.enabled", 0 }, { "fx.lucy.enabled", 0 }, { "fx.chorus.enabled", 1 }, { "fx.spread.enabled", 0 },
        { "voice.filter2.enabled", 0 },
        { "voice.osc2.enabled", 1 }, { "voice.osc2.mode", saw }, { "voice.osc2.tuning.octave", -1.0f }, { "mix.osc2.level", 0.38f },
        { "voice.filter1.enabled", 1 }, { "voice.filter1.type", bandPass }, { "voice.filter1.cutoff", 1400.0f }, { "voice.filter1.resonance", 0.95f },
        { "voice.amp.attack", 0.030f }, { "voice.amp.decay", 0.30f }, { "voice.amp.sustain", 0.82f }, { "voice.amp.release", 0.34f },
        { "fx.chorus.amount", 0.48f }, { "fx.chorus.mode", ce1 }, { "fx.chorus.character", 0.72f }, { "fx.chorus.tone", -0.25f },
        { "fx.chorus.width", 0.0f },
        { "fx.delay.amount", 0.22f }, { "fx.delay.algorithm", analogBbd }, { "fx.delay.time", 0.42f }, { "fx.delay.feedback", 0.30f },
        { "fx.reverb.amount", 0.08f }, { "fx.reverb.algorithm", plate },
        { "mix.master.level", 0.95f } } },

    // =======================================================================
    // PADS
    // =======================================================================

    { "Slow Weather", "PADS", "P(X3)",
      "A supersaw pad under the Dimension's 2+4 combination - both buttons' switch states on "
      "one anti-phase pair, faster and deeper than either alone. CLOUD reverb behind it.",
      { { "voice.osc1.mode", superSaw }, { "voice.osc1.macro.a", 0.46f }, { "voice.osc1.macro.b", 0.70f }, { "voice.osc1.macro.c", 0.40f },
        { "fx.analog.enabled", 0 }, { "fx.delay.enabled", 0 }, { "fx.reverb.enabled", 1 }, { "fx.mood.enabled", 0 },
        { "fx.doom.enabled", 0 }, { "fx.lucy.enabled", 0 }, { "fx.chorus.enabled", 1 }, { "fx.spread.enabled", 1 },
        { "voice.filter2.enabled", 1 }, { "global.character.enabled", 1 }, { "global.character.profile", 4 },
        { "voice.filter1.enabled", 1 }, { "voice.filter1.type", lp24 }, { "voice.filter1.cutoff", 3400.0f }, { "voice.filter1.resonance", 0.42f },
        { "voice.amp.attack", 0.85f }, { "voice.amp.decay", 1.20f }, { "voice.amp.sustain", 0.85f }, { "voice.amp.release", 2.20f },
        { "fx.chorus.amount", 0.55f }, { "fx.chorus.mode", dim2plus4 }, { "fx.chorus.rate", 0.22f }, { "fx.chorus.width", 0.90f },
        { "fx.reverb.amount", 0.36f }, { "fx.reverb.algorithm", cloud }, { "fx.reverb.size", 0.72f }, { "fx.reverb.decay", 0.68f },
        { "fx.reverb.width", 0.90f },
        { "fx.spread.amount", 0.42f }, { "fx.spread.mode", deep },
        { "mix.master.level", 0.56f } } },

    { "Frozen Transmission", "PADS", "P(X3)",
      "LUCY's spectral freeze in its SLUSHY state - it keeps updating from whatever you play, "
      "so the pad is a shifting copy of your own chords rather than a held snapshot.",
      { { "voice.osc1.mode", wavetable }, { "voice.osc1.macro.a", 0.38f }, { "voice.osc1.macro.b", 0.55f }, { "voice.osc1.macro.c", 0.48f },
        { "fx.analog.enabled", 0 }, { "fx.delay.enabled", 0 }, { "fx.reverb.enabled", 1 }, { "fx.mood.enabled", 0 },
        { "fx.doom.enabled", 0 }, { "fx.lucy.enabled", 1 }, { "fx.chorus.enabled", 0 }, { "fx.spread.enabled", 0 },
        { "voice.filter2.enabled", 0 },
        { "voice.filter1.enabled", 1 }, { "voice.filter1.type", lp24 }, { "voice.filter1.cutoff", 5200.0f }, { "voice.filter1.resonance", 0.38f },
        { "voice.amp.attack", 0.35f }, { "voice.amp.decay", 0.90f }, { "voice.amp.sustain", 0.88f }, { "voice.amp.release", 1.60f },
        { "fx.lucy.global", 0.62f }, { "fx.lucy.freeze", freezeSlushy }, { "fx.lucy.freezer", 0.72f },
        { "fx.lucy.speed", 0.30f }, { "fx.lucy.loss", 0.40f }, { "fx.lucy.mode", standard },
        { "fx.lucy.verb", 0.55f }, { "fx.lucy.decay", 0.72f }, { "fx.lucy.limiter.threshold", 0.72f },
        { "fx.lucy.spread", 0.75f }, { "fx.lucy.loss.gain", 4.0f },
        { "fx.reverb.amount", 0.14f }, { "fx.reverb.algorithm", cloud }, { "fx.reverb.decay", 0.60f },
        { "mix.master.level", 0.54f } } },

    { "Ghost Ensemble", "PADS", "P(X3)",
      "An additive stack fed into DOOM's SOUP - a spectral reverb that resynthesises what "
      "passes through it. MODIFY is up, so it remembers your instrument rather than reflecting it.",
      { { "voice.osc1.mode", additive }, { "voice.osc1.harmonics.1", 1.0f }, { "voice.osc1.harmonics.2", 0.55f }, { "voice.osc1.harmonics.3", 0.62f },
        { "fx.analog.enabled", 0 }, { "fx.delay.enabled", 0 }, { "fx.reverb.enabled", 0 }, { "fx.mood.enabled", 0 },
        { "fx.doom.enabled", 1 }, { "fx.lucy.enabled", 0 }, { "fx.chorus.enabled", 0 }, { "fx.spread.enabled", 1 },
        { "voice.filter2.enabled", 0 },
        { "voice.osc1.harmonics.4", 0.28f }, { "voice.osc1.harmonics.5", 0.34f }, { "voice.osc1.harmonics.6", 0.16f }, { "voice.osc1.harmonics.7", 0.20f }, { "voice.osc1.harmonics.8", 0.10f },
        { "voice.filter1.enabled", 1 }, { "voice.filter1.type", lp24 }, { "voice.filter1.cutoff", 6000.0f }, { "voice.filter1.resonance", 0.35f },
        { "voice.amp.attack", 0.55f }, { "voice.amp.decay", 1.00f }, { "voice.amp.sustain", 0.80f }, { "voice.amp.release", 1.80f },
        { "fx.doom.mix", 0.52f }, { "fx.doom.wet.mode", soup }, { "fx.doom.wet.time", 0.72f }, { "fx.doom.wet.modify", 0.68f },
        { "fx.doom.balance", 1.0f }, { "fx.doom.routing", inputOnly },
        // A low clock darkens and slows SOUP; a high one is where the sparkle is.
        { "fx.doom.clock", 0.55f }, { "fx.doom.glue", 0.12f }, { "fx.doom.eq", -0.15f }, { "fx.doom.spread", 0.80f },
        { "fx.spread.amount", 0.38f }, { "fx.spread.mode", deep },
        { "mix.master.level", 0.54f } } },

    { "Tidal Organ", "PADS", "P(X3)",
      "Drawbar organ tone through the string-machine ensemble chorus, with MOOD holding a "
      "slow reverb underneath. Sits still and moves at the same time.",
      { { "voice.osc1.mode", organ }, { "voice.osc1.macro.a", 0.60f }, { "voice.osc1.macro.b", 0.45f }, { "voice.osc1.macro.c", 0.52f },
        { "fx.analog.enabled", 1 }, { "fx.delay.enabled", 0 }, { "fx.reverb.enabled", 0 }, { "fx.mood.enabled", 1 },
        { "fx.doom.enabled", 0 }, { "fx.lucy.enabled", 0 }, { "fx.chorus.enabled", 1 }, { "fx.spread.enabled", 0 },
        { "voice.filter2.enabled", 0 },
        { "voice.osc2.enabled", 1 }, { "voice.osc2.mode", sine }, { "voice.osc2.tuning.octave", 1.0f }, { "mix.osc2.level", 0.34f },
        { "voice.filter1.enabled", 1 }, { "voice.filter1.type", lp12 }, { "voice.filter1.cutoff", 4200.0f }, { "voice.filter1.resonance", 0.30f },
        { "voice.amp.attack", 0.12f }, { "voice.amp.decay", 0.60f }, { "voice.amp.sustain", 0.92f }, { "voice.amp.release", 0.85f },
        { "fx.chorus.amount", 0.62f }, { "fx.chorus.mode", ensemble }, { "fx.chorus.rate", 0.18f }, { "fx.chorus.depth", 0.62f },
        { "fx.mood.mix", 0.28f }, { "fx.mood.wet.mode", moodReverb }, { "fx.mood.wet.time", 0.62f }, { "fx.mood.wet.modify", 0.40f },
        { "fx.mood.clock", 0.70f }, { "fx.mood.spread", 0.70f },
        { "mix.master.level", 0.50f } } },

    // =======================================================================
    // PLUCKS
    // =======================================================================

    { "Porcelain", "PLUCKS", "P(X3)",
      "An FM strike into the comb filter - a bell-like pluck ringing through a tuned resonator. "
      "The diffusion delay smears the tails without repeating them.",
      { { "voice.osc1.mode", fm }, { "voice.osc1.macro.a", 0.55f }, { "voice.osc1.macro.b", 0.68f }, { "voice.osc1.macro.c", 0.40f },
        { "fx.analog.enabled", 0 }, { "fx.delay.enabled", 1 }, { "fx.reverb.enabled", 1 }, { "fx.mood.enabled", 0 },
        { "fx.doom.enabled", 0 }, { "fx.lucy.enabled", 0 }, { "fx.chorus.enabled", 0 }, { "fx.spread.enabled", 0 },
        { "voice.filter2.enabled", 0 },
        { "voice.filter1.enabled", 1 }, { "voice.filter1.type", comb },
        { "voice.filter1.comb.tuning", 660.0f }, { "voice.filter1.comb.decay", 1.40f }, { "voice.filter1.comb.damping", 0.35f },
        { "voice.filter1.comb.dispersion", 0.45f }, { "voice.filter1.comb.drive", 0.15f }, { "voice.filter1.comb.mix", 0.68f },
        { "voice.amp.attack", 0.001f }, { "voice.amp.decay", 0.42f }, { "voice.amp.sustain", 0.24f }, { "voice.amp.release", 0.55f },
        { "fx.delay.amount", 0.30f }, { "fx.delay.algorithm", diffusion }, { "fx.delay.time", 0.26f }, { "fx.delay.feedback", 0.34f },
        { "fx.reverb.amount", 0.11f }, { "fx.reverb.algorithm", plate }, { "fx.reverb.decay", 0.40f },
        { "fx.chorus.amount", 0.22f }, { "fx.chorus.mode", dim1 },
        { "mix.master.level", 0.95f } } },

    { "Rain on Copper", "PLUCKS", "P(X3)",
      "A short digital pluck with LUCY set to PACKET REPEAT. Dropped frames are filled with "
      "the last good one, phase advanced - so the glitches smear instead of stuttering.",
      { { "voice.osc1.mode", digital }, { "voice.osc1.macro.a", 0.62f }, { "voice.osc1.macro.b", 0.40f }, { "voice.osc1.macro.c", 0.58f },
        { "fx.analog.enabled", 0 }, { "fx.delay.enabled", 0 }, { "fx.reverb.enabled", 0 }, { "fx.mood.enabled", 0 },
        { "fx.doom.enabled", 0 }, { "fx.lucy.enabled", 1 }, { "fx.chorus.enabled", 0 }, { "fx.spread.enabled", 1 },
        { "voice.filter2.enabled", 0 },
        { "voice.filter1.enabled", 1 }, { "voice.filter1.type", lp24 }, { "voice.filter1.cutoff", 7200.0f }, { "voice.filter1.resonance", 0.68f },
        { "voice.amp.attack", 0.001f }, { "voice.amp.decay", 0.20f }, { "voice.amp.sustain", 0.08f }, { "voice.amp.release", 0.22f },
        { "fx.lucy.global", 0.50f }, { "fx.lucy.packets", packetRepeat }, { "fx.lucy.mode", standard },
        { "fx.lucy.loss", 0.58f }, { "fx.lucy.speed", 0.68f }, { "fx.lucy.spread", 0.85f },
        { "fx.lucy.filter", 0.26f }, { "fx.lucy.freq", 0.66f }, { "fx.lucy.limiter.threshold", 0.68f }, { "fx.lucy.loss.gain", 9.0f },
        { "fx.delay.amount", 0.26f }, { "fx.delay.algorithm", stereoDelay }, { "fx.delay.time", 0.22f }, { "fx.delay.feedback", 0.30f },
        { "fx.reverb.amount", 0.09f }, { "fx.reverb.algorithm", room },
        { "mix.master.level", 0.95f } } },

    { "Music Box", "PLUCKS", "P(X3)",
      "A clean FM bell with a tape delay behind it. Nothing exotic - it is here because a "
      "preset library needs something you can just play.",
      { { "voice.osc1.mode", fm }, { "voice.osc1.macro.a", 0.52f }, { "voice.osc1.macro.b", 0.22f }, { "voice.osc1.macro.c", 0.66f },
        { "fx.analog.enabled", 0 }, { "fx.delay.enabled", 1 }, { "fx.reverb.enabled", 1 }, { "fx.mood.enabled", 0 },
        { "fx.doom.enabled", 0 }, { "fx.lucy.enabled", 0 }, { "fx.chorus.enabled", 0 }, { "fx.spread.enabled", 0 },
        { "voice.filter2.enabled", 0 }, { "global.character.enabled", 1 }, { "global.character.profile", 0 },
        { "voice.filter1.enabled", 1 }, { "voice.filter1.type", lp12 }, { "voice.filter1.cutoff", 8000.0f }, { "voice.filter1.resonance", 0.30f },
        { "voice.amp.attack", 0.001f }, { "voice.amp.decay", 0.55f }, { "voice.amp.sustain", 0.05f }, { "voice.amp.release", 0.70f },
        { "fx.delay.amount", 0.28f }, { "fx.delay.algorithm", tape }, { "fx.delay.time", 0.34f }, { "fx.delay.feedback", 0.32f },
        { "fx.reverb.amount", 0.14f }, { "fx.reverb.algorithm", hall }, { "fx.reverb.size", 0.60f }, { "fx.reverb.decay", 0.45f },
        { "fx.chorus.amount", 0.24f }, { "fx.chorus.mode", dim1 }, { "fx.chorus.rate", 0.20f },
        { "mix.master.level", 0.72f } } },

    { "Lamp Swirl", "PLUCKS", "P(X3)",
      "A bright saw pluck through VIBE in CHORUS: the lamp snaps bright and drifts dark, so the "
      "swirl lunges and settles rather than sweeping evenly. ANALOG adds a little per-voice wander.",
      { { "voice.osc1.mode", saw }, { "voice.osc1.macro.a", 0.35f },
        { "fx.analog.enabled", 1 }, { "fx.vibe.enabled", 1 }, { "fx.delay.enabled", 0 }, { "fx.reverb.enabled", 1 },
        { "fx.mood.enabled", 0 }, { "fx.doom.enabled", 0 }, { "fx.lucy.enabled", 0 }, { "fx.chorus.enabled", 0 },
        { "fx.spread.enabled", 0 },
        { "voice.filter2.enabled", 0 },
        { "voice.filter1.enabled", 1 }, { "voice.filter1.type", lp24 }, { "voice.filter1.cutoff", 3200.0f }, { "voice.filter1.resonance", 0.45f },
        { "voice.amp.attack", 0.002f }, { "voice.amp.decay", 0.55f }, { "voice.amp.sustain", 0.40f }, { "voice.amp.release", 0.45f },
        { "fx.vibe.mode", vibeChorus }, { "fx.vibe.intensity", 0.72f }, { "fx.vibe.speed", 0.42f },
        { "fx.analog.amount", 0.18f }, { "fx.analog.type", vintage },
        { "fx.reverb.amount", 0.05f }, { "fx.reverb.algorithm", room },
        { "mix.master.level", 0.62f } } },

    // =======================================================================
    // EXPERIMENTAL
    // =======================================================================

    { "Bad Signal", "EXPERIMENTAL", "P(X3)",
      "LUCY with PACKET LOSS, JITTER and the gate open. Losses arrive in bursts rather than "
      "evenly, which is why it sounds like a failing connection and not like tremolo.",
      { { "voice.osc1.mode", superSaw }, { "voice.osc1.macro.a", 0.55f }, { "voice.osc1.macro.b", 0.60f },
        { "fx.analog.enabled", 0 }, { "fx.delay.enabled", 0 }, { "fx.reverb.enabled", 0 }, { "fx.mood.enabled", 0 },
        { "fx.doom.enabled", 0 }, { "fx.lucy.enabled", 1 }, { "fx.chorus.enabled", 0 }, { "fx.spread.enabled", 0 },
        { "voice.filter2.enabled", 0 },
        { "voice.filter1.enabled", 1 }, { "voice.filter1.type", lp24 }, { "voice.filter1.cutoff", 5000.0f }, { "voice.filter1.resonance", 0.55f },
        { "voice.amp.attack", 0.02f }, { "voice.amp.decay", 0.40f }, { "voice.amp.sustain", 0.80f }, { "voice.amp.release", 0.40f },
        { "fx.lucy.global", 0.78f }, { "fx.lucy.mode", jitter }, { "fx.lucy.packets", packetLoss },
        { "fx.lucy.loss", 0.72f }, { "fx.lucy.speed", 0.42f },
        { "fx.lucy.gate", 1 }, { "fx.lucy.gate.threshold", 0.30f },
        { "fx.lucy.filter", 0.40f }, { "fx.lucy.freq", 0.55f }, { "fx.lucy.slope", slope96 },
        { "fx.lucy.verb", 0.38f }, { "fx.lucy.decay", 0.55f }, { "fx.lucy.spread", 0.90f },
        { "fx.lucy.limiter.threshold", 0.62f }, { "fx.lucy.loss.gain", 11.0f },
        { "mix.master.level", 0.95f } } },

    { "Splinter Choir", "EXPERIMENTAL", "P(X3)",
      "DOOM's FLIP mode - fourths, fifths and octaves stacked on what you play and spread "
      "across time, so the chord arrives one note at a time.",
      { { "voice.osc1.mode", physical }, { "voice.osc1.macro.a", 0.48f }, { "voice.osc1.macro.b", 0.62f }, { "voice.osc1.macro.c", 0.40f },
        { "fx.analog.enabled", 0 }, { "fx.delay.enabled", 0 }, { "fx.reverb.enabled", 1 }, { "fx.mood.enabled", 0 },
        { "fx.doom.enabled", 1 }, { "fx.lucy.enabled", 0 }, { "fx.chorus.enabled", 0 }, { "fx.spread.enabled", 0 },
        { "voice.filter2.enabled", 0 }, { "global.character.enabled", 1 }, { "global.character.profile", 2 },
        { "voice.filter1.enabled", 1 }, { "voice.filter1.type", lp24 }, { "voice.filter1.cutoff", 5600.0f }, { "voice.filter1.resonance", 0.48f },
        { "voice.amp.attack", 0.05f }, { "voice.amp.decay", 0.70f }, { "voice.amp.sustain", 0.60f }, { "voice.amp.release", 1.10f },
        { "fx.doom.mix", 0.58f }, { "fx.doom.wet.mode", flip }, { "fx.doom.wet.time", 0.42f }, { "fx.doom.wet.modify", 0.88f },
        { "fx.doom.balance", 1.0f }, { "fx.doom.routing", inputOnly }, { "fx.doom.clock", 0.85f },
        { "fx.doom.glue", 0.20f }, { "fx.doom.spread", 0.85f }, { "fx.doom.eq", 0.10f },
        { "fx.reverb.amount", 0.17f }, { "fx.reverb.algorithm", hall }, { "fx.reverb.decay", 0.55f },
        { "mix.master.level", 0.54f } } },

    { "Radio Ghost", "EXPERIMENTAL", "P(X3)",
      "DOOM primed for its micro-looper: play a phrase, then engage LOOPER to catch what you "
      "already played. RADIO scans five loopers with interference between the stations.",
      { { "voice.osc1.mode", isaac }, { "voice.osc1.macro.a", 0.58f }, { "voice.osc1.macro.b", 0.66f }, { "voice.osc1.macro.c", 0.44f },
        { "fx.analog.enabled", 0 }, { "fx.delay.enabled", 1 }, { "fx.reverb.enabled", 0 }, { "fx.mood.enabled", 0 },
        { "fx.doom.enabled", 1 }, { "fx.lucy.enabled", 0 }, { "fx.chorus.enabled", 0 }, { "fx.spread.enabled", 0 },
        { "voice.filter2.enabled", 0 },
        { "voice.filter1.enabled", 1 }, { "voice.filter1.type", lp12 }, { "voice.filter1.cutoff", 4400.0f }, { "voice.filter1.resonance", 0.62f },
        { "voice.amp.attack", 0.02f }, { "voice.amp.decay", 0.55f }, { "voice.amp.sustain", 0.70f }, { "voice.amp.release", 0.60f },
        { "fx.doom.mix", 0.50f }, { "fx.doom.wet.mode", soup }, { "fx.doom.wet.time", 0.55f }, { "fx.doom.wet.modify", 0.50f },
        // The looper is left OFF on purpose. It is always listening, so engaging
        // it captures what you already played - loading a preset with it on
        // would capture the silence before you touched a key.
        { "fx.doom.loop.active", 0 }, { "fx.doom.loop.mode", radio }, { "fx.doom.loop.length", 0.52f },
        { "fx.doom.loop.modify", 0.30f }, { "fx.doom.routing", inputPlusLoop }, { "fx.doom.balance", 0.55f },
        { "fx.doom.clock", 0.42f }, { "fx.doom.glue", 0.28f }, { "fx.doom.spread", 0.70f },
        { "fx.doom.cross", 0.35f }, { "fx.doom.cross.source", 0 },
        { "fx.reverb.amount", 0.11f }, { "fx.reverb.algorithm", cloud },
        { "mix.master.level", 0.52f } } },

    { "Comb Reactor", "EXPERIMENTAL", "P(X3)",
      "The comb filter driven near self-oscillation and fed with ROB, then DOOM's CROSS "
      "modulating pitch and loudness from the signal itself. Unstable on purpose.",
      { { "voice.osc1.mode", rob }, { "voice.osc1.macro.a", 0.76f }, { "voice.osc1.macro.b", 0.68f }, { "voice.osc1.macro.c", 0.82f },
        { "fx.analog.enabled", 1 }, { "fx.delay.enabled", 0 }, { "fx.reverb.enabled", 0 }, { "fx.mood.enabled", 1 },
        { "fx.doom.enabled", 1 }, { "fx.lucy.enabled", 0 }, { "fx.chorus.enabled", 0 }, { "fx.spread.enabled", 0 },
        { "voice.filter2.enabled", 1 },
        { "voice.filter1.enabled", 1 }, { "voice.filter1.type", comb },
        { "voice.filter1.comb.tuning", 220.0f }, { "voice.filter1.comb.decay", 8.0f }, { "voice.filter1.comb.damping", 0.18f },
        { "voice.filter1.comb.dispersion", 0.62f }, { "voice.filter1.comb.drive", 0.55f }, { "voice.filter1.comb.mix", 0.75f },
        { "voice.amp.attack", 0.008f }, { "voice.amp.decay", 0.45f }, { "voice.amp.sustain", 0.55f }, { "voice.amp.release", 0.80f },
        { "fx.doom.mix", 0.42f }, { "fx.doom.wet.mode", relay }, { "fx.doom.wet.time", 0.30f }, { "fx.doom.wet.modify", 0.72f },
        { "fx.doom.balance", 1.0f }, { "fx.doom.cross", 0.78f }, { "fx.doom.cross.source", 1 },
        { "fx.doom.glue", 0.45f }, { "fx.doom.clock", 0.62f }, { "fx.doom.eq", -0.20f }, { "fx.doom.spread", 0.65f },
        { "fx.spread.amount", 0.30f }, { "fx.spread.mode", classic },
        { "mix.master.level", 0.44f } } },

    { "Dimension Drift", "EXPERIMENTAL", "P(X3)",
      "A demonstration of the two spatial effects with nothing else in the way: the stacked "
      "Dimension chorus into SPREAD on WIDE. Mono-compatible at every setting - check it.",
      { { "voice.osc1.mode", wavetable }, { "voice.osc1.macro.a", 0.50f }, { "voice.osc1.macro.b", 0.62f }, { "voice.osc1.macro.c", 0.38f },
        { "fx.analog.enabled", 0 }, { "fx.delay.enabled", 0 }, { "fx.reverb.enabled", 0 }, { "fx.mood.enabled", 0 },
        { "fx.doom.enabled", 0 }, { "fx.lucy.enabled", 0 }, { "fx.chorus.enabled", 1 }, { "fx.spread.enabled", 1 },
        { "voice.filter2.enabled", 0 },
        { "voice.osc2.enabled", 1 }, { "voice.osc2.mode", triangle }, { "voice.osc2.tuning.octave", 1.0f }, { "voice.osc2.tuning.cents", 6.0f },
        { "mix.osc2.level", 0.42f },
        { "voice.filter1.enabled", 1 }, { "voice.filter1.type", lp24 }, { "voice.filter1.cutoff", 6400.0f }, { "voice.filter1.resonance", 0.36f },
        { "voice.amp.attack", 0.28f }, { "voice.amp.decay", 0.80f }, { "voice.amp.sustain", 0.85f }, { "voice.amp.release", 1.40f },
        { "fx.chorus.amount", 0.68f }, { "fx.chorus.mode", dim3plus4 }, { "fx.chorus.rate", 0.26f },
        { "fx.chorus.width", 0.95f }, { "fx.chorus.depth", 0.58f }, { "fx.chorus.character", 0.45f },
        { "fx.spread.amount", 0.70f }, { "fx.spread.mode", wide }, { "fx.spread.width", 0.85f },
        { "fx.spread.depth", 0.60f }, { "fx.spread.high.width", 0.90f },
        { "fx.reverb.amount", 0.08f }, { "fx.reverb.algorithm", plate },
        { "mix.master.level", 0.54f } } },

    };

    useReverbPreset(presets, "Sunken Bell", room, "Small Studio");
    useReverbPreset(presets, "Neon Arterial", plate, "Synth Plate");
    useReverbPreset(presets, "Hollow Siren", room, "Synth Room");
    useReverbPreset(presets, "Vowel Machine", plate, "Vocal-ish Synth");
    useReverbPreset(presets, "Slow Weather", cloud, "Synth Cloud");
    useReverbPreset(presets, "Frozen Transmission", cloud, "Ethereal Pad");
    useReverbPreset(presets, "Porcelain", plate, "Bright Plate");
    useReverbPreset(presets, "Rain on Copper", room, "Wide Room");
    useReverbPreset(presets, "Music Box", hall, "Synth Hall");
    useReverbPreset(presets, "Lamp Swirl", room, "Synth Room");
    useReverbPreset(presets, "Splinter Choir", hall, "Wide Pad");
    useReverbPreset(presets, "Radio Ghost", cloud, "Dark Cloud");
    useReverbPreset(presets, "Dimension Drift", plate, "Long Plate");
    return presets;
}

} // namespace px3::presets
