#include "TestSupport.h"

#include "../../tools/FxSweep/FxSweep.h"

// testFxSweep: every control of every effect, swept with that effect on and
// everything else off, through the processor directly (the standalone path).
// The same spec runs through the built AU / VST3 in
// tools/FxSweep/PluginHostSweep, so the two paths can be compared row by row.

namespace px3tests
{
namespace
{
struct SweepPlayHead : juce::AudioPlayHead
{
    juce::int64 samples { 0 };
    double rate { 48000.0 };
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm(px3::fxsweep::kBpm);
        info.setTimeSignature(TimeSignature { 4, 4 });
        info.setIsPlaying(true);
        info.setTimeInSamples(samples);
        info.setTimeInSeconds(static_cast<double>(samples) / rate);
        info.setPpqPosition(static_cast<double>(samples) / rate * px3::fxsweep::kBpm / 60.0);
        return info;
    }
};

std::map<juce::String, juce::AudioProcessorParameter*> byName(juce::AudioProcessor& p)
{
    std::map<juce::String, juce::AudioProcessorParameter*> m;
    for (auto* param : p.getParameters()) { m[param->getName(100)] = param; }
    return m;
}

px3::fxsweep::Audio renderProcessor(const px3::fxsweep::Settings& settings, const px3::fxsweep::Ramp* ramp,
                                    int blockSize = kBlockSize, const px3::FxOrder* order = nullptr,
                                    juce::StringArray* missing = nullptr)
{
    PX3SynthAudioProcessor processor;
    auto params = byName(processor);
    std::vector<std::pair<px3::fxsweep::Setting, bool>> timed;
    for (const auto& s : settings)
    {
        const auto it = params.find(s.name);
        if (it == params.end()) { if (missing != nullptr) { missing->addIfNotAlreadyThere(s.name); } continue; }
        if (s.atSeconds < 0.0) { it->second->setValueNotifyingHost(s.value); }
        else { timed.push_back({ s, false }); }
    }
    if (order != nullptr) { processor.setFxProcessingOrder(*order); }
    SweepPlayHead playHead;
    playHead.rate = kSampleRate;
    processor.setPlayHead(&playHead);
    processor.setPlayConfigDetails(0, 2, kSampleRate, blockSize);
    processor.prepareToPlay(kSampleRate, blockSize);

    px3::fxsweep::Audio out;
    out.sampleRate = kSampleRate;
    const auto total = static_cast<int>(px3::fxsweep::kSeconds * kSampleRate);
    const auto notes = px3::fxsweep::material();
    juce::AudioProcessorParameter* rampParam = nullptr;
    if (ramp != nullptr)
    {
        if (const auto it = params.find(ramp->name); it != params.end()) { rampParam = it->second; }
    }
    juce::AudioBuffer<float> buffer(2, blockSize);
    for (int pos = 0; pos < total; pos += blockSize)
    {
        const auto n = juce::jmin(blockSize, total - pos);
        buffer.setSize(2, n, false, false, true);
        buffer.clear();
        juce::MidiBuffer midi;
        for (const auto& note : notes)
        {
            const auto on = static_cast<int>(note.on * kSampleRate);
            const auto off = static_cast<int>(note.off * kSampleRate);
            if (on >= pos && on < pos + n) { midi.addEvent(juce::MidiMessage::noteOn(1, note.note, note.velocity), on - pos); }
            if (off >= pos && off < pos + n) { midi.addEvent(juce::MidiMessage::noteOff(1, note.note), off - pos); }
        }
        for (auto& entry : timed)
        {
            auto& [setting, applied] = entry;
            if (! applied && static_cast<double>(pos) / kSampleRate >= setting.atSeconds)
            {
                params[setting.name]->setValueNotifyingHost(setting.value);
                applied = true;
            }
        }
        if (rampParam != nullptr)
        {
            const auto t = static_cast<double>(pos) / kSampleRate;
            const auto f = juce::jlimit(0.0, 1.0, (t - ramp->fromSeconds) / (ramp->toSeconds - ramp->fromSeconds));
            rampParam->setValueNotifyingHost(ramp->from + static_cast<float>(f) * (ramp->to - ramp->from));
        }
        playHead.samples = pos;
        processor.processBlock(buffer, midi);
        for (int i = 0; i < n; ++i)
        {
            out.left.push_back(buffer.getSample(0, i));
            out.right.push_back(buffer.getSample(1, i));
        }
    }
    processor.setPlayHead(nullptr);
    return out;
}
} // namespace

void testFxSweep()
{
    suite("FX SWEEP (standalone path)");
    using namespace px3::fxsweep;

    std::vector<ParameterInfo> params;
    {
        PX3SynthAudioProcessor probe;
        for (auto* p : probe.getParameters())
        {
            params.push_back({ p->getName(100), p->getDefaultValue(), p->getNumSteps(), {} });
        }
    }

    juce::StringArray missing;
    const Renderer renderer = [&missing](const Settings& s, const Ramp* r) { return renderProcessor(s, r, kBlockSize, nullptr, &missing); };
    const auto results = run(params, renderer, [](const juce::String& line)
    {
        if (std::getenv("PX3_FXSWEEP_VERBOSE") != nullptr) { std::printf("  ..    %s\n", line.toRawUTF8()); }
    });

    if (const auto* path = std::getenv("PX3_FXSWEEP_TSV"))
    {
        juce::File(juce::String(path)).replaceWithText(toTsv(results));
    }

    check("FxSweep_EverySettingNamedExists", missing.isEmpty(), missing.isEmpty() ? "" : missing.joinIntoString(", "));

    const auto known = knownInert();
    auto controls = 0, values = 0;
    for (const auto& section : results)
    {
        juce::StringArray nan, clip, dead, silences, clicks, knownDead;
        for (const auto& c : section.controls)
        {
            ++controls;
            values += static_cast<int>(c.values.size());
            if (c.flags.contains("NAN")) { nan.add(c.name); }
            if (c.flags.contains("CLIP")) { clip.add(c.name); }
            if (c.flags.contains("DEAD"))
            {
                if (known.count(c.name) > 0) { knownDead.add(c.name); }
                else { dead.add(c.name + " (" + juce::String(c.maxVsDefaultDb, 1) + " dB)"); }
            }
            if (c.flags.contains("SILENCES")) { silences.add(c.name); }
            if (c.flags.contains("CLICK"))
            {
                clicks.add(c.name + " (" + juce::String(c.rampStep, 3) + " vs " + juce::String(c.staticStep, 3) + ")");
            }
        }
        const auto prefix = "FxSweep_" + section.label.replaceCharacter(' ', '_') + "_";
        check((prefix + "SwitchingItOnChangesTheOutput").toRawUTF8(), section.onVsOffDb > -60.0,
              juce::String(section.onVsOffDb, 1) + " dB");
        check((prefix + "NothingIsNonFiniteOrClips").toRawUTF8(), nan.isEmpty() && clip.isEmpty(),
              (nan.isEmpty() ? juce::String() : "NaN: " + nan.joinIntoString(", ") + " ")
                  + (clip.isEmpty() ? juce::String() : "clip: " + clip.joinIntoString(", ")));
        check((prefix + "EveryControlMovesTheOutput").toRawUTF8(), dead.isEmpty(),
              juce::String(static_cast<int>(section.controls.size())) + " controls"
                  + (dead.isEmpty() ? juce::String() : "; dead: " + dead.joinIntoString(", "))
                  + (knownDead.isEmpty() ? juce::String() : "; known dead: " + knownDead.joinIntoString(", ")));
        check((prefix + "NoControlSilencesTheSynth").toRawUTF8(), silences.isEmpty(), silences.joinIntoString(", "));
        check((prefix + "SweepingAControlDoesNotClick").toRawUTF8(), clicks.isEmpty(), clicks.joinIntoString(", "));
    }
    std::printf("  ..    %d controls, %d settings swept\n", controls, values);

    // Every reverb card preset renders, differs from the reverb off, and from
    // the other presets of its type.
    {
        const auto offAudio = renderProcessor(join(everythingOff(), { { "Reverb", 0.5f } }), nullptr);
        juce::StringArray bad;
        std::vector<std::pair<juce::String, Audio>> rendered;
        for (const auto& [name, settings] : reverbPresetSettings())
        {
            const auto audio = renderProcessor(join(join(everythingOff(), { { "Reverb Enabled", 1.0f }, { "Reverb", 0.5f } }), settings),
                                               nullptr);
            if (! finite(audio) || peak(audio) >= 1.0 || differenceDb(offAudio, audio) < -40.0)
            {
                bad.add(name + " (" + juce::String(differenceDb(offAudio, audio), 1) + " dB vs off, peak " + juce::String(peak(audio), 3) + ")");
            }
            for (const auto& [other, otherAudio] : rendered)
            {
                if (other.upToFirstOccurrenceOf("/", false, false) == name.upToFirstOccurrenceOf("/", false, false)
                    && differenceDb(otherAudio, audio) < -60.0)
                {
                    bad.add(name + " = " + other);
                }
            }
            rendered.push_back({ name, audio });
        }
        check("FxSweep_EveryReverbPresetIsHeardAndDistinct", bad.isEmpty(),
              juce::String(static_cast<int>(rendered.size())) + " presets" + (bad.isEmpty() ? juce::String() : "; " + bad.joinIntoString(", ")));
    }

    // The send chain in a few orders: every order renders, and order matters.
    {
        auto everything = everythingOff();
        for (const auto& s : Settings { { "Drive Enabled", 1.0f }, { "Drive Mix", 1.0f }, { "Chorus Enabled", 1.0f }, { "Chorus Amount", 0.8f },
                                        { "Doom Enabled", 1.0f }, { "Doom Mix", 0.6f }, { "Delay Enabled", 1.0f }, { "Delay Amount", 0.5f },
                                        { "Mood Enabled", 1.0f }, { "Reverb Enabled", 1.0f }, { "Reverb", 0.5f } })
        {
            everything.push_back(s);
        }
        const px3::FxOrder standard = px3::kDefaultFxOrder;
        px3::FxOrder reversed = standard;
        std::reverse(reversed.begin() + 2, reversed.begin() + 8);   // the six send stages
        px3::FxOrder reverbFirst = standard;
        std::rotate(reverbFirst.begin() + 2, reverbFirst.begin() + 7, reverbFirst.begin() + 8);
        const auto a = renderProcessor(everything, nullptr, kBlockSize, &standard);
        const auto b = renderProcessor(everything, nullptr, kBlockSize, &reversed);
        const auto c = renderProcessor(everything, nullptr, kBlockSize, &reverbFirst);
        const auto ok = finite(a) && finite(b) && finite(c) && peak(b) < 1.0 && peak(c) < 1.0;
        check("FxSweep_TheSendChainOrderMatters", ok && differenceDb(a, b) > -40.0 && differenceDb(a, c) > -40.0,
              "reversed " + juce::String(differenceDb(a, b), 1) + " dB, reverb first " + juce::String(differenceDb(a, c), 1) + " dB");
    }

    // A block size the host might use: the same settings render the same at
    // 128, and stay finite at an odd one.
    {
        const auto s = join(everythingOff(), { { "Delay Enabled", 1.0f }, { "Delay Amount", 0.6f }, { "Lucy Enabled", 1.0f },
                                               { "Lucy Global", 0.8f }, { "Reverb Enabled", 1.0f }, { "Reverb", 0.5f } });
        const auto a = renderProcessor(s, nullptr, 512);
        const auto b = renderProcessor(s, nullptr, 137);
        check("FxSweep_AnOddBlockSizeRendersTheSameMix", finite(b) && std::abs(toDb(rms(a) / juce::jmax(1.0e-12, rms(b)))) < 0.5,
              "512 vs 137 samples: " + juce::String(toDb(rms(a) / juce::jmax(1.0e-12, rms(b))), 2) + " dB");
    }
}
} // namespace px3tests

namespace px3tests
{
// What `fxsweep` found, pinned in the default run (fxsweep itself takes
// minutes and runs on its own).
void testSweepFindings()
{
    suite("FX SWEEP FINDINGS");
    using namespace px3::fxsweep;

    // A control swept while a chord is held must not step the output at block
    // boundaries or at one end of its travel.
    struct Swept { const char* label; const char* section; const char* control; Settings extra; };
    const std::vector<Swept> swept {
        // MOOD translated its knobs into tap and loop lengths once a block, so
        // a swept LOOP LENGTH or CLOCK jumped a read position at every block
        // start: crest 1.36 against 0.14 still. (CLOCK still makes a slightly
        // larger step where it crosses a semitone - it is quantised by design.)
        { "Mood_SweepingLoopLengthIsSmooth", "MOOD", "Mood Loop Length", {} },
        // LUCY's band filter was bypassed below FILTER 0.001 and a whole
        // band-pass just above it: crest 0.11 against 0.02.
        { "Lucy_SweepingFilterUpFromZeroIsSmooth", "LUCY", "Lucy Filter", {} },
    };
    for (const auto& s : swept)
    {
        Settings settings = everythingOff();
        for (const auto& section : sections())
        {
            if (section.label == s.section) { settings = join(join(settings, { { section.enable, 1.0f } }), section.active); }
        }
        settings = join(settings, s.extra);
        Ramp ramp { s.control, 0.0f, 1.0f };
        const auto moving = renderProcessor(settings, &ramp);
        double still = 0.0, stillStep = 0.0;
        for (const auto value : { 0.0f, 0.5f, 1.0f })
        {
            const auto audio = renderProcessor(join(settings, { { s.control, value } }), nullptr);
            still = juce::jmax(still, stepCrest(audio, ramp.fromSeconds, ramp.toSeconds));
            stillStep = juce::jmax(stillStep, maxStep(audio, ramp.fromSeconds, ramp.toSeconds));
        }
        const auto crest = stepCrest(moving, ramp.fromSeconds, ramp.toSeconds);
        const auto step = maxStep(moving, ramp.fromSeconds, ramp.toSeconds);
        check(s.label, finite(moving) && ! (crest > 2.0 * still && step > 1.5 * stillStep),
              juce::String(s.control) + " swept: step " + juce::String(step, 4) + " (crest " + juce::String(crest, 2) + ") vs "
                  + juce::String(stillStep, 4) + " (" + juce::String(still, 2) + ") still");
    }

    // DELAY's default type and mode, Granular CLASSIC, ignored TIME and
    // FEEDBACK altogether. In the synth, where AMOUNT is only a level, they
    // set the grain delay / size and the feedback.
    {
        const auto base = join(everythingOff(), { { "Delay Enabled", 1.0f }, { "Delay Amount", 0.6f } });
        const auto at = [&](const char* control, float value) { return renderProcessor(join(base, { { control, value } }), nullptr); };
        const auto timeDb = differenceDb(at("Delay Time", 0.0f), at("Delay Time", 1.0f));
        const auto feedbackDb = differenceDb(at("Delay Feedback", 0.0f), at("Delay Feedback", 1.0f));
        // FEEDBACK recirculates the grains, a quieter change than TIME's.
        check("Delay_GranularClassicTimeAndFeedbackDoSomething", timeDb > -30.0 && feedbackDb > -45.0,
              "TIME 0 vs 1: " + juce::String(timeDb, 1) + " dB, FEEDBACK 0 vs 1: " + juce::String(feedbackDb, 1) + " dB");
    }
}
} // namespace px3tests
