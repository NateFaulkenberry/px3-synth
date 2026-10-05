#include "TestSupport.h"

// testAnalogGolden
//
// ANALOG (the per-voice drift that used to live under VIBE) was moved out of
// VIBE into a component of its own. The move was meant to change nothing at
// all, and "sounds the same" is not a measurement - so these renders were
// hashed on the code BEFORE the move and stored in tests/golden. Every one of
// them has to reproduce bit for bit.
//
// The drift path is bitwise deterministic: its engine is seeded by hash, and
// each voice's noise and start phase come from the voice's own note count, not
// from the shared system Random. Each render is made twice and the two must
// agree before the stored hash means anything.
//
// Recording: PX3_RECORD_GOLDEN=1 PX3Tests analoggolden (run from the repo root).

namespace px3tests
{
namespace
{
const char* const kGoldenPath = "tests/golden/analog_drift_golden.txt";

// The parameter ids ANALOG answers to. The only thing in this file allowed to
// change across the extraction.
const char* const kDriftEnabledId = "fx.analog.enabled";
const char* const kDriftAmountId = "fx.analog.amount";
const char* const kDriftTypeId = "fx.analog.type";

struct GoldenCase
{
    juce::String name;
    double sampleRate;
    int blockSize;
    float amount;
    int type;
    int patch; // 0 sine solo, 1 saw chord, 2 dense 24 notes, 3 legato with long release
};

juce::uint64 renderHash(const GoldenCase& c)
{
    PX3SynthAudioProcessor processor;
    makePlainPatch(processor);
    // Before the split, fx.vibe.enabled gated the drift and the Uni-Vibe sat at
    // INTENSITY 0 (sample-identical). After it, VIBE is a separate effect and
    // is simply switched off.
    setParam(processor, "fx.vibe.enabled", 0.0f);
    setParam(processor, kDriftEnabledId, 1.0f);
    setParam(processor, kDriftAmountId, c.amount);
    setChoice(processor, kDriftTypeId, c.type);

    std::vector<std::pair<int, std::pair<bool, int>>> events; // sample, (on, note)
    auto totalSeconds = 1.5;

    if (c.patch == 0)
    {
        events.push_back({ 1000, { true, 57 } });
        events.push_back({ 50000, { false, 57 } });
    }
    else if (c.patch == 1)
    {
        setChoice(processor, "voice.osc1.mode", 1);
        setParam(processor, "voice.osc2.enabled", 1.0f);
        setChoice(processor, "voice.osc2.mode", 1);
        setParam(processor, "voice.osc2.tuning.cents", -9.0f);
        setParam(processor, "voice.sub.enabled", 1.0f);
        setParam(processor, "voice.filter1.enabled", 1.0f);
        setParam(processor, "voice.filter1.cutoff", 2400.0f);
        for (const auto note : { 48, 55, 60, 64 })
        {
            events.push_back({ 700, { true, note } });
            events.push_back({ 52000, { false, note } });
        }
    }
    else if (c.patch == 2)
    {
        setChoice(processor, "voice.osc1.mode", 1);
        setParam(processor, "voice.amp.release", 0.6f);
        for (int i = 0; i < 24; ++i)
        {
            events.push_back({ 300 + i * 400, { true, 36 + i * 2 } });
            events.push_back({ 40000 + i * 300, { false, 36 + i * 2 } });
        }
    }
    else
    {
        setChoice(processor, "voice.osc1.mode", 3);
        setParam(processor, "voice.amp.release", 2.0f);
        totalSeconds = 2.0;
        for (int i = 0; i < 8; ++i)
        {
            const auto note = 60 + ((i * 5) % 12);
            events.push_back({ 500 + i * 6000, { true, note } });
            events.push_back({ 500 + i * 6000 + 7000, { false, note } });
        }
    }

    // Event times are written for 48 kHz; scale them so every rate plays the
    // same musical material.
    for (auto& e : events)
    {
        e.first = static_cast<int>(std::lround(e.first * c.sampleRate / 48000.0));
    }
    std::stable_sort(events.begin(), events.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });

    const auto totalSamples = static_cast<int>(totalSeconds * c.sampleRate);
    processor.setPlayConfigDetails(0, 2, c.sampleRate, c.blockSize);
    processor.prepareToPlay(c.sampleRate, c.blockSize);
    juce::AudioBuffer<float> buffer(2, c.blockSize);

    juce::uint64 hash = 14695981039346656037ull;
    std::size_t next = 0;
    for (int position = 0; position < totalSamples; position += c.blockSize)
    {
        buffer.clear();
        juce::MidiBuffer midi;
        while (next < events.size() && events[next].first < position + c.blockSize)
        {
            const auto& e = events[next];
            midi.addEvent(e.second.first ? juce::MidiMessage::noteOn(1, e.second.second, 0.9f)
                                         : juce::MidiMessage::noteOff(1, e.second.second),
                          juce::jmax(0, e.first - position));
            ++next;
        }
        processor.processBlock(buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* data = buffer.getReadPointer(ch);
            for (int i = 0; i < c.blockSize; ++i)
            {
                juce::uint32 bits = 0;
                std::memcpy(&bits, &data[i], sizeof(bits));
                hash ^= static_cast<juce::uint64>(bits);
                hash *= 1099511628211ull;
            }
        }
    }
    return hash;
}

std::vector<GoldenCase> goldenCases()
{
    static const char* const kTypes[] = { "Warm", "Hot", "Cool", "Vintage", "Clean", "LoFi" };
    std::vector<GoldenCase> cases;
    for (const auto amount : { 0.15f, 0.5f, 1.0f })
    {
        for (int type = 0; type < 6; ++type)
        {
            cases.push_back({ "sine " + juce::String(kTypes[type]) + " " + juce::String(amount, 2),
                              48000.0, 512, amount, type, 0 });
        }
    }
    cases.push_back({ "saw chord Warm 0.70", 48000.0, 512, 0.7f, 0, 1 });
    cases.push_back({ "saw chord LoFi 0.70", 48000.0, 512, 0.7f, 5, 1 });
    cases.push_back({ "dense 24 Hot 0.85", 48000.0, 512, 0.85f, 1, 2 });
    cases.push_back({ "legato release Vintage 0.80", 48000.0, 512, 0.8f, 3, 3 });
    cases.push_back({ "saw chord Warm 0.70 block64", 48000.0, 64, 0.7f, 0, 1 });
    cases.push_back({ "legato release Vintage 0.80 block64", 48000.0, 64, 0.8f, 3, 3 });
    cases.push_back({ "saw chord Hot 0.60 44k1", 44100.0, 512, 0.6f, 1, 1 });
    cases.push_back({ "saw chord Cool 0.60 96k", 96000.0, 512, 0.6f, 2, 1 });
    cases.push_back({ "sine off 0.00", 48000.0, 512, 0.0f, 0, 0 });
    return cases;
}
} // namespace

void testAnalogGolden()
{
    suite("ANALOG GOLDEN (bit-exact across the VIBE/ANALOG split)");

    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(kGoldenPath);
    const auto recording = juce::SystemStats::getEnvironmentVariable("PX3_RECORD_GOLDEN", {}) == "1";

    juce::StringArray lines;
    bool deterministic = true;
    for (const auto& c : goldenCases())
    {
        const auto a = renderHash(c);
        const auto b = renderHash(c);
        deterministic = deterministic && a == b;
        lines.add(c.name + "\t" + juce::String::toHexString(static_cast<juce::int64>(a)));
    }
    check("AnalogGolden_RendersAreDeterministic", deterministic, "two renders of each case agree bit for bit");

    if (recording)
    {
        file.getParentDirectory().createDirectory();
        file.replaceWithText(lines.joinIntoString("\n") + "\n");
        std::printf("  recorded %d hashes to %s\n", lines.size(), file.getFullPathName().toRawUTF8());
        return;
    }

    juce::StringArray stored;
    stored.addLines(file.loadFileAsString());
    stored.removeEmptyStrings();

    juce::StringArray mismatched;
    for (const auto& line : lines)
    {
        if (! stored.contains(line)) { mismatched.add(line.upToFirstOccurrenceOf("\t", false, false)); }
    }
    check("AnalogGolden_EveryRenderMatchesThePreSplitHash",
          file.existsAsFile() && mismatched.isEmpty() && stored.size() == lines.size(),
          ! file.existsAsFile() ? juce::String("no golden file at ") + kGoldenPath
          : mismatched.isEmpty() ? juce::String(lines.size()) + " renders bit-identical to the recording"
                                 : "differs: " + mismatched.joinIntoString(", "));
}
} // namespace px3tests
