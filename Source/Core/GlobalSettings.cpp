#include "GlobalSettings.h"

namespace px3
{
namespace
{
const juce::Identifier kSettingsRootId("PX3Settings");
const juce::Identifier kAnimationsEnabledId("animationsEnabled");
} // namespace

GlobalSettings& GlobalSettings::getInstance()
{
    // Constructed on first use and destroyed at exit, which is after every
    // editor that could be listening to it - so a listener outliving the
    // broadcaster is not a shape this can take.
    static GlobalSettings instance;
    return instance;
}

GlobalSettings::GlobalSettings()
{
    load();
}

juce::File& GlobalSettings::testFileOverride()
{
    static juce::File override;
    return override;
}

juce::File GlobalSettings::settingsFile()
{
    if (testFileOverride() != juce::File()) { return testFileOverride(); }

    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("P(X3)")
        .getChildFile("settings.xml");
}

void GlobalSettings::debugUseSettingsFile(const juce::File& file)
{
    testFileOverride() = file;
}

void GlobalSettings::debugReloadFromDisk()
{
    // What happens at the start of a new plugin session, without needing a new
    // process: the value comes back from the file rather than from memory.
    animationsEnabled.store(true, std::memory_order_relaxed);
    load();
    sendSynchronousChangeMessage();
}

bool GlobalSettings::areAnimationsEnabled() const noexcept
{
    return animationsEnabled.load(std::memory_order_relaxed);
}

void GlobalSettings::setAnimationsEnabled(bool shouldBeEnabled)
{
    if (animationsEnabled.exchange(shouldBeEnabled, std::memory_order_relaxed) == shouldBeEnabled)
    {
        return;
    }

    save();

    // Synchronous: the editors listening to this are on this thread, and the
    // point of the setting being global is that the other open windows change
    // now rather than on their next tick.
    sendSynchronousChangeMessage();
}

void GlobalSettings::load()
{
    const auto file = settingsFile();
    if (! file.existsAsFile()) { return; }

    if (auto xml = juce::XmlDocument::parse(file))
    {
        const auto tree = juce::ValueTree::fromXml(*xml);
        if (tree.hasType(kSettingsRootId) && tree.hasProperty(kAnimationsEnabledId))
        {
            animationsEnabled.store(static_cast<bool>(tree.getProperty(kAnimationsEnabledId)),
                                    std::memory_order_relaxed);
        }
    }
}

void GlobalSettings::save() const
{
    juce::ValueTree tree(kSettingsRootId);
    tree.setProperty(kAnimationsEnabledId, areAnimationsEnabled(), nullptr);

    const auto file = settingsFile();
    file.getParentDirectory().createDirectory();

    if (auto xml = tree.createXml())
    {
        // Best effort: a preference that cannot be written is not worth
        // interrupting anyone over, and the session keeps working either way.
        xml->writeTo(file);
    }
}

} // namespace px3
