#include "PresetManager.h"

#include "FactoryPresets.h"

#include <algorithm>
#include <limits>

namespace
{
constexpr auto presetRootName = "Presets";
constexpr auto factoryRootName = "Factory";
constexpr auto userRootName = "User";
constexpr auto settingsRootName = "Settings";
constexpr auto favoritesFileName = "favorites.xml";

const std::array<const char*, 5> kDefaultCategories {
    "BASS",
    "LEADS",
    "PADS",
    "PLUCKS",
    "EXPERIMENTAL"
};

const juce::Identifier kPresetRootId("PX3_PRESET");
const juce::Identifier kPluginStateId("PX3_STATE");
const juce::Identifier kAssetsId("ASSETS");

const juce::String kPresetExtension(PresetManager::presetFileExtension);
}

const juce::String& PresetManager::initPresetName()
{
    // Written with the dashes because it is what the user sees, in the tab and
    // at the top of the browser, and it is not the name of anything on disk.
    static const juce::String name { "- INIT -" };
    return name;
}

PresetManager::PresetManager(PX3SynthAudioProcessor& processorIn)
    : processor(processorIn)
{
}

bool PresetManager::initialise(juce::String& error)
{
    if (!ensureDirectoryLayout(error))
    {
        return false;
    }

    loadFavorites();

    if (!ensureFactoryPresetLibrary(error))
    {
        return false;
    }

    rebuildIndex();
    return true;
}

void PresetManager::refreshIndex()
{
    rebuildIndex();
}

std::vector<juce::String> PresetManager::getAllCategories() const
{
    std::vector<juce::String> categories;
    categories.reserve(indexedPresets.size() + kDefaultCategories.size());

    for (const auto* category : kDefaultCategories)
    {
        categories.push_back(category);
    }

    for (const auto& p : indexedPresets)
    {
        const auto normalized = normalizeCategory(p.metadata.category);
        if (normalized.isNotEmpty())
        {
            categories.push_back(normalized);
        }
    }

    std::sort(categories.begin(), categories.end(), [](const juce::String& a, const juce::String& b)
    {
        return a.compareIgnoreCase(b) < 0;
    });

    categories.erase(std::unique(categories.begin(), categories.end(), [](const juce::String& a, const juce::String& b)
    {
        return a.equalsIgnoreCase(b);
    }), categories.end());

    return categories;
}

std::vector<PresetManager::PresetRecord> PresetManager::queryPresets(const Query& query) const
{
    std::vector<PresetRecord> result;
    const auto search = query.searchText.toLowerCase().trim();
    const auto category = normalizeCategory(query.category);

    // INIT leads the list. It is not indexed with the presets because it is not
    // one - it has no file, no category and no author - but it is the thing you
    // reach for to start again, so it belongs at the top rather than nowhere.
    // It is a state, so a favourites view and a search that does not name it
    // both leave it out.
    if (! query.favoritesOnly && (search.isEmpty() || initPresetName().toLowerCase().contains(search)))
    {
        PresetRecord init;
        init.metadata.name = initPresetName();
        init.metadata.description = "The state the plugin loads with.";
        init.isInit = true;
        result.push_back(init);
    }

    for (const auto& preset : indexedPresets)
    {
        if (!query.includeFactory && preset.isFactory)
        {
            continue;
        }

        if (!query.includeUser && !preset.isFactory)
        {
            continue;
        }

        if (query.favoritesOnly && !preset.isFavorite)
        {
            continue;
        }

        if (category.isNotEmpty() && category != "ALL" && !normalizeCategory(preset.metadata.category).equalsIgnoreCase(category))
        {
            continue;
        }

        if (search.isNotEmpty())
        {
            const auto haystack = (preset.metadata.name + "\n"
                                   + preset.metadata.category + "\n"
                                   + preset.metadata.author + "\n"
                                   + preset.metadata.description)
                                      .toLowerCase();
            if (!haystack.contains(search))
            {
                continue;
            }
        }

        result.push_back(preset);
    }

    std::sort(result.begin(), result.end(), [](const PresetRecord& a, const PresetRecord& b)
    {
        const auto c = normalizeCategory(a.metadata.category).compareIgnoreCase(normalizeCategory(b.metadata.category));
        if (c != 0)
        {
            return c < 0;
        }

        const auto n = a.metadata.name.compareIgnoreCase(b.metadata.name);
        if (n != 0)
        {
            return n < 0;
        }

        if (a.isFactory != b.isFactory)
        {
            return a.isFactory;
        }

        return a.file.getFullPathName() < b.file.getFullPathName();
    });

    return result;
}

bool PresetManager::loadPreset(const PresetRecord& preset, juce::String& error)
{
    // INIT has no file to read: it is the default state, restored from memory.
    if (preset.isInit)
    {
        return loadInitState(error);
    }

    return loadPresetFile(preset.file, error);
}

bool PresetManager::loadPresetFile(const juce::File& file, juce::String& error)
{
    juce::ValueTree presetTree;
    if (!readPresetFile(file, presetTree, nullptr, error))
    {
        return false;
    }

    return applyPresetTree(presetTree, error);
}

bool PresetManager::saveUserPreset(const PresetMetadata& metadata,
                                   bool overwrite,
                                   juce::String& error,
                                   juce::File* outFile)
{
    if (metadata.name.trim().isEmpty())
    {
        error = "Preset name is required.";
        return false;
    }

    auto userDir = getUserPresetRootDir();
    if (!userDir.exists() && !userDir.createDirectory())
    {
        error = "Unable to create user preset directory.";
        return false;
    }

    auto targetFile = userDir.getChildFile(sanitizeFileName(metadata.name) + kPresetExtension);
    if (targetFile.existsAsFile() && !overwrite)
    {
        error = "A preset with this name already exists.";
        return false;
    }

    auto tree = buildPresetTreeFromCurrentState(metadata, false, error);
    if (!tree.isValid())
    {
        return false;
    }

    if (!writePresetFile(targetFile, tree, error))
    {
        return false;
    }

    if (outFile != nullptr)
    {
        *outFile = targetFile;
    }

    rebuildIndex();
    return true;
}

bool PresetManager::setFavorite(const PresetRecord& preset, bool favorite, juce::String& error)
{
    const auto id = canonicalPresetId(getPresetRootDir(), preset.file);
    if (id.isEmpty())
    {
        error = "Invalid preset path for favorite metadata.";
        return false;
    }

    if (favorite)
    {
        if (!favoriteIds.contains(id, true))
        {
            favoriteIds.add(id);
        }
    }
    else
    {
        favoriteIds.removeString(id, true);
    }

    if (!saveFavorites(error))
    {
        return false;
    }

    rebuildIndex();
    return true;
}

bool PresetManager::isFavorite(const juce::File& presetFile) const
{
    const auto id = canonicalPresetId(getPresetRootDir(), presetFile);
    return favoriteIds.contains(id, true);
}

bool PresetManager::exportPreset(const PresetRecord& preset, const juce::File& destinationFile, juce::String& error) const
{
    if (!preset.file.existsAsFile())
    {
        error = "Preset file does not exist.";
        return false;
    }

    auto destination = destinationFile;
    if (destination.hasFileExtension(kPresetExtension))
    {
        if (destination.existsAsFile() && !destination.deleteFile())
        {
            error = "Unable to overwrite existing export file.";
            return false;
        }

        if (!preset.file.copyFileTo(destination))
        {
            error = "Failed to export preset file.";
            return false;
        }

        return true;
    }

    if (!destination.exists() && !destination.createDirectory())
    {
        error = "Unable to create export directory.";
        return false;
    }

    destination = destination.getChildFile(preset.file.getFileName());
    if (destination.existsAsFile() && !destination.deleteFile())
    {
        error = "Unable to overwrite existing export file.";
        return false;
    }

    if (!preset.file.copyFileTo(destination))
    {
        error = "Failed to export preset file.";
        return false;
    }

    return true;
}

bool PresetManager::importPreset(const juce::File& sourceFile, juce::String& error, PresetRecord* outImported)
{
    if (!sourceFile.existsAsFile())
    {
        error = "Import file does not exist.";
        return false;
    }

    if (!sourceFile.hasFileExtension(kPresetExtension))
    {
        error = "Import file must have .px3preset extension.";
        return false;
    }

    juce::ValueTree presetTree;
    PresetRecord importedMeta;
    if (!readPresetFile(sourceFile, presetTree, &importedMeta, error))
    {
        return false;
    }

    auto userDir = getUserPresetRootDir();
    if (!userDir.exists() && !userDir.createDirectory())
    {
        error = "Unable to create user preset directory.";
        return false;
    }

    auto baseName = sanitizeFileName(importedMeta.metadata.name.isEmpty()
                                         ? sourceFile.getFileNameWithoutExtension()
                                         : importedMeta.metadata.name);
    if (baseName.isEmpty())
    {
        baseName = "Imported Preset";
    }

    auto target = userDir.getChildFile(baseName + kPresetExtension);
    int suffix = 2;
    while (target.existsAsFile())
    {
        target = userDir.getChildFile(baseName + " " + juce::String(suffix++) + kPresetExtension);
    }

    if (!writePresetFile(target, presetTree, error))
    {
        return false;
    }

    rebuildIndex();

    if (outImported != nullptr)
    {
        if (const auto* found = findByFile(target))
        {
            *outImported = *found;
        }
    }

    return true;
}

bool PresetManager::dumpCurrentStateToPresetFile(const juce::File& destinationFile,
                                                 const PresetMetadata& metadata,
                                                 bool overwrite,
                                                 bool validateRoundTrip,
                                                 juce::String& error,
                                                 int* outSerializedBytes)
{
    auto destination = destinationFile;
    if (!destination.hasFileExtension(kPresetExtension))
    {
        destination = destination.withFileExtension(kPresetExtension);
    }

    auto parentDir = destination.getParentDirectory();
    if (!parentDir.exists() && !parentDir.createDirectory())
    {
        error = "Unable to create destination directory.";
        return false;
    }

    if (destination.existsAsFile() && !overwrite)
    {
        error = "Preset file already exists.";
        return false;
    }

    auto normalizedMetadata = metadata;
    if (normalizedMetadata.name.trim().isEmpty())
    {
        normalizedMetadata.name = destination.getFileNameWithoutExtension();
    }

    auto tree = buildPresetTreeFromCurrentState(normalizedMetadata, false, error);
    if (!tree.isValid())
    {
        return false;
    }

    int serializedBytes = 0;
    if (validateRoundTrip)
    {
        auto xml = tree.createXml();
        if (xml == nullptr)
        {
            error = "Failed to serialize preset for validation.";
            return false;
        }

        const auto xmlText = xml->toString();
        serializedBytes = static_cast<int>(juce::jmin(static_cast<size_t>(std::numeric_limits<int>::max()),
                                  xmlText.getNumBytesAsUTF8()));

        std::unique_ptr<juce::XmlElement> parsed(juce::XmlDocument::parse(xmlText));
        if (parsed == nullptr)
        {
            error = "Preset validation failed: XML parse error.";
            return false;
        }

        auto parsedTree = juce::ValueTree::fromXml(*parsed);
        if (!parsedTree.isValid() || parsedTree.getType() != kPresetRootId)
        {
            error = "Preset validation failed: missing PX3_PRESET root.";
            return false;
        }

        auto migrated = migratePresetTreeIfNeeded(parsedTree, error);
        if (!migrated.isValid())
        {
            return false;
        }

        if (!migrated.getChildWithName(kPluginStateId).isValid())
        {
            error = "Preset validation failed: missing plugin state block.";
            return false;
        }
    }

    if (!writePresetFile(destination, tree, error))
    {
        return false;
    }

    if (outSerializedBytes != nullptr)
    {
        *outSerializedBytes = serializedBytes > 0 ? serializedBytes : static_cast<int>(destination.getSize());
    }

    return true;
}

// The state the plugin starts in. Built in memory and never written anywhere:
// INIT is not a preset, it is the default the instrument loads with, and giving
// it a file meant it also got a category of its own and a row in the browser
// alongside sounds somebody actually designed.
juce::ValueTree PresetManager::initPresetTree(juce::String& error) const
{
    PresetMetadata md;
    md.name = initPresetName();
    md.category = initPresetName();
    md.author = "P(X3)";
    md.description = "The state the plugin loads with.";

    auto tree = buildPresetTreeFromCurrentState(md, true, error);
    if (!tree.isValid())
    {
        return {};
    }

    auto state = tree.getChildWithName(kPluginStateId);
    if (!state.isValid())
    {
        error = "Failed to build the INIT state: missing plugin state node.";
        return {};
    }

    // Canonical INIT payload captured from /INIT.px3preset in the repository.
    // These normalized values define the shipped first-run factory INIT state.
    state.setProperty("stateVersion", 7, nullptr);
    state.setProperty("osc1Enabled", 1.0f, nullptr);
    state.setProperty("osc1Level", 1.0f, nullptr);
    state.setProperty("osc1Coarse", 0.5f, nullptr);
    state.setProperty("osc1Fine", 0.5f, nullptr);
    state.setProperty("osc1Mode", 0.0f, nullptr);
    state.setProperty("osc1MacroA", 0.5f, nullptr);
    state.setProperty("osc1MacroB", 0.5f, nullptr);
    state.setProperty("osc1MacroC", 0.5f, nullptr);
    state.setProperty("osc1Vowel", 0.0f, nullptr);
    state.setProperty("osc1H1", 1.0f, nullptr);
    state.setProperty("osc1H2", 0.699999988079071f, nullptr);
    state.setProperty("osc1H3", 0.449999988079071f, nullptr);
    state.setProperty("osc1H4", 0.300000011920929f, nullptr);
    state.setProperty("osc1H5", 0.2000000029802322f, nullptr);
    state.setProperty("osc1H6", 0.1400000005960464f, nullptr);
    state.setProperty("osc1H7", 0.1000000014901161f, nullptr);
    state.setProperty("osc1H8", 0.07000000029802322f, nullptr);
    state.setProperty("osc2Enabled", 0.0f, nullptr);
    state.setProperty("osc2Level", 1.0f, nullptr);
    state.setProperty("osc2Coarse", 0.5f, nullptr);
    state.setProperty("osc2Fine", 0.5f, nullptr);
    state.setProperty("osc2Mode", 0.0f, nullptr);
    state.setProperty("osc2MacroA", 0.5f, nullptr);
    state.setProperty("osc2MacroB", 0.5f, nullptr);
    state.setProperty("osc2MacroC", 0.5f, nullptr);
    state.setProperty("osc2Vowel", 0.0f, nullptr);
    state.setProperty("osc2H1", 1.0f, nullptr);
    state.setProperty("osc2H2", 0.699999988079071f, nullptr);
    state.setProperty("osc2H3", 0.449999988079071f, nullptr);
    state.setProperty("osc2H4", 0.300000011920929f, nullptr);
    state.setProperty("osc2H5", 0.2000000029802322f, nullptr);
    state.setProperty("osc2H6", 0.1400000005960464f, nullptr);
    state.setProperty("osc2H7", 0.1000000014901161f, nullptr);
    state.setProperty("osc2H8", 0.07000000029802322f, nullptr);
    state.setProperty("osc3Enabled", 0.0f, nullptr);
    state.setProperty("osc3Level", 1.0f, nullptr);
    state.setProperty("osc3Coarse", 0.5f, nullptr);
    state.setProperty("osc3Fine", 0.5f, nullptr);
    state.setProperty("osc3Mode", 0.0f, nullptr);
    state.setProperty("osc3MacroA", 0.5f, nullptr);
    state.setProperty("osc3MacroB", 0.5f, nullptr);
    state.setProperty("osc3MacroC", 0.5f, nullptr);
    state.setProperty("osc3Vowel", 0.0f, nullptr);
    state.setProperty("osc3H1", 1.0f, nullptr);
    state.setProperty("osc3H2", 0.699999988079071f, nullptr);
    state.setProperty("osc3H3", 0.449999988079071f, nullptr);
    state.setProperty("osc3H4", 0.300000011920929f, nullptr);
    state.setProperty("osc3H5", 0.2000000029802322f, nullptr);
    state.setProperty("osc3H6", 0.1400000005960464f, nullptr);
    state.setProperty("osc3H7", 0.1000000014901161f, nullptr);
    state.setProperty("osc3H8", 0.07000000029802322f, nullptr);
    state.setProperty("filter1Cutoff", 0.2481092661619186f, nullptr);
    state.setProperty("filter1Resonance", 0.282051295042038f, nullptr);
    state.setProperty("filter1Type", 0.0f, nullptr);
    state.setProperty("ampAttack", 0.044675063341856f, nullptr);
    state.setProperty("ampDecay", 0.1328198909759521f, nullptr);
    state.setProperty("ampSustain", 0.800000011920929f, nullptr);
    state.setProperty("ampRelease", 0.09388426691293716f, nullptr);
    state.setProperty("masterGain", 0.6000000238418579f, nullptr);
    state.setProperty("vibeAmount", 0.6022812128067017f, nullptr);
    state.setProperty("vibeEnabled", 1.0f, nullptr);
    state.setProperty("vibeType", 0.6000000238418579f, nullptr);
    state.setProperty("delayAmount", 0.3621250092983246f, nullptr);
    state.setProperty("granularSyncDivision", 0.0f, nullptr);
    state.setProperty("granularMode", 0.3333333432674408f, nullptr);
    state.setProperty("delayAlgorithm", 0.3333333432674408f, nullptr);
    state.setProperty("delayEnabled", 1.0f, nullptr);
    state.setProperty("delayTime", 0.3499999940395355f, nullptr);
    state.setProperty("delayFeedback", 0.3799999952316284f, nullptr);
    state.setProperty("reverbAmount", 0.4072031378746033f, nullptr);
    state.setProperty("reverbEnabled", 1.0f, nullptr);
    state.setProperty("reverbAlgorithm", 0.0f, nullptr);
    state.setProperty("reverbSize", 0.5199999809265137f, nullptr);
    state.setProperty("reverbDecay", 0.4799999892711639f, nullptr);
    state.setProperty("reverbDamping", 0.4600000083446503f, nullptr);
    state.setProperty("reverbPreDelay", 0.07999999821186066f, nullptr);
    state.setProperty("reverbModDepth", 0.239999994635582f, nullptr);
    state.setProperty("reverbModRate", 0.1800000071525574f, nullptr);
    state.setProperty("reverbWidth", 0.8600000143051147f, nullptr);
    state.setProperty("reverbCloudFeedback", 0.6200000047683716f, nullptr);
    state.setProperty("reverbCloudDiffusion", 0.5400000214576721f, nullptr);
    state.setProperty("pitchBendRange", 0.04347826167941093f, nullptr);
    state.setProperty("lfoFrequency", 0.3851140737533569f, nullptr);
    state.setProperty("moduleOrderRevision", 2, nullptr);

    const juce::Identifier moduleOrderId("MODULE_ORDER");
    const juce::Identifier moduleEntryId("MODULE");
    const juce::Identifier moduleIdProperty("id");
    if (auto existingOrder = state.getChildWithName(moduleOrderId); existingOrder.isValid())
    {
        state.removeChild(existingOrder, nullptr);
    }
    juce::ValueTree moduleOrder(moduleOrderId);
    {
        juce::ValueTree module(moduleEntryId);
        module.setProperty(moduleIdProperty, "harmonicDrive", nullptr);
        moduleOrder.addChild(module, -1, nullptr);
    }
    {
        juce::ValueTree module(moduleEntryId);
        module.setProperty(moduleIdProperty, "delay", nullptr);
        moduleOrder.addChild(module, -1, nullptr);
    }
    {
        juce::ValueTree module(moduleEntryId);
        module.setProperty(moduleIdProperty, "reverb", nullptr);
        moduleOrder.addChild(module, -1, nullptr);
    }
    state.addChild(moduleOrder, -1, nullptr);

    const juce::Identifier lfoStateId("LFO");
    if (auto existingLfo = state.getChildWithName(lfoStateId); existingLfo.isValid())
    {
        state.removeChild(existingLfo, nullptr);
    }
    juce::ValueTree lfoState(lfoStateId);
    lfoState.setProperty("frequency", 0.8406999707221985f, nullptr);
    lfoState.setProperty("assignment", "filter1Cutoff", nullptr);
    state.addChild(lfoState, -1, nullptr);

    const juce::Identifier vibeStateId("VIBE");
    if (auto existingVibe = state.getChildWithName(vibeStateId); existingVibe.isValid())
    {
        state.removeChild(existingVibe, nullptr);
    }
    juce::ValueTree vibeState(vibeStateId);
    vibeState.setProperty("bypass", false, nullptr);
    vibeState.setProperty("seed", 1337, nullptr);
    state.addChild(vibeState, -1, nullptr);

    return tree;
}

bool PresetManager::loadInitState(juce::String& error)
{
    const auto tree = initPresetTree(error);
    if (! tree.isValid())
    {
        return false;
    }

    const auto state = tree.getChildWithName(kPluginStateId);
    if (! state.isValid())
    {
        error = "The INIT state has no plugin state node.";
        return false;
    }

    return processor.applyParameterStateTree(state, &error, false);
}

// Anyone who ran an earlier build has an INIT.px3preset sitting in a factory
// category of its own. It is not a preset and should never have been a file, so
// it is removed rather than left to keep appearing in the browser.
void PresetManager::removeLegacyInitPreset() const
{
    const auto root = getFactoryPresetRootDir();
    root.getChildFile("INIT").withFileExtension(kPresetExtension).deleteFile();

    const auto initCategoryDir = root.getChildFile("INIT");
    if (initCategoryDir.isDirectory())
    {
        initCategoryDir.deleteRecursively();
    }
}

const PresetManager::PresetRecord* PresetManager::findByFile(const juce::File& file) const
{
    const auto path = file.getFullPathName();
    for (const auto& record : indexedPresets)
    {
        if (record.file.getFullPathName() == path)
        {
            return &record;
        }
    }

    return nullptr;
}

juce::File PresetManager::getRootDir() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("P(X3)");
}

juce::File PresetManager::getPresetRootDir() const { return getRootDir().getChildFile(presetRootName); }
juce::File PresetManager::getFactoryPresetRootDir() const { return getPresetRootDir().getChildFile(factoryRootName); }
juce::File PresetManager::getUserPresetRootDir() const { return getPresetRootDir().getChildFile(userRootName); }
juce::File PresetManager::getSettingsDir() const { return getRootDir().getChildFile(settingsRootName); }

juce::String PresetManager::sanitizeFileName(const juce::String& input)
{
    auto s = input.trim();
    if (s.isEmpty())
    {
        return "Preset";
    }

    static const juce::String invalid = "\\/:*?\"<>|";
    for (auto ch : invalid)
    {
        s = s.replaceCharacter(ch, '_');
    }

    while (s.contains("  "))
    {
        s = s.replace("  ", " ");
    }

    return s.trim();
}

juce::String PresetManager::normalizeCategory(const juce::String& category)
{
    return category.trim().toUpperCase();
}

juce::String PresetManager::canonicalPresetId(const juce::File& baseDir, const juce::File& presetFile)
{
    auto relative = presetFile.getRelativePathFrom(baseDir).replaceCharacter('\\', '/');
    return relative.trim();
}

bool PresetManager::ensureDirectoryLayout(juce::String& error) const
{
    const std::array<juce::File, 4> required {
        getRootDir(),
        getFactoryPresetRootDir(),
        getUserPresetRootDir(),
        getSettingsDir()
    };

    for (const auto& dir : required)
    {
        if (!dir.exists() && !dir.createDirectory())
        {
            error = "Failed to create required directory: " + dir.getFullPathName();
            return false;
        }
    }

    for (const auto* cat : kDefaultCategories)
    {
        const auto factoryCat = getFactoryPresetRootDir().getChildFile(cat);
        if (!factoryCat.exists() && !factoryCat.createDirectory())
        {
            error = "Failed to create factory preset category directories.";
            return false;
        }
    }

    // Migrate legacy user category folders into a single flat User directory.
    const auto userRoot = getUserPresetRootDir();
    const auto userPresetFiles = userRoot.findChildFiles(juce::File::findFiles, true, "*" + kPresetExtension);
    for (const auto& file : userPresetFiles)
    {
        if (file.getParentDirectory() == userRoot)
        {
            continue;
        }

        auto baseName = sanitizeFileName(file.getFileNameWithoutExtension());
        if (baseName.isEmpty())
        {
            baseName = "Imported Preset";
        }

        auto target = userRoot.getChildFile(baseName + kPresetExtension);
        int suffix = 2;
        while (target.existsAsFile())
        {
            target = userRoot.getChildFile(baseName + " " + juce::String(suffix++) + kPresetExtension);
        }

        if (!file.moveFileTo(target))
        {
            error = "Failed to migrate user preset into flat directory: " + file.getFullPathName();
            return false;
        }
    }

    const auto userSubdirs = userRoot.findChildFiles(juce::File::findDirectories, false);
    for (const auto& dir : userSubdirs)
    {
        if (!dir.deleteRecursively())
        {
            error = "Failed to remove legacy user preset directory: " + dir.getFullPathName();
            return false;
        }
    }

    return true;
}

bool PresetManager::ensureFactoryPresetLibrary(juce::String& error)
{
    removeLegacyInitPreset();

    // The library itself lives in FactoryPresets.cpp - that is where a sound
    // designer looks, and it keeps the writing of preset files separate from
    // the choosing of what is in them.
    const auto defs = px3::presets::factoryPresets();

    // Look the parameter up and let it do its own conversion. An id that does
    // not exist is reported rather than silently ignored, because a typo would
    // otherwise ship as a preset that quietly does not set what it claims to.
    juce::StringArray unknownIds;
    auto normalisedFor = [this, &unknownIds](const juce::String& id, float realValue)
    {
        for (auto* parameter : processor.getParameters())
        {
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            {
                if (ranged->getParameterID() == id)
                {
                    return juce::jlimit(0.0f, 1.0f, ranged->convertTo0to1(realValue));
                }
            }
        }

        unknownIds.addIfNotAlreadyThere(id);
        return -1.0f;
    };

    // The factory library is versioned. Without this, a refreshed library never
    // reaches anyone who already ran the plugin once: the writer below skips
    // files that exist, which is right for not clobbering the same preset twice
    // and wrong for shipping new ones.
    const auto stampFile = getFactoryPresetRootDir().getChildFile(".factory-version");
    const auto installedVersion = stampFile.existsAsFile() ? stampFile.loadFileAsString().trim().getIntValue() : 0;
    const auto rewriteAll = installedVersion != px3::presets::kFactoryLibraryVersion;

    if (rewriteAll && getFactoryPresetRootDir().isDirectory())
    {
        // Only the factory tree, and only the preset files in it. User presets
        // live in a separate root and are never touched.
        for (const auto& stale : getFactoryPresetRootDir().findChildFiles(juce::File::findFiles, true,
                                                                          "*" + kPresetExtension))
        {
            if (true)
            {
                stale.deleteFile();
            }
        }
    }

    auto baseState = processor.createPresetStateTree();

    for (const auto& def : defs)
    {
        auto state = baseState.createCopy();
        for (const auto& [id, value] : def.params)
        {
            const auto normalised = normalisedFor(id, value);
            if (normalised >= 0.0f)
            {
                state.setProperty(id, normalised, nullptr);
            }
        }

        PresetMetadata md;
        md.name = def.name;
        md.category = def.category;
        md.author = def.author;
        md.description = def.description;

        juce::ValueTree preset(kPresetRootId);
        preset.setProperty("presetVersion", currentPresetFormatVersion, nullptr);
        preset.setProperty("pluginVersion", ProjectInfo::versionString, nullptr);
        preset.setProperty("name", md.name, nullptr);
        preset.setProperty("category", md.category, nullptr);
        preset.setProperty("author", md.author, nullptr);
        preset.setProperty("description", md.description, nullptr);
        preset.setProperty("isFactory", true, nullptr);

        preset.addChild(state, -1, nullptr);
        preset.addChild(juce::ValueTree(kAssetsId), -1, nullptr);

        auto categoryDir = getFactoryPresetRootDir().getChildFile(normalizeCategory(md.category));
        if (!categoryDir.exists() && !categoryDir.createDirectory())
        {
            error = "Unable to create factory category directory.";
            return false;
        }

        const auto outFile = categoryDir.getChildFile(sanitizeFileName(md.name) + kPresetExtension);
        if (!outFile.existsAsFile())
        {
            if (!writePresetFile(outFile, preset, error))
            {
                return false;
            }
        }
    }

    if (!unknownIds.isEmpty())
    {
        error = "Factory presets reference unknown parameter ids: " + unknownIds.joinIntoString(", ");
        return false;
    }

    if (rewriteAll)
    {
        // A category the library no longer uses leaves an empty folder behind,
        // and an empty folder shows up in the browser as a category with
        // nothing in it.
        for (const auto& dir : getFactoryPresetRootDir().findChildFiles(juce::File::findDirectories, false))
        {
            if (dir.findChildFiles(juce::File::findFilesAndDirectories, true).isEmpty())
            {
                dir.deleteRecursively();
            }
        }

        stampFile.replaceWithText(juce::String(px3::presets::kFactoryLibraryVersion));
    }

    return true;
}

bool PresetManager::readPresetFile(const juce::File& file,
                                   juce::ValueTree& outPresetTree,
                                   PresetRecord* outRecord,
                                   juce::String& error) const
{
    // Parsing and migration are centralized here so all load paths (browser,
    // import, debug dump validation) enforce the same compatibility rules.
    if (!file.existsAsFile())
    {
        error = "Preset file not found: " + file.getFullPathName();
        return false;
    }

    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file));
    if (xml == nullptr)
    {
        error = "Failed to parse preset XML: " + file.getFileName();
        return false;
    }

    auto tree = juce::ValueTree::fromXml(*xml);
    if (!tree.isValid() || tree.getType() != kPresetRootId)
    {
        error = "Invalid preset format (missing PX3_PRESET root).";
        return false;
    }

    auto migrated = migratePresetTreeIfNeeded(tree, error);
    if (!migrated.isValid())
    {
        return false;
    }

    const auto isFactoryFile = file.isAChildOf(getFactoryPresetRootDir());
    if (isFactoryFile && !migrated.isEquivalentTo(tree))
    {
        juce::String ignoredWriteError;
        writePresetFile(file, migrated, ignoredWriteError);
    }

    auto state = migrated.getChildWithName(kPluginStateId);
    if (!state.isValid())
    {
        error = "Preset missing parameter state block.";
        return false;
    }

    outPresetTree = migrated;

    if (outRecord != nullptr)
    {
        *outRecord = makeRecordFromTree(file, isFactoryFile, migrated);
    }

    return true;
}

bool PresetManager::writePresetFile(const juce::File& file,
                                    const juce::ValueTree& presetTree,
                                    juce::String& error) const
{
    if (!presetTree.isValid())
    {
        error = "Attempted to write invalid preset tree.";
        return false;
    }

    // Atomic-style write: serialize to a temporary file first, then swap. This
    // reduces risk of half-written presets when failures occur.
    juce::TemporaryFile temp(file);
    if (auto xml = presetTree.createXml())
    {
        if (!xml->writeTo(temp.getFile()))
        {
            error = "Failed to write preset file: " + temp.getFile().getFullPathName();
            return false;
        }
    }
    else
    {
        error = "Failed to serialize preset XML.";
        return false;
    }

    if (!temp.overwriteTargetFileWithTemporary())
    {
        temp.getFile().deleteFile();
        error = "Failed to finalize preset write.";
        return false;
    }

    return true;
}

juce::ValueTree PresetManager::buildPresetTreeFromCurrentState(const PresetMetadata& metadata,
                                                               bool asFactory,
                                                               juce::String& error) const
{
    // Capture canonical processor state; preset files intentionally do not own
    // a separate parameter model.
    auto pluginState = processor.createPresetStateTree();
    if (!pluginState.isValid())
    {
        error = "Unable to capture plugin parameter state.";
        return {};
    }

    juce::ValueTree preset(kPresetRootId);
    preset.setProperty("presetVersion", currentPresetFormatVersion, nullptr);
    preset.setProperty("pluginVersion", ProjectInfo::versionString, nullptr);
    preset.setProperty("name", metadata.name.trim(), nullptr);
    preset.setProperty("category", normalizeCategory(metadata.category), nullptr);
    preset.setProperty("author", metadata.author.trim(), nullptr);
    preset.setProperty("description", metadata.description.trim(), nullptr);
    preset.setProperty("isFactory", asFactory, nullptr);

    juce::ValueTree assets(kAssetsId);
    collectAssetsForState(pluginState, assets);

    preset.addChild(pluginState, -1, nullptr);
    preset.addChild(assets, -1, nullptr);
    return preset;
}

bool PresetManager::applyPresetTree(const juce::ValueTree& presetTree, juce::String& error)
{
    if (!presetTree.isValid())
    {
        error = "Invalid preset tree.";
        return false;
    }

    auto state = presetTree.getChildWithName(kPluginStateId);
    if (!state.isValid())
    {
        error = "Preset does not contain plugin state.";
        return false;
    }

    // Materialize a mutable copy so embedded asset path rewrite does not alter
    // the original parsed tree.
    auto stateCopy = state.createCopy();
    auto assets = presetTree.getChildWithName(kAssetsId);

    if (!materializeEmbeddedAssets(stateCopy, assets, error))
    {
        return false;
    }

    return processor.applyParameterStateTree(stateCopy, &error, false);
}

juce::ValueTree PresetManager::migratePresetTreeIfNeeded(const juce::ValueTree& presetTree,
                                                         juce::String& error) const
{
    // Migration keeps older presets loadable while preserving strict rejection
    // of unknown future schema versions.
    auto migrated = presetTree.createCopy();

    const auto version = static_cast<int>(migrated.getProperty("presetVersion", 1));
    if (version > currentPresetFormatVersion)
    {
        error = "Preset format version is newer than this plugin build.";
        return {};
    }

    if (version < 1)
    {
        error = "Unsupported preset format version.";
        return {};
    }

    if (!migrated.hasProperty("name"))
    {
        migrated.setProperty("name", "Unnamed", nullptr);
    }

    if (!migrated.hasProperty("category"))
    {
        migrated.setProperty("category", "UNCATEGORIZED", nullptr);
    }

    if (!migrated.getChildWithName(kAssetsId).isValid())
    {
        migrated.addChild(juce::ValueTree(kAssetsId), -1, nullptr);
    }

    auto state = migrated.getChildWithName(kPluginStateId);
    if (state.isValid())
    {
        if (state.hasProperty("topMenuView"))
        {
            state.removeProperty("topMenuView", nullptr);
        }

        // Fill missing parameters/children from the current processor defaults
        // so older presets remain complete as new parameters are introduced.
        const auto defaultState = processor.createPresetStateTree();
        if (defaultState.isValid())
        {
            for (int i = 0; i < defaultState.getNumProperties(); ++i)
            {
                const auto propertyName = defaultState.getPropertyName(i);
                if (!state.hasProperty(propertyName))
                {
                    state.setProperty(propertyName, defaultState.getProperty(propertyName), nullptr);
                }
            }

            if (defaultState.hasProperty("stateVersion"))
            {
                state.setProperty("stateVersion", defaultState.getProperty("stateVersion"), nullptr);
            }

            const auto ensureChildState = [&state, &defaultState](const juce::Identifier& childId)
            {
                const auto defaultChild = defaultState.getChildWithName(childId);
                if (!defaultChild.isValid())
                {
                    return;
                }

                auto targetChild = state.getChildWithName(childId);
                if (!targetChild.isValid())
                {
                    state.addChild(defaultChild.createCopy(), -1, nullptr);
                    return;
                }

                for (int i = 0; i < defaultChild.getNumProperties(); ++i)
                {
                    const auto propertyName = defaultChild.getPropertyName(i);
                    if (!targetChild.hasProperty(propertyName))
                    {
                        targetChild.setProperty(propertyName, defaultChild.getProperty(propertyName), nullptr);
                    }
                }
            };

            ensureChildState(juce::Identifier("MODULE_ORDER"));
            ensureChildState(juce::Identifier("LFO"));
            ensureChildState(juce::Identifier("VIBE"));
        }
    }

    migrated.setProperty("presetVersion", currentPresetFormatVersion, nullptr);

    return migrated;
}

void PresetManager::collectAssetsForState(juce::ValueTree& pluginState,
                                          juce::ValueTree& assetsNode) const
{
    juce::ignoreUnused(pluginState, assetsNode);
}

bool PresetManager::materializeEmbeddedAssets(juce::ValueTree& pluginState,
                                              const juce::ValueTree& assetsNode,
                                              juce::String& error) const
{
    juce::ignoreUnused(pluginState, assetsNode, error);
    return true;
}

bool PresetManager::loadFavorites()
{
    favoriteIds.clear();

    const auto file = getSettingsDir().getChildFile(favoritesFileName);
    if (!file.existsAsFile())
    {
        return true;
    }

    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file));
    if (xml == nullptr)
    {
        return false;
    }

    auto tree = juce::ValueTree::fromXml(*xml);
    if (!tree.isValid() || tree.getType().toString() != "PX3_FAVORITES")
    {
        return false;
    }

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto item = tree.getChild(i);
        if (item.getType().toString() == "FAVORITE")
        {
            const auto id = item.getProperty("id").toString();
            if (id.isNotEmpty())
            {
                favoriteIds.addIfNotAlreadyThere(id);
            }
        }
    }

    return true;
}

bool PresetManager::saveFavorites(juce::String& error) const
{
    juce::ValueTree root("PX3_FAVORITES");
    for (const auto& id : favoriteIds)
    {
        juce::ValueTree item("FAVORITE");
        item.setProperty("id", id, nullptr);
        root.addChild(item, -1, nullptr);
    }

    auto file = getSettingsDir().getChildFile(favoritesFileName);
    if (auto xml = root.createXml())
    {
        if (!xml->writeTo(file))
        {
            error = "Failed to persist favorites metadata.";
            return false;
        }
        return true;
    }

    error = "Failed to serialize favorites metadata.";
    return false;
}

void PresetManager::rebuildIndex()
{
    indexedPresets.clear();

    const auto collectFromRoot = [this](const juce::File& root, bool isFactory, std::vector<PresetRecord>& out)
    {
        const auto files = root.findChildFiles(juce::File::findFiles, true, "*" + kPresetExtension);
        for (const auto& file : files)
        {
            juce::ValueTree tree;
            juce::String error;
            if (readPresetFile(file, tree, nullptr, error))
            {
                out.push_back(makeRecordFromTree(file, isFactory, tree));
            }
        }
    };

    collectFromRoot(getFactoryPresetRootDir(), true, indexedPresets);
    collectFromRoot(getUserPresetRootDir(), false, indexedPresets);

    std::sort(indexedPresets.begin(), indexedPresets.end(), [](const PresetRecord& a, const PresetRecord& b)
    {
        if (a.isFactory != b.isFactory)
        {
            return a.isFactory;
        }

        const auto c = normalizeCategory(a.metadata.category).compareIgnoreCase(normalizeCategory(b.metadata.category));
        if (c != 0)
        {
            return c < 0;
        }

        return a.metadata.name.compareIgnoreCase(b.metadata.name) < 0;
    });
}

PresetManager::PresetRecord PresetManager::makeRecordFromTree(const juce::File& file,
                                                              bool isFactory,
                                                              const juce::ValueTree& presetTree) const
{
    PresetRecord record;
    record.file = file;
    record.isFactory = isFactory;
    record.presetVersion = static_cast<int>(presetTree.getProperty("presetVersion", 1));
    record.pluginVersion = presetTree.getProperty("pluginVersion").toString();
    record.metadata.name = presetTree.getProperty("name", file.getFileNameWithoutExtension()).toString();
    record.metadata.category = presetTree.getProperty("category", "UNCATEGORIZED").toString();
    record.metadata.author = presetTree.getProperty("author", "").toString();
    record.metadata.description = presetTree.getProperty("description", "").toString();
    record.modifiedTimeMs = file.getLastModificationTime().toMilliseconds();
    record.isFavorite = isFavorite(file);
    return record;
}
