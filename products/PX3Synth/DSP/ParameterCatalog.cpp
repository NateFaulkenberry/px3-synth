#include "ParameterCatalog.h"

#include <algorithm>
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

    if (key.startsWith("osc1") || key.startsWith("osc2") || key.startsWith("osc3"))
    {
        const auto slot = key.substring(3, 4);
        return path("voice", "VOICE", "osc" + slot, "OSC " + slot);
    }
    if (key.startsWith("subosc"))
    {
        return path("voice", "VOICE", "sub", "SUB OSC");
    }
    if (key.startsWith("filter"))
    {
        const auto slot = (key.startsWith("filter2") ? juce::String("2")
                          : key.startsWith("filter1") ? juce::String("1") : juce::String("routing"));
        return path("voice", "VOICE", "filter" + slot,
                    slot == "routing" ? juce::String("FILTER ROUTING") : "FILTER " + slot);
    }
    if (key.startsWith("amp"))
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
        groupKey << "/" << segment.id;
        groupNames.add(segment.name);
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
}
