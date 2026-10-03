#include "ParameterCatalog.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <map>
#include <utility>

namespace px3::synth
{
std::vector<ParameterCatalog::GroupSegment> ParameterCatalog::groupPathForId(const juce::String& id)
{
    const auto key = id.toLowerCase();
    const auto path = [](const char* rootId, const char* rootName,
                         const juce::String& moduleId, const juce::String& moduleName)
    {
        return std::vector<GroupSegment> { { rootId, rootName }, { moduleId, moduleName } };
    };

    if (key.startsWith("fx."))
    {
        const auto module = key.substring(3).upToFirstOccurrenceOf(".", false, false);
        return path("effects", "EFFECTS", module, module == "spread" ? juce::String("STEREO SPREAD") : module.toUpperCase());
    }
    if (key.startsWith("voice.osc1.") || key.startsWith("voice.osc2.") || key.startsWith("voice.osc3."))
    {
        const auto slot = key.startsWith("voice.osc3.") ? juce::String("3")
                          : key.startsWith("voice.osc2.") ? juce::String("2") : juce::String("1");
        return path("voice", "VOICE", "osc" + slot, "OSC " + slot);
    }
    if (key.startsWith("voice.sub."))
    {
        return path("voice", "VOICE", "sub", "SUB OSC");
    }
    if (key.startsWith("voice.filters."))
    {
        return { { "voice", "VOICE" }, { "filters", "FILTERS" }, { "routing", "ROUTING" } };
    }
    if (key.startsWith("voice.filter1.") || key.startsWith("voice.filter2."))
    {
        const auto slot = key.startsWith("voice.filter2.") ? juce::String("2") : juce::String("1");
        return path("voice", "VOICE", "filter" + slot, "FILTER " + slot);
    }
    if (key.startsWith("voice.amp."))
    {
        return path("voice", "VOICE", "amp", "AMP ENVELOPE");
    }
    if (key.startsWith("mod.routes."))
    {
        return path("modulation", "MODULATION", "routes", "ROUTES");
    }
    if (key.startsWith("mod.env") || key.startsWith("mod.lfo"))
    {
        const auto module = key.substring(4).upToFirstOccurrenceOf(".", false, false);
        return path("modulation", "MODULATION", module, module.substring(0, 3).toUpperCase() + " " + module.substring(3));
    }
    if (key.startsWith("mod.macro"))
    {
        return path("modulation", "MODULATION", "macros", "MACROS");
    }
    if (key.startsWith("mix."))
    {
        const auto module = key.substring(4).upToFirstOccurrenceOf(".", false, false);
        const auto name = module == "dry" ? juce::String("DRY BUS")
                          : module == "fx" ? juce::String("FX RETURN")
                          : module == "send" ? juce::String("FX SEND")
                          : module.startsWith("osc") ? "OSC " + module.substring(3) : module.toUpperCase();
        auto result = path("mixer", "MIXER", module, name);
        if (key.contains(".insert.eq.")) { result.push_back({ "eq", "EQ" }); }
        if (key.contains(".insert.comp.")) { result.push_back({ "comp", "COMPRESSOR" }); }
        return result;
    }
    if (key.startsWith("global.character."))
    {
        return path("global", "GLOBAL", "character", "CHARACTER");
    }
    if (key.startsWith("performance.pitch."))
    {
        return path("performance", "PERFORMANCE", "pitch", "PITCH");
    }
    if (key.startsWith("global.outputs."))
    {
        return path("global", "GLOBAL", "outputs", "OUTPUTS");
    }
    return path("global", "GLOBAL", "system", "SYSTEM");
}

juce::AudioParameterFloat* ParameterCatalog::createFloat(const juce::ParameterID& id,
                                                        const juce::String& name,
                                                        juce::NormalisableRange<float> range,
                                                        float defaultValue,
                                                        const juce::AudioParameterFloatAttributes& attributes)
{
    auto definition = std::make_unique<ParameterDefinition>(ParameterDefinition {
        id, name, ParameterKind::continuous, range, defaultValue, {}, attributes });
    auto* parameter = new juce::AudioParameterFloat(definition->id, definition->name,
                                                   definition->range, definition->defaultValue,
                                                   std::get<juce::AudioParameterFloatAttributes>(definition->attributes));
    definitions.push_back(std::move(definition));
    return parameter;
}

juce::AudioParameterBool* ParameterCatalog::createBool(const juce::ParameterID& id,
                                                      const juce::String& name, bool defaultValue,
                                                      const juce::AudioParameterBoolAttributes& attributes)
{
    auto definition = std::make_unique<ParameterDefinition>(ParameterDefinition {
        id, name, ParameterKind::boolean, { 0.0f, 1.0f, 1.0f }, defaultValue ? 1.0f : 0.0f, {}, attributes });
    auto* parameter = new juce::AudioParameterBool(definition->id, definition->name,
                                                  definition->defaultValue != 0.0f,
                                                  std::get<juce::AudioParameterBoolAttributes>(definition->attributes));
    definitions.push_back(std::move(definition));
    return parameter;
}

juce::AudioParameterChoice* ParameterCatalog::createChoice(const juce::ParameterID& id,
                                                          const juce::String& name,
                                                          const juce::StringArray& choices, int defaultIndex,
                                                          const juce::AudioParameterChoiceAttributes& attributes)
{
    auto definition = std::make_unique<ParameterDefinition>(ParameterDefinition {
        id, name, ParameterKind::choice, { 0.0f, static_cast<float>(choices.size() - 1), 1.0f },
        static_cast<float>(defaultIndex), choices, attributes });
    auto* parameter = new juce::AudioParameterChoice(definition->id, definition->name,
                                                    definition->choices,
                                                    static_cast<int>(definition->defaultValue),
                                                    std::get<juce::AudioParameterChoiceAttributes>(definition->attributes));
    definitions.push_back(std::move(definition));
    return parameter;
}

juce::AudioParameterInt* ParameterCatalog::createInt(const juce::ParameterID& id,
                                                    const juce::String& name,
                                                    int minimum, int maximum, int defaultValue,
                                                    const juce::AudioParameterIntAttributes& attributes)
{
    auto definition = std::make_unique<ParameterDefinition>(ParameterDefinition {
        id, name, ParameterKind::integer,
        { static_cast<float>(minimum), static_cast<float>(maximum), 1.0f },
        static_cast<float>(defaultValue), {}, attributes });
    auto* parameter = new juce::AudioParameterInt(definition->id, definition->name,
                                                 static_cast<int>(definition->range.start),
                                                 static_cast<int>(definition->range.end),
                                                 static_cast<int>(definition->defaultValue),
                                                 std::get<juce::AudioParameterIntAttributes>(definition->attributes));
    definitions.push_back(std::move(definition));
    return parameter;
}

void ParameterCatalog::add(juce::AudioProcessorParameter* parameter)
{
    if (parameter == nullptr || attached)
    {
        jassert(parameter != nullptr && ! attached);
        return;
    }

    auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
    const auto id = ranged != nullptr ? ranged->getParameterID() : parameter->getName(128);
    if (id.isEmpty() || find(id) != nullptr)
    {
        jassert(id.isNotEmpty() && find(id) == nullptr);
        return;
    }

    const auto path = groupPathForId(id);
    juce::String groupKey;
    juce::StringArray groupNames;
    juce::AudioProcessorParameterGroup* parent = nullptr;
    for (const auto& segment : path)
    {
        groupNames.add(segment.name);
        groupKey << "/" << segment.id;
        auto index = groupKeys.indexOf(groupKey);
        if (index < 0)
        {
            auto group = std::make_unique<juce::AudioProcessorParameterGroup>(segment.id,
                                                                               segment.name,
                                                                               ":");
            auto* groupPointer = group.get();
            if (parent == nullptr)
            {
                rootGroups.push_back(std::move(group));
            }
            else
            {
                parent->addChild(std::move(group));
            }
            groupKeys.add(groupKey);
            groups.push_back(groupPointer);
            index = static_cast<int>(groups.size() - 1);
        }
        parent = groups[static_cast<std::size_t>(index)];
    }

    parent->addChild(std::unique_ptr<juce::AudioProcessorParameter>(parameter));

    ParameterCatalogEntry entry;
    entry.id = id;
    entry.name = parameter->getName(128);
    entry.unit = parameter->getLabel();
    entry.groupPath = groupNames.joinIntoString(" / ");
    for (const auto& segment : path)
    {
        entry.groupIds.push_back(segment.id);
        entry.groupNames.push_back(segment.name);
    }
    entry.automatable = parameter->isAutomatable();
    entry.meta = parameter->isMetaParameter();
    entry.parameter = parameter;
    for (const auto& definition : definitions)
    {
        if (definition->id.getParamID() == id)
        {
            entry.definition = definition.get();
            break;
        }
    }
    if (ranged != nullptr)
    {
        const auto& range = ranged->getNormalisableRange();
        entry.ranged = true;
        entry.minimum = range.start;
        entry.maximum = range.end;
        entry.interval = range.interval;
        entry.defaultValue = range.convertFrom0to1(ranged->getDefaultValue());
        entry.formattedDefault = parameter->getText(ranged->getDefaultValue(), 128);
    }
    if (entry.definition != nullptr)
    {
        entry.name = entry.definition->name;
        std::visit([&entry](const auto& attributes)
        {
            const auto& host = attributes.getAudioProcessorParameterWithIDAttributes();
            entry.unit = host.getLabel();
            entry.automatable = host.getAutomatable();
            entry.meta = host.getMeta();
        }, entry.definition->attributes);
        entry.minimum = entry.definition->range.start;
        entry.maximum = entry.definition->range.end;
        entry.interval = entry.definition->range.interval;
        entry.defaultValue = entry.definition->defaultValue;
        const auto& key = entry.id;
        entry.sourceControl = (key.startsWith("mod.lfo") && (key.endsWith(".frequency") || key.endsWith(".ramp.time")))
            || (key.startsWith("mod.env") && (key.endsWith(".attack") || key.endsWith(".decay")
                                          || key.endsWith(".sustain") || key.endsWith(".release")));
        entry.modulationDestination = entry.definition->kind == ParameterKind::continuous
            && (entry.sourceControl || (! key.startsWith("mod.") && ! key.startsWith("voice.amp.")
                                       && key != "performance.pitch.bend.range"));
    }
    catalogEntries.push_back(std::move(entry));
}

void ParameterCatalog::attachTo(juce::AudioProcessor& processor)
{
    jassert(! attached);
    if (attached)
    {
        return;
    }
    for (auto& group : rootGroups)
    {
        processor.addParameterGroup(std::move(group));
    }
    rootGroups.clear();
    juce::Array<juce::var> schema;
    for (const auto& entry : catalogEntries)
    {
        juce::Array<juce::var> choices;
        if (entry.definition != nullptr)
        {
            for (const auto& choice : entry.definition->choices) { choices.add(choice); }
        }
        const auto kind = entry.definition != nullptr ? static_cast<int>(entry.definition->kind) : -1;
        const auto skew = entry.definition != nullptr ? entry.definition->range.skew : 1.0f;
        schema.add(juce::var(juce::Array<juce::var> {
            entry.id, kind, entry.minimum, entry.maximum, entry.interval, entry.defaultValue, skew, juce::var(choices) }));
    }
    const auto text = juce::JSON::toString(juce::var(schema), true);
    schemaFingerprint = juce::SHA256(text.toRawUTF8(), static_cast<std::size_t>(text.getNumBytesAsUTF8())).toHexString();
    attached = true;
}

const ParameterCatalogEntry* ParameterCatalog::find(const juce::String& id) const noexcept
{
    const auto found = std::find_if(catalogEntries.begin(), catalogEntries.end(), [&id](const auto& entry)
    {
        return entry.id == id;
    });
    return found == catalogEntries.end() ? nullptr : &*found;
}

juce::ValueTree ParameterCatalog::createStateTree() const
{
    juce::ValueTree root("PARAMETERS");
    root.setProperty("schemaFingerprint", schemaFingerprint, nullptr);
    std::map<juce::String, juce::ValueTree> groupsByPath;
    for (const auto& entry : catalogEntries)
    {
        auto parent = root;
        juce::String path;
        for (std::size_t i = 0; i < entry.groupIds.size(); ++i)
        {
            path << "/" << entry.groupIds[i];
            auto found = groupsByPath.find(path);
            if (found == groupsByPath.end())
            {
                juce::ValueTree group("GROUP");
                group.setProperty("id", entry.groupIds[i], nullptr);
                group.setProperty("name", entry.groupNames[i], nullptr);
                parent.addChild(group, -1, nullptr);
                found = groupsByPath.emplace(path, group).first;
            }
            parent = found->second;
        }

        juce::ValueTree value("PARAMETER");
        value.setProperty("id", entry.id, nullptr);
        value.setProperty("value", entry.parameter != nullptr ? entry.parameter->getValue() : 0.0f, nullptr);
        parent.addChild(value, -1, nullptr);
    }
    return root;
}

juce::ValueTree ParameterCatalog::findStateEntry(const juce::ValueTree& state,
                                                  const juce::String& id) const
{
    const auto* entry = find(id);
    if (entry == nullptr || state.getType() != juce::Identifier("PARAMETERS"))
    {
        return {};
    }

    auto parent = state;
    for (const auto& groupId : entry->groupIds)
    {
        auto child = juce::ValueTree();
        for (const auto& candidate : parent)
        {
            if (candidate.getType() == juce::Identifier("GROUP")
                && candidate.getProperty("id").toString() == groupId)
            {
                child = candidate;
                break;
            }
        }
        if (! child.isValid()) { return {}; }
        parent = child;
    }

    for (const auto& candidate : parent)
    {
        if (candidate.getType() == juce::Identifier("PARAMETER")
            && candidate.getProperty("id").toString() == id)
        {
            return candidate;
        }
    }
    return {};
}

bool ParameterCatalog::readStateValues(const juce::ValueTree& state,
                                       std::vector<StateValue>& values,
                                       juce::String& error) const
{
    values.clear();
    if (state.getType() != juce::Identifier("PARAMETERS"))
    {
        error = "Grouped parameter state is missing or invalid.";
        return false;
    }
    if (schemaFingerprint.isEmpty() || state.getProperty("schemaFingerprint").toString() != schemaFingerprint)
    {
        error = "Parameter schema does not match this PX3 generation.";
        return false;
    }

    values.reserve(catalogEntries.size());
    for (const auto& entry : catalogEntries)
    {
        const auto node = findStateEntry(state, entry.id);
        const auto value = node.getProperty("value", {});
        if (! node.isValid())
        {
            error = "Grouped parameter state is missing a numeric value for " + entry.id;
            values.clear();
            return false;
        }
        auto normalized = 0.0f;
        if (value.isInt() || value.isInt64() || value.isDouble())
        {
            normalized = static_cast<float>(static_cast<double>(value));
        }
        else if (value.isString())
        {
            const auto text = value.toString().trim();
            const auto* start = text.toRawUTF8();
            char* end = nullptr;
            normalized = std::strtof(start, &end);
            if (end == start || *end != '\0')
            {
                error = "Grouped parameter state has invalid numeric text for " + entry.id;
                values.clear();
                return false;
            }
        }
        else
        {
            error = "Grouped parameter state is missing a numeric value for " + entry.id;
            values.clear();
            return false;
        }
        if (! std::isfinite(normalized) || normalized < 0.0f || normalized > 1.0f)
        {
            error = "Grouped parameter value is not finite and normalized for " + entry.id;
            values.clear();
            return false;
        }
        values.push_back({ entry.parameter, normalized });
    }

    auto parameterCount = 0;
    std::function<void(const juce::ValueTree&)> countParameters = [&](const juce::ValueTree& node)
    {
        if (node.getType() == juce::Identifier("PARAMETER")) { ++parameterCount; }
        for (const auto& child : node) { countParameters(child); }
    };
    countParameters(state);
    if (parameterCount != static_cast<int>(catalogEntries.size()))
    {
        error = "Grouped parameter state has missing or duplicate parameters.";
        values.clear();
        return false;
    }

    error.clear();
    return true;
}
}
