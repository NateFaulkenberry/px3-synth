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
    // What the ecosystem knows about a product.
    //
    // One record, so the updater, the installer and any diagnostic all read
    // the same facts rather than each keeping a list. The build system has the
    // same identity in px3_add_product(); these are the runtime half of it.
    struct Registration
    {
        juce::String productId;      // "px3-synth" - stable, machine-facing
        juce::String displayName;    // "PX3 Synth" - what a person reads
        std::function<juce::String()> versionProvider;

        // macOS bundle identifier. PX3 Synth's is grandfathered as
        // com.px3.px3synth: it is what is already installed on people's
        // machines, and changing it would orphan every existing install. New
        // products follow com.px3.<product>.
        juce::String bundleId;

        // Which plug-in formats this product ships. An effect has no reason to
        // have a standalone application, and the default says so.
        bool hasVst3 { true };
        bool hasAudioUnit { true };
        bool hasStandalone { false };

        // The identity the installer uses for this product's component, so the
        // component list can be derived from the registry rather than kept as
        // a second hardcoded list beside it.
        juce::String installerComponentId;
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

    // The full record, including the identity the build and installer use.
    // Returns a registration with an empty productId if there is no such
    // product.
    Registration definition(const juce::String& productId) const;
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
