#pragma once

#include <JuceHeader.h>

#include <memory>
#include <vector>

namespace px3::synth
{
struct ParameterCatalogEntry
{
    juce::String id;
    juce::String name;
    juce::String unit;
    juce::String groupPath;
    juce::String formattedDefault;
    float minimum { 0.0f };
    float maximum { 0.0f };
    float interval { 0.0f };
    float defaultValue { 0.0f };
    bool automatable { false };
    bool meta { false };
    bool ranged { false };
    juce::AudioProcessorParameter* parameter { nullptr };
};

class ParameterCatalog final
{
public:
    void add(juce::AudioProcessorParameter* parameter);
    void attachTo(juce::AudioProcessor& processor);

    const std::vector<ParameterCatalogEntry>& entries() const noexcept { return catalogEntries; }
    const ParameterCatalogEntry* find(const juce::String& id) const noexcept;

private:
    struct GroupSegment
    {
        juce::String id;
        juce::String name;
    };

    static std::vector<GroupSegment> groupPathForId(const juce::String& id);

    juce::StringArray groupKeys;
    std::vector<juce::AudioProcessorParameterGroup*> groups;
    std::vector<std::unique_ptr<juce::AudioProcessorParameterGroup>> rootGroups;
    std::vector<ParameterCatalogEntry> catalogEntries;
    bool attached { false };
};
}
