#include "PresetManager.h"

#include <algorithm>
#include <limits>

namespace
{
constexpr auto presetRootName = "Presets";
constexpr auto factoryRootName = "Factory";
constexpr auto userRootName = "User";
constexpr auto assetsRootName = "Assets";
constexpr auto imageAssetsName = "Images";
constexpr auto audioAssetsName = "Audio";
constexpr auto settingsRootName = "Settings";
constexpr auto favoritesFileName = "favorites.xml";

const std::array<const char*, 7> kDefaultCategories {
    "BASS",
    "LEADS",
    "PADS",
    "PLUCKS",
    "EXPERIMENTAL",
    "IMAGE ENGINE",
    "AUDIO ENGINE"
};

const juce::Identifier kPresetRootId("PX3_PRESET");
const juce::Identifier kPluginStateId("PX3_STATE");
const juce::Identifier kAssetsId("ASSETS");
const juce::Identifier kAssetId("ASSET");

const juce::String kPresetExtension(PresetManager::presetFileExtension);
}

PresetManager::PresetManager(SynthProjectAudioProcessor& processorIn)
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

bool PresetManager::deleteUserPreset(const PresetRecord& preset, juce::String& error)
{
    if (preset.isFactory)
    {
        error = "Factory presets cannot be deleted.";
        return false;
    }

    if (!preset.file.existsAsFile())
    {
        error = "Preset file does not exist.";
        return false;
    }

    if (!preset.file.deleteFile())
    {
        error = "Unable to delete preset file.";
        return false;
    }

    const auto id = canonicalPresetId(getPresetRootDir(), preset.file);
    favoriteIds.removeString(id, true);
    juce::String saveError;
    saveFavorites(saveError);

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

bool PresetManager::createInitPresetIfMissing(juce::String& error)
{
    const auto initFile = getFactoryPresetRootDir().getChildFile("INIT").withFileExtension(kPresetExtension);
    if (initFile.existsAsFile())
    {
        return true;
    }

    PresetMetadata md;
    md.name = "INIT";
    md.category = "INIT";
    md.author = "P(X3)";
    md.description = "Default initialized state.";

    auto tree = buildPresetTreeFromCurrentState(md, true, error);
    if (!tree.isValid())
    {
        return false;
    }

    auto state = tree.getChildWithName(kPluginStateId);
    if (!state.isValid())
    {
        error = "Failed to build INIT preset: missing plugin state node.";
        return false;
    }

    // Canonical INIT payload captured from /INIT.px3preset in the repository.
    // These normalized values define the shipped first-run factory INIT state.
    state.setProperty("stateVersion", 4, nullptr);
    state.setProperty("oscSine", 1.0f, nullptr);
    state.setProperty("oscSaw", 0.0f, nullptr);
    state.setProperty("oscSquare", 0.0f, nullptr);
    state.setProperty("oscMode", 0.0f, nullptr);
    state.setProperty("oscMacroA", 0.5f, nullptr);
    state.setProperty("oscMacroB", 0.5f, nullptr);
    state.setProperty("oscMacroC", 0.5f, nullptr);
    state.setProperty("oscVowel", 0.0f, nullptr);
    state.setProperty("oscH1", 1.0f, nullptr);
    state.setProperty("oscH2", 0.699999988079071f, nullptr);
    state.setProperty("oscH3", 0.449999988079071f, nullptr);
    state.setProperty("oscH4", 0.300000011920929f, nullptr);
    state.setProperty("oscH5", 0.2000000029802322f, nullptr);
    state.setProperty("oscH6", 0.1400000005960464f, nullptr);
    state.setProperty("oscH7", 0.1000000014901161f, nullptr);
    state.setProperty("oscH8", 0.07000000029802322f, nullptr);
    state.setProperty("filterCutoff", 0.4037925601005554f, nullptr);
    state.setProperty("filterResonance", 0.282051295042038f, nullptr);
    state.setProperty("filterType", 0.0f, nullptr);
    state.setProperty("ampAttack", 0.5603691935539246f, nullptr);
    state.setProperty("ampDecay", 0.3086085021495819f, nullptr);
    state.setProperty("ampSustain", 0.278243213891983f, nullptr);
    state.setProperty("ampRelease", 0.0f, nullptr);
    state.setProperty("masterGain", 0.4870468974113464f, nullptr);
    state.setProperty("robAmount", 0.7861562371253967f, nullptr);
    state.setProperty("robEnabled", 1.0f, nullptr);
    state.setProperty("vibeType", 0.6000000238418579f, nullptr);
    state.setProperty("isaacAmount", 0.7113437652587891f, nullptr);
    state.setProperty("granularSyncDivision", 0.0f, nullptr);
    state.setProperty("granularMode", 0.3333333432674408f, nullptr);
    state.setProperty("delayAlgorithm", 0.3333333432674408f, nullptr);
    state.setProperty("delayEnabled", 1.0f, nullptr);
    state.setProperty("delayTime", 0.3743749856948853f, nullptr);
    state.setProperty("delayFeedback", 1.0f, nullptr);
    state.setProperty("reverbAmount", 0.1919843852519989f, nullptr);
    state.setProperty("reverbEnabled", 1.0f, nullptr);
    state.setProperty("reverbAlgorithm", 1.0f, nullptr);
    state.setProperty("reverbSize", 0.5199999809265137f, nullptr);
    state.setProperty("reverbDecay", 0.4799999892711639f, nullptr);
    state.setProperty("reverbDamping", 0.4600000083446503f, nullptr);
    state.setProperty("reverbPreDelay", 0.07999999821186066f, nullptr);
    state.setProperty("reverbModDepth", 0.239999994635582f, nullptr);
    state.setProperty("reverbModRate", 0.1800000071525574f, nullptr);
    state.setProperty("reverbWidth", 0.8600000143051147f, nullptr);
    state.setProperty("reverbCloudFeedback", 0.6200000047683716f, nullptr);
    state.setProperty("reverbCloudDiffusion", 0.5400000214576721f, nullptr);
    state.setProperty("sourceEngine", 0.0f, nullptr);
    state.setProperty("imagePosition", 0.5f, nullptr);
    state.setProperty("imageAnimate", 0.0f, nullptr);
    state.setProperty("imageRate", 0.3774764239788055f, nullptr);
    state.setProperty("imageAnimMode", 1.0f, nullptr);
    state.setProperty("imageAnimSync", 0.0f, nullptr);
    state.setProperty("imageTarget", 0.0f, nullptr);
    state.setProperty("audioPosition", 0.5f, nullptr);
    state.setProperty("audioGrain", 0.449999988079071f, nullptr);
    state.setProperty("audioTexture", 0.3499999940395355f, nullptr);
    state.setProperty("audioAnimate", 0.0f, nullptr);
    state.setProperty("audioRate", 0.3897614181041718f, nullptr);
    state.setProperty("audioAnimMode", 1.0f, nullptr);
    state.setProperty("audioAnimSync", 0.0f, nullptr);
    state.setProperty("audioTarget", 0.5f, nullptr);
    state.setProperty("pitchBendRange", 0.04347826167941093f, nullptr);
    state.setProperty("lfoFrequency", 0.3851140737533569f, nullptr);
    state.setProperty("imagePath", "", nullptr);
    state.setProperty("audioPath", "", nullptr);
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
    lfoState.setProperty("assignment", "filterCutoff", nullptr);
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

    if (!writePresetFile(initFile, tree, error))
    {
        return false;
    }

    return true;
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
juce::File PresetManager::getAssetsRootDir() const { return getRootDir().getChildFile(assetsRootName); }
juce::File PresetManager::getImageAssetsDir() const { return getAssetsRootDir().getChildFile(imageAssetsName); }
juce::File PresetManager::getAudioAssetsDir() const { return getAssetsRootDir().getChildFile(audioAssetsName); }
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

juce::String PresetManager::computeFileHash(const juce::File& file)
{
    if (!file.existsAsFile())
    {
        return {};
    }

    juce::FileInputStream in(file);
    if (!in.openedOk())
    {
        return {};
    }

    juce::MemoryBlock block;
    in.readIntoMemoryBlock(block);
    juce::int64 hashSeed = static_cast<juce::int64>(file.hashCode64());
    hashSeed ^= static_cast<juce::int64>(block.getSize() * 1315423911ULL);
    return juce::String::toHexString(hashSeed);
}

bool PresetManager::ensureDirectoryLayout(juce::String& error) const
{
    const std::array<juce::File, 6> required {
        getRootDir(),
        getFactoryPresetRootDir(),
        getUserPresetRootDir(),
        getImageAssetsDir(),
        getAudioAssetsDir(),
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
    if (!createInitPresetIfMissing(error))
    {
        return false;
    }

    struct Def
    {
        const char* name;
        const char* category;
        const char* author;
        const char* description;
        std::vector<std::pair<const char*, float>> params;
    };

    const std::vector<Def> defs {
        { "Neon Machine", "LEADS", "P(X3)", "Bright PX3 lead with motion.",
          { { "oscMode", 19.0f / 19.0f }, { "oscMacroA", 0.62f }, { "oscMacroB", 0.55f }, { "oscMacroC", 0.58f }, { "filterCutoff", 0.82f }, { "filterResonance", 0.36f }, { "robAmount", 0.35f }, { "robEnabled", 1.0f }, { "reverbAmount", 0.20f }, { "reverbEnabled", 1.0f } } },
        { "Sub Pressure", "BASS", "P(X3)", "Low-end focused bass with light drive.",
                    { { "oscMode", 1.0f / 19.0f }, { "oscMacroA", 0.0f }, { "filterCutoff", 0.28f }, { "filterResonance", 0.15f }, { "robAmount", 0.42f }, { "robEnabled", 1.0f }, { "ampAttack", 0.01f }, { "ampDecay", 0.24f }, { "ampSustain", 0.75f } } },
        { "Soft Orbit", "PADS", "P(X3)", "Slow evolving supersaw pad.",
                    { { "oscMode", 6.0f / 19.0f }, { "oscMacroA", 0.38f }, { "oscMacroB", 0.72f }, { "ampAttack", 0.40f }, { "ampRelease", 0.65f }, { "reverbAmount", 0.44f }, { "reverbEnabled", 1.0f } } },
        { "Glass Pluck", "PLUCKS", "P(X3)", "Fast attack pluck with ping-pong delay.",
                    { { "oscMode", 11.0f / 19.0f }, { "oscMacroA", 0.42f }, { "oscMacroB", 0.44f }, { "ampAttack", 0.0f }, { "ampDecay", 0.16f }, { "ampSustain", 0.10f }, { "ampRelease", 0.18f }, { "delayEnabled", 1.0f }, { "delayAlgorithm", 3.0f / 6.0f }, { "isaacAmount", 0.30f } } },
        { "Chaos Reactor", "EXPERIMENTAL", "P(X3)", "Aggressive ROB chaos texture.",
          { { "oscMode", 17.0f / 19.0f }, { "oscMacroA", 0.82f }, { "oscMacroB", 0.74f }, { "oscMacroC", 0.93f }, { "robAmount", 0.68f }, { "robEnabled", 1.0f }, { "delayEnabled", 1.0f }, { "isaacAmount", 0.35f } } },
        { "Image Sweep", "IMAGE ENGINE", "P(X3)", "Wavetable image scan style preset.",
          { { "oscMode", 8.0f / 19.0f }, { "sourceEngine", 0.0f }, { "imagePosition", 0.25f }, { "imageAnimate", 0.50f }, { "imageRate", 0.55f }, { "imageTarget", 1.0f / 2.0f } } },
        { "Broken Radio", "AUDIO ENGINE", "P(X3)", "Audio engine granular texture demo.",
                    { { "sourceEngine", 1.0f }, { "audioPosition", 0.52f }, { "audioGrain", 0.62f }, { "audioTexture", 0.70f }, { "audioAnimate", 0.35f }, { "audioRate", 0.4f }, { "delayEnabled", 1.0f }, { "isaacAmount", 0.44f } } },

                // Extra randomized-style factory starters for shipping variety.
                { "Dustline Runner", "BASS", "P(X3)", "Tight low bass with controlled grit.",
                    { { "oscMode", 2.0f / 19.0f }, { "oscMacroA", 0.18f }, { "oscMacroB", 0.71f }, { "filterCutoff", 0.24f }, { "filterResonance", 0.29f }, { "ampAttack", 0.01f }, { "ampDecay", 0.22f }, { "ampSustain", 0.78f }, { "ampRelease", 0.27f }, { "robEnabled", 1.0f }, { "robAmount", 0.47f }, { "masterGain", 0.66f } } },
                { "Arc Light Mono", "LEADS", "P(X3)", "Focused mono lead with short ambience.",
                    { { "oscMode", 12.0f / 19.0f }, { "oscMacroA", 0.69f }, { "oscMacroB", 0.26f }, { "oscMacroC", 0.52f }, { "filterCutoff", 0.66f }, { "filterResonance", 0.42f }, { "ampAttack", 0.02f }, { "ampDecay", 0.19f }, { "ampSustain", 0.48f }, { "ampRelease", 0.21f }, { "reverbEnabled", 1.0f }, { "reverbAmount", 0.18f } } },
                { "Moonglass Bloom", "PADS", "P(X3)", "Wide evolving pad with slow movement.",
                    { { "oscMode", 6.0f / 19.0f }, { "oscMacroA", 0.41f }, { "oscMacroB", 0.84f }, { "oscMacroC", 0.37f }, { "filterCutoff", 0.58f }, { "filterResonance", 0.24f }, { "ampAttack", 0.54f }, { "ampDecay", 0.46f }, { "ampSustain", 0.72f }, { "ampRelease", 0.78f }, { "reverbEnabled", 1.0f }, { "reverbAmount", 0.56f }, { "delayEnabled", 1.0f }, { "isaacAmount", 0.21f } } },
                { "Pixel Harp", "PLUCKS", "P(X3)", "Snappy digital pluck with timed echoes.",
                    { { "oscMode", 15.0f / 19.0f }, { "oscMacroA", 0.77f }, { "oscMacroB", 0.33f }, { "filterCutoff", 0.74f }, { "filterResonance", 0.45f }, { "ampAttack", 0.0f }, { "ampDecay", 0.14f }, { "ampSustain", 0.12f }, { "ampRelease", 0.17f }, { "delayEnabled", 1.0f }, { "delayAlgorithm", 4.0f / 6.0f }, { "isaacAmount", 0.39f }, { "reverbEnabled", 1.0f }, { "reverbAmount", 0.11f } } },
                { "Volt Garden", "EXPERIMENTAL", "P(X3)", "Animated hybrid patch with shifting harmonics.",
                    { { "oscMode", 18.0f / 19.0f }, { "oscMacroA", 0.74f }, { "oscMacroB", 0.62f }, { "oscMacroC", 0.81f }, { "filterCutoff", 0.63f }, { "filterResonance", 0.58f }, { "robEnabled", 1.0f }, { "robAmount", 0.52f }, { "delayEnabled", 1.0f }, { "isaacAmount", 0.48f }, { "reverbEnabled", 1.0f }, { "reverbAmount", 0.31f } } },
                { "Raster Drift", "IMAGE ENGINE", "P(X3)", "Image source morph texture with moderate ambience.",
                    { { "sourceEngine", 0.0f }, { "oscMode", 8.0f / 19.0f }, { "imagePosition", 0.63f }, { "imageAnimate", 0.42f }, { "imageRate", 0.27f }, { "imageTarget", 2.0f / 2.0f }, { "filterCutoff", 0.57f }, { "reverbEnabled", 1.0f }, { "reverbAmount", 0.24f } } },
                { "Tape Phantom", "AUDIO ENGINE", "P(X3)", "Granular audio texture with diffused tail.",
                    { { "sourceEngine", 1.0f }, { "audioPosition", 0.34f }, { "audioGrain", 0.76f }, { "audioTexture", 0.58f }, { "audioAnimate", 0.49f }, { "audioRate", 0.22f }, { "delayEnabled", 1.0f }, { "delayAlgorithm", 1.0f / 6.0f }, { "isaacAmount", 0.36f }, { "reverbEnabled", 1.0f }, { "reverbAmount", 0.29f } } }
    };

    auto baseState = processor.createParameterStateTree();

    for (const auto& def : defs)
    {
        auto state = baseState.createCopy();
        for (const auto& [id, value] : def.params)
        {
            state.setProperty(id, juce::jlimit(0.0f, 1.0f, value), nullptr);
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

    auto state = migrated.getChildWithName(kPluginStateId);
    if (!state.isValid())
    {
        error = "Preset missing parameter state block.";
        return false;
    }

    outPresetTree = migrated;

    if (outRecord != nullptr)
    {
        const auto isFactory = file.isAChildOf(getFactoryPresetRootDir());
        *outRecord = makeRecordFromTree(file, isFactory, migrated);
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
    auto pluginState = processor.createParameterStateTree();
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

    return processor.applyParameterStateTree(stateCopy, &error);
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

    return migrated;
}

void PresetManager::collectAssetsForState(juce::ValueTree& pluginState,
                                          juce::ValueTree& assetsNode) const
{
    const auto collectOne = [&pluginState, &assetsNode](const juce::String& key,
                                                               const juce::String& type,
                                                               const juce::File& cacheDir)
    {
        // Embed path-backed media into preset payload so presets are portable.
        // If no media is loaded, this quietly does nothing.
        if (!pluginState.hasProperty(key))
        {
            return;
        }

        const auto sourcePath = pluginState.getProperty(key).toString();
        if (sourcePath.trim().isEmpty())
        {
            return;
        }

        juce::File source(sourcePath);
        if (!source.existsAsFile())
        {
            return;
        }

        juce::MemoryBlock data;
        if (!source.loadFileAsData(data))
        {
            return;
        }

        auto asset = juce::ValueTree(kAssetId);
        asset.setProperty("type", type, nullptr);
        asset.setProperty("parameterKey", key, nullptr);
        asset.setProperty("originalPath", sourcePath, nullptr);
        asset.setProperty("fileName", source.getFileName(), nullptr);
        juce::int64 hashSeed = static_cast<juce::int64>(source.hashCode64());
        hashSeed ^= static_cast<juce::int64>(data.getSize() * 2654435761ULL);
        asset.setProperty("hash", juce::String::toHexString(hashSeed), nullptr);
        asset.setProperty("embedded", true, nullptr);
        asset.setProperty("data", data.toBase64Encoding(), nullptr);

        // Cache by content hash so repeated saves of identical assets reuse the
        // same on-disk blob.
        const auto hashName = asset.getProperty("hash").toString() + source.getFileExtension();
        const auto cached = cacheDir.getChildFile(hashName);
        if (!cached.existsAsFile())
        {
            cached.replaceWithData(data.getData(), data.getSize());
        }

        // State points at local cached file path so restore can succeed even if
        // the original source path is no longer available.
        pluginState.setProperty(key, cached.getFullPathName(), nullptr);
        assetsNode.addChild(asset, -1, nullptr);
    };

    collectOne("imagePath", "image", getImageAssetsDir());
    collectOne("audioPath", "audio", getAudioAssetsDir());
}

bool PresetManager::materializeEmbeddedAssets(juce::ValueTree& pluginState,
                                              const juce::ValueTree& assetsNode,
                                              juce::String& error) const
{
    if (!assetsNode.isValid())
    {
        return true;
    }

    for (int i = 0; i < assetsNode.getNumChildren(); ++i)
    {
        auto asset = assetsNode.getChild(i);
        if (asset.getType() != kAssetId)
        {
            continue;
        }

        const auto key = asset.getProperty("parameterKey").toString();
        if (key.isEmpty())
        {
            continue;
        }

        const auto embedded = static_cast<bool>(asset.getProperty("embedded", false));
        if (!embedded)
        {
            continue;
        }

        const auto base64 = asset.getProperty("data").toString();
        if (base64.isEmpty())
        {
            continue;
        }

        juce::MemoryBlock data;
        if (!data.fromBase64Encoding(base64))
        {
            error = "Failed to decode embedded asset data for key: " + key;
            return false;
        }

        const auto hash = asset.getProperty("hash").toString();
        const auto fileName = asset.getProperty("fileName").toString();
        const auto ext = juce::File(fileName).getFileExtension();
        const auto type = asset.getProperty("type").toString().toLowerCase();
        auto dir = type == "audio" ? getAudioAssetsDir() : getImageAssetsDir();
        // Materialization is idempotent: existing cache files are reused.
        const auto outFile = dir.getChildFile((hash.isNotEmpty() ? hash : juce::String::toHexString(juce::Random::getSystemRandom().nextInt64())) + ext);

        if (!outFile.existsAsFile())
        {
            if (!outFile.replaceWithData(data.getData(), data.getSize()))
            {
                error = "Failed to materialize embedded asset: " + outFile.getFullPathName();
                return false;
            }
        }

        // Rewrite state path to materialized local copy before apply.
        pluginState.setProperty(key, outFile.getFullPathName(), nullptr);
    }

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
