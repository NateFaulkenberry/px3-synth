#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>

#include "UpdateModel.h"

namespace px3::update
{

// Where releases come from.
//
// The one abstraction the whole design rests on. Everything above it - the
// service, the Settings UI, the helper application - is written against
// UpdateRelease and knows nothing about how a release was discovered. Swapping
// GitHub for a CDN or a build pipeline later is a new subclass, not a rewrite.
//
// The lookup is asynchronous because it is a network call, and network calls
// have no business on the message thread any more than on the audio thread.
// The callback is delivered ON the message thread, so a UI can act on it
// without marshalling.
class UpdateProvider
{
public:
    virtual ~UpdateProvider() = default;

    // What this provider is, for the log and for diagnostics.
    virtual juce::String name() const = 0;

    struct LookupResult
    {
        UpdateResult result;
        // Valid only when result.ok(). A provider that finds no release at all
        // returns ok() with an invalid release, which is "nothing published"
        // rather than "something went wrong".
        UpdateRelease release;
    };

    using LookupCallback = std::function<void(LookupResult)>;

    // Find the newest release of this product for this platform and
    // architecture. Must not block the caller.
    virtual void fetchLatestRelease(const juce::String& productId,
                                    const juce::String& platform,
                                    const juce::String& architecture,
                                    LookupCallback callback) = 0;

    // Stop any in-flight work. Called when the owner is going away; must be
    // safe to call when nothing is running.
    virtual void cancel() {}
};

} // namespace px3::update
