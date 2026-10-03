#pragma once

#include <JuceHeader.h>

#include <memory>
#include <variant>
#include <vector>

namespace px3::synth
{
enum class ParameterKind { continuous, boolean, choice, integer };

struct ParameterDefinition
{
    juce::ParameterID id;
    juce::String name;
    ParameterKind kind;
    juce::NormalisableRange<float> range;
    float defaultValue;
    juce::StringArray choices;
    std::variant<juce::AudioParameterFloatAttributes, juce::AudioParameterBoolAttributes,
                 juce::AudioParameterChoiceAttributes, juce::AudioParameterIntAttributes> attributes;
};

struct ParameterCatalogEntry
{
    juce::String id;
    juce::String name;
    juce::String unit;
    juce::String groupPath;
    std::vector<juce::String> groupIds;
    std::vector<juce::String> groupNames;
    juce::String formattedDefault;
    float minimum { 0.0f };
    float maximum { 0.0f };
    float interval { 0.0f };
    float defaultValue { 0.0f };
    bool automatable { false };
    bool meta { false };
    bool ranged { false };
    juce::AudioProcessorParameter* parameter { nullptr };
    const ParameterDefinition* definition { nullptr };
};

class ParameterCatalog final
{
public:
    struct StateValue
    {
        juce::AudioProcessorParameter* parameter { nullptr };
        float normalizedValue { 0.0f };
    };

    juce::AudioParameterFloat* createFloat(const juce::ParameterID& id, const juce::String& name,
                                         juce::NormalisableRange<float> range, float defaultValue,
                                         const juce::AudioParameterFloatAttributes& attributes = {});
    juce::AudioParameterBool* createBool(const juce::ParameterID& id, const juce::String& name,
                                       bool defaultValue,
                                       const juce::AudioParameterBoolAttributes& attributes = {});
    juce::AudioParameterChoice* createChoice(const juce::ParameterID& id, const juce::String& name,
                                           const juce::StringArray& choices, int defaultIndex,
                                           const juce::AudioParameterChoiceAttributes& attributes = {});
    juce::AudioParameterInt* createInt(const juce::ParameterID& id, const juce::String& name,
                                     int minimum, int maximum, int defaultValue,
                                     const juce::AudioParameterIntAttributes& attributes = {});
    void add(juce::AudioProcessorParameter* parameter);
    void attachTo(juce::AudioProcessor& processor);

    const std::vector<ParameterCatalogEntry>& entries() const noexcept { return catalogEntries; }
    const ParameterCatalogEntry* find(const juce::String& id) const noexcept;
    juce::ValueTree createStateTree() const;
    juce::ValueTree findStateEntry(const juce::ValueTree& state, const juce::String& id) const;
    bool readStateValues(const juce::ValueTree& state,
                         std::vector<StateValue>& values,
                         juce::String& error) const;

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
    std::vector<std::unique_ptr<ParameterDefinition>> definitions;
    bool attached { false };
};
}
