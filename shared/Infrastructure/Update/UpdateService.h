#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <memory>

#include "ProductRegistry.h"
#include "UpdateProvider.h"

namespace px3::update
{

// The update flow, in one object that knows nothing about the UI.
//
// Checking, comparing, staging, and the state everything else observes. A
// ChangeBroadcaster rather than a callback list, because that is how this
// project already publishes shared state - GlobalSettings does the same - and
// because the Settings panel is not the only thing that will want to watch it.
//
// Nothing here touches the audio thread, and nothing here is called FROM it.
// The network lives on the provider's own thread; the staging work lives on
// this class's; the state is published on the message thread.
class UpdateService final : public juce::ChangeBroadcaster
{
public:
    // Shared, so the plugin and the standalone are the same update flow rather
    // than two implementations that can disagree.
    static UpdateService& getInstance();

    UpdateService();
    ~UpdateService() override;

    // What to ask. Replacing this is how GitHub becomes something else later.
    void setProvider(std::unique_ptr<UpdateProvider> provider);
    UpdateProvider* getProvider() const { return provider.get(); }

    // Which product this service is following. One for now.
    void setProductId(juce::String productId);

    // The pre-release channel, applied to whatever provider is installed.
    // Kept here rather than only on the provider so the state is one thing:
    // the UI asks the service, not a downcast to a GitHub type.
    void setPreReleaseChannelEnabled(bool shouldBeEnabled);
    bool isPreReleaseChannelEnabled() const { return preReleaseChannel; }
    juce::String getProductId() const { return productId; }

    //---- state ------------------------------------------------------------
    UpdateState getState() const { return state; }
    UpdateError getError() const { return error; }
    juce::String getErrorMessage() const { return describe(error); }
    // 0..1 while downloading, otherwise 0.
    float getProgress() const { return progress.load(); }
    // Valid only once a check has found something newer.
    UpdateRelease getAvailableRelease() const { return available; }
    ProductInfo getProduct() const;

    //---- operations -------------------------------------------------------

    // Ask the provider what the newest release is. Does nothing if a check is
    // already running, or if one finished recently - see kMinimumCheckInterval.
    // `force` ignores the interval but not a check already in flight.
    void checkForUpdates(bool force = false);

    // Download, verify and stage the installer. Deliberately NOT an install:
    // the user can carry on working while this happens, and the installation
    // waits until the host is no longer using the plugin.
    void prepareUpdate();

    // Throw away anything staged and return to idle.
    void cancel();

    // Hand the staged installer to the helper application and let it take over.
    //
    // Deliberately NOT an install from this process. This code runs inside
    // somebody's DAW, holding the very plug-in the installer would replace;
    // the helper is a separate process precisely so it can outlive the host
    // and act once the files are no longer in use. Returns false if there is
    // nothing staged or the helper could not be started.
    bool launchInstaller();

    // The helper as SHIPPED, inside the standalone's bundle. Public so a
    // diagnostic can report whether it is actually there.
    static juce::File updaterApplication();

    //---- staging ----------------------------------------------------------

    // ~/Library/P(X3)/Updates - beside Presets, Settings and Wavetables, which
    // is where this project already keeps its user data. Created on demand.
    static juce::File stagingDirectory();
    // What is staged right now, or an invalid file.
    juce::File stagedInstaller() const;
    // True if a staged, verified installer for this release is already there,
    // so a restart does not re-download what is already on disk.
    bool hasStagedUpdate() const;

    //---- for tests --------------------------------------------------------
    // Replaces the download step, so the flow can be exercised without a
    // network and the failure paths - which are most of it - can be reached.
    using Downloader = std::function<bool(const juce::URL&, const juce::File&,
                                          std::function<void(float)>)>;
    void setDownloaderForTesting(Downloader downloader) { downloadFile = std::move(downloader); }
    void setStagingDirectoryForTesting(juce::File directory) { stagingOverride = std::move(directory); }
    void setSynchronousForTesting(bool shouldBeSynchronous) { synchronous = shouldBeSynchronous; }
    void resetForTesting();

    // SHA-256 of a file, as lowercase hex. Public because the helper
    // application verifies the same way, from the same code.
    static juce::String sha256Of(const juce::File& file);

private:
    void setState(UpdateState newState, UpdateError newError = UpdateError::none,
                  const juce::String& detail = {});
    void onLookupComplete(UpdateProvider::LookupResult reply);
    void runPreparation();
    juce::File stagingRoot() const;
    void writeStagedMetadata(const UpdateRelease& release, const juce::File& installer) const;
    juce::var readStagedMetadata() const;

    std::unique_ptr<UpdateProvider> provider;
    juce::String productId;

    UpdateState state { UpdateState::idle };
    UpdateError error { UpdateError::none };
    std::atomic<float> progress { 0.0f };
    UpdateRelease available;

    juce::Time lastCheck;
    // A check no more than once every ten minutes unless asked directly. The
    // point is not to spare GitHub but to stop an editor that is opened and
    // closed repeatedly from checking every time.
    static constexpr int kMinimumCheckIntervalSeconds = 600;

    Downloader downloadFile;
    juce::File stagingOverride;
    bool synchronous { false };
    bool preReleaseChannel { false };
    std::atomic<bool> busy { false };
    std::unique_ptr<juce::Thread> worker;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpdateService)
};

// Registers PX3 Synth and points the service at the repository's Releases.
// Called once, from wherever the plugin or the standalone starts up; calling
// it again is harmless.
//
// This is the only place a GitHub repository is named. Pointing the updater
// somewhere else later is this function, not a search through the codebase.
void installDefaultConfiguration();

// Just the product registrations, without the provider or the command line.
//
// Separate from the above because that one is call_once - it must not repeat
// its side effects - while this is idempotent and safe to call whenever the
// registry needs putting back, which anything that clears the registry has to
// do. A registration that exists only inside a call_once is a registration
// that cannot be restored.
void registerDefaultProducts();

} // namespace px3::update
