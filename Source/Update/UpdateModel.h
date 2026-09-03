#pragma once

#include <JuceHeader.h>

#include "SemanticVersion.h"

namespace px3::update
{

// The vocabulary the updater works in.
//
// Deliberately says nothing about GitHub, about synths, or about PX3 Synth in
// particular. A provider translates whatever a release source gives it into
// these types, and everything above this line - the service, the UI, the
// helper application - is written against them. Replacing GitHub with a CDN or
// a build pipeline later is then a new provider rather than a rewrite.

// A thing that can be updated. PX3 Synth is the first; the registry is a list
// so the second costs a registration rather than a new updater.
struct ProductInfo
{
    juce::String productId;        // "px3-synth" - stable, machine-facing
    juce::String displayName;      // "PX3 Synth" - what a person reads
    SemanticVersion installedVersion;

    bool isValid() const { return productId.isNotEmpty() && installedVersion.isValid; }
};

// One release of one product, as the updater understands it.
struct UpdateRelease
{
    juce::String productId;
    SemanticVersion version;
    juce::String releaseNotes;
    juce::URL downloadUrl;
    juce::String installerFilename;
    juce::String platform;         // "macOS"
    juce::String architecture;     // "arm64"
    juce::String sha256;           // empty when the source published none
    juce::Time releaseDate;

    bool isValid() const
    {
        return productId.isNotEmpty() && version.isValid
               && downloadUrl.isWellFormed() && installerFilename.isNotEmpty();
    }
};

// Where the update flow has got to. One enum rather than a set of booleans,
// for the same reason the macro UI uses one: the combinations that a set of
// flags allows mostly have no meaning.
enum class UpdateState
{
    notConfigured,      // no provider, or no product registered
    idle,               // nothing known yet
    checking,
    upToDate,
    updateAvailable,
    downloading,
    verifying,
    readyToInstall,     // staged and verified, waiting for the host to quit
    installing,
    updated,
    failed
};

juce::String describe(UpdateState state);

// Why something failed, in terms a person can be told. The raw HTTP status or
// system error goes to the log; this is what the UI is allowed to say.
enum class UpdateError
{
    none,
    noNetwork,
    providerUnavailable,
    malformedResponse,
    noMatchingInstaller,
    unsupportedArchitecture,
    downloadFailed,
    checksumMismatch,
    stagingFailed,
    installerLaunchFailed,
    installerFailed,
    verificationFailed,
    cancelled,
    alreadyInProgress
};

// One sentence, written for a musician rather than for a developer.
juce::String describe(UpdateError error);

// The result of an operation that can fail, with the detail kept separate from
// the sentence the user sees.
struct UpdateResult
{
    UpdateError error { UpdateError::none };
    juce::String technicalDetail;   // for the log, never for the UI

    bool ok() const { return error == UpdateError::none; }

    static UpdateResult success() { return {}; }
    static UpdateResult failure(UpdateError e, const juce::String& detail = {})
    {
        return { e, detail };
    }
};

} // namespace px3::update
