// Developer mode, not pass/fail: `PX3Tests uisnapshot <dir> [w h]` renders the
// editor at its default size on every section and writes, per section, a PNG
// and a text dump of every visible component's bounds in editor coordinates.
// Run it before and after a layout refactor and diff the dumps: an identical
// dump is the strongest "visually equivalent" statement there is.

#include "TestSupport.h"

#include <typeinfo>

namespace px3tests
{
namespace
{
void dumpComponent(juce::Component& root, juce::Component& c, int depth, juce::String& out)
{
    if (depth > 0 && ! c.isVisible()) { return; }
    const auto r = root.getLocalArea(&c, c.getLocalBounds());
    juce::String name = c.getName();
    if (name.isEmpty()) { name = c.getComponentID(); }
    if (auto* button = dynamic_cast<juce::Button*>(&c); button != nullptr && name.isEmpty())
    {
        name = button->getButtonText();
    }
    out << juce::String::repeatedString("  ", depth)
        << typeid(c).name() << " '" << name << "' "
        << r.getX() << "," << r.getY() << " " << r.getWidth() << "x" << r.getHeight() << "\n";
    for (auto* child : c.getChildren())
    {
        dumpComponent(root, *child, depth + 1, out);
    }
}
}

int runUISnapshot(const juce::String& outDir, int width, int height)
{
    const juce::File dir(outDir);
    dir.createDirectory();

    PX3SynthAudioProcessor processor;
    processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay(kSampleRate, kBlockSize);

    std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
    auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());
    if (editor == nullptr) { return 1; }

    editor->setSize(width, height);
    // PX3_SNAPSHOT_ROUTES=1 patches a few routes first, so the MOD page, the
    // patch bar and the knob rings have something to show.
    if (juce::SystemStats::getEnvironmentVariable("PX3_SNAPSHOT_ROUTES", {}).isNotEmpty())
    {
        juce::String error;
        processor.setGraphRoute(0, { 0, "voice.filter1.cutoff" }, error);
        processor.getGraphRouteDepthParam(0).setValueNotifyingHost(processor.getGraphRouteDepthParam(0).convertTo0to1(0.6f));
        processor.setGraphRoute(1, { 3, "voice.osc1.tuning.cents" }, error);
        processor.getGraphRouteDepthParam(1).setValueNotifyingHost(processor.getGraphRouteDepthParam(1).convertTo0to1(0.4f));
        processor.setGraphRoute(2, { 1, "voice.filter1.cutoff" }, error);
        processor.getGraphRouteDepthParam(2).setValueNotifyingHost(processor.getGraphRouteDepthParam(2).convertTo0to1(-0.3f));
        processor.setGraphRoute(3, { 6, "voice.filter2.resonance" }, error);
        processor.getGraphRouteDepthParam(3).setValueNotifyingHost(processor.getGraphRouteDepthParam(3).convertTo0to1(0.8f));
    }
    for (int section = 0; section <= 6; ++section)
    {
        editor->debugTimerTick();
        editor->debugSelectSection(section);
        editor->debugRefreshModRouting();
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds());
        auto png = dir.getChildFile("section" + juce::String(section) + ".png");
        png.deleteFile();
        {
            juce::FileOutputStream stream(png);
            juce::PNGImageFormat().writeImageToStream(image, stream);
        }

        juce::String text;
        dumpComponent(*editor, *editor, 0, text);
        dir.getChildFile("section" + juce::String(section) + ".txt").replaceWithText(text);
    }
    std::printf("wrote snapshots to %s\n", dir.getFullPathName().toRawUTF8());
    return 0;
}
}
