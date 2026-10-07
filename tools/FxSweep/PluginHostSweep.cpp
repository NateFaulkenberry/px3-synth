// The plug-in path of the FX sweep (see FxSweep.h and README.md).
//
//   PX3FxSweepHost <plugin> AU|VST3 <rate> <block|var> <all|main> <out.tsv> [SECTION ...]
//
// Every render is a fresh instance, as Logic would load it: buses enabled the
// way the AU wrapper enables them (all) or the main pair only, settings applied
// through the host parameters by name, then prepared and played with a
// 120 BPM playhead.

#include <JuceHeader.h>

#include "FxSweep.h"

#include <iostream>

using namespace juce;

namespace
{
String gPath, gFormat;
double gRate = 48000.0;
int gBlock = 512;        // 0 = variable
bool gAllBuses = true;

struct HostPlayHead : AudioPlayHead
{
    int64 samples { 0 };
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm(px3::fxsweep::kBpm);
        info.setTimeSignature(TimeSignature { 4, 4 });
        info.setIsPlaying(true);
        info.setTimeInSamples(samples);
        info.setTimeInSeconds(static_cast<double>(samples) / gRate);
        info.setPpqPosition(static_cast<double>(samples) / gRate * px3::fxsweep::kBpm / 60.0);
        return info;
    }
};

std::unique_ptr<AudioPluginInstance> load(int maxBlock)
{
    AudioPluginFormatManager fm;
    fm.addFormat(new AudioUnitPluginFormat());
    fm.addFormat(new VST3PluginFormat());
    OwnedArray<PluginDescription> descs;
    for (auto* f : fm.getFormats())
    {
        if (f->getName() == gFormat || (gFormat == "AU" && f->getName() == "AudioUnit")) { f->findAllTypesForFile(descs, gPath); }
    }
    if (descs.isEmpty()) { return {}; }
    String error;
    return fm.createPluginInstance(*descs[0], gRate, maxBlock, error);
}

std::map<String, AudioProcessorParameter*> byName(AudioPluginInstance& p)
{
    std::map<String, AudioProcessorParameter*> m;
    for (auto* param : p.getParameters()) { m[param->getName(100)] = param; }
    return m;
}

StringArray gMissing;

px3::fxsweep::Audio render(const px3::fxsweep::Settings& settings, const px3::fxsweep::Ramp* ramp)
{
    const auto maxBlock = gBlock > 0 ? gBlock : 1024;
    auto p = load(maxBlock);
    px3::fxsweep::Audio out;
    out.sampleRate = gRate;
    if (p == nullptr) { return out; }
    if (gAllBuses) { p->enableAllBuses(); }
    else
    {
        auto layout = p->getBusesLayout();
        for (int i = 1; i < layout.outputBuses.size(); ++i) { layout.outputBuses.getReference(i) = AudioChannelSet::disabled(); }
        p->setBusesLayout(layout);
    }
    auto params = byName(*p);
    std::vector<std::pair<px3::fxsweep::Setting, bool>> timed;
    for (const auto& s : settings)
    {
        const auto it = params.find(s.name);
        if (it == params.end()) { gMissing.addIfNotAlreadyThere(s.name); continue; }
        if (s.atSeconds < 0.0) { it->second->setValueNotifyingHost(s.value); }
        else { timed.push_back({ s, false }); }
    }
    HostPlayHead playHead;
    p->setPlayHead(&playHead);
    p->prepareToPlay(gRate, maxBlock);
    MessageManager::getInstance()->runDispatchLoopUntil(1);

    AudioProcessorParameter* rampParam = nullptr;
    if (ramp != nullptr)
    {
        if (const auto it = params.find(ramp->name); it != params.end()) { rampParam = it->second; }
    }
    const auto total = static_cast<int>(px3::fxsweep::kSeconds * gRate);
    const auto notes = px3::fxsweep::material();
    const int pattern[] = { 1, 37, 512, 1024, 256 };
    AudioBuffer<float> buffer(jmax(2, p->getTotalNumOutputChannels()), maxBlock);
    int pos = 0, blockIndex = 0;
    while (pos < total)
    {
        const auto want = gBlock > 0 ? gBlock : pattern[blockIndex++ % 5];
        const auto n = jmin(want, total - pos);
        buffer.setSize(buffer.getNumChannels(), n, false, false, true);
        buffer.clear();
        MidiBuffer midi;
        for (const auto& note : notes)
        {
            const auto on = static_cast<int>(note.on * gRate);
            const auto off = static_cast<int>(note.off * gRate);
            if (on >= pos && on < pos + n) { midi.addEvent(MidiMessage::noteOn(1, note.note, note.velocity), on - pos); }
            if (off >= pos && off < pos + n) { midi.addEvent(MidiMessage::noteOff(1, note.note), off - pos); }
        }
        for (auto& entry : timed)
        {
            auto& [setting, applied] = entry;
            if (! applied && static_cast<double>(pos) / gRate >= setting.atSeconds)
            {
                params[setting.name]->setValueNotifyingHost(setting.value);
                applied = true;
            }
        }
        if (rampParam != nullptr)
        {
            const auto t = static_cast<double>(pos) / gRate;
            const auto f = jlimit(0.0, 1.0, (t - ramp->fromSeconds) / (ramp->toSeconds - ramp->fromSeconds));
            rampParam->setValueNotifyingHost(ramp->from + static_cast<float>(f) * (ramp->to - ramp->from));
        }
        playHead.samples = pos;
        p->processBlock(buffer, midi);
        for (int i = 0; i < n; ++i)
        {
            out.left.push_back(buffer.getSample(0, i));
            out.right.push_back(buffer.getSample(jmin(1, buffer.getNumChannels() - 1), i));
        }
        pos += n;
    }
    p->releaseResources();
    p->setPlayHead(nullptr);
    return out;
}
} // namespace

int main(int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    if (argc < 7)
    {
        std::cerr << "usage: PX3FxSweepHost <plugin> AU|VST3 <rate> <block|var> <all|main> <out.tsv> [SECTION ...]\n";
        return 2;
    }
    gPath = argv[1];
    gFormat = argv[2];
    gRate = String(argv[3]).getDoubleValue();
    gBlock = String(argv[4]) == "var" ? 0 : String(argv[4]).getIntValue();
    gAllBuses = String(argv[5]) == "all";
    const File tsv { String(argv[6]) };
    StringArray only;
    for (int i = 7; i < argc; ++i) { only.add(argv[i]); }

    std::vector<px3::fxsweep::ParameterInfo> params;
    {
        auto p = load(512);
        if (p == nullptr) { std::cerr << "could not load " << gPath << "\n"; return 1; }
        std::cout << "Loaded " << p->getName() << " " << p->getPluginDescription().version << " (" << gFormat << "), "
                  << p->getBusCount(false) << " output bus(es), " << p->getParameters().size() << " parameters\n";
        for (auto* param : p->getParameters())
        {
            params.push_back({ param->getName(100), param->getDefaultValue(), param->getNumSteps(), {} });
        }
    }

    const auto results = px3::fxsweep::run(params, render, [](const String& line) { std::cout << line << "\n" << std::flush; }, only);
    tsv.replaceWithText(px3::fxsweep::toTsv(results));
    if (! gMissing.isEmpty()) { std::cout << "MISSING PARAMETERS: " << gMissing.joinIntoString(", ") << "\n"; }
    std::cout << "wrote " << tsv.getFullPathName() << "\n";
    return 0;
}
