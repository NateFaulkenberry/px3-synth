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

    if (key.startsWith("voice.osc1.") || key.startsWith("voice.osc2.") || key.startsWith("voice.osc3.")
        || key.startsWith("osc1") || key.startsWith("osc2") || key.startsWith("osc3"))
    {
        const auto slot = (key.startsWith("voice.osc3.") || key.startsWith("osc3")) ? juce::String("3")
                          : (key.startsWith("voice.osc2.") || key.startsWith("osc2")) ? juce::String("2")
                                                                                       : juce::String("1");
        return path("voice", "VOICE", "osc" + slot, "OSC " + slot);
    }
    if (key.startsWith("voice.sub.") || key.startsWith("subosc"))
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
    if (key.startsWith("env"))
    {
        const auto slot = key.startsWith("env3") ? juce::String("3")
                          : key.startsWith("env2") ? juce::String("2") : juce::String("1");
        return path("modulation", "MODULATION", "env" + slot, "ENV " + slot);
    }
    if (key.startsWith("lfo"))
    {
        const auto slot = key.startsWith("lfo3") ? juce::String("3")
                          : key.startsWith("lfo2") ? juce::String("2") : juce::String("1");
        return path("modulation", "MODULATION", "lfo" + slot, "LFO " + slot);
    }
    if (key.startsWith("macro"))
    {
        return path("modulation", "MODULATION", "macros", "MACROS");
    }
    if (key.startsWith("mix"))
    {
        return path("mixer", "MIXER", "channels", "CHANNELS");
    }
    if (key.startsWith("dry") || key.startsWith("fxreturn") || key.startsWith("fxsend")
        || key.startsWith("fxeq") || key.startsWith("fxcomp") || key.startsWith("mastergain"))
    {
        return path("mixer", "MIXER", "buses", "BUSES");
    }
    if (key.startsWith("delay") || key.startsWith("granular"))
    {
        return path("effects", "EFFECTS", "delay", "DELAY");
    }
    if (key.startsWith("reverb"))
    {
        return path("effects", "EFFECTS", "reverb", "REVERB");
    }
    if (key.startsWith("mood"))
    {
        return path("effects", "EFFECTS", "mood", "MOOD");
    }
    if (key.startsWith("doom"))
    {
        return path("effects", "EFFECTS", "doom", "DOOM");
    }
    if (key.startsWith("lucy"))
    {
        return path("effects", "EFFECTS", "lucy", "LUCY");
    }
    if (key.startsWith("chorus"))
    {
        return path("effects", "EFFECTS", "chorus", "CHORUS");
    }
    if (key.startsWith("spread"))
    {
        return path("effects", "EFFECTS", "spread", "STEREO SPREAD");
    }
    if (key.startsWith("vibe"))
    {
        return path("effects", "EFFECTS", "vibe", "VIBE");
    }
    if (key.startsWith("analog"))
    {
        return path("global", "GLOBAL", "character", "CHARACTER");
    }
    if (key.startsWith("pitchbend"))
    {
        return path("performance", "PERFORMANCE", "pitch", "PITCH");
    }
    if (key.startsWith("fxseparate"))
    {
        return path("global", "GLOBAL", "outputs", "OUTPUTS");
    }
    return path("global", "GLOBAL", "system", "SYSTEM");
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
