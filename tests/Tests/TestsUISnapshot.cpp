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
    for (int section = 0; section <= 6; ++section)
    {
        editor->debugSelectSection(section);
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
