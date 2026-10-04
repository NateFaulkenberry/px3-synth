// Native-window UI benchmark (`PX3Bench uinative`).
//
// The `ui` mode paints into an offscreen image, which isolates the editor's own
// paint work but says nothing about what it costs in a real window: no native
// peer, no compositor, no message-thread load from timers, attachments and
// repaint coalescing. This mode puts the editor in an on-screen window, runs a
// realtime-paced audio thread and a host automation thread against it, and
// measures what a user's machine would see:
//
//   - message-thread and whole-process CPU, idle and under live audio/automation
//   - latency from a host parameter write to the bound control showing it
//   - resize: synchronous layout time and native repaint time per size
//   - editor open/close lifecycle time and memory across repeated cycles
//   - INIT and factory preset load latency, including the UI refresh and paint
//   - repeated host state recall: timing and byte-for-byte stability
//   - stale-cache check: an editor that has lived through preset changes must
//     paint the same pixels as a freshly constructed editor in the same state
//
// Uses only APIs present in both 0.7.6 and 0.8 so the same file can measure
// either. WindowServer/GPU compositing time is outside this process and is not
// included in any CPU figure here.

// System headers first: MacTypes.h declares a global Point, which becomes
// ambiguous with juce::Point once JUCE's headers have been seen.
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach.h>
#include <malloc/malloc.h>
#include <sys/resource.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PresetManager.h"
#include "GlobalSettings.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

double millisSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

double timevalSeconds(const timeval& tv) { return static_cast<double>(tv.tv_sec) + 1.0e-6 * static_cast<double>(tv.tv_usec); }

double processCpuSeconds()
{
    rusage usage {};
    getrusage(RUSAGE_SELF, &usage);
    return timevalSeconds(usage.ru_utime) + timevalSeconds(usage.ru_stime);
}

double threadCpuSeconds(thread_act_t thread)
{
    thread_basic_info_data_t info {};
    mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
    if (thread_info(thread, THREAD_BASIC_INFO, reinterpret_cast<thread_info_t>(&info), &count) != KERN_SUCCESS)
    {
        return 0.0;
    }
    return static_cast<double>(info.user_time.seconds) + 1.0e-6 * static_cast<double>(info.user_time.microseconds)
         + static_cast<double>(info.system_time.seconds) + 1.0e-6 * static_cast<double>(info.system_time.microseconds);
}

struct Memory
{
    double footprintMB { 0.0 };
    double heapMB { 0.0 };
};

Memory memoryNow()
{
    Memory memory;
    task_vm_info_data_t vm {};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&vm), &count) == KERN_SUCCESS)
    {
        memory.footprintMB = static_cast<double>(vm.phys_footprint) / (1024.0 * 1024.0);
    }
    malloc_statistics_t stats {};
    malloc_zone_statistics(nullptr, &stats);
    memory.heapMB = static_cast<double>(stats.size_in_use) / (1024.0 * 1024.0);
    return memory;
}

// Runs the real Cocoa run loop, which is where JUCE's message queue, timers,
// vblank-driven repaints and CoreAnimation commits all happen.
long long pumpSlices = 0;

void pumpFor(double milliseconds)
{
    const auto end = Clock::now() + std::chrono::microseconds(static_cast<long long>(milliseconds * 1000.0));
    while (Clock::now() < end)
    {
        ++pumpSlices;
        const auto remaining = std::chrono::duration<double>(end - Clock::now()).count();
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, juce::jmax(0.0005, remaining), false);
    }
}

struct Stats
{
    double median { 0.0 };
    double p95 { 0.0 };
    double max { 0.0 };
};

Stats summarise(std::vector<double> values)
{
    Stats stats;
    if (values.empty()) { return stats; }
    std::sort(values.begin(), values.end());
    stats.median = values[values.size() / 2];
    stats.p95 = values[juce::jmin(values.size() - 1, static_cast<std::size_t>(0.95 * static_cast<double>(values.size())))];
    stats.max = values.back();
    return stats;
}

void printMemory(const char* phase)
{
    const auto m = memoryNow();
    std::printf("    [memory after %s: footprint %.1f MB, heap %.1f MB]\n", phase, m.footprintMB, m.heapMB);
    std::fflush(stdout);
}

void printStats(const char* label, const std::vector<double>& values, const char* unit = "ms")
{
    const auto s = summarise(values);
    std::printf("  %-44s n=%-4zu median %8.3f  p95 %8.3f  max %8.3f %s\n",
                label, values.size(), s.median, s.p95, s.max, unit);
    std::fflush(stdout);
}

// Realtime-paced audio: 512-sample blocks at 48 kHz on their own thread, as a
// host would run them, so meters and animations see live audio.
class PacedAudio
{
public:
    explicit PacedAudio(juce::AudioProcessor& p) : processor(p) {}
    ~PacedAudio() { stop(); }

    void start(bool holdChord)
    {
        stop();
        running.store(true);
        thread = std::thread([this, holdChord]
        {
            juce::AudioBuffer<float> buffer(2, 512);
            auto next = Clock::now();
            int block = 0;
            while (running.load())
            {
                juce::MidiBuffer midi;
                if (holdChord && block == 0)
                {
                    for (const auto note : { 48, 55, 60, 64, 67 }) { midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0); }
                }
                buffer.clear();
                processor.processBlock(buffer, midi);
                ++block;
                next += std::chrono::microseconds(10667);
                std::this_thread::sleep_until(next);
            }
            juce::MidiBuffer off;
            for (const auto note : { 48, 55, 60, 64, 67 }) { off.addEvent(juce::MidiMessage::noteOff(1, note), 0); }
            buffer.clear();
            processor.processBlock(buffer, off);
        });
    }

    void stop()
    {
        running.store(false);
        if (thread.joinable()) { thread.join(); }
    }

private:
    juce::AudioProcessor& processor;
    std::atomic<bool> running { false };
    std::thread thread;
};

juce::RangedAudioParameter* findParam(juce::AudioProcessor& processor, const juce::String& id)
{
    for (auto* parameter : processor.getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter); ranged != nullptr && ranged->paramID == id)
        {
            return ranged;
        }
    }
    return nullptr;
}

void collectSliders(juce::Component& root, std::vector<juce::Slider*>& out)
{
    for (auto* child : root.getChildren())
    {
        if (auto* slider = dynamic_cast<juce::Slider*>(child); slider != nullptr && slider->isShowing())
        {
            out.push_back(slider);
        }
        collectSliders(*child, out);
    }
}

// Writes a parameter from a non-message thread, as host automation does.
void hostWrite(juce::RangedAudioParameter& parameter, float normalised)
{
    std::thread([&parameter, normalised] { parameter.setValueNotifyingHost(normalised); }).join();
}

double differingPixelFraction(const juce::Image& a, const juce::Image& b)
{
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight()) { return 1.0; }
    const juce::Image::BitmapData da(a, juce::Image::BitmapData::readOnly);
    const juce::Image::BitmapData db(b, juce::Image::BitmapData::readOnly);
    long long differing = 0;
    for (int y = 0; y < a.getHeight(); ++y)
    {
        for (int x = 0; x < a.getWidth(); ++x)
        {
            const auto pa = da.getPixelColour(x, y);
            const auto pb = db.getPixelColour(x, y);
            const auto delta = std::abs(pa.getRed() - pb.getRed()) + std::abs(pa.getGreen() - pb.getGreen())
                             + std::abs(pa.getBlue() - pb.getBlue());
            if (delta > 24) { ++differing; }
        }
    }
    return static_cast<double>(differing) / static_cast<double>(a.getWidth() * a.getHeight());
}
}

int runNativeUiBenchmark()
{
    const auto phaseSeconds = juce::jmax(1.0, juce::SystemStats::getEnvironmentVariable("PX3_BENCH_UI_SECONDS", "6").getDoubleValue());
    std::printf("\nPX3 NATIVE UI BENCHMARK (on-screen window)\n");
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        std::printf("  primary display %d x %d logical, scale %.1f\n",
                    display->userArea.getWidth(), display->userArea.getHeight(), display->scale);
    }

    // PX3_BENCH_UI_ANIMATIONS=on|off measures with that setting, on a scratch
    // settings file so the user's own settings are neither read nor written.
    if (const auto animations = juce::SystemStats::getEnvironmentVariable("PX3_BENCH_UI_ANIMATIONS", {}); animations.isNotEmpty())
    {
        const auto scratch = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("px3-bench-settings.xml");
        scratch.deleteFile();
        px3::GlobalSettings::debugUseSettingsFile(scratch);
        px3::GlobalSettings::getInstance().setAnimationsEnabled(animations.equalsIgnoreCase("on"));
        std::printf("  animations %s (scratch settings)\n", animations.equalsIgnoreCase("on") ? "ON" : "OFF");
    }

    const auto memStart = memoryNow();
    PX3SynthAudioProcessor processor;
    processor.setPlayConfigDetails(0, 2, 48000.0, 512);
    processor.prepareToPlay(48000.0, 512);
    PacedAudio audio(processor);
    const auto mainThread = mach_thread_self();

    auto window = std::make_unique<juce::DocumentWindow>("PX3 native UI benchmark", juce::Colours::black,
                                                         juce::DocumentWindow::allButtons, true);
    window->setUsingNativeTitleBar(true);

    std::unique_ptr<juce::AudioProcessorEditor> editor;
    auto openEditor = [&](double& createMs, double& firstPaintMs)
    {
        const auto start = Clock::now();
        editor.reset(processor.createEditor());
        createMs = millisSince(start);
        const auto requestedWidth = juce::SystemStats::getEnvironmentVariable("PX3_BENCH_UI_WIDTH", "0").getIntValue();
        const auto requestedHeight = juce::SystemStats::getEnvironmentVariable("PX3_BENCH_UI_HEIGHT", "0").getIntValue();
        if (requestedWidth > 0 && requestedHeight > 0) { editor->setSize(requestedWidth, requestedHeight); }
        window->setContentNonOwned(editor.get(), true);
        window->setVisible(true);
        const auto paintStart = Clock::now();
        if (auto* peer = window->getPeer()) { peer->performAnyPendingRepaintsNow(); }
        firstPaintMs = millisSince(paintStart);
    };
    auto closeEditor = [&]() -> double
    {
        const auto start = Clock::now();
        window->setVisible(false);
        window->clearContentComponent();
        editor.reset();
        return millisSince(start);
    };

    // Calibration: the same window and run loop with no editor in it. This is
    // the floor every CPU figure below sits on; it is the harness and the
    // framework, not the editor.
    {
        window->setContentOwned(new juce::Component(), false);
        window->setSize(1518, 918);
        window->setVisible(true);
        pumpFor(500.0);
        pumpSlices = 0;
        const auto wallStart = Clock::now();
        const auto mainStart = threadCpuSeconds(mainThread);
        pumpFor(phaseSeconds * 1000.0);
        const auto wall = millisSince(wallStart) / 1000.0;
        std::printf("  %-44s message thread %6.2f%% of one core (%lld run-loop slices)\n",
                    "calibration: empty window, no editor", 100.0 * (threadCpuSeconds(mainThread) - mainStart) / wall, pumpSlices);
        window->setVisible(false);
        window->clearContentComponent();
    }

    double createMs = 0.0, firstPaintMs = 0.0;
    openEditor(createMs, firstPaintMs);
    window->centreWithSize(window->getWidth(), window->getHeight());
    pumpFor(1500.0);
    std::printf("  editor %d x %d; construction %.2f ms; first native paint %.2f ms\n",
                editor->getWidth(), editor->getHeight(), createMs, firstPaintMs);
    const auto memWithEditor = memoryNow();

    // PX3_BENCH_UI_LIFECYCLE_ONLY=1 skips straight to the open/close cycles,
    // so their memory is not read on top of the stress phases' allocations.
    const auto lifecycleOnly = juce::SystemStats::getEnvironmentVariable("PX3_BENCH_UI_LIFECYCLE_ONLY", {}).isNotEmpty();
    if (! lifecycleOnly)
    {
    // --- CPU: idle and active --------------------------------------------------
    auto measureCpu = [&](const char* label, bool chord, bool automate)
    {
        audio.start(chord);
        std::atomic<bool> automating { automate };
        std::thread automation;
        if (automate)
        {
            automation = std::thread([&]
            {
                const juce::StringArray ids { "voice.filter1.cutoff", "mix.master.level", "voice.osc1.macro.a",
                                              "filter1Cutoff", "masterGain" };
                std::vector<juce::RangedAudioParameter*> targets;
                for (const auto& id : ids) { if (auto* p = findParam(processor, id)) { targets.push_back(p); } }
                for (int tick = 0; automating.load(); ++tick)
                {
                    const auto value = 0.5f + 0.4f * std::sin(static_cast<float>(tick) * 0.07f);
                    for (auto* p : targets) { p->setValueNotifyingHost(value); }
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));
                }
            });
        }
        pumpFor(500.0);
        const auto wallStart = Clock::now();
        const auto mainStart = threadCpuSeconds(mainThread);
        const auto processStart = processCpuSeconds();
        pumpFor(phaseSeconds * 1000.0);
        const auto wall = millisSince(wallStart) / 1000.0;
        const auto mainCpu = threadCpuSeconds(mainThread) - mainStart;
        const auto processCpu = processCpuSeconds() - processStart;
        automating.store(false);
        if (automation.joinable()) { automation.join(); }
        audio.stop();
        std::printf("  %-44s message thread %6.2f%%  process %6.2f%% of one core over %.1f s\n",
                    label, 100.0 * mainCpu / wall, 100.0 * processCpu / wall, wall);
        std::fflush(stdout);
    };
    pumpSlices = 0;
    measureCpu("idle (audio running, silent)", false, false);
    std::printf("    (%lld run-loop slices during idle phase incl. settle)\n", pumpSlices);
    measureCpu("active (chord held, no automation)", true, false);
    measureCpu("active (chord + 60 Hz host automation)", true, true);
    printMemory("CPU phases");

    // --- Automation response -----------------------------------------------------
    {
        const juce::StringArray candidates { "mix.master.level", "voice.filter1.cutoff", "voice.amp.attack",
                                             "masterGain", "filter1Cutoff", "ampAttack" };
        juce::RangedAudioParameter* chosen = nullptr;
        int sliderIndex = -1;
        std::vector<juce::Slider*> sliders;
        collectSliders(*editor, sliders);
        for (const auto& id : candidates)
        {
            auto* parameter = findParam(processor, id);
            if (parameter == nullptr) { continue; }
            hostWrite(*parameter, 0.2f);
            pumpFor(150.0);
            std::vector<double> before;
            for (auto* s : sliders) { before.push_back(s->getValue()); }
            hostWrite(*parameter, 0.8f);
            pumpFor(150.0);
            for (std::size_t i = 0; i < sliders.size(); ++i)
            {
                if (sliders[i]->getValue() != before[i]) { chosen = parameter; sliderIndex = static_cast<int>(i); break; }
            }
            if (chosen != nullptr) { break; }
        }

        if (chosen == nullptr)
        {
            std::printf("  automation response: no candidate parameter has a visible control\n");
        }
        else
        {
            std::vector<double> latencies;
            int timeouts = 0;
            for (int trial = 0; trial < 40; ++trial)
            {
                auto* slider = sliders[static_cast<std::size_t>(sliderIndex)];
                const auto before = slider->getValue();
                const auto target = trial % 2 == 0 ? 0.25f : 0.75f;
                const auto start = Clock::now();
                hostWrite(*chosen, target);
                auto seen = false;
                while (millisSince(start) < 500.0)
                {
                    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.0005, true);
                    if (slider->getValue() != before) { seen = true; break; }
                }
                if (seen) { latencies.push_back(millisSince(start)); } else { ++timeouts; }
                pumpFor(40.0);
            }
            std::printf("  automation target: %s (timeouts %d)\n", chosen->paramID.toRawUTF8(), timeouts);
            printStats("host write -> bound control updated", latencies);
        }
    }

    // --- Resize --------------------------------------------------------------------
    {
        const juce::Rectangle<int> sizes[] = { { 0, 0, 1100, 680 }, { 0, 0, 1518, 918 }, { 0, 0, 1900, 980 },
                                               { 0, 0, 980, 600 },  { 0, 0, 1322, 826 }, { 0, 0, 1700, 900 } };
        const auto originalWidth = editor->getWidth();
        const auto originalHeight = editor->getHeight();
        std::vector<double> layout, paint;
        for (int repeat = 0; repeat < 3; ++repeat)
        {
            for (const auto& size : sizes)
            {
                const auto start = Clock::now();
                editor->setSize(size.getWidth(), size.getHeight());
                layout.push_back(millisSince(start));
                const auto paintStart = Clock::now();
                if (auto* peer = window->getPeer()) { peer->performAnyPendingRepaintsNow(); }
                paint.push_back(millisSince(paintStart));
                pumpFor(60.0);
            }
        }
        editor->setSize(originalWidth, originalHeight);
        pumpFor(300.0);
        printStats("resize: setSize + layout", layout);
        printStats("resize: native repaint after resize", paint);
        printMemory("resize");
    }

    // --- INIT and factory preset latency --------------------------------------------
    std::vector<PresetManager::PresetRecord> factory;
    PresetManager presets(processor);
    {
        juce::String error;
        if (! presets.initialise(error)) { std::printf("  preset manager failed: %s\n", error.toRawUTF8()); }
        PresetManager::Query query;
        query.includeUser = false;
        for (const auto& record : presets.queryPresets(query)) { if (! record.isInit) { factory.push_back(record); } }

        auto* synthEditor = dynamic_cast<PX3SynthAudioProcessorEditor*>(editor.get());
        auto refreshAndPaint = [&]
        {
            if (synthEditor != nullptr) { synthEditor->debugTimerTick(); }
            if (auto* peer = window->getPeer()) { peer->performAnyPendingRepaintsNow(); }
        };

        std::vector<double> initApply, initVisible;
        for (int i = 0; i < 12; ++i)
        {
            const auto start = Clock::now();
            presets.loadInitState(error);
            initApply.push_back(millisSince(start));
            refreshAndPaint();
            initVisible.push_back(millisSince(start));
            pumpFor(30.0);
        }
        std::vector<double> presetApply, presetVisible;
        int failures = 0;
        for (const auto& record : factory)
        {
            const auto start = Clock::now();
            if (! presets.loadPreset(record, error)) { ++failures; continue; }
            presetApply.push_back(millisSince(start));
            refreshAndPaint();
            presetVisible.push_back(millisSince(start));
            pumpFor(30.0);
        }
        printStats("INIT: load + apply", initApply);
        printStats("INIT: load + apply + UI refresh + paint", initVisible);
        std::printf("  factory presets: %zu (load failures %d)\n", factory.size(), failures);
        printStats("factory preset: load + apply", presetApply);
        printStats("factory preset: load + apply + UI refresh + paint", presetVisible);
        printMemory("presets");
    }

    // --- Repeated host state recall --------------------------------------------------
    {
        juce::String error;
        if (! factory.empty()) { presets.loadPreset(factory[factory.size() / 2], error); }
        pumpFor(100.0);
        juce::MemoryBlock reference;
        processor.getStateInformation(reference);
        std::vector<double> setTimes, getTimes;
        int mismatches = 0;
        for (int i = 0; i < 50; ++i)
        {
            auto start = Clock::now();
            processor.setStateInformation(reference.getData(), static_cast<int>(reference.getSize()));
            setTimes.push_back(millisSince(start));
            juce::MemoryBlock again;
            start = Clock::now();
            processor.getStateInformation(again);
            getTimes.push_back(millisSince(start));
            if (again != reference)
            {
                if (mismatches == 0)
                {
                    const auto a = juce::String::fromUTF8(static_cast<const char*>(reference.getData()), static_cast<int>(reference.getSize()));
                    const auto b = juce::String::fromUTF8(static_cast<const char*>(again.getData()), static_cast<int>(again.getSize()));
                    int at = 0;
                    while (at < juce::jmin(a.length(), b.length()) && a[at] == b[at]) { ++at; }
                    std::printf("    first state difference at byte %d of %zu/%zu: \"%s\" vs \"%s\"\n", at,
                                reference.getSize(), again.getSize(),
                                a.substring(juce::jmax(0, at - 60), at + 60).replace("\n", " ").toRawUTF8(),
                                b.substring(juce::jmax(0, at - 60), at + 60).replace("\n", " ").toRawUTF8());
                }
                ++mismatches;
            }
            if (i % 10 == 0) { pumpFor(20.0); }
        }
        std::printf("  host state: %zu bytes; %d of 50 recalls not byte-identical\n", reference.getSize(), mismatches);
        printStats("setStateInformation", setTimes);
        printStats("getStateInformation", getTimes);
        printMemory("state recall");
    }

    // --- Stale cache: lived-in editor vs fresh editor -------------------------------
    {
        juce::String error;
        if (factory.size() >= 2)
        {
            presets.loadPreset(factory.front(), error);
            pumpFor(400.0);
            presets.loadPreset(factory.back(), error);
            if (auto* e = dynamic_cast<PX3SynthAudioProcessorEditor*>(editor.get())) { e->debugTimerTick(); }
            pumpFor(800.0);
            const auto lived = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
            double c = 0.0, f = 0.0;
            closeEditor();
            openEditor(c, f);
            pumpFor(800.0);
            const auto fresh = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
            closeEditor();
            openEditor(c, f);
            pumpFor(800.0);
            const auto freshAgain = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
            std::printf("  stale-cache check: %.4f%% of pixels differ lived-in vs fresh; noise floor fresh vs fresh %.4f%%\n",
                        100.0 * differingPixelFraction(lived, fresh), 100.0 * differingPixelFraction(fresh, freshAgain));
            juce::PNGImageFormat png;
            for (const auto& [name, image] : { std::pair { "lived", lived }, std::pair { "fresh", fresh } })
            {
                const auto dir = juce::SystemStats::getEnvironmentVariable("PX3_BENCH_UI_SNAPSHOT_DIR", {});
                if (dir.isEmpty()) { break; }
                juce::FileOutputStream out(juce::File(dir).getChildFile(juce::String(name) + ".png"));
                if (out.openedOk()) { out.setPosition(0); out.truncate(); png.writeImageToStream(image, out); }
            }
            printMemory("stale-cache check");
        }
    }

    }

    // --- Editor lifecycle --------------------------------------------------------------
    {
        closeEditor();
        pumpFor(300.0);
        const auto memClosed = memoryNow();
        std::vector<double> creates, paints, closes;
        Memory afterFirst {};
        const auto cycles = juce::jmax(2, juce::SystemStats::getEnvironmentVariable("PX3_BENCH_UI_CYCLES", "15").getIntValue());
        for (int cycle = 0; cycle < cycles; ++cycle)
        {
            double c = 0.0, f = 0.0;
            openEditor(c, f);
            creates.push_back(c);
            paints.push_back(f);
            pumpFor(150.0);
            closes.push_back(closeEditor());
            pumpFor(100.0);
            if (cycle == 0) { afterFirst = memoryNow(); }
            if (lifecycleOnly)
            {
                const auto m = memoryNow();
                std::printf("    cycle %2d: footprint %.1f MB, heap %.1f MB\n", cycle + 1, m.footprintMB, m.heapMB);
            }
        }
        const auto memAfter = memoryNow();
        printStats("lifecycle: editor construction", creates);
        printStats("lifecycle: first native paint", paints);
        printStats("lifecycle: close + destroy", closes);
        std::printf("  memory MB (footprint / heap): start %.1f / %.1f; editor open %.1f / %.1f; closed %.1f / %.1f;\n"
                    "    after 1 cycle %.1f / %.1f; after %d cycles %.1f / %.1f (growth/cycle after first %.3f / %.3f)\n",
                    memStart.footprintMB, memStart.heapMB, memWithEditor.footprintMB, memWithEditor.heapMB,
                    memClosed.footprintMB, memClosed.heapMB, afterFirst.footprintMB, afterFirst.heapMB, cycles,
                    memAfter.footprintMB, memAfter.heapMB,
                    (memAfter.footprintMB - afterFirst.footprintMB) / (cycles - 1),
                    (memAfter.heapMB - afterFirst.heapMB) / (cycles - 1));
    }

    window.reset();
    processor.releaseResources();
    return 0;
}
