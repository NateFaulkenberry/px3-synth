// Developer mode, not pass/fail: `PX3Tests uisnapshot <dir> [w h]` renders the
// editor at its default size on every section and writes, per section, a PNG
// and a text dump of every visible component's bounds in editor coordinates.
// Run it before and after a layout refactor and diff the dumps: an identical
// dump is the strongest "visually equivalent" statement there is.

#include "TestSupport.h"

#include "../../products/PX3Reverb/PluginEditor.h"
#include "ReverbPresets.h"

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
        processor.setGraphRoute(1, { PX3SynthAudioProcessor::kLfoSourceCount, "voice.osc1.tuning.cents" }, error);
        processor.getGraphRouteDepthParam(1).setValueNotifyingHost(processor.getGraphRouteDepthParam(1).convertTo0to1(0.4f));
        processor.setGraphRoute(2, { 1, "voice.filter1.cutoff" }, error);
        processor.getGraphRouteDepthParam(2).setValueNotifyingHost(processor.getGraphRouteDepthParam(2).convertTo0to1(-0.3f));
        processor.setGraphRoute(3, { PX3SynthAudioProcessor::kLfoSourceCount + PX3SynthAudioProcessor::kEnvelopeSourceCount, "voice.filter2.resonance" }, error);
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
    // The whole FX page, scrolled region included: the rack rendered at its
    // full height on the chassis colour, with a send card bypassed and the
    // first send card hovered so the rail states show.
    if (auto* fx = editor->debugFxPanel())
    {
        editor->debugSelectSection(4);   // FX
        auto& rack = fx->debugRack();
        const auto send = rack.stagesIn(px3::ui::FxDomain::send);
        if (! send.empty()) { rack.setHoveredStage(send.front()); }
        juce::Image page(juce::Image::ARGB, rack.getWidth(), rack.getHeight(), true);
        {
            juce::Graphics g(page);
            g.fillAll(juce::Colour(0xff121417));
            g.drawImageAt(rack.createComponentSnapshot(rack.getLocalBounds()), 0, 0);
        }
        rack.setHoveredStage(-1);
        auto png = dir.getChildFile("fx-rack.png");
        png.deleteFile();
        juce::FileOutputStream stream(png);
        juce::PNGImageFormat().writeImageToStream(page, stream);
    }
    // The two insert sheets, each live and bypassed, at 2x for detail work.
    for (const auto wantsEq : { true, false })
    {
        for (const auto live : { true, false })
        {
            const juce::String id = wantsEq ? "mix.dry.insert.eq.enabled" : "mix.dry.insert.comp.enabled";
            for (auto* p : processor.getParameters())
            {
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p); ranged != nullptr && ranged->getParameterID() == id)
                {
                    ranged->setValueNotifyingHost(live ? 1.0f : 0.0f);
                }
            }
            editor->debugOpenBusInsert(PX3SynthAudioProcessor::dryBusInsert, wantsEq);
            for (int tick = 0; tick < 4; ++tick) { editor->debugTimerTick(); }
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
            auto png = dir.getChildFile(juce::String(wantsEq ? "eq" : "comp") + (live ? "" : "-bypassed") + ".png");
            png.deleteFile();
            juce::FileOutputStream stream(png);
            juce::PNGImageFormat().writeImageToStream(image, stream);
            editor->debugCloseBusInsert();
        }
    }
    // The standalone PX3 Reverb card, on each type with a preset applied, at 2x.
    {
        PX3ReverbAudioProcessor reverb;
        std::unique_ptr<juce::AudioProcessorEditor> fxEditor(reverb.createEditor());
        if (auto* reverbEditor = dynamic_cast<PX3ReverbAudioProcessorEditor*>(fxEditor.get()))
        {
            for (int type = 0; type < px3::reverb::kTypeCount; ++type)
            {
                reverb.applyPreset(type, px3::reverb::presetsForType(type).front().name);
                if (type == px3::reverb::cloud)
                    reverb.control(px3::reverb::Control::decay).setValueNotifyingHost(0.5f);   // shows "Name*"
                reverbEditor->debugRefresh();
                const auto image = fxEditor->createComponentSnapshot(fxEditor->getLocalBounds(), true, 2.0f);
                auto png = dir.getChildFile(juce::String("px3reverb-") + px3::reverb::kTypeNames[type] + ".png");
                png.deleteFile();
                juce::FileOutputStream stream(png);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
        }
    }
    std::printf("wrote snapshots to %s\n", dir.getFullPathName().toRawUTF8());
    return 0;
}
}
