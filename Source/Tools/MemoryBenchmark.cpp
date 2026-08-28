// PX3 offline memory benchmark.
//
// Measures how much memory a PX3SynthAudioProcessor actually costs, by creating
// real instances in a real process and reading the process's own accounting -
// rather than by adding up sizeof() figures, which is what `PX3Diag memory`
// does and which cannot see allocator overhead, fragmentation, or anything JUCE
// allocates on the way past.
//
// Built in the SHIPPING configuration (PX3_DIAGNOSTICS off) so it measures the
// code the plugin actually ships.
//
// Everything here is process-local: no network, no host, no external services.

#include <JuceHeader.h>

#include "../DSP/PluginProcessor.h"

#include <mach/mach.h>
#include <mach/task_info.h>
#include <malloc/malloc.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{
constexpr double kBytesPerMB = 1024.0 * 1024.0;

// ---------------------------------------------------------------------------
// Measurement
// ---------------------------------------------------------------------------

struct MemorySample
{
    double rss { 0.0 };         // resident set size
    double footprint { 0.0 };   // phys_footprint - what Activity Monitor calls "Memory"
    double heap { 0.0 };        // bytes in use across every malloc zone
    double virtualSize { 0.0 };
};

// Sums live bytes across all malloc zones rather than just the default one.
// JUCE, CoreAudio and the Objective-C runtime each create their own, so the
// default zone alone under-reports.
double heapBytesInUse()
{
    vm_address_t* zones = nullptr;
    unsigned zoneCount = 0;
    double total = 0.0;

    if (malloc_get_all_zones(mach_task_self(), nullptr, &zones, &zoneCount) == KERN_SUCCESS
        && zones != nullptr)
    {
        for (unsigned i = 0; i < zoneCount; ++i)
        {
            auto* zone = reinterpret_cast<malloc_zone_t*>(zones[i]);
            if (zone == nullptr || zone->introspect == nullptr)
                continue;

            malloc_statistics_t stats {};
            malloc_zone_statistics(zone, &stats);
            total += static_cast<double>(stats.size_in_use);
        }
        return total;
    }

    malloc_statistics_t stats {};
    malloc_zone_statistics(malloc_default_zone(), &stats);
    return static_cast<double>(stats.size_in_use);
}

MemorySample readMemoryOnce()
{
    MemorySample sample;

    mach_task_basic_info info {};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
    {
        sample.rss = static_cast<double>(info.resident_size);
        sample.virtualSize = static_cast<double>(info.virtual_size);
    }

    task_vm_info_data_t vmInfo {};
    count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO,
                  reinterpret_cast<task_info_t>(&vmInfo), &count) == KERN_SUCCESS)
    {
        sample.footprint = static_cast<double>(vmInfo.phys_footprint);
    }

    sample.heap = heapBytesInUse();
    return sample;
}

// Takes several samples and returns the median of each metric independently.
//
// Process memory is not stable to the byte: deferred frees, lazily faulted
// pages and background threads all move it between one read and the next. The
// median of a few samples is steadier than a single read, and steadier than a
// mean, which one outlier can drag.
MemorySample measureSettled(int samples = 5, int settleMs = 25)
{
    std::vector<double> rss, footprint, heap, virt;
    rss.reserve(static_cast<std::size_t>(samples));
    footprint.reserve(static_cast<std::size_t>(samples));
    heap.reserve(static_cast<std::size_t>(samples));
    virt.reserve(static_cast<std::size_t>(samples));

    for (int i = 0; i < samples; ++i)
    {
        juce::Thread::sleep(settleMs);
        const auto s = readMemoryOnce();
        rss.push_back(s.rss);
        footprint.push_back(s.footprint);
        heap.push_back(s.heap);
        virt.push_back(s.virtualSize);
    }

    auto median = [](std::vector<double>& v)
    {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };

    MemorySample result;
    result.rss = median(rss);
    result.footprint = median(footprint);
    result.heap = median(heap);
    result.virtualSize = median(virt);
    return result;
}

double toMB(double bytes) { return bytes / kBytesPerMB; }

juce::String mb(double bytes)
{
    return juce::String(toMB(bytes), 1) + " MB";
}

// Virtual size on arm64 macOS is a few hundred GB of mostly-unmapped reserved
// address space. Printed in MB it reads as nonsense, and it says nothing about
// memory actually consumed - it is reported only for completeness.
juce::String gb(double bytes)
{
    return juce::String(bytes / (kBytesPerMB * 1024.0), 1) + " GB";
}

juce::String signedMB(double bytes)
{
    const auto value = toMB(bytes);
    return (value >= 0.0 ? "+" : "") + juce::String(value, 1) + " MB";
}

// ---------------------------------------------------------------------------
// The plugin under test
// ---------------------------------------------------------------------------

enum class Scenario { plain, initialized, stress };

juce::String scenarioName(Scenario s)
{
    switch (s)
    {
        case Scenario::plain:       return "default";
        case Scenario::initialized: return "initialized";
        case Scenario::stress:      return "stress";
    }
    return "default";
}

struct Instance
{
    std::unique_ptr<PX3SynthAudioProcessor> processor;
    juce::AudioBuffer<float> buffer;
};

void setParameterById(PX3SynthAudioProcessor& processor, const juce::String& id, float value)
{
    for (auto* parameter : processor.getParameters())
    {
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter))
        {
            if (withId->paramID == id)
            {
                withId->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, value));
                return;
            }
        }
    }
}

// Drives an instance far enough that anything allocated lazily has been
// allocated. Measuring straight after the constructor reports a number the
// plugin never actually runs at.
void exerciseInstance(Instance& instance, Scenario scenario, double sampleRate, int blockSize)
{
    if (scenario == Scenario::plain)
        return;

    auto& processor = *instance.processor;
    processor.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    processor.prepareToPlay(sampleRate, blockSize);
    instance.buffer.setSize(2, blockSize);

    auto runBlocks = [&](int blocks, const juce::MidiBuffer& midiTemplate)
    {
        for (int i = 0; i < blocks; ++i)
        {
            instance.buffer.clear();
            juce::MidiBuffer midi (i == 0 ? midiTemplate : juce::MidiBuffer());
            processor.processBlock(instance.buffer, midi);
        }
    };

    if (scenario == Scenario::initialized)
    {
        // Enough blocks for the processor's own prepare-time work to settle.
        runBlocks(16, juce::MidiBuffer());
        return;
    }

    // stress: bring up every module that owns a buffer, then play through them.
    // Nothing here reaches into the DSP - it only sets the parameters a user
    // would set, so the allocations are the ones the plugin really makes.
    for (const auto& id : { "vibeEnabled", "delayEnabled", "reverbEnabled",
                            "moodEnabled", "subOscEnabled", "ampEnvEnabled" })
    {
        setParameterById(processor, id, 1.0f);
    }
    for (const auto& id : { "vibeAmount", "delayAmount", "reverbAmount", "moodMix",
                            "delayFeedback", "moodFeedback", "moodSpread", "moodDegrade" })
    {
        setParameterById(processor, id, 0.75f);
    }

    // A chord, so a useful number of voices allocate their per-voice storage.
    juce::MidiBuffer notes;
    for (int note = 36; note <= 84; note += 4)
        notes.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);

    runBlocks(64, notes);

    // Walk the FX algorithm choices so each one's delay lines are allocated.
    for (const auto& id : { "delayAlgorithm", "reverbAlgorithm", "granularMode",
                            "moodWetMode", "moodLoopMode", "vibeType" })
    {
        for (float v : { 0.0f, 0.34f, 0.67f, 1.0f })
        {
            setParameterById(processor, id, v);
            runBlocks(4, juce::MidiBuffer());
        }
    }

    juce::MidiBuffer offs;
    for (int note = 36; note <= 84; note += 4)
        offs.addEvent(juce::MidiMessage::noteOff(1, note), 0);
    runBlocks(48, offs);
}

// ---------------------------------------------------------------------------
// Results
// ---------------------------------------------------------------------------

struct Checkpoint
{
    int instances { 0 };
    MemorySample memory;
};

struct Results
{
    juce::String scenario;
    double sampleRate { 48000.0 };
    int blockSize { 512 };
    int maxInstances { 8 };
    MemorySample baseline;
    std::vector<Checkpoint> checkpoints;
    MemorySample peak;
    MemorySample afterDestruction;
    MemorySample afterDrain;
    bool editorMeasured { false };
    double editorFirstFootprint { 0.0 };
    double editorIncrementalFootprint { 0.0 };
    int editorCount { 0 };
};

// Checkpoints at 1, 2, 4, 8 ... up to the requested maximum, with the maximum
// always included even when it is not a power of two.
std::vector<int> checkpointsFor(int maxInstances)
{
    std::vector<int> points;
    for (int n = 1; n < maxInstances; n *= 2)
        points.push_back(n);
    points.push_back(maxInstances);
    return points;
}

double firstInstanceOverhead(const Results& r)
{
    if (r.checkpoints.empty()) return 0.0;
    return r.checkpoints.front().memory.footprint - r.baseline.footprint;
}

// Averaged across the whole span from one instance to the last, rather than by
// averaging the per-checkpoint deltas: the checkpoints are unevenly spaced, so
// averaging them would weight the early, noisier steps far too heavily.
double averageIncrementalCost(const Results& r)
{
    if (r.checkpoints.size() < 2) return 0.0;
    const auto& first = r.checkpoints.front();
    const auto& last = r.checkpoints.back();
    const auto span = last.instances - first.instances;
    if (span <= 0) return 0.0;
    return (last.memory.footprint - first.memory.footprint) / static_cast<double>(span);
}
}   // namespace

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

namespace
{
juce::var sampleToVar(const MemorySample& s)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("rssMB", juce::var(juce::roundToInt(toMB(s.rss) * 10.0) / 10.0));
    object->setProperty("footprintMB", juce::var(juce::roundToInt(toMB(s.footprint) * 10.0) / 10.0));
    object->setProperty("heapMB", juce::var(juce::roundToInt(toMB(s.heap) * 10.0) / 10.0));
    object->setProperty("virtualMB", juce::var(juce::roundToInt(toMB(s.virtualSize) * 10.0) / 10.0));
    return juce::var(object);
}

juce::String resultsToJson(const Results& r)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("build", "Release");
    root->setProperty("architecture",
#if defined(__aarch64__)
                      "arm64"
#else
                      "x86_64"
#endif
    );
    root->setProperty("scenario", r.scenario);
    root->setProperty("sampleRate", r.sampleRate);
    root->setProperty("blockSize", r.blockSize);
    root->setProperty("baseline", sampleToVar(r.baseline));

    juce::Array<juce::var> instances;
    for (const auto& c : r.checkpoints)
    {
        auto* entry = new juce::DynamicObject();
        entry->setProperty("count", c.instances);
        entry->setProperty("rssMB", juce::var(juce::roundToInt(toMB(c.memory.rss) * 10.0) / 10.0));
        entry->setProperty("footprintMB", juce::var(juce::roundToInt(toMB(c.memory.footprint) * 10.0) / 10.0));
        entry->setProperty("heapMB", juce::var(juce::roundToInt(toMB(c.memory.heap) * 10.0) / 10.0));
        instances.add(juce::var(entry));
    }
    root->setProperty("instances", instances);

    root->setProperty("firstInstanceOverheadMB",
                      juce::var(juce::roundToInt(toMB(firstInstanceOverhead(r)) * 10.0) / 10.0));
    root->setProperty("averageIncrementalInstanceMB",
                      juce::var(juce::roundToInt(toMB(averageIncrementalCost(r)) * 10.0) / 10.0));
    root->setProperty("peak", sampleToVar(r.peak));
    root->setProperty("afterDestruction", sampleToVar(r.afterDestruction));
    root->setProperty("afterAllocatorDrain", sampleToVar(r.afterDrain));

    if (r.editorMeasured)
    {
        auto* editor = new juce::DynamicObject();
        editor->setProperty("count", r.editorCount);
        editor->setProperty("firstEditorMB",
                            juce::var(juce::roundToInt(toMB(r.editorFirstFootprint) * 10.0) / 10.0));
        editor->setProperty("incrementalEditorMB",
                            juce::var(juce::roundToInt(toMB(r.editorIncrementalFootprint) * 10.0) / 10.0));
        root->setProperty("editor", juce::var(editor));
    }

    return juce::JSON::toString(juce::var(root), false);
}
}   // namespace

// ---------------------------------------------------------------------------
// Reporting
// ---------------------------------------------------------------------------

namespace
{
void printReport(const Results& r)
{
    std::printf("\nPX3 Synth Memory Benchmark\n");
    std::printf("==========================\n\n");
    std::printf("Build:        Release (PX3_DIAGNOSTICS off)\n");
#if defined(__aarch64__)
    std::printf("Architecture: arm64\n");
#else
    std::printf("Architecture: x86_64\n");
#endif
    std::printf("Scenario:     %s\n", r.scenario.toRawUTF8());
    std::printf("Sample rate:  %.0f Hz\n", r.sampleRate);
    std::printf("Block size:   %d\n\n", r.blockSize);

    std::printf("Baseline (process before any plugin instance exists)\n");
    std::printf("  RSS:        %s\n", mb(r.baseline.rss).toRawUTF8());
    std::printf("  Footprint:  %s\n", mb(r.baseline.footprint).toRawUTF8());
    std::printf("  Heap:       %s\n", mb(r.baseline.heap).toRawUTF8());
    std::printf("  Virtual:    %s  (reserved address space, not consumption)\n\n",
                gb(r.baseline.virtualSize).toRawUTF8());

    std::printf("Processor benchmark\n");
    std::printf("--------------------------------------------------------------------\n");
    std::printf("Instances    RSS         Footprint   Heap        Increment/instance\n");
    std::printf("%-12d %-11s %-11s %-11s %s\n", 0,
                mb(r.baseline.rss).toRawUTF8(),
                mb(r.baseline.footprint).toRawUTF8(),
                mb(r.baseline.heap).toRawUTF8(), "-");

    const MemorySample* previous = &r.baseline;
    int previousCount = 0;
    for (const auto& c : r.checkpoints)
    {
        const auto added = c.instances - previousCount;
        const auto perInstance = added > 0
            ? (c.memory.footprint - previous->footprint) / static_cast<double>(added)
            : 0.0;
        std::printf("%-12d %-11s %-11s %-11s %s\n",
                    c.instances,
                    mb(c.memory.rss).toRawUTF8(),
                    mb(c.memory.footprint).toRawUTF8(),
                    mb(c.memory.heap).toRawUTF8(),
                    signedMB(perInstance).toRawUTF8());
        previous = &c.memory;
        previousCount = c.instances;
    }

    std::printf("\n  First-instance overhead:        %s\n",
                signedMB(firstInstanceOverhead(r)).toRawUTF8());
    std::printf("    baseline -> 1 instance. Includes one-time initialisation that\n");
    std::printf("    every later instance then shares, so it is not the per-instance cost.\n");
    std::printf("  Avg incremental instance cost:  %s\n",
                signedMB(averageIncrementalCost(r)).toRawUTF8());
    std::printf("    (footprint at %d instances - footprint at 1) / %d.\n",
                r.checkpoints.empty() ? 0 : r.checkpoints.back().instances,
                r.checkpoints.empty() ? 0 : juce::jmax(1, r.checkpoints.back().instances - 1));
    std::printf("    This is the number to plan a session's instance count against.\n");

    std::printf("\nDestruction\n");
    std::printf("--------------------------------------------------------------------\n");
    std::printf("%-26s %-11s %-11s %s\n", "", "RSS", "Footprint", "Heap in use");
    std::printf("  %-24s %-11s %-11s %s\n", "Peak (all instances)",
                mb(r.peak.rss).toRawUTF8(), mb(r.peak.footprint).toRawUTF8(),
                mb(r.peak.heap).toRawUTF8());
    std::printf("  %-24s %-11s %-11s %s\n", "After destroying all",
                mb(r.afterDestruction.rss).toRawUTF8(), mb(r.afterDestruction.footprint).toRawUTF8(),
                mb(r.afterDestruction.heap).toRawUTF8());
    std::printf("  %-24s %-11s %-11s %s\n", "After allocator drain",
                mb(r.afterDrain.rss).toRawUTF8(), mb(r.afterDrain.footprint).toRawUTF8(),
                mb(r.afterDrain.heap).toRawUTF8());
    std::printf("  %-24s %-11s %-11s %s\n", "Baseline, for comparison",
                mb(r.baseline.rss).toRawUTF8(), mb(r.baseline.footprint).toRawUTF8(),
                mb(r.baseline.heap).toRawUTF8());

    const auto heapRetained = r.afterDrain.heap - r.baseline.heap;
    const auto heapPeak = juce::jmax(1.0, r.peak.heap - r.baseline.heap);
    std::printf("\n  Heap in use returned to within %s of baseline (%.1f%% of peak).\n",
                signedMB(heapRetained).toRawUTF8(), 100.0 * heapRetained / heapPeak);
    std::printf("  Footprint retained above baseline: %s\n",
                signedMB(r.afterDrain.footprint - r.baseline.footprint).toRawUTF8());
    std::printf("\n  Read those two together. Heap in use is what the plugin asked for and\n");
    std::printf("  has not given back - that is the leak indicator. Footprint and RSS stay\n");
    std::printf("  high because the allocator keeps freed pages mapped for reuse rather\n");
    std::printf("  than returning them to the kernel, which is normal and not a leak.\n");

    if (r.editorMeasured)
    {
        std::printf("\nEditor benchmark\n");
        std::printf("--------------------------------------------------------------------\n");
        std::printf("  Editors created:           %d\n", r.editorCount);
        std::printf("  First editor:              %s\n", signedMB(r.editorFirstFootprint).toRawUTF8());
        std::printf("  Each additional editor:    %s\n", signedMB(r.editorIncrementalFootprint).toRawUTF8());
        std::printf("  Measured separately from the processor figures above and never\n");
        std::printf("  folded into them. A host only pays this while a window is open.\n");
    }

    std::printf("\nThese are process-level measurements, not per-object allocation counts.\n");
    std::printf("Run `PX3Diag memory` for the static per-object breakdown.\n\n");
}
}   // namespace

// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    int maxInstances = 8;
    double sampleRate = 48000.0;
    int blockSize = 512;
    auto scenario = Scenario::stress;
    bool jsonOutput = false;
    bool runEditor = false;
    juce::String saveBaselinePath;
    juce::String compareBaselinePath;
    double tolerancePercent = 5.0;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        auto next = [&]() -> juce::String { return (i + 1 < argc) ? juce::String(argv[++i]) : juce::String(); };

        if (arg == "--instances")            maxInstances = juce::jlimit(1, 256, next().getIntValue());
        else if (arg == "--sample-rate")     sampleRate = juce::jmax(8000.0, next().getDoubleValue());
        else if (arg == "--block-size")      blockSize = juce::jlimit(16, 8192, next().getIntValue());
        else if (arg == "--json")            jsonOutput = true;
        else if (arg == "--editor")          runEditor = true;
        else if (arg == "--tolerance")       tolerancePercent = juce::jmax(0.0, next().getDoubleValue());
        else if (arg == "--save-baseline")   saveBaselinePath = next();
        else if (arg == "--compare-baseline") compareBaselinePath = next();
        else if (arg == "--scenario")
        {
            const auto value = next();
            if (value == "default")           scenario = Scenario::plain;
            else if (value == "initialized")  scenario = Scenario::initialized;
            else if (value == "stress")       scenario = Scenario::stress;
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::printf("PX3 memory benchmark\n\n"
                        "  --instances N        highest instance count to reach (default 8)\n"
                        "  --scenario S         default | initialized | stress (default stress)\n"
                        "  --sample-rate HZ     default 48000\n"
                        "  --block-size N       default 512\n"
                        "  --editor             also measure editor memory, reported separately\n"
                        "  --json               emit JSON instead of the table\n"
                        "  --save-baseline P    write results to P for later comparison\n"
                        "  --compare-baseline P compare against results previously saved to P\n"
                        "  --tolerance PCT      comparison tolerance, default 5%%\n");
            return 0;
        }
    }

    Results results;
    results.scenario = scenarioName(scenario);
    results.sampleRate = sampleRate;
    results.blockSize = blockSize;
    results.maxInstances = maxInstances;

    std::vector<Instance> instances;
    instances.reserve(static_cast<std::size_t>(maxInstances));

    // Baseline is taken after JUCE has initialised and after the harness has
    // reserved its own storage, so what follows is attributable to the plugin
    // rather than to the benchmark warming up around it.
    results.baseline = measureSettled();

    const auto points = checkpointsFor(maxInstances);
    std::size_t nextPoint = 0;

    for (int n = 1; n <= maxInstances; ++n)
    {
        Instance instance;
        instance.processor = std::make_unique<PX3SynthAudioProcessor>();
        exerciseInstance(instance, scenario, sampleRate, blockSize);
        instances.push_back(std::move(instance));

        if (nextPoint < points.size() && n == points[nextPoint])
        {
            results.checkpoints.push_back({ n, measureSettled() });
            ++nextPoint;
        }
    }

    results.peak = results.checkpoints.empty() ? results.baseline
                                               : results.checkpoints.back().memory;

    instances.clear();
    instances.shrink_to_fit();
    results.afterDestruction = measureSettled();

    // Ask the allocator to hand back what it is holding for reuse. Without
    // this, "after destruction" mostly measures the allocator's cache and looks
    // like a leak that is not there.
    malloc_zone_pressure_relief(nullptr, 0);
    results.afterDrain = measureSettled();

    if (runEditor)
    {
        const auto editorCount = juce::jmin(4, maxInstances);
        std::vector<std::unique_ptr<PX3SynthAudioProcessor>> processors;
        std::vector<std::unique_ptr<juce::AudioProcessorEditor>> editors;
        processors.reserve(static_cast<std::size_t>(editorCount));
        editors.reserve(static_cast<std::size_t>(editorCount));

        for (int i = 0; i < editorCount; ++i)
        {
            processors.push_back(std::make_unique<PX3SynthAudioProcessor>());
            processors.back()->setPlayConfigDetails(0, 2, sampleRate, blockSize);
            processors.back()->prepareToPlay(sampleRate, blockSize);
        }
        const auto beforeEditors = measureSettled();

        for (int i = 0; i < editorCount; ++i)
        {
            editors.push_back(std::unique_ptr<juce::AudioProcessorEditor>(processors[static_cast<std::size_t>(i)]->createEditor()));

            // No message loop is pumped: the editor is constructed but never
            // shown, which is enough for its allocations to happen. Anything
            // that only allocates once a window is on screen is therefore not
            // counted here - see the note in the report.
            if (i == 0)
                results.editorFirstFootprint = measureSettled().footprint - beforeEditors.footprint;
        }

        const auto afterEditors = measureSettled();
        if (editorCount > 1)
        {
            results.editorIncrementalFootprint =
                (afterEditors.footprint - beforeEditors.footprint - results.editorFirstFootprint)
                / static_cast<double>(editorCount - 1);
        }
        results.editorCount = editorCount;
        results.editorMeasured = true;

        editors.clear();
        processors.clear();
    }

    const auto json = resultsToJson(results);

    if (saveBaselinePath.isNotEmpty())
    {
        juce::File file (saveBaselinePath);
        if (file.getParentDirectory().createDirectory() || file.getParentDirectory().exists())
            file.replaceWithText(json);
        if (! jsonOutput)
            std::printf("Baseline written to %s\n", file.getFullPathName().toRawUTF8());
    }

    if (jsonOutput)
        std::printf("%s\n", json.toRawUTF8());
    else
        printReport(results);

    int exitCode = 0;

    if (compareBaselinePath.isNotEmpty())
    {
        juce::File file (compareBaselinePath);
        if (! file.existsAsFile())
        {
            std::printf("\nNo baseline to compare at %s\n", file.getFullPathName().toRawUTF8());
            return 0;
        }

        const auto previous = juce::JSON::parse(file.loadFileAsString());
        std::printf("\nComparison against %s\n", file.getFileName().toRawUTF8());
        std::printf("--------------------------------------------------------------------\n");

        bool regressed = false;
        if (auto* previousInstances = previous["instances"].getArray())
        {
            for (const auto& c : results.checkpoints)
            {
                for (const auto& entry : *previousInstances)
                {
                    if (static_cast<int>(entry["count"]) != c.instances)
                        continue;

                    const auto was = static_cast<double>(entry["footprintMB"]);
                    const auto now = toMB(c.memory.footprint);
                    const auto delta = now - was;
                    const auto percent = was > 0.0 ? (delta / was) * 100.0 : 0.0;

                    std::printf("  %-3d instance(s): %s (%s%.1f%%)   %.1f MB -> %.1f MB\n",
                                c.instances,
                                (delta >= 0.0 ? "+" + juce::String(delta, 1) + " MB"
                                              : juce::String(delta, 1) + " MB").toRawUTF8(),
                                percent >= 0.0 ? "+" : "", percent, was, now);

                    if (percent > tolerancePercent)
                        regressed = true;
                }
            }
        }

        if (regressed)
        {
            std::printf("\nWARNING: memory usage increased by more than the %.1f%% tolerance.\n",
                        tolerancePercent);
            exitCode = 2;
        }
        else
        {
            std::printf("\nWithin the %.1f%% tolerance.\n", tolerancePercent);
        }
    }

    return exitCode;
}
