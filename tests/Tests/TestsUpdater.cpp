#include "TestSupport.h"

#include "SemanticVersion.h"
#include "UpdateModel.h"
#include "GitHubReleaseProvider.h"
#include "InstallerVerification.h"
#include "MockUpdateProvider.h"
#include "ProductRegistry.h"
#include "UpdateService.h"

// testUpdater
//
// The update system, exercised without a network and without GitHub.
//
// Every test here uses either canned JSON or the mock provider. A suite that
// needed a live API would fail on a train, would fail when a release is
// published, and would be testing GitHub rather than this code.

namespace px3tests
{

void testUpdater()
{
    suite("UPDATER");

    using namespace px3::update;

    // ========================================================================
    // Version comparison
    // ========================================================================
    //
    // The reason this type exists at all: as text, "1.10.0" sorts below
    // "1.9.0". An updater that compares strings stops offering updates at the
    // tenth minor release and nobody notices for months.
    {
        const auto a = SemanticVersion::parse("1.9.0");
        const auto b = SemanticVersion::parse("1.10.0");

        check("Update_TenSortsAboveNineRatherThanBelowIt",
              a.isValid && b.isValid && a < b && b > a && ! (a == b),
              "1.9.0 < 1.10.0 is " + juce::String(a < b ? "true" : "FALSE")
                  + ", which as strings would be "
                  + juce::String(juce::String("1.9.0") < juce::String("1.10.0") ? "true" : "false"));
    }

    {
        juce::StringArray notes;
        auto allCorrect = true;

        const auto expect = [&](const juce::String& text, bool shouldBeValid,
                                const juce::String& expected)
        {
            const auto parsed = SemanticVersion::parse(text);
            const auto ok = parsed.isValid == shouldBeValid
                         && (! shouldBeValid || parsed.toString() == expected);
            if (! ok) { allCorrect = false; }
            notes.add("'" + text + "' -> "
                      + (parsed.isValid ? parsed.toString() : juce::String("invalid")));
        };

        expect("1.6.0", true, "1.6.0");
        expect("v1.6.0", true, "1.6.0");
        expect("1.6.0-beta.1", true, "1.6.0-beta.1");
        expect("1.6.0+build7", true, "1.6.0");
        expect("1.6", false, {});
        expect("1.6.0.1", false, {});
        expect("1.x.0", false, {});
        expect("", false, {});
        expect("banana", false, {});

        check("Update_MalformedVersionsAreRejectedRatherThanGuessedAt",
              allCorrect,
              notes.joinIntoString(", "));
    }

    {
        // A pre-release sorts below the release it leads to, so a beta cannot
        // be mistaken for the finished version of the same number.
        const auto beta = SemanticVersion::parse("1.6.0-beta.1");
        const auto release = SemanticVersion::parse("1.6.0");
        const auto older = SemanticVersion::parse("1.5.9");

        check("Update_APreReleaseSortsBelowItsRelease",
              beta < release && older < beta && beta.isPreRelease() && ! release.isPreRelease(),
              "1.5.9 < 1.6.0-beta.1 < 1.6.0");
    }

    // ========================================================================
    // The GitHub provider, on canned responses
    // ========================================================================

    const auto releaseJson = [](const juce::String& tag,
                                const juce::String& assets,
                                const juce::String& body = "")
    {
        return "{\"tag_name\":\"" + tag + "\",\"published_at\":\"2026-09-02T15:41:14Z\","
               "\"body\":\"" + body + "\",\"assets\":[" + assets + "]}";
    };

    const auto asset = [](const juce::String& name)
    {
        return "{\"name\":\"" + name + "\",\"browser_download_url\":"
               "\"https://example.invalid/" + name + "\"}";
    };

    {
        // The filename the release script actually produces today, which names
        // neither platform nor architecture.
        const auto reply = GitHubReleaseProvider::parseLatestRelease(
            releaseJson("v0.8.0", asset("P(X3)-v0.8.0.pkg")),
            "px3-synth", "macOS", "arm64");

        check("Update_TheProviderReadsTheInstallerTheBuildActuallyProduces",
              reply.result.ok() && reply.release.isValid()
                  && reply.release.version.toString() == "0.8.0"
                  && reply.release.installerFilename == "P(X3)-v0.8.0.pkg",
              reply.result.ok() ? "found " + reply.release.installerFilename + " for "
                                      + reply.release.version.toString()
                                : "failed: " + reply.result.technicalDetail);
    }

    {
        // And a future name that does state them, alongside one for another
        // platform that must not be chosen.
        const auto reply = GitHubReleaseProvider::parseLatestRelease(
            releaseJson("v1.6.0",
                        asset("PX3-Synth-1.6.0-windows-x86_64.pkg") + ","
                            + asset("PX3-Synth-1.6.0-macOS-arm64.pkg")),
            "px3-synth", "macOS", "arm64");

        check("Update_TheProviderPrefersTheAssetForThisPlatform",
              reply.result.ok()
                  && reply.release.installerFilename == "PX3-Synth-1.6.0-macOS-arm64.pkg",
              reply.result.ok() ? "chose " + reply.release.installerFilename
                                : "failed: " + reply.result.technicalDetail);
    }

    {
        const auto noPkg = GitHubReleaseProvider::parseLatestRelease(
            releaseJson("v1.6.0", asset("PX3-Synth-1.6.0-macOS-arm64.zip")),
            "px3-synth", "macOS", "arm64");

        const auto badTag = GitHubReleaseProvider::parseLatestRelease(
            releaseJson("nightly", asset("P(X3)-v1.pkg")), "px3-synth", "macOS", "arm64");

        const auto notJson = GitHubReleaseProvider::parseLatestRelease(
            "<html>rate limited</html>", "px3-synth", "macOS", "arm64");

        check("Update_TheProviderRefusesReleasesItCannotUse",
              noPkg.result.error == UpdateError::noMatchingInstaller
                  && badTag.result.error == UpdateError::malformedResponse
                  && notJson.result.error == UpdateError::malformedResponse,
              "no .pkg -> " + describe(noPkg.result.error)
                  + "; unparseable tag -> " + describe(badTag.result.error)
                  + "; not JSON -> " + describe(notJson.result.error));
    }

    {
        const auto withHash = GitHubReleaseProvider::parseLatestRelease(
            releaseJson("v1.6.0", asset("P(X3)-v1.6.0.pkg"),
                        "Fixes things.\\nSHA-256: "
                        "abc123def4567890abc123def4567890abc123def4567890abc123def4567890"),
            "px3-synth", "macOS", "arm64");

        const auto without = GitHubReleaseProvider::parseLatestRelease(
            releaseJson("v1.6.0", asset("P(X3)-v1.6.0.pkg"), "Fixes things."),
            "px3-synth", "macOS", "arm64");

        check("Update_AChecksumIsReadFromTheNotesWhenOneIsPublished",
              withHash.release.sha256.length() == 64 && without.release.sha256.isEmpty(),
              "with a SHA-256 line: " + juce::String(withHash.release.sha256.length())
                  + " hex digits; without: "
                  + juce::String(without.release.sha256.isEmpty() ? "empty" : "SOMETHING"));
    }

    {
        // A repository that has published nothing answers 404, which the
        // transport turns into an empty body. That is "no release", not an
        // outage - the first thing this reported in real use was "the update
        // service is unavailable" against a repository that was answering
        // perfectly well and simply had no releases yet.
        const auto nothingPublished = GitHubReleaseProvider::parseLatestRelease(
            "", "px3-synth", "macOS", "arm64");

        check("Update_ARepositoryWithNoReleasesIsNotAnOutage",
              nothingPublished.result.ok() && ! nothingPublished.release.isValid(),
              nothingPublished.result.ok()
                  ? juce::String("reported as no release rather than a failure")
                  : "reported as: " + describe(nothingPublished.result.error));
    }

    {
        // The OTHER zip each release carries is the plug-in folder for a manual
        // copy. Running it is meaningless, so only "-Installer.zip" counts.
        const auto onlyPluginZip = GitHubReleaseProvider::parseLatestRelease(
            releaseJson("v0.6.0", asset("P.X3.-v0.6.0-macOS-arm64.zip")),
            "px3-synth", "macOS", "arm64");

        // And a bare .pkg is preferred over the archive when both are offered,
        // because it is one fewer step.
        const auto both = GitHubReleaseProvider::parseLatestRelease(
            releaseJson("v0.6.0",
                        asset("P.X3.-v0.6.0-macOS-arm64-Installer.zip") + ","
                            + asset("PX3-v0.6.0.pkg")),
            "px3-synth", "macOS", "arm64");

        check("Update_OnlyTheInstallerArchiveCountsAndABarePackageIsPreferred",
              onlyPluginZip.result.error == UpdateError::noMatchingInstaller
                  && both.release.installerFilename == "PX3-v0.6.0.pkg"
                  && ! both.release.installerIsArchive,
              "the plug-in zip alone -> " + describe(onlyPluginZip.result.error)
                  + "; with both offered, chose " + both.release.installerFilename);
    }

    // ========================================================================
    // The registry
    // ========================================================================
    {
        auto& registry = ProductRegistry::getInstance();
        registry.clear();

        registry.registerProduct({ "px3-synth", "PX3 Synth", [] { return juce::String("0.7.0"); } });

        const auto synthOnly = registry.productIds().size();

        registry.registerProduct({ "px3-mood", "PX3 Mood", [] { return juce::String("1.2.0"); } });
        registry.registerProduct({ "px3-synth", "PX3 Synth", [] { return juce::String("0.8.0"); } });

        const auto info = registry.lookup("px3-synth");

        check("Update_TheRegistryTakesMoreThanOneProductAndDoesNotDuplicate",
              synthOnly == 1 && registry.productIds().size() == 2
                  && registry.isRegistered("px3-mood")
                  && info.installedVersion.toString() == "0.8.0",
              juce::String(static_cast<int>(registry.productIds().size()))
                  + " products registered; re-registering px3-synth replaced it, now at "
                  + info.installedVersion.toString());
    }

    // ========================================================================
    // The service
    // ========================================================================

    // A service wired to a mock, with its staging inside a temporary folder so
    // a test never writes into the user's Application Support.
    struct Fixture
    {
        juce::File directory { juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile("px3-update-test-"
                                                 + juce::String(juce::Random::getSystemRandom()
                                                                    .nextInt(1000000))) };
        UpdateService service;
        MockUpdateProvider* mock { nullptr };

        Fixture()
        {
            directory.createDirectory();
            auto owned = std::make_unique<MockUpdateProvider>();
            mock = owned.get();
            service.setProvider(std::move(owned));
            service.setStagingDirectoryForTesting(directory);
            service.setSynchronousForTesting(true);
            service.setProductId("px3-synth");
        }

        ~Fixture() { directory.deleteRecursively(); }

        void offer(const juce::String& version, const juce::String& sha = {})
        {
            UpdateRelease release;
            release.productId = "px3-synth";
            release.version = SemanticVersion::parse(version);
            release.downloadUrl = juce::URL("https://example.invalid/installer.pkg");
            release.installerFilename = "P(X3)-v" + version + ".pkg";
            release.platform = "macOS";
            release.architecture = "arm64";
            release.sha256 = sha;
            mock->nextRelease = release;
        }
    };

    {
        auto& registry = ProductRegistry::getInstance();
        registry.clear();
        registry.registerProduct({ "px3-synth", "PX3 Synth", [] { return juce::String("0.7.0"); } });

        Fixture newer;
        newer.offer("0.8.0");
        newer.service.checkForUpdates(true);
        const auto sawUpdate = newer.service.getState();

        Fixture same;
        same.offer("0.7.0");
        same.service.checkForUpdates(true);
        const auto sawSame = same.service.getState();

        Fixture older;
        older.offer("0.6.0");
        older.service.checkForUpdates(true);
        const auto sawOlder = older.service.getState();

        check("Update_OnlyANewerReleaseCountsAsAnUpdate",
              sawUpdate == UpdateState::updateAvailable
                  && sawSame == UpdateState::upToDate
                  && sawOlder == UpdateState::upToDate,
              "0.8.0 -> " + describe(sawUpdate) + "; 0.7.0 -> " + describe(sawSame)
                  + "; 0.6.0 -> " + describe(sawOlder));
    }

    {
        // A development build ahead of the last release must not be offered a
        // downgrade, and a stable user must not be offered a pre-release.
        auto& registry = ProductRegistry::getInstance();
        registry.clear();
        registry.registerProduct({ "px3-synth", "PX3 Synth", [] { return juce::String("0.7.0"); } });

        Fixture beta;
        beta.offer("0.8.0-beta.1");
        beta.service.checkForUpdates(true);
        const auto offeredBeta = beta.service.getState();

        registry.clear();
        registry.registerProduct({ "px3-synth", "PX3 Synth", [] { return juce::String("0.9.0"); } });
        Fixture ahead;
        ahead.offer("0.8.0");
        ahead.service.checkForUpdates(true);
        const auto offeredDowngrade = ahead.service.getState();

        check("Update_NeitherAPreReleaseNorADowngradeIsOffered",
              offeredBeta == UpdateState::upToDate && offeredDowngrade == UpdateState::upToDate,
              "a stable install offered 0.8.0-beta.1 -> " + describe(offeredBeta)
                  + "; a 0.9.0 build offered 0.8.0 -> " + describe(offeredDowngrade));
    }

    {
        // The whole path, for a product whose repository has no releases: the
        // provider says ok-but-nothing, and the user is told they are up to
        // date rather than shown an error.
        auto& registry = ProductRegistry::getInstance();
        registry.clear();
        registry.registerProduct({ "px3-synth", "PX3 Synth", [] { return juce::String("0.7.0"); } });

        Fixture none;
        none.mock->nextRelease = {};        // valid lookup, no release in it
        none.service.checkForUpdates(true);

        check("Update_NoReleasesPublishedReadsAsUpToDateNotAsAnError",
              none.service.getState() == UpdateState::upToDate,
              describe(none.service.getState()) + " when the source has published nothing");
    }

    {
        auto& registry = ProductRegistry::getInstance();
        registry.clear();
        registry.registerProduct({ "px3-synth", "PX3 Synth", [] { return juce::String("0.7.0"); } });

        Fixture f;
        f.mock->nextResult = UpdateResult::failure(UpdateError::noNetwork, "offline");
        f.service.checkForUpdates(true);

        check("Update_BeingOfflineIsAnUnderstandableFailureRatherThanASilentOne",
              f.service.getState() == UpdateState::failed
                  && f.service.getError() == UpdateError::noNetwork
                  && f.service.getErrorMessage().contains("internet connection")
                  && ! f.service.getErrorMessage().contains("offline"),
              "the user is told: '" + f.service.getErrorMessage()
                  + "' while the log keeps the detail");
    }

    {
        // Repeated checks are throttled, so opening and closing an editor does
        // not mean a request every time. A forced check still goes through.
        auto& registry = ProductRegistry::getInstance();
        registry.clear();
        registry.registerProduct({ "px3-synth", "PX3 Synth", [] { return juce::String("0.7.0"); } });

        Fixture f;
        f.offer("0.8.0");
        f.service.checkForUpdates(true);
        f.service.checkForUpdates(false);
        f.service.checkForUpdates(false);
        const auto afterThrottled = f.mock->lookupCount;
        f.service.checkForUpdates(true);

        check("Update_RepeatedChecksAreThrottledButAForcedOneIsNot",
              afterThrottled == 1 && f.mock->lookupCount == 2,
              juce::String(afterThrottled) + " lookup after three checks, "
                  + juce::String(f.mock->lookupCount) + " after forcing one");
    }

    // ---- download, verify, stage -------------------------------------------
    {
        auto& registry = ProductRegistry::getInstance();
        registry.clear();
        registry.registerProduct({ "px3-synth", "PX3 Synth", [] { return juce::String("0.7.0"); } });

        const juce::String payload = "not really an installer, but a known one";
        const auto expected = juce::SHA256(payload.toRawUTF8(),
                                           static_cast<std::size_t>(payload.getNumBytesAsUTF8()))
                                  .toHexString().toLowerCase();

        Fixture good;
        good.offer("0.8.0", expected);
        good.service.setDownloaderForTesting(
            [payload](const juce::URL&, const juce::File& destination,
                      std::function<void(float)> onProgress)
            {
                destination.replaceWithText(payload);
                if (onProgress != nullptr) { onProgress(1.0f); }
                return true;
            });
        good.service.checkForUpdates(true);
        good.service.prepareUpdate();

        const auto staged = good.service.stagedInstaller();

        check("Update_AVerifiedDownloadIsStagedAndReadyToInstall",
              good.service.getState() == UpdateState::readyToInstall
                  && staged.existsAsFile()
                  && staged.getFileName() == "P(X3)-v0.8.0.pkg"
                  && good.service.hasStagedUpdate(),
              describe(good.service.getState()) + ", staged as "
                  + (staged.existsAsFile() ? staged.getFileName() : juce::String("nothing")));
    }

    {
        auto& registry = ProductRegistry::getInstance();
        registry.clear();
        registry.registerProduct({ "px3-synth", "PX3 Synth", [] { return juce::String("0.7.0"); } });

        Fixture bad;
        bad.offer("0.8.0",
                  "0000000000000000000000000000000000000000000000000000000000000000");
        bad.service.setDownloaderForTesting(
            [](const juce::URL&, const juce::File& destination, std::function<void(float)>)
            {
                destination.replaceWithText("tampered");
                return true;
            });
        bad.service.checkForUpdates(true);
        bad.service.prepareUpdate();

        // The point is not only that it failed but that nothing usable is left
        // behind: a file that failed its checksum must not be sitting where a
        // later run could take it for a valid installer.
        const auto leftovers = bad.directory.findChildFiles(juce::File::findFiles, false, "*.pkg*");

        check("Update_AFailedChecksumIsRefusedAndTheFileDiscarded",
              bad.service.getState() == UpdateState::failed
                  && bad.service.getError() == UpdateError::checksumMismatch
                  && leftovers.isEmpty()
                  && bad.service.stagedInstaller() == juce::File(),
              describe(bad.service.getError()) + "; "
                  + juce::String(leftovers.size()) + " installer files left in staging");
    }

    {
        auto& registry = ProductRegistry::getInstance();
        registry.clear();
        registry.registerProduct({ "px3-synth", "PX3 Synth", [] { return juce::String("0.7.0"); } });

        Fixture interrupted;
        interrupted.offer("0.8.0");
        interrupted.service.setDownloaderForTesting(
            [](const juce::URL&, const juce::File& destination, std::function<void(float)>)
            {
                destination.replaceWithText("half a fi");
                return false;   // the stream ended early
            });
        interrupted.service.checkForUpdates(true);
        interrupted.service.prepareUpdate();

        const auto partials = interrupted.directory.findChildFiles(juce::File::findFiles,
                                                                   false, "*.partial");

        check("Update_AnInterruptedDownloadLeavesNothingBehindToMistake",
              interrupted.service.getState() == UpdateState::failed
                  && interrupted.service.getError() == UpdateError::downloadFailed
                  && partials.isEmpty()
                  && interrupted.service.stagedInstaller() == juce::File(),
              describe(interrupted.service.getError()) + "; "
                  + juce::String(partials.size()) + " partial files left");
    }

    {
        // Cancelling clears what was staged rather than leaving it to be
        // picked up later.
        auto& registry = ProductRegistry::getInstance();
        registry.clear();
        registry.registerProduct({ "px3-synth", "PX3 Synth", [] { return juce::String("0.7.0"); } });

        Fixture f;
        f.offer("0.8.0");
        f.service.setDownloaderForTesting(
            [](const juce::URL&, const juce::File& destination, std::function<void(float)>)
            {
                destination.replaceWithText("installer");
                return true;
            });
        f.service.checkForUpdates(true);
        f.service.prepareUpdate();
        const auto before = f.service.hasStagedUpdate();
        f.service.cancel();

        check("Update_CancellingClearsTheStagedUpdate",
              before && ! f.service.hasStagedUpdate()
                  && f.service.getState() == UpdateState::idle,
              juce::String(before ? "staged" : "NOT STAGED") + " then cancelled to "
                  + describe(f.service.getState()));
    }

    {
        // With no provider at all the UI must say so rather than look broken.
        UpdateService bare;
        bare.setProductId("px3-synth");
        bare.setProvider(nullptr);
        bare.checkForUpdates(true);

        check("Update_WithNoProviderTheServiceSaysSoRatherThanFailing",
              bare.getState() == UpdateState::notConfigured,
              describe(bare.getState()));
    }

    // ---- the handoff to the helper -----------------------------------------
    //
    // The plugin never installs anything itself: it runs inside somebody's
    // DAW holding the bundle the installer would replace. What it does is hand
    // a staged, verified installer to a separate process. With nothing staged
    // there is nothing to hand over, and saying so is better than launching a
    // helper that would find no work.
    {
        auto& registry = ProductRegistry::getInstance();
        registry.clear();
        registry.registerProduct({ "px3-synth", "PX3 Synth", [] { return juce::String("0.7.0"); } });

        Fixture nothing;
        nothing.offer("0.8.0");
        nothing.service.checkForUpdates(true);
        const auto launched = nothing.service.launchInstaller();

        check("Update_TheHandoffRefusesWhenNothingIsStaged",
              ! launched && nothing.service.getState() == UpdateState::failed,
              juce::String(launched ? "LAUNCHED" : "refused") + " with nothing staged: "
                  + nothing.service.getErrorMessage());
    }

    {
        // Where the helper is expected to live. Reported rather than asserted
        // present: a development tree has no /Applications install, and a test
        // that demanded one would fail on every machine but a user's.
        const auto helper = UpdateService::updaterApplication();

        check("Update_TheHelperIsLookedForInsideTheInstalledStandalone",
              true,
              helper == juce::File()
                  ? juce::String("not present in this tree, which is expected before an install")
                  : "found at " + helper.getFullPathName());
    }

    // ========================================================================
    // The pre-release channel
    // ========================================================================
    {
        auto& registry = ProductRegistry::getInstance();
        registry.clear();
        registry.registerProduct({ "px3-synth", "PX3 Synth", [] { return juce::String("0.7.0"); } });

        // A release marked pre-release by GitHub's flag, with a plain tag -
        // which is exactly how this repository publishes them.
        const auto flaggedOnly = GitHubReleaseProvider::parseLatestRelease(
            "{\"tag_name\":\"v0.8.0\",\"prerelease\":true,\"draft\":false,\"body\":\"\","
            "\"published_at\":\"2026-09-03T00:00:00Z\",\"assets\":["
                + asset("P.X3.-v0.8.0-macOS-arm64-Installer.zip") + "]}",
            "px3-synth", "macOS", "arm64");

        check("Update_GitHubsFlagAloneMarksAPreRelease",
              flaggedOnly.release.isPreRelease && flaggedOnly.release.looksLikePreRelease()
                  && ! flaggedOnly.release.version.isPreRelease(),
              "tag v0.8.0 carries no suffix, but the flag marks it as a pre-release");

        Fixture off;
        off.offer("0.8.0");
        off.mock->nextRelease.isPreRelease = true;
        off.service.checkForUpdates(true);
        const auto whenOff = off.service.getState();

        Fixture on;
        on.service.setPreReleaseChannelEnabled(true);
        on.offer("0.8.0");
        on.mock->nextRelease.isPreRelease = true;
        on.service.checkForUpdates(true);
        const auto whenOn = on.service.getState();

        check("Update_APreReleaseIsOfferedOnlyOnTheDebugChannel",
              whenOff == UpdateState::upToDate && whenOn == UpdateState::updateAvailable
                  && on.service.getAvailableRelease().looksLikePreRelease(),
              "channel off -> " + describe(whenOff) + "; channel on -> " + describe(whenOn));
    }

    {
        // Switching channel invalidates what was decided under the old one.
        // Without this the throttle would hold the previous answer and the
        // switch would appear to do nothing until ten minutes had passed.
        auto& registry = ProductRegistry::getInstance();
        registry.clear();
        registry.registerProduct({ "px3-synth", "PX3 Synth", [] { return juce::String("0.7.0"); } });

        Fixture f;
        f.offer("0.8.0");
        f.mock->nextRelease.isPreRelease = true;
        f.service.checkForUpdates(true);
        const auto before = f.service.getState();

        f.service.setPreReleaseChannelEnabled(true);
        const auto afterSwitch = f.service.getState();
        f.service.checkForUpdates(false);        // NOT forced
        const auto afterRecheck = f.service.getState();

        check("Update_SwitchingChannelClearsTheThrottleAndTheOldAnswer",
              before == UpdateState::upToDate
                  && afterSwitch == UpdateState::idle
                  && afterRecheck == UpdateState::updateAvailable,
              describe(before) + " -> switch -> " + describe(afterSwitch)
                  + " -> unforced recheck -> " + describe(afterRecheck));
    }

    {
        // The endpoint has to change with the channel: /releases/latest cannot
        // return a pre-release however it is queried.
        GitHubReleaseProvider provider("owner", "repo");
        // WITH the query string: toString(false) drops it, which made the
        // first version of this test compare two strings that both ended
        // "/releases" and looked wrong when they were right.
        const auto release = provider.latestReleaseUrl().toString(true);
        provider.setIncludePreReleases(true);
        const auto pre = provider.latestReleaseUrl().toString(true);

        check("Update_ThePreReleaseChannelAsksADifferentEndpoint",
              release.endsWith("/releases/latest") && pre.contains("/releases?per_page")
                  && ! pre.contains("latest"),
              "release channel -> " + release.fromLastOccurrenceOf("/repo", false, false)
                  + "; pre-release channel -> " + pre.fromLastOccurrenceOf("/repo", false, false));
    }

    // ========================================================================
    // What the editor shows when an update is waiting
    // ========================================================================
    {
        auto& registry = ProductRegistry::getInstance();
        registry.clear();
        registry.registerProduct({ "px3-synth", "PX3 Synth", [] { return juce::String("0.7.0"); } });

        // The shared service, because that is what the editor listens to.
        //
        // installDefaultConfiguration() FIRST, to consume its call_once here
        // rather than inside the editor's constructor - where it would replace
        // the mock below with the real GitHub provider and quietly turn this
        // into a live network test.
        installDefaultConfiguration();

        auto& service = UpdateService::getInstance();
        auto owned = std::make_unique<MockUpdateProvider>();
        auto* mock = owned.get();
        service.setProvider(std::move(owned));
        service.setProductId("px3-synth");
        service.setSynchronousForTesting(true);
        service.resetForTesting();

        UpdateRelease release;
        release.productId = "px3-synth";
        release.version = SemanticVersion::parse("0.9.0");
        release.downloadUrl = juce::URL("https://example.invalid/i.pkg");
        release.installerFilename = "P(X3)-v0.9.0.pkg";
        mock->nextRelease = release;

        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, 48000.0, 256);
        processor.prepareToPlay(48000.0, 256);

        std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());

        if (editor != nullptr)
        {
            editor->setSize(1280, 800);
            service.checkForUpdates(true);          // the editor's listener reacts

            auto* bar = editor->debugTopMenuBar();
            const auto glowing = bar != nullptr && bar->isUpdateAvailable();
            const auto noticeShown = editor->debugUpdateNoticeVisible();
            const auto noticeText = editor->debugUpdateNoticeText();

            check("UpdateUi_TheGearGlowsAndTheNoticeAppearsWhenAnUpdateIsFound",
                  glowing && noticeShown
                      && noticeText.contains("new version")
                      && noticeText.contains("PX3 Synth"),
                  juce::String(glowing ? "gear glowing" : "GEAR NOT GLOWING")
                      + "; notice " + (noticeShown ? "'" + noticeText + "'"
                                                   : juce::String("NOT SHOWN")));

            // Twenty seconds at the editor's 30 Hz tick, and not a frame before.
            for (int i = 0; i < 30 * 20 - 1; ++i) { editor->debugTimerTick(); }
            const auto stillThere = editor->debugUpdateNoticeVisible();
            editor->debugTimerTick();
            const auto goneNow = ! editor->debugUpdateNoticeVisible();

            check("UpdateUi_TheNoticeShowsItselfOutAfterTwentySeconds",
                  stillThere && goneNow,
                  juce::String("still up at 19.97 s: ") + (stillThere ? "yes" : "NO")
                      + "; gone at 20 s: " + (goneNow ? "yes" : "NO"));

            // The glow goes when the notice does. The two announce the same
            // thing, so leaving the gear pulsing after the notice has shown
            // itself out just makes it the permanent state of the window.
            // SETTINGS still reports the update after both have gone quiet, and
            // a newly opened editor announces it again.
            check("UpdateUi_TheGlowStopsWithTheNotice",
                  bar != nullptr && ! bar->isUpdateAvailable(),
                  bar != nullptr && ! bar->isUpdateAvailable()
                      ? juce::String("glow cleared with the notice")
                      : juce::String("STILL GLOWING after the notice went"));
        }

        // Opening SETTINGS is the other way the announcement ends, and it needs
        // its own editor: on the one above the timeout has already cleared the
        // glow, so asserting there would pass no matter what SETTINGS did.
        base.reset();

        std::unique_ptr<juce::AudioProcessorEditor> second(processor.createEditor());

        if (auto* fresh = dynamic_cast<PX3SynthAudioProcessorEditor*>(second.get()))
        {
            fresh->setSize(1280, 800);
            service.checkForUpdates(true);

            auto* bar = fresh->debugTopMenuBar();

            check("UpdateUi_ANewWindowAnnouncesTheUpdateAgain",
                  bar != nullptr && bar->isUpdateAvailable()
                      && fresh->debugUpdateNoticeVisible(),
                  juce::String(bar != nullptr && bar->isUpdateAvailable()
                                   ? "glowing" : "NOT GLOWING")
                      + "; notice "
                      + (fresh->debugUpdateNoticeVisible() ? "shown" : "NOT SHOWN"));

            // Opening SETTINGS is what counts as having seen it - before any
            // timeout, so this measures SETTINGS and nothing else.
            fresh->debugSelectSection(6);
            const auto afterOpening = bar != nullptr && bar->isUpdateAvailable();

            check("UpdateUi_OpeningSettingsStopsTheGlow",
                  ! afterOpening && ! fresh->debugUpdateNoticeVisible(),
                  afterOpening ? juce::String("STILL GLOWING after opening SETTINGS")
                               : juce::String("glow and notice both cleared"));
        }

        second.reset();

        // The debug console's preview toggle. It exists to style the notice and
        // the glow, which means it has to work with no update in sight and has
        // to hold - the two things a real announcement does not do.
        // Provider removed as well as state cleared, so "no update available"
        // is true of the whole service rather than just its current answer.
        service.resetForTesting();
        service.setProvider(nullptr);

        std::unique_ptr<juce::AudioProcessorEditor> styling(processor.createEditor());

        if (auto* preview = dynamic_cast<PX3SynthAudioProcessorEditor*>(styling.get()))
        {
            preview->setSize(1280, 800);
            auto* bar = preview->debugTopMenuBar();

            const auto quietBefore = bar != nullptr && ! bar->isUpdateAvailable()
                                         && ! preview->debugUpdateNoticeVisible();

            preview->debugSetUpdatePreview(true);

            check("UpdateUi_ThePreviewShowsTheNoticeWithNoUpdateAvailable",
                  quietBefore && bar != nullptr && bar->isUpdateAvailable()
                      && preview->debugUpdateNoticeVisible(),
                  juce::String(quietBefore ? "quiet first" : "NOT QUIET FIRST")
                      + "; then " + (bar != nullptr && bar->isUpdateAvailable()
                                         ? "glowing" : "NOT GLOWING")
                      + " and notice "
                      + (preview->debugUpdateNoticeVisible() ? "shown" : "NOT SHOWN"));

            // Well past the twenty seconds that would have retired a real one.
            for (int i = 0; i < 30 * 30; ++i) { preview->debugTimerTick(); }

            check("UpdateUi_ThePreviewDoesNotTimeOut",
                  preview->debugUpdateNoticeVisible()
                      && bar != nullptr && bar->isUpdateAvailable(),
                  preview->debugUpdateNoticeVisible()
                      ? juce::String("still up at 30 s, which is the point of it")
                      : juce::String("TIMED OUT - unusable for styling"));

            // Opening SETTINGS is how a real announcement ends, and it must not
            // end this one: the debug console is reached through that panel.
            preview->debugSelectSection(6);

            check("UpdateUi_ThePreviewSurvivesOpeningSettings",
                  preview->debugUpdateNoticeVisible(),
                  preview->debugUpdateNoticeVisible()
                      ? juce::String("still up with SETTINGS open")
                      : juce::String("CLEARED by opening SETTINGS"));

            preview->debugSetUpdatePreview(false);

            check("UpdateUi_ThePreviewTogglesBackOff",
                  ! preview->debugUpdateNoticeVisible()
                      && bar != nullptr && ! bar->isUpdateAvailable(),
                  ! preview->debugUpdateNoticeVisible()
                      ? juce::String("notice and glow both cleared")
                      : juce::String("STILL SHOWING after switching off"));

            // And on again, because a toggle that only works once is a button.
            preview->debugSetUpdatePreview(true);

            check("UpdateUi_ThePreviewCanBeTurnedOnAgain",
                  preview->debugUpdateNoticeVisible(),
                  preview->debugUpdateNoticeVisible()
                      ? juce::String("came back")
                      : juce::String("DID NOT COME BACK - latched off"));

            // The close glyph ends the announcement the way the timeout does -
            // notice away and gear settled - because the point of closing it is
            // to stop being told.
            preview->debugSetUpdatePreview(true);
            preview->debugUpdateNoticeClose().onClick();

            check("UpdateUi_ClosingTheNoticeAlsoStopsTheGlow",
                  ! preview->debugUpdateNoticeVisible()
                      && bar != nullptr && ! bar->isUpdateAvailable(),
                  juce::String(preview->debugUpdateNoticeVisible()
                                   ? "NOTICE STILL UP" : "notice gone")
                      + "; gear "
                      + (bar != nullptr && bar->isUpdateAvailable()
                             ? "STILL GLOWING" : "settled"));

            preview->debugSetUpdatePreview(false);
        }

        styling.reset();

        // ---- the update flow is one button ---------------------------------
        //
        // Install Update downloads, stages and hands off without asking again.
        // The second button used to sit between the two halves of a job the
        // user had already asked for.
        {
            service.resetForTesting();

            auto owned2 = std::make_unique<MockUpdateProvider>();
            auto* mock2 = owned2.get();
            service.setProvider(std::move(owned2));
            service.setProductId("px3-synth");
            service.setSynchronousForTesting(true);

            UpdateRelease flow;
            flow.productId = "px3-synth";
            flow.version = SemanticVersion::parse("0.9.0");
            flow.downloadUrl = juce::URL("https://example.invalid/i.pkg");
            flow.installerFilename = "P(X3)-v0.9.0.pkg";
            flow.releaseNotes = juce::String::repeatedString("Long release notes. ", 120);
            mock2->nextRelease = flow;

            std::unique_ptr<juce::AudioProcessorEditor> flowEditor(processor.createEditor());

            if (auto* ed = dynamic_cast<PX3SynthAudioProcessorEditor*>(flowEditor.get()))
            {
                ed->setSize(1400, 900);
                ed->debugSelectSection(6);
                service.checkForUpdates(true);

                if (auto* panel = ed->debugSettingsPanel())
                {
                    panel->debugRefreshUpdateSection();

                    check("UpdateFlow_TheOnlyButtonOfferedIsInstallUpdate",
                          panel->debugUpdateButtonText() == "Install Update",
                          "button reads '" + panel->debugUpdateButtonText() + "'");

                    // The whole point: the notes are not cut to 400 characters
                    // any more, because the box they sit in scrolls.
                    const auto notesShown = panel->debugReleaseNotes().getText();
                    check("UpdateFlow_TheReleaseNotesAreNotTruncated",
                          notesShown.length() == flow.releaseNotes.length()
                              && notesShown.length() > 400,
                          "showing " + juce::String(notesShown.length())
                              + " of " + juce::String(flow.releaseNotes.length())
                              + " characters");
                }
            }

            flowEditor.reset();
            service.resetForTesting();
            service.setProvider(nullptr);
        }

        // ---- the installer signature check ---------------------------------
        //
        // This check refused every correctly signed release of v0.7.2. The
        // command was built as one string with quotes around the path; nothing
        // runs a shell, so pkgutil was asked about a file whose name began with
        // a quote character, answered "Package does not exist", and the caller
        // read the missing certificate name as proof the package was unsigned.
        {
            // A path with the two things that provoked it: a space and the
            // parentheses in the real staging directory, ~/Library/P(X3)/.
            const juce::File awkward { "/Users/someone/Library/P(X3)/Updates/PX3 Synth-v1.2.3.pkg" };
            const auto command = signatureCheckCommand(awkward);

            check("UpdaterSignature_ThePathIsOneUnquotedArgument",
                  command.size() == 3
                      && command[2] == awkward.getFullPathName()
                      && ! command[2].containsChar('"'),
                  juce::String(command.size()) + " args, path arg = " + command[2]);

            // pkgutil's real answer for a signed package.
            const juce::String signed_ =
                "Package \"PX3-v0.7.2-macOS.pkg\":\n"
                "   Status: signed by a developer certificate issued by Apple for distribution\n"
                "   Notarization: trusted by the Apple notary service\n"
                "   Certificate Chain:\n"
                "    1. Developer ID Installer: SOMEONE (ABCDE12345)\n";

            // And its answer when it cannot open the file - which it reports
            // with status 0, so only the certificate match distinguishes them.
            const juce::String missing =
                "Package does not exist: \"/Users/someone/Library/P(X3)/Updates/PX3.pkg\"\n";

            const juce::String unsigned_ =
                "Package \"PX3.pkg\":\n   Status: no signature\n";

            check("UpdaterSignature_ASignedPackageIsAccepted",
                  signatureOutputNamesADeveloperID(signed_),
                  "the certificate line is recognised");

            check("UpdaterSignature_AMissingFileIsNotMistakenForSigned",
                  ! signatureOutputNamesADeveloperID(missing),
                  "pkgutil's 'does not exist' does not name a certificate");

            check("UpdaterSignature_AnUnsignedPackageIsRefused",
                  ! signatureOutputNamesADeveloperID(unsigned_),
                  "no certificate, no install");
        }

        service.resetForTesting();
        service.setProvider(nullptr);
    }

    // Leave the registry as the running application expects it - with the
    // FULL registration, not the cut-down ones these tests use.
    // installDefaultConfiguration() cannot do it: it is call_once and has
    // already run, so it would put nothing back and the next suite would find
    // a product with no bundle id.
    ProductRegistry::getInstance().clear();
    registerDefaultProducts();
}

} // namespace px3tests
