#include "UIConfigManager.h"

juce::File UIConfigManager::findArtworkFile(const juce::String& fileName)
{
    if (fileName.isEmpty()) { return {}; }

    const auto executableDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                                   .getParentDirectory();

    // Installed: inside the bundle that is running. The same walk the config
    // uses, for the same reason - a plug-in's own binary is what dladdr
    // reports, so this climbs its bundle rather than the host's.
    for (auto probe = executableDir; probe.exists(); probe = probe.getParentDirectory())
    {
        const auto inBundle = probe.getChildFile("Resources/Artwork").getChildFile(fileName);
        if (inBundle.existsAsFile()) { return inBundle; }
        if (probe.getParentDirectory() == probe) { break; }
    }

    // A development build, run from the repository.
    const auto fromCwd = juce::File::getCurrentWorkingDirectory()
                             .getChildFile("shared/UI/Artwork").getChildFile(fileName);
    if (fromCwd.existsAsFile()) { return fromCwd; }

    for (auto probe = executableDir; probe.exists(); probe = probe.getParentDirectory())
    {
        const auto inTree = probe.getChildFile("shared/UI/Artwork").getChildFile(fileName);
        if (inTree.existsAsFile()) { return inTree; }
        if (probe.getParentDirectory() == probe) { break; }
    }

    return {};
}

juce::File UIConfigManager::findShippingConfigFile()
{
    const auto executableDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                                   .getParentDirectory();

    // Installed: inside the bundle that is running.
    for (auto probe = executableDir; probe.exists(); probe = probe.getParentDirectory())
    {
        const auto inBundle = probe.getChildFile("Resources/UIConfig.json");
        if (inBundle.existsAsFile()) { return inBundle; }
        if (probe.getParentDirectory() == probe) { break; }
    }

    // A development build, run from the repository.
    const auto fromCwd = juce::File::getCurrentWorkingDirectory()
                             .getChildFile("shared/UI/Style/UIConfig.json");
    if (fromCwd.existsAsFile()) { return fromCwd; }

    for (auto probe = executableDir; probe.exists(); probe = probe.getParentDirectory())
    {
        const auto inTree = probe.getChildFile("shared/UI/Style/UIConfig.json");
        if (inTree.existsAsFile()) { return inTree; }
        if (probe.getParentDirectory() == probe) { break; }
    }

    return {};
}

UIConfigManager::UIConfigManager() = default;

void UIConfigManager::setConfigFile(const juce::File& file)
{
    configFile = file;
    lastWriteTime = juce::Time();
}

const juce::File& UIConfigManager::getConfigFile() const
{
    return configFile;
}

std::shared_ptr<const UIConfig> UIConfigManager::getConfig() const
{
    return activeConfig;
}

UIConfigManager::LoadResult UIConfigManager::loadInitial()
{
    return loadFromDisk(true);
}

UIConfigManager::LoadResult UIConfigManager::reloadIfChanged()
{
    return loadFromDisk(false);
}

UIConfigManager::LoadResult UIConfigManager::loadFromDisk(bool forceReload)
{
    LoadResult result;

    if (configFile == juce::File())
    {
        result.message = "UIConfig file not configured.";
        return result;
    }

    if (!configFile.existsAsFile())
    {
        result.message = "UIConfig file not found: " + configFile.getFullPathName();
        return result;
    }

    const auto writeTime = configFile.getLastModificationTime();
    if (!forceReload && writeTime == lastWriteTime)
    {
        return result;
    }

    const auto raw = configFile.loadFileAsString();
    if (raw.isEmpty())
    {
        result.message = "Unable to read UIConfig file: " + configFile.getFullPathName();
        return result;
    }

    juce::String parseError;
    auto parsed = UIConfig::fromJsonText(raw, parseError);
    if (parsed == nullptr || !parsed->isValid())
    {
        result.message = parseError.isNotEmpty() ? parseError
                                                 : "Unknown UIConfig parse failure.";
        return result;
    }

    const auto wasEmpty = (activeConfig == nullptr);
    activeConfig = std::move(parsed);
    lastWriteTime = writeTime;
    result.loaded = true;
    result.changed = forceReload || !wasEmpty;

    auto msg = juce::String("Loaded UIConfig: ") + configFile.getFullPathName();
    const auto warning = activeConfig->getValidationWarning();
    if (warning.isNotEmpty())
    {
        msg << " | Warning: " << warning;
    }

    result.message = msg;
    return result;
}
