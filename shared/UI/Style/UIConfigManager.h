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

    // Where the shipped UIConfig.json is, for any product.
    //
    // Every PX3 product needs the same answer, and the search is the same
    // every time: the bundle's own Resources first, because that is what an
    // installed plug-in has, then the source tree, so a development build
    // picks up edits without a reinstall.
    //
    // Returns a non-existent File if there is none, which callers must check -
    // a product that cannot find its style should look plain, not crash.
    static juce::File findShippingConfigFile();

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
