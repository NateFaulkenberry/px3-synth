#include "TestSupport.h"

#include <algorithm>
#include <map>

// The modulation matrix, exhaustively: every source against every destination,
// through the same graph route a patch cable on the MOD page creates.
//
// Layer 1 (routing). Every pair is either refused - and the only refusal
// allowed is a source patched into its own controls, which would be a cycle -
// or it reaches its destination at the source's live value and full depth,
// moves that destination and no other, and lets go when it is unpatched.
//
// Layer 2 (sound). For every destination, the audio with modulation applied is
// the audio with the knob simply set to the value the modulation reaches. Layer
// 1 proves the routing and the readout; this proves the DSP reads the same
// number. It is run from MACRO 1, which takes the global path, and from ENV 1,
// which takes the per-voice path - the one route family layer 1 cannot read,
// because the global readout leaves per-voice sources out of in-voice
// destinations. The LFOs share the macros' global path, so MACRO 1 stands for
// them there.
namespace px3tests
{
namespace
{
using Processor = PX3SynthAudioProcessor;
constexpr int kSourceCount = Processor::kLfoSourceCount + Processor::kEnvelopeSourceCount + Processor::kMacroCount;
constexpr int kEnv1 = Processor::kLfoSourceCount;
constexpr int kMacro1 = Processor::kLfoSourceCount + Processor::kEnvelopeSourceCount;

bool isLfo(int source) { return source < Processor::kLfoSourceCount; }
bool isEnvelope(int source) { return source >= Processor::kLfoSourceCount && source < kMacro1; }

juce::String ownPrefix(int source)
{
    if (isLfo(source)) { return "mod.lfo" + juce::String(source + 1) + "."; }
    if (isEnvelope(source)) { return "mod.env" + juce::String(source - Processor::kLfoSourceCount + 1) + "."; }
    return "mod.macro" + juce::String(source - kMacro1 + 1) + ".";
}

// Every source held at a steady extreme: a square LFO parked in one half (its
// sign is read back, not assumed), an envelope at full sustain from an instant
// attack, a macro at the top.
void driveSource(Processor& processor, int source)
{
    if (isLfo(source))
    {
        const auto prefix = ownPrefix(source);
        setParam(processor, prefix + "enabled", 1.0f);
        setParam(processor, prefix + "frequency", 0.01f);
        setChoice(processor, prefix + "waveform", 3);   // SQUARE
    }
    else if (isEnvelope(source))
    {
        const auto slot = source - Processor::kLfoSourceCount;
        setParam(processor, ownPrefix(source) + "enabled", 1.0f);
        processor.getEnvelopeAttackParam(slot).setValueNotifyingHost(0.0f);
        processor.getEnvelopeSustainParam(slot).setValueNotifyingHost(1.0f);
    }
    else
    {
        processor.getMacroParam(source - kMacro1).setValueNotifyingHost(1.0f);
    }
}

float liveValue(const Processor& processor, int source)
{
    if (isLfo(source)) { return processor.debugGetLfoCurrentValue(source); }
    if (isEnvelope(source)) { return processor.debugGetEnvelopeCurrentValue(source - Processor::kLfoSourceCount); }
    return processor.getMacroParam(source - kMacro1).get();
}

void processBlocks(Processor& processor, int blocks, bool noteOnFirst)
{
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        juce::MidiBuffer midi;
        if (noteOnFirst && b == 0) { midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0); }
        processor.processBlock(buffer, midi);
    }
}

std::vector<juce::RangedAudioParameter*> graphDestinations(Processor& processor)
{
    std::vector<juce::RangedAudioParameter*> out;
    for (auto* parameter : processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
        if (ranged != nullptr && processor.isGraphDestination(ranged->getParameterID())) { out.push_back(ranged); }
    }
    return out;
}

juce::RangedAudioParameter* byId(Processor& processor, const juce::String& id)
{
    for (auto* parameter : processor.getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter); ranged != nullptr && ranged->getParameterID() == id)
        {
            return ranged;
        }
    }
    return nullptr;
}

juce::String first(const juce::StringArray& list)
{
    if (list.isEmpty()) { return "none"; }
    juce::String text = juce::String(list.size()) + ": " + list[0];
    for (int i = 1; i < juce::jmin(4, list.size()); ++i) { text << ", " << list[i]; }
    return text;
}

// ---- layer 2 fixtures ------------------------------------------------------------

// Three patches, tried in order until the destination is audible (the middle one
// also gets the destination's context - see applyContext): the plain
// sine voice; the whole voice (every oscillator on SAW, the sub, both filters)
// with no time-based effects, so nothing smears one moment of a note across
// the window; and everything, effects and inserts included.
enum class Patch { plain, voice, everything };

void setNormalised(Processor& processor, const juce::String& id, float value)
{
    if (auto* p = findParameter(processor, id)) { p->setValueNotifyingHost(value); }
}

// Patch a source into filter 1's cutoff on slot 1 (the test's own route is
// slot 0), so a control of that source's is heard through the filter.
void routeIntoCutoff(Processor& processor, int source)
{
    juce::String error;
    processor.setGraphRoute(1, { source, "voice.filter1.cutoff", px3::synth::ModulationPolarity::native,
                                 px3::synth::ModulationCurve::linear }, error);
    auto& depth = processor.getGraphRouteDepthParam(1);
    depth.setValueNotifyingHost(depth.convertTo0to1(1.0f));
    setParam(processor, "voice.filter1.enabled", 1.0f);
    setChoice(processor, "voice.filter1.type", 1);   // LP24
    setNormalised(processor, "voice.filter1.cutoff", 0.25f);
}

// What a destination needs around it before it can be heard at all: the mode
// that uses it, the switch that brings it in, or a route that makes its source
// audible. Applied to the module patch, so every destination is tested in the
// one setting where it does something.
void applyContext(Processor& processor, const juce::String& id)
{
    for (int osc = 1; osc <= 3; ++osc)
    {
        const auto prefix = "voice.osc" + juce::String(osc) + ".";
        if (! id.startsWith(prefix)) { continue; }
        if (id.contains(".harmonics.")) { setChoice(processor, prefix + "mode", 9); }           // ADDITIVE
        else if (id.contains(".macro.")) { setChoice(processor, prefix + "mode", 14); }         // DIGITAL: three macros
        else if (id.contains(".wavetable.")) { setChoice(processor, prefix + "mode", 8); }      // WAVETABLE
    }
    for (int filter = 1; filter <= 2; ++filter)
    {
        const auto prefix = "voice.filter" + juce::String(filter) + ".";
        setChoice(processor, prefix + "type", id.startsWith(prefix + "comb.") ? 7 : 0);       // COMB or LP12
        if (id == prefix + "keytrack.key") { setNormalised(processor, prefix + "keytrack", 1.0f); }
    }
    if (id.startsWith("voice.filters.routing."))
    {
        setChoice(processor, "voice.filters.routing", 1);   // PARALLEL
        setNormalised(processor, "voice.filter1.cutoff", 0.15f);
        setNormalised(processor, "voice.filter2.cutoff", 0.9f);
    }
    for (int i = 0; i < Processor::kEnvelopeSourceCount; ++i)
    {
        const auto prefix = "mod.env" + juce::String(i + 1) + ".";
        if (! id.startsWith(prefix)) { continue; }
        setParam(processor, prefix + "enabled", 1.0f);
        routeIntoCutoff(processor, Processor::kLfoSourceCount + i);
    }
    for (int i = 0; i < Processor::kLfoSourceCount; ++i)
    {
        const auto prefix = "mod.lfo" + juce::String(i + 1) + ".";
        if (! id.startsWith(prefix)) { continue; }
        setParam(processor, prefix + "enabled", 1.0f);
        setChoice(processor, prefix + "waveform", id.contains("ramp") ? 4 : 0);   // RAMP UP or SINE
        routeIntoCutoff(processor, i);
    }
    for (const auto* bus : { "mix.dry.insert.", "mix.fx.insert." })
    {
        if (! id.startsWith(bus) || ! id.contains(".eq.")) { continue; }
        setNormalised(processor, juce::String(bus) + "eq.enabled", 1.0f);
        const auto band = id.getLastCharacters(1);
        setNormalised(processor, juce::String(bus) + "eq.gain." + band, 0.95f);
    }
    if (id.startsWith("mix.fx."))
    {
        setParam(processor, "fx.reverb.enabled", 1.0f);
        setNormalised(processor, "fx.reverb.amount", 1.0f);
        for (const auto* src : { "sub", "osc1", "osc2", "osc3" }) { setParam(processor, juce::String("mix.") + src + ".send.fx", 0.8f); }
    }
    if (id.startsWith("fx."))
    {
        const auto module = id.upToFirstOccurrenceOf(".", true, false)
                            + id.fromFirstOccurrenceOf(".", false, false).upToFirstOccurrenceOf(".", false, false);
        setNormalised(processor, module + ".enabled", 1.0f);
        for (const auto* wet : { ".amount", ".mix", ".blend", ".global", ".intensity" })
        {
            if (id != module + wet) { setNormalised(processor, module + wet, 1.0f); }
        }
        for (const auto* src : { "sub", "osc1", "osc2", "osc3" }) { setParam(processor, juce::String("mix.") + src + ".send.fx", 0.8f); }
        setChoice(processor, "voice.osc1.mode", 1);   // SAW
        if (id == "fx.delay.wobble" || id.startsWith("fx.delay.tape.")) { setChoice(processor, "fx.delay.algorithm", 1); }  // Tape
        if (id == "fx.delay.mod.depth") { setChoice(processor, "fx.delay.algorithm", 5); }                                 // Modulated
        if (id.startsWith("fx.reverb.cloud.") || id == "fx.reverb.shimmer") { setChoice(processor, "fx.reverb.algorithm", 3); }  // CLOUD
        if (id.startsWith("fx.doom.loop.")) { setNormalised(processor, "fx.doom.loop.active", 1.0f); }
        if (id.startsWith("fx.doom.wet.")) { setNormalised(processor, "fx.doom.wet.active", 1.0f); }
        if (id == "fx.lucy.gate.threshold") { setNormalised(processor, "fx.lucy.gate", 1.0f); }
        if (id == "fx.lucy.freezer") { setChoice(processor, "fx.lucy.freeze", 1); }
    }
}

// The module that owns the destination, sounding alone: OSC N's controls play
// OSC N, the sub's the sub, the filters' a saw through both filters. Alone,
// because summed oscillators interfere - a route that reaches its value a
// moment after a knob would shift one oscillator's phase against another's
// and change how their shared harmonics add, with nothing wrong.
void makeVoicePatch(Processor& processor, const juce::String& id)
{
    makePlainPatch(processor);
    for (int osc = 1; osc <= 3; ++osc)
    {
        const auto prefix = "voice.osc" + juce::String(osc);
        const auto owns = id.startsWith(prefix + ".");
        setParam(processor, prefix + ".enabled", owns || (osc == 1 && ! id.startsWith("voice.osc") && ! id.startsWith("voice.sub")) ? 1.0f : 0.0f);
        setChoice(processor, prefix + ".mode", 1);   // SAW: harmonics for a filter to act on
    }
    setParam(processor, "voice.sub.enabled", id.startsWith("voice.sub.") ? 1.0f : 0.0f);
    // Only the filter that owns the destination (both, for the shared routing
    // controls): in series the other would already have removed what this
    // one's settings change.
    setParam(processor, "voice.filter1.enabled", id.startsWith("voice.filter2.") ? 0.0f : 1.0f);
    setParam(processor, "voice.filter2.enabled", id.startsWith("voice.filter1.") ? 0.0f : 1.0f);
    applyContext(processor, id);
}

void makeFullPatch(Processor& processor)
{
    makePlainPatch(processor);
    for (const auto* osc : { "voice.osc1", "voice.osc2", "voice.osc3" })
    {
        setParam(processor, juce::String(osc) + ".enabled", 1.0f);
        setChoice(processor, juce::String(osc) + ".mode", 1);   // SAW: harmonics for every filter to act on
    }
    setParam(processor, "voice.sub.enabled", 1.0f);
    setParam(processor, "voice.filter1.enabled", 1.0f);
    setParam(processor, "voice.filter2.enabled", 1.0f);
    for (const auto* fx : { "fx.vibe", "fx.delay", "fx.reverb", "fx.mood", "fx.doom", "fx.lucy", "fx.chorus",
                            "fx.spread", "fx.distortion" })
    {
        if (auto* p = findParameter(processor, juce::String(fx) + ".enabled")) { p->setValueNotifyingHost(1.0f); }
    }
    for (const auto* id : { "sub", "osc1", "osc2", "osc3" })
    {
        setParam(processor, juce::String("mix.") + id + ".send.fx", 0.6f);
    }
    for (const auto* bus : { "mix.dry", "mix.fx" })
    {
        for (const auto* insert : { ".insert.eq.enabled", ".insert.comp.enabled" })
        {
            if (auto* p = findParameter(processor, juce::String(bus) + insert)) { p->setValueNotifyingHost(1.0f); }
        }
    }
}

// A silent lead-in before the note, so every smoother has settled on its value
// before there is sound for a difference to echo through; then a held note
// and its release.
constexpr int kNoteOn = 8192;
constexpr int kNoteOff = kNoteOn + 8192;
constexpr int kRenderSamples = kNoteOff + 4096;
constexpr int kCompareFrom = kNoteOn + 2048;   // past the envelope's attack

// knobAtNoteOn: the knob sits at the base through the lead-in and moves to
// knobOverride at note-on - what an instant-attack envelope does to it.
int gStepDelayBlocks = 1;

Capture renderWith(Patch patch, const juce::String& id, float base, int routeSource, float knobOverride,
                   bool knobAtNoteOn = false, bool* refused = nullptr)
{
    Processor processor;
    if (patch == Patch::plain) { makePlainPatch(processor); }
    else if (patch == Patch::voice) { makeVoicePatch(processor, id); }
    else { makeFullPatch(processor); }
    if (routeSource >= 0) { driveSource(processor, routeSource); }
    if (auto* p = byId(processor, id)) { p->setValueNotifyingHost(knobOverride >= 0.0f ? knobOverride : base); }
    if (routeSource >= 0)
    {
        juce::String error;
        const auto accepted = processor.setGraphRoute(0, { routeSource, id, px3::synth::ModulationPolarity::native,
                                                           px3::synth::ModulationCurve::linear }, error);
        if (refused != nullptr) { *refused = ! accepted; }
        auto& depth = processor.getGraphRouteDepthParam(0);
        depth.setValueNotifyingHost(depth.convertTo0to1(1.0f));
    }
    auto* knob = byId(processor, id);
    if (knobAtNoteOn && knob != nullptr) { knob->setValueNotifyingHost(base); }
    return render(processor, kRenderSamples, { { kNoteOn, true, 57, 0.9f }, { kNoteOff, false, 57, 0.0f } },
                  [&](int block)
                  {
                      if (knobAtNoteOn && knob != nullptr && block * kBlockSize == kNoteOn + kBlockSize * gStepDelayBlocks)
                      {
                          knob->setValueNotifyingHost(knobOverride);
                      }
                  });
}

// How differently two renders SOUND between `from` and `to`: the RMS
// difference of their short-time magnitude spectra, per channel. Magnitudes,
// not samples, because a route that reaches its value a few milliseconds
// later than a knob leaves the oscillator's phase shifted for the rest of the
// note - identical pitch, level and tone, but a large sample difference.
double distance(const Capture& a, const Capture& b, int from = kCompareFrom, int to = kRenderSamples)
{
    constexpr int order = 11;
    constexpr int size = 1 << order;
    constexpr int hop = size / 2;
    static juce::dsp::FFT fft(order);
    const auto last = juce::jmin(to, static_cast<int>(juce::jmin(a.left.size(), b.left.size())));
    std::vector<float> fa(static_cast<std::size_t>(size) * 2), fb(static_cast<std::size_t>(size) * 2);
    double e = 0.0;
    int bins = 0;
    for (int start = from; start + size <= last; start += hop)
    {
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto& xa = channel == 0 ? a.left : a.right;
            const auto& xb = channel == 0 ? b.left : b.right;
            for (int i = 0; i < size; ++i)
            {
                const auto w = static_cast<float>(0.5 - 0.5 * std::cos(juce::MathConstants<double>::twoPi * i / size));
                fa[static_cast<std::size_t>(i)] = xa[static_cast<std::size_t>(start + i)] * w;
                fb[static_cast<std::size_t>(i)] = xb[static_cast<std::size_t>(start + i)] * w;
            }
            std::fill(fa.begin() + size, fa.end(), 0.0f);
            std::fill(fb.begin() + size, fb.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform(fa.data());
            fft.performFrequencyOnlyForwardTransform(fb.data());
            for (int k = 0; k < size / 2; ++k)
            {
                const auto d = static_cast<double>(fa[static_cast<std::size_t>(k)]) - fb[static_cast<std::size_t>(k)];
                e += d * d;
                ++bins;
            }
        }
    }
    return bins > 0 ? std::sqrt(e / bins) / size : 0.0;
}

// The value a full-depth route from a source at +1 takes this destination to,
// from this base - asked of the processor rather than re-derived here.
float reachedFrom(const juce::String& id, float base)
{
    Processor processor;
    makePlainPatch(processor);
    driveSource(processor, kMacro1);
    auto* p = byId(processor, id);
    if (p == nullptr) { return -1.0f; }
    p->setValueNotifyingHost(base);
    juce::String error;
    processor.setGraphRoute(0, { kMacro1, id, px3::synth::ModulationPolarity::native,
                                 px3::synth::ModulationCurve::linear }, error);
    auto& depth = processor.getGraphRouteDepthParam(0);
    depth.setValueNotifyingHost(depth.convertTo0to1(1.0f));
    return processor.getModulatedNormalisedValue(*p);
}
} // namespace

void testModulationMatrix()
{
    suite("MODULATION MATRIX");

    // ---- layer 1: every source x every destination --------------------------------
    {
        Processor processor;
        makePlainPatch(processor);
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);
        for (int s = 0; s < kSourceCount; ++s) { driveSource(processor, s); }
        processBlocks(processor, 4, true);   // envelopes up and held

        juce::StringArray undriven;
        for (int s = 0; s < kSourceCount; ++s)
        {
            if (std::abs(std::abs(liveValue(processor, s)) - 1.0f) > 0.02f)
            {
                undriven.add(Processor::graphSourceName(s) + " at " + fmt(liveValue(processor, s), 3));
            }
        }
        check("ModMatrix_EverySourceIsDrivenToAnExtreme", undriven.isEmpty(), "not at +/-1: " + first(undriven));

        const auto destinations = graphDestinations(processor);
        auto& depth = processor.getGraphRouteDepthParam(0);
        depth.setValueNotifyingHost(depth.convertTo0to1(1.0f));

        int routed = 0, refused = 0, voiceLocal = 0;
        juce::StringArray wrongReach, leaked, stuck, badRefusal;
        for (int s = 0; s < kSourceCount; ++s)
        {
            const auto name = Processor::graphSourceName(s);
            for (auto* destination : destinations)
            {
                const auto id = destination->getParameterID();
                const auto original = destination->getValue();
                destination->setValueNotifyingHost(0.5f);

                juce::String error;
                const auto accepted = processor.setGraphRoute(
                    0, { s, id, px3::synth::ModulationPolarity::native, px3::synth::ModulationCurve::linear }, error);
                if (! accepted)
                {
                    ++refused;
                    if (! (error.containsIgnoreCase("cycle") && id.startsWith(ownPrefix(s))))
                    {
                        badRefusal.add(name + ">" + id + " (" + error + ")");
                    }
                    destination->setValueNotifyingHost(original);
                    continue;
                }
                ++routed;
                processBlocks(processor, 1, false);

                // An envelope into an in-voice destination is applied by each
                // voice, and the global readout leaves it out: layer 2 hears it.
                const auto perVoice = isEnvelope(s) && processor.isVoiceLocalDestination(*destination);
                voiceLocal += perVoice ? 1 : 0;
                const auto expected = perVoice ? 0.5f : 0.5f + 0.5f * liveValue(processor, s);
                const auto reached = processor.getUnclampedModulatedNormalisedValue(*destination);
                if (std::abs(reached - expected) > 0.02f)
                {
                    wrongReach.add(name + ">" + id + " " + fmt(reached, 3) + " (expected " + fmt(expected, 3) + ")");
                }
                for (auto* other : destinations)
                {
                    if (other != destination && processor.isParameterModulated(other->getParameterID()))
                    {
                        leaked.add(name + ">" + id + " also moved " + other->getParameterID());
                        break;
                    }
                }

                processor.setGraphRoute(0, {}, error);   // unpatch
                processBlocks(processor, 1, false);
                if (processor.isParameterModulated(id)) { stuck.add(name + ">" + id); }
                destination->setValueNotifyingHost(original);
            }
        }

        std::printf("  ..    %d sources x %d destinations: %d routed (%d per-voice, heard in layer 2), %d refused as cycles\n",
                    kSourceCount, static_cast<int>(destinations.size()), routed, voiceLocal, refused);

        check("ModMatrix_EveryRouteReachesItsDestinationAtFullDepth", routed > 2000 && wrongReach.isEmpty(),
              juce::String(routed) + " routes; off target " + first(wrongReach));
        check("ModMatrix_ARouteMovesOnlyItsOwnDestination", leaked.isEmpty(), "leaks " + first(leaked));
        check("ModMatrix_UnpatchingReleasesTheDestination", stuck.isEmpty(), "still modulated " + first(stuck));
        check("ModMatrix_OnlyASourcesOwnControlsAreRefused", badRefusal.isEmpty(), "other refusals " + first(badRefusal));
    }

    // ---- PITCH MOD: the modulation-only pitch targets ----------------------------------
    // Their knobs are deliberately inert - pitch moves only by what is routed in,
    // around the centre - so "sounds like the knob" does not apply. What does: a
    // half-depth route at +1 is +12 semitones, an octave, through either path.
    {
        juce::StringArray wrong;
        for (const auto* pid : { "voice.osc1.pitch.mod", "voice.osc2.pitch.mod", "voice.osc3.pitch.mod", "voice.sub.pitch.mod" })
        {
            const juce::String id(pid);
            for (const int source : { kMacro1, kEnv1 })
            {
                const auto pitch = [&](bool routed)
                {
                    Processor processor;
                    makePlainPatch(processor);
                    for (int osc = 1; osc <= 3; ++osc)
                    {
                        setParam(processor, "voice.osc" + juce::String(osc) + ".enabled",
                                 id.startsWith("voice.osc" + juce::String(osc) + ".") ? 1.0f : 0.0f);
                    }
                    setParam(processor, "voice.sub.enabled", id.startsWith("voice.sub.") ? 1.0f : 0.0f);
                    setChoice(processor, "voice.sub.waveform", 0);   // SINE
                    setParam(processor, "voice.sub.tuning.octave", 0.0f);
                    if (routed)
                    {
                        driveSource(processor, source);
                        juce::String error;
                        processor.setGraphRoute(0, { source, id, px3::synth::ModulationPolarity::native,
                                                     px3::synth::ModulationCurve::linear }, error);
                        auto& depth = processor.getGraphRouteDepthParam(0);
                        depth.setValueNotifyingHost(depth.convertTo0to1(0.5f));
                    }
                    const auto capture = render(processor, kRenderSamples, { { kNoteOn, true, 57, 0.9f }, { kNoteOff, false, 57, 0.0f } });
                    return estimateFrequency(capture.left, kCompareFrom, kNoteOff, 20.0, 4000.0);
                };
                const auto ratio = pitch(true) / juce::jmax(1.0, pitch(false));
                if (std::abs(ratio - 2.0) > 0.02)
                {
                    wrong.add(id + " via " + Processor::graphSourceName(source) + " x" + fmt(ratio, 3));
                }
            }
        }
        check("ModMatrix_PitchModRoutesMoveThePitchByTheirDepth", wrong.isEmpty(),
              wrong.isEmpty() ? juce::String("4 targets x MACRO 1 and ENV 1: half depth is exactly an octave")
                              : "off: " + first(wrong));
    }

    // ---- layer 2: the sound follows the modulation ----------------------------------
    {
        std::vector<juce::String> ids;
        {
            Processor probe;
            for (auto* d : graphDestinations(probe)) { ids.push_back(d->getParameterID()); }
        }

        constexpr float base = 0.25f;
        int audible = 0, globalOk = 0, voiceOk = 0;
        juce::StringArray globalWrong, voiceWrong, silent;
        for (const auto& id : ids)
        {
            const auto target = reachedFrom(id, base);
            if (target < 0.0f || std::abs(target - base) < 0.05f) { silent.add(id + " (no travel)"); continue; }

            // The knob at the modulated value, twice: the second render is the
            // noise floor two identical setups differ by.
            auto full = Patch::plain;
            auto knob = renderWith(full, id, base, -1, target);
            auto plain = renderWith(full, id, base, -1, -1.0f);
            for (const auto next : { Patch::voice, Patch::everything })
            {
                if (distance(knob, plain) >= 1.0e-5) { break; }
                full = next;
                knob = renderWith(full, id, base, -1, target);
                plain = renderWith(full, id, base, -1, -1.0f);
            }
            const auto knobEffect = distance(knob, plain);
            const auto floor = distance(knob, renderWith(full, id, base, -1, target));
            if (knobEffect < juce::jmax(1.0e-5, floor * 10.0))
            {
                silent.add(id);
               
                continue;
            }
            ++audible;

            // Within a tenth of what the knob itself changes, above the floor.
            const auto tolerance = juce::jmax(knobEffect * 0.1, floor * 4.0);
            const auto viaMacro = distance(renderWith(full, id, base, kMacro1, -1.0f), knob);
            if (viaMacro <= tolerance) { ++globalOk; }
            else { globalWrong.add(id + " " + fmt(viaMacro / knobEffect, 2) + "x"); }

            // Against a knob that moves when the envelope's value arrives, over
            // the held note, measured against the knob's own effect there. The
            // envelope reaches a destination in the block its note starts (the
            // FX, after the voices) or one block later (anything set before the
            // voices render - their own controls, the LFOs - takes the value the
            // voices read back at the end of the last block). At most one block:
            // the closer of the two references is taken, and any more lag fails.
            gStepDelayBlocks = 0;
            const auto steppedNow = renderWith(full, id, base, -1, target, true);
            gStepDelayBlocks = 1;
            const auto steppedNext = renderWith(full, id, base, -1, target, true);
            const auto heldKnobEffect = distance(steppedNow, plain, kCompareFrom, kNoteOff);
            const auto heldTolerance = juce::jmax(heldKnobEffect * 0.1,
                                                  distance(steppedNow, renderWith(full, id, base, -1, target, true), kCompareFrom, kNoteOff) * 4.0);
            auto cycle = false;
            const auto viaEnvelopeRender = renderWith(full, id, base, kEnv1, -1.0f, false, &cycle);
            const auto viaEnvelope = juce::jmin(distance(viaEnvelopeRender, steppedNow, kCompareFrom, kNoteOff),
                                                distance(viaEnvelopeRender, steppedNext, kCompareFrom, kNoteOff));
            if (cycle) { ++voiceOk; }   // ENV 1's own controls: refused as a cycle, checked in layer 1
            else if (viaEnvelope <= heldTolerance) { ++voiceOk; }
            else { voiceWrong.add(id + " " + fmt(viaEnvelope / juce::jmax(1.0e-12, heldKnobEffect), 2) + "x"); }
        }

        std::printf("  ..    %d destinations: %d audible, %d not audible in any patch (%s)\n",
                    static_cast<int>(ids.size()), audible, silent.size(), silent.joinIntoString(", ").toRawUTF8());
        // 189 of 228 are audible in a 0.4 s render; the rest (long FX tails, the
        // envelopes' later stages, pitch mod) are listed above. The floor keeps a
        // lost context from quietly shrinking what is checked.
        check("ModMatrix_MostDestinationsAreHeard", audible >= 185, juce::String(audible) + " audible of " + juce::String(static_cast<int>(ids.size())));
        check("ModMatrix_MacroModulationSoundsLikeTurningTheKnob", audible > 100 && globalWrong.isEmpty(),
              juce::String(globalOk) + " of " + juce::String(audible) + " match; differ " + first(globalWrong));
        check("ModMatrix_EnvelopeModulationSoundsLikeTurningTheKnob", audible > 100 && voiceWrong.isEmpty(),
              juce::String(voiceOk) + " of " + juce::String(audible) + " match; differ " + first(voiceWrong));
    }
}
} // namespace px3tests
