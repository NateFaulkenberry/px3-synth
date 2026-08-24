#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"

#include <vector>

/**
 * Preset file management for P(X3).
 *
 * This layer intentionally reuses the processor's state representation
 * (`PX3_STATE`) instead of maintaining a second independent model.
 *
 * Result: DAW project restore and .px3preset files round-trip through the same
 * parameter/state architecture, which keeps automation and compatibility stable.
 */
class PresetManager
{
public:
    static constexpr int currentPresetFormatVersion = 1;
    static constexpr const char* presetFileExtension = ".px3preset";

    struct PresetMetadata
    {
        juce::String name;
        juce::String category;
        juce::String author;
        juce::String description;
    };

    struct PresetRecord
    {
        juce::File file;
        PresetMetadata metadata;
        juce::String pluginVersion;
        bool isFactory { false };
        bool isFavorite { false };
        int presetVersion { currentPresetFormatVersion };
        juce::int64 modifiedTimeMs { 0 };
    };

    struct Query
    {
        bool includeFactory { true };
        bool includeUser { true };
        bool favoritesOnly { false };
        juce::String category;
        juce::String searchText;
    };

    explicit PresetManager(PX3SynthAudioProcessor& processorIn);

    bool initialise(juce::String& error);
    void refreshIndex();

    std::vector<juce::String> getAllCategories() const;
    std::vector<PresetRecord> queryPresets(const Query& query) const;

    bool loadPreset(const PresetRecord& preset, juce::String& error);
    bool loadPresetFile(const juce::File& file, juce::String& error);

    bool saveUserPreset(const PresetMetadata& metadata,
                        bool overwrite,
                        juce::String& error,
                        juce::File* outFile = nullptr);

    bool deleteUserPreset(const PresetRecord& preset, juce::String& error);

    bool setFavorite(const PresetRecord& preset, bool favorite, juce::String& error);
    bool isFavorite(const juce::File& presetFile) const;

    bool exportPreset(const PresetRecord& preset, const juce::File& destinationFile, juce::String& error) const;
    bool importPreset(const juce::File& sourceFile, juce::String& error, PresetRecord* outImported = nullptr);

    bool dumpCurrentStateToPresetFile(const juce::File& destinationFile,
                                      const PresetMetadata& metadata,
                                      bool overwrite,
                                      bool validateRoundTrip,
                                      juce::String& error,
                                      int* outSerializedBytes = nullptr);

    bool createInitPresetIfMissing(juce::String& error);

    const PresetRecord* findByFile(const juce::File& file) const;

    juce::File getRootDir() const;
    juce::File getPresetRootDir() const;
    juce::File getFactoryPresetRootDir() const;
    juce::File getUserPresetRootDir() const;
    juce::File getAssetsRootDir() const;
    juce::File getImageAssetsDir() const;
    juce::File getAudioAssetsDir() const;
    juce::File getSettingsDir() const;

private:
    struct AssetReference
    {
        juce::String type;
        juce::String parameterKey;
        juce::String originalPath;
        juce::String fileName;
        juce::String hash;
        bool embedded { false };
        juce::String base64Data;
    };

    PX3SynthAudioProcessor& processor;

    std::vector<PresetRecord> indexedPresets;
    juce::StringArray favoriteIds;

    static juce::String sanitizeFileName(const juce::String& input);
    static juce::String normalizeCategory(const juce::String& category);
    static juce::String canonicalPresetId(const juce::File& baseDir, const juce::File& presetFile);
    static juce::String computeFileHash(const juce::File& file);

    bool ensureDirectoryLayout(juce::String& error) const;
    bool ensureFactoryPresetLibrary(juce::String& error);

    bool readPresetFile(const juce::File& file,
                        juce::ValueTree& outPresetTree,
                        PresetRecord* outRecord,
                        juce::String& error) const;

    bool writePresetFile(const juce::File& file,
                         const juce::ValueTree& presetTree,
                         juce::String& error) const;

    juce::ValueTree buildPresetTreeFromCurrentState(const PresetMetadata& metadata,
                                                    bool asFactory,
                                                    juce::String& error) const;

    bool applyPresetTree(const juce::ValueTree& presetTree, juce::String& error);

    juce::ValueTree migratePresetTreeIfNeeded(const juce::ValueTree& presetTree,
                                              juce::String& error) const;

    void collectAssetsForState(juce::ValueTree& pluginState,
                               juce::ValueTree& assetsNode) const;

    bool materializeEmbeddedAssets(juce::ValueTree& pluginState,
                                   const juce::ValueTree& assetsNode,
                                   juce::String& error) const;

    bool loadFavorites();
    bool saveFavorites(juce::String& error) const;
    void rebuildIndex();

    PresetRecord makeRecordFromTree(const juce::File& file,
                                    bool isFactory,
                                    const juce::ValueTree& presetTree) const;
};
