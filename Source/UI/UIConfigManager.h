#pragma once

#include <JuceHeader.h>

#include <memory>

#include "UIConfig.h"

class UIConfigManager final
{
public:
    struct LoadResult
    {
        bool loaded { false };
        bool changed { false };
        juce::String message;
    };

    UIConfigManager();

    void setConfigFile(const juce::File& file);
    const juce::File& getConfigFile() const;

    std::shared_ptr<const UIConfig> getConfig() const;
    LoadResult loadInitial();
    LoadResult reloadIfChanged();

private:
    LoadResult loadFromDisk(bool forceReload);

    juce::File configFile;
    juce::Time lastWriteTime;
    std::shared_ptr<UIConfig> activeConfig;
};
