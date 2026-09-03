#include "GlobalSettings.h"

namespace px3
{
namespace
{
const juce::Identifier kSettingsRootId("PX3Settings");
const juce::Identifier kAnimationsEnabledId("animationsEnabled");
const juce::Identifier kPreReleaseChannelId("preReleaseChannel");
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
    preReleaseChannel.store(false, std::memory_order_relaxed);
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

bool GlobalSettings::isPreReleaseChannelEnabled() const noexcept
{
    return preReleaseChannel.load(std::memory_order_relaxed);
}

void GlobalSettings::setPreReleaseChannelEnabled(bool shouldBeEnabled)
{
    if (preReleaseChannel.exchange(shouldBeEnabled, std::memory_order_relaxed) == shouldBeEnabled)
    {
        return;
    }

    save();
    sendSynchronousChangeMessage();
}

void GlobalSettings::load()
{
    const auto file = settingsFile();
    if (! file.existsAsFile()) { return; }

    if (auto xml = juce::XmlDocument::parse(file))
    {
        const auto tree = juce::ValueTree::fromXml(*xml);
        if (tree.hasType(kSettingsRootId))
        {
            if (tree.hasProperty(kAnimationsEnabledId))
            {
                animationsEnabled.store(static_cast<bool>(tree.getProperty(kAnimationsEnabledId)),
                                        std::memory_order_relaxed);
            }

            // Absent in a file written before this existed, which means off -
            // the default, and the right answer for anyone who never asked for
            // pre-releases.
            if (tree.hasProperty(kPreReleaseChannelId))
            {
                preReleaseChannel.store(static_cast<bool>(tree.getProperty(kPreReleaseChannelId)),
                                        std::memory_order_relaxed);
            }
        }
    }
}

void GlobalSettings::save() const
{
    juce::ValueTree tree(kSettingsRootId);
    tree.setProperty(kAnimationsEnabledId, areAnimationsEnabled(), nullptr);
    tree.setProperty(kPreReleaseChannelId, isPreReleaseChannelEnabled(), nullptr);

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
