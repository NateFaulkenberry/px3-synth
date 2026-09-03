#pragma once

#include "UpdateProvider.h"

#include <atomic>
#include <memory>

namespace px3::update
{

// Releases, read from a GitHub repository's Releases.
//
// The temporary source. Everything GitHub-shaped is confined to this file: the
// API URL, the JSON field names, the asset-matching rule. Above it there are
// only UpdateReleases, which is what makes replacing this with a CDN or a
// build pipeline a new class rather than a rewrite.
//
// PARSING IS SEPARATE FROM FETCHING, and public, so the interesting half can
// be tested on canned responses. Automated tests must not depend on GitHub
// being reachable or on a particular release existing.
class GitHubReleaseProvider final : public UpdateProvider
{
public:
    GitHubReleaseProvider(juce::String repositoryOwner, juce::String repositoryName);
    ~GitHubReleaseProvider() override;

    juce::String name() const override { return "GitHub Releases"; }

    void fetchLatestRelease(const juce::String& productId,
                            const juce::String& platform,
                            const juce::String& architecture,
                            LookupCallback callback) override;

    void cancel() override;

    juce::URL latestReleaseUrl() const;

    // Turn one GitHub release document into our own representation.
    //
    // ASSET MATCHING. The installer is the .pkg the release script already
    // produces - today "P(X3)-v0.7.0.pkg", which names neither platform nor
    // architecture. So the rule is: take .pkg assets, drop any that name a
    // DIFFERENT platform or architecture, and prefer one that names ours.
    // That accepts today's filename and tomorrow's
    // "PX3-Synth-1.6.0-macOS-arm64.pkg" without a change here.
    //
    // CHECKSUM. GitHub publishes no checksum of its own. A release whose notes
    // contain a line "SHA-256: <64 hex digits>" gets one; anything else leaves
    // the field empty, and the installer's own Developer ID signature and
    // notarisation are then what authenticate it. See UpdateService for what
    // that means for what gets executed.
    static LookupResult parseLatestRelease(const juce::String& jsonText,
                                           const juce::String& productId,
                                           const juce::String& platform,
                                           const juce::String& architecture);

    // The transport, so a test can answer without a network. Returns the body,
    // and sets the result on failure.
    using Fetcher = std::function<juce::String(const juce::URL&, UpdateResult&)>;
    void setFetcherForTesting(Fetcher fetcher) { transport = std::move(fetcher); }

    // Runs the lookup on the CALLING thread rather than a background one, so a
    // test does not need a message loop to see the answer.
    void setSynchronousForTesting(bool shouldBeSynchronous) { synchronous = shouldBeSynchronous; }

private:
    class LookupJob;

    juce::String owner, repo;
    Fetcher transport;
    bool synchronous { false };
    std::unique_ptr<LookupJob> job;
    std::shared_ptr<std::atomic<bool>> cancelled { std::make_shared<std::atomic<bool>>(false) };
};

} // namespace px3::update
