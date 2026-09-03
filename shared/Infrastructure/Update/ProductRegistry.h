#pragma once

#include <JuceHeader.h>

#include <functional>
#include <vector>

#include "UpdateModel.h"

namespace px3::update
{

// What the updater knows how to update.
//
// A list, not a hardcoded product. PX3 Synth is the first entry; a second PX3
// plugin costs a registration rather than a change to anything below. The
// version comes from a callback rather than a stored string so there is one
// source of truth for it - the product's own build - and no second copy here
// to fall out of date.
class ProductRegistry
{
public:
    struct Registration
    {
        juce::String productId;
        juce::String displayName;
        std::function<juce::String()> versionProvider;
    };

    // The registry every part of this build shares. A singleton for the same
    // reason GlobalSettings is one: there is exactly one set of installed
    // products on a machine, and two views of it could disagree.
    static ProductRegistry& getInstance();

    // Replaces any registration with the same id, so registering twice is not
    // a way to end up with a product listed twice.
    void registerProduct(Registration registration);
    void clear();

    std::vector<juce::String> productIds() const;
    bool isRegistered(const juce::String& productId) const;

    // The product as it stands right now, with its version read fresh.
    ProductInfo lookup(const juce::String& productId) const;
    std::vector<ProductInfo> installedProducts() const;

    // The one product this build ships. Registered by the processor at
    // startup; named here so nothing has to spell the string twice.
    static const char* const kSynthProductId;

private:
    ProductRegistry() = default;

    mutable juce::CriticalSection lock;
    std::vector<Registration> registrations;
};

} // namespace px3::update
