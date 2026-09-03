#pragma once

#include "UpdateProvider.h"

namespace px3::update
{

// A provider that answers from a script rather than from the network.
//
// Exists so the update flow is testable without publishing a release, and so
// the failure paths - which are most of the code - can be exercised at all.
// A test that could only reach "network fine, release found" would be testing
// the one case that needs the least attention.
class MockUpdateProvider final : public UpdateProvider
{
public:
    juce::String name() const override { return "Mock"; }

    // What the next lookup answers with.
    UpdateResult nextResult { UpdateResult::success() };
    UpdateRelease nextRelease;
    // Set to deliver the answer only when deliverPending() is called, so a
    // test can observe the "checking" state rather than racing past it.
    bool deferReplies { false };

    int lookupCount { 0 };
    juce::String lastProductId, lastPlatform, lastArchitecture;

    void fetchLatestRelease(const juce::String& productId,
                            const juce::String& platform,
                            const juce::String& architecture,
                            LookupCallback callback) override
    {
        ++lookupCount;
        lastProductId = productId;
        lastPlatform = platform;
        lastArchitecture = architecture;

        LookupResult reply;
        reply.result = nextResult;
        reply.release = nextRelease;

        if (deferReplies) { pending = [callback, reply] { callback(reply); }; }
        else              { callback(reply); }
    }

    void deliverPending()
    {
        if (pending != nullptr)
        {
            auto deliver = pending;
            pending = nullptr;
            deliver();
        }
    }

    bool hasPending() const { return pending != nullptr; }

    void cancel() override { pending = nullptr; }

private:
    std::function<void()> pending;
};

} // namespace px3::update
