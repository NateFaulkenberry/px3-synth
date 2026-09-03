#include "UpdateService.h"

#include "PX3Version.h"
#include "GitHubReleaseProvider.h"
#include "GlobalSettings.h"

#include <mutex>

#include <unistd.h>

namespace px3::update
{
namespace
{
constexpr const char* kMetadataFilename = "staged-update.json";
constexpr const char* kInstallerSubdirectory = "Updates";

// What this build is, for asset matching. Both are compile-time facts about
// the binary that is asking, not settings.
juce::String currentPlatform() { return "macOS"; }

juce::String currentArchitecture()
{
   #if JUCE_ARM || defined(__aarch64__)
    return "arm64";
   #else
    return "x86_64";
   #endif
}
} // namespace

UpdateService& UpdateService::getInstance()
{
    static UpdateService instance;
    return instance;
}

UpdateService::UpdateService()
{
    productId = ProductRegistry::kSynthProductId;

    downloadFile = [](const juce::URL& url, const juce::File& destination,
                      std::function<void(float)> onProgress) -> bool
    {
        destination.deleteFile();

        int status = 0;
        auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                           .withConnectionTimeoutMs(30000)
                           .withStatusCode(&status)
                           .withExtraHeaders("User-Agent: PX3-Updater\r\n");

        auto stream = url.createInputStream(options);
        if (stream == nullptr || status >= 400) { return false; }

        juce::FileOutputStream out(destination);
        if (out.failedToOpen()) { return false; }

        const auto total = stream->getTotalLength();
        juce::HeapBlock<char> block(65536);
        juce::int64 written = 0;

        for (;;)
        {
            const auto read = stream->read(block.getData(), 65536);
            if (read <= 0) { break; }
            if (! out.write(block.getData(), static_cast<std::size_t>(read))) { return false; }

            written += read;
            if (onProgress != nullptr && total > 0)
            {
                onProgress(juce::jlimit(0.0f, 1.0f,
                                        static_cast<float>(written) / static_cast<float>(total)));
            }
        }

        out.flush();

        // A stream that ended early leaves a file that looks like an
        // installer. Length is the cheapest way to notice; the checksum is the
        // real one, and runs next.
        return written > 0 && (total <= 0 || written == total);
    };
}

UpdateService::~UpdateService()
{
    if (provider != nullptr) { provider->cancel(); }
    if (worker != nullptr) { worker->stopThread(5000); }
}

void UpdateService::setProvider(std::unique_ptr<UpdateProvider> newProvider)
{
    if (provider != nullptr) { provider->cancel(); }
    provider = std::move(newProvider);
    setState(provider != nullptr ? UpdateState::idle : UpdateState::notConfigured);
}

void UpdateService::setProductId(juce::String newProductId)
{
    productId = std::move(newProductId);
}

void UpdateService::setPreReleaseChannelEnabled(bool shouldBeEnabled)
{
    if (preReleaseChannel == shouldBeEnabled) { return; }

    preReleaseChannel = shouldBeEnabled;

    if (auto* gitHub = dynamic_cast<GitHubReleaseProvider*>(provider.get()))
    {
        gitHub->setIncludePreReleases(shouldBeEnabled);
    }

    // What "up to date" means has changed, so anything already decided is
    // stale. Re-checking is the caller's business, but the throttle must not
    // stop them: a channel switch is exactly when a fresh answer is wanted.
    lastCheck = juce::Time();
    available = {};
    setState(UpdateState::idle);
}

ProductInfo UpdateService::getProduct() const
{
    return ProductRegistry::getInstance().lookup(productId);
}

void UpdateService::setState(UpdateState newState, UpdateError newError, const juce::String& detail)
{
    state = newState;
    error = newError;

    // The technical half goes to the log; describe(error) is what the UI says.
    if (newError != UpdateError::none)
    {
        juce::Logger::writeToLog("PX3 update: " + describe(newState)
                                 + " - " + describe(newError)
                                 + (detail.isNotEmpty() ? " [" + detail + "]" : juce::String()));
    }
    else
    {
        juce::Logger::writeToLog("PX3 update: " + describe(newState));
    }

    // Listeners are UI. Broadcasting from whichever thread got here would put
    // a repaint on it, so this always arrives on the message thread - and once
    // there it is SYNCHRONOUS, for the same reason GlobalSettings is: the gear
    // should light up when the check finishes, not on the next turn of the
    // message loop. The asynchronous form also never arrives at all in a test,
    // which has no loop to deliver it.
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr
        && ! juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        juce::MessageManager::callAsync([this] { sendSynchronousChangeMessage(); });
    }
    else
    {
        sendSynchronousChangeMessage();
    }
}

void UpdateService::checkForUpdates(bool force)
{
    if (provider == nullptr)
    {
        setState(UpdateState::notConfigured);
        return;
    }

    const auto product = getProduct();
    if (! product.isValid())
    {
        setState(UpdateState::notConfigured, UpdateError::none,
                 "no product registered as " + productId);
        return;
    }

    if (state == UpdateState::checking || state == UpdateState::downloading
        || state == UpdateState::verifying || state == UpdateState::installing)
    {
        return;   // one at a time; not an error, just nothing to do
    }

    if (! force && lastCheck != juce::Time()
        && (juce::Time::getCurrentTime() - lastCheck).inSeconds() < kMinimumCheckIntervalSeconds)
    {
        return;
    }

    lastCheck = juce::Time::getCurrentTime();
    setState(UpdateState::checking);

    provider->fetchLatestRelease(productId, currentPlatform(), currentArchitecture(),
                                 [this](UpdateProvider::LookupResult reply)
                                 {
                                     onLookupComplete(std::move(reply));
                                 });
}

void UpdateService::onLookupComplete(UpdateProvider::LookupResult reply)
{
    if (! reply.result.ok())
    {
        setState(UpdateState::failed, reply.result.error, reply.result.technicalDetail);
        return;
    }

    if (! reply.release.isValid())
    {
        setState(UpdateState::upToDate);
        return;
    }

    const auto product = getProduct();
    if (! product.installedVersion.isValid)
    {
        setState(UpdateState::failed, UpdateError::malformedResponse,
                 "the installed version could not be read");
        return;
    }

    // A pre-release is never offered to somebody running a release, unless
    // they have asked for that channel. Without this an updater offers
    // 1.7.0-beta1 to everyone the moment it is tagged, and there is no way
    // back down.
    //
    // On the channel, a pre-release is an ordinary update - found, downloaded
    // and installed the same way - and is labelled as one wherever it appears.
    if (! preReleaseChannel
        && reply.release.looksLikePreRelease() && ! product.installedVersion.isPreRelease())
    {
        setState(UpdateState::upToDate);
        return;
    }

    available = reply.release;

    if (reply.release.version > product.installedVersion)
    {
        setState(UpdateState::updateAvailable);
    }
    else
    {
        // Same, or the installed build is NEWER - a development build ahead of
        // the last release. Both are "nothing to do", and offering a downgrade
        // to a developer would be an update loop.
        setState(UpdateState::upToDate);
    }
}

juce::File UpdateService::stagingDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("P(X3)")
        .getChildFile(kInstallerSubdirectory);
}

juce::File UpdateService::stagingRoot() const
{
    return stagingOverride != juce::File() ? stagingOverride : stagingDirectory();
}

juce::String UpdateService::sha256Of(const juce::File& file)
{
    if (! file.existsAsFile()) { return {}; }

    juce::FileInputStream stream(file);
    if (stream.failedToOpen()) { return {}; }

    return juce::SHA256(stream).toHexString().toLowerCase();
}

void UpdateService::writeStagedMetadata(const UpdateRelease& release,
                                        const juce::File& installer) const
{
    auto* object = new juce::DynamicObject();
    object->setProperty("productId", release.productId);
    object->setProperty("version", release.version.toString());
    // Relative to the staging directory, because an installer lifted out of an
    // archive sits in a subfolder rather than beside the metadata.
    object->setProperty("installer", installer.getRelativePathFrom(stagingRoot()));
    object->setProperty("sha256", release.sha256);
    object->setProperty("stagedAt", juce::Time::getCurrentTime().toISO8601(true));

    stagingRoot().getChildFile(kMetadataFilename)
        .replaceWithText(juce::JSON::toString(juce::var(object)));
}

juce::var UpdateService::readStagedMetadata() const
{
    const auto file = stagingRoot().getChildFile(kMetadataFilename);
    if (! file.existsAsFile()) { return {}; }

    juce::var parsed;
    if (juce::JSON::parse(file.loadFileAsString(), parsed).failed()) { return {}; }
    return parsed;
}

juce::File UpdateService::stagedInstaller() const
{
    const auto metadata = readStagedMetadata();
    if (! metadata.isObject()) { return {}; }

    const auto relative = metadata.getProperty("installer", juce::var()).toString();
    if (relative.isEmpty()) { return {}; }

    const auto file = stagingRoot().getChildFile(relative);
    return file.existsAsFile() ? file : juce::File();
}

bool UpdateService::hasStagedUpdate() const
{
    const auto installer = stagedInstaller();
    if (installer == juce::File()) { return false; }

    const auto metadata = readStagedMetadata();
    const auto staged = SemanticVersion::parse(
        metadata.getProperty("version", juce::var()).toString());

    return staged.isValid && available.version.isValid && staged == available.version;
}

void UpdateService::prepareUpdate()
{
    if (state != UpdateState::updateAvailable && state != UpdateState::failed)
    {
        return;
    }

    if (! available.isValid())
    {
        setState(UpdateState::failed, UpdateError::noMatchingInstaller,
                 "no release to prepare");
        return;
    }

    if (busy.exchange(true))
    {
        setState(UpdateState::failed, UpdateError::alreadyInProgress);
        return;
    }

    if (synchronous)
    {
        runPreparation();
        busy.store(false);
        return;
    }

    // A plain thread rather than the pool: this is one long job, and the pool
    // is not a thing this project already has.
    struct PreparationThread final : public juce::Thread
    {
        explicit PreparationThread(UpdateService& ownerIn)
            : juce::Thread("PX3 update download"), owner(ownerIn) {}
        void run() override { owner.runPreparation(); owner.busy.store(false); }
        UpdateService& owner;
    };

    if (worker != nullptr) { worker->stopThread(5000); }
    worker = std::make_unique<PreparationThread>(*this);
    worker->startThread();
}

void UpdateService::runPreparation()
{
    const auto directory = stagingRoot();

    if (! directory.createDirectory())
    {
        setState(UpdateState::failed, UpdateError::stagingFailed,
                 "could not create " + directory.getFullPathName());
        return;
    }

    // Downloaded beside the final name rather than to it, so an interrupted
    // download can never be mistaken for a staged installer: only a verified
    // file is ever given the name the metadata points at.
    const auto target = directory.getChildFile(available.installerFilename);
    const auto partial = directory.getChildFile(available.installerFilename + ".partial");

    progress.store(0.0f);
    setState(UpdateState::downloading);

    const auto downloaded = downloadFile(available.downloadUrl, partial,
                                         [this](float fraction)
                                         {
                                             progress.store(fraction);
                                         });

    if (! downloaded)
    {
        partial.deleteFile();
        setState(UpdateState::failed, UpdateError::downloadFailed,
                 "download of " + available.installerFilename + " did not complete");
        return;
    }

    setState(UpdateState::verifying);

    if (available.sha256.isNotEmpty())
    {
        const auto actual = sha256Of(partial);
        if (actual != available.sha256)
        {
            // Quarantined by deletion. A file that failed its checksum must
            // not be left where a later run could pick it up as valid.
            partial.deleteFile();
            setState(UpdateState::failed, UpdateError::checksumMismatch,
                     "expected " + available.sha256 + ", got " + actual);
            return;
        }
    }

    target.deleteFile();
    if (! partial.moveFileTo(target))
    {
        partial.deleteFile();
        setState(UpdateState::failed, UpdateError::stagingFailed, "could not name the installer");
        return;
    }

    // WHAT GETS STAGED IS ALWAYS A .pkg.
    //
    // The release script publishes a distribution archive - a zip holding the
    // .pkg and the uninstaller - rather than a bare package, so for that shape
    // the package has to be lifted out before there is anything to run. Only
    // the .pkg entry is extracted: the uninstaller beside it is not ours to
    // install, and a zip round-trip would not preserve its signature anyway.
    //
    // Extracting one entry copies its bytes exactly, so the package's own
    // Developer ID signature survives - which matters, because that signature
    // is what the helper checks before running it.
    auto installer = target;

    if (available.installerIsArchive)
    {
        juce::ZipFile archive(target);
        auto packageIndex = -1;

        for (int i = 0; i < archive.getNumEntries(); ++i)
        {
            const auto* entry = archive.getEntry(i);
            if (entry != nullptr && entry->filename.endsWithIgnoreCase(".pkg"))
            {
                packageIndex = i;
                break;
            }
        }

        if (packageIndex < 0)
        {
            target.deleteFile();
            setState(UpdateState::failed, UpdateError::noMatchingInstaller,
                     available.installerFilename + " contains no .pkg");
            return;
        }

        const auto extractedInto = directory.getChildFile("extracted");
        extractedInto.deleteRecursively();
        extractedInto.createDirectory();

        const auto extracted = archive.uncompressEntry(packageIndex, extractedInto);
        if (! extracted.wasOk())
        {
            extractedInto.deleteRecursively();
            target.deleteFile();
            setState(UpdateState::failed, UpdateError::stagingFailed,
                     "could not extract the installer: " + extracted.getErrorMessage());
            return;
        }

        installer = extractedInto.getChildFile(archive.getEntry(packageIndex)->filename);

        if (! installer.existsAsFile())
        {
            extractedInto.deleteRecursively();
            target.deleteFile();
            setState(UpdateState::failed, UpdateError::stagingFailed,
                     "the extracted installer was not where the archive said");
            return;
        }

        // The archive has served its purpose and is 30 MB.
        target.deleteFile();
    }

    writeStagedMetadata(available, installer);
    progress.store(1.0f);
    setState(UpdateState::readyToInstall);
}

void UpdateService::cancel()
{
    if (provider != nullptr) { provider->cancel(); }
    if (worker != nullptr) { worker->stopThread(3000); }

    const auto directory = stagingRoot();
    directory.getChildFile(kMetadataFilename).deleteFile();
    for (const auto& file : directory.findChildFiles(juce::File::findFiles, false, "*.partial"))
    {
        file.deleteFile();
    }
    directory.getChildFile("extracted").deleteRecursively();

    progress.store(0.0f);
    busy.store(false);
    setState(UpdateState::idle, UpdateError::none);
}

juce::File UpdateService::updaterApplication()
{
    // Shipped inside the standalone, by the same installer, signed with the
    // same Developer ID and notarised in the same submission. That is what
    // makes it trustworthy: it is not something the plug-in fetches and runs.
    const auto inBundle = juce::File("/Applications/PX3 Synth.app/Contents/MacOS/PX3 Updater");
    if (inBundle.existsAsFile()) { return inBundle; }

    // A development build: beside whatever binary is running.
    const auto beside = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                            .getParentDirectory()
                            .getChildFile("PX3 Updater");
    return beside.existsAsFile() ? beside : juce::File();
}

bool UpdateService::launchInstaller()
{
    const auto installer = stagedInstaller();
    if (installer == juce::File())
    {
        // Not a disk-space problem, which is what stagingFailed says. This is
        // only reachable if something asks to install with nothing prepared.
        setState(UpdateState::failed, UpdateError::installerLaunchFailed,
                 "nothing staged to install");
        return false;
    }

    const auto shipped = updaterApplication();
    if (shipped == juce::File())
    {
        setState(UpdateState::failed, UpdateError::installerLaunchFailed,
                 "the PX3 Updater helper was not found");
        return false;
    }

    // RUN A COPY, NOT THE SHIPPED ONE.
    //
    // The helper lives inside PX3 Synth.app, and the installer it is about to
    // run replaces PX3 Synth.app. A process cannot safely have the bundle
    // containing it replaced underneath it - which is the lifecycle hazard
    // that makes naive self-updaters corrupt themselves halfway through.
    //
    // So it is copied out to the staging directory first and the COPY is what
    // runs. The installer is then free to replace everything in /Applications
    // while this keeps working, and the copy is discarded afterwards.
    const auto runner = stagingRoot().getChildFile("PX3 Updater");
    runner.deleteFile();

    if (! shipped.copyFileTo(runner))
    {
        setState(UpdateState::failed, UpdateError::installerLaunchFailed,
                 "could not stage the updater helper");
        return false;
    }

    runner.setExecutePermission(true);

    // The handoff: which file to install, and which process to wait for.
    // Arguments rather than a socket - this is a one-shot request, and the
    // simplest robust mechanism is the right one for a first version.
    juce::StringArray command;
    command.add(runner.getFullPathName());
    command.add("--install");
    command.add(installer.getFullPathName());
    command.add("--wait-for-pid");
    command.add(juce::String(static_cast<int>(getpid())));

    juce::ChildProcess launcher;
    if (! launcher.start(command))
    {
        setState(UpdateState::failed, UpdateError::installerLaunchFailed,
                 "could not start " + runner.getFullPathName());
        return false;
    }

    juce::Logger::writeToLog("PX3 update: handed " + installer.getFileName()
                             + " to the updater, waiting on pid "
                             + juce::String(static_cast<int>(getpid())));
    setState(UpdateState::installing);
    return true;
}

void UpdateService::resetForTesting()
{
    if (worker != nullptr) { worker->stopThread(3000); }
    worker.reset();
    state = UpdateState::idle;
    error = UpdateError::none;
    available = {};
    progress.store(0.0f);
    lastCheck = juce::Time();
    busy.store(false);
}

void installDefaultConfiguration()
{
    static std::once_flag once;
    std::call_once(once, []
    {
        ProductRegistry::getInstance().registerProduct(
            { ProductRegistry::kSynthProductId,
              "PX3 Synth",
              [] { return px3::version::string(); } });

        auto& service = UpdateService::getInstance();
        service.setProductId(ProductRegistry::kSynthProductId);
        service.setProvider(std::make_unique<GitHubReleaseProvider>("NateFaulkenberry",
                                                                    "px3-synth"));

        // --debug true, from the standalone's command line. A plug-in has no
        // command line, so the flag is recorded in the global preference and
        // every instance in every host then sees it - which is also what makes
        // it possible to turn off again from one place.
        if (auto* app = juce::JUCEApplicationBase::getInstance())
        {
            const auto arguments = app->getCommandLineParameterArray();

            for (int i = 0; i < arguments.size(); ++i)
            {
                if (! arguments[i].startsWithIgnoreCase("--debug")) { continue; }

                // "--debug true", "--debug=false", or a bare "--debug".
                auto value = arguments[i].fromFirstOccurrenceOf("=", false, false).trim();
                if (value.isEmpty() && i + 1 < arguments.size()
                    && ! arguments[i + 1].startsWith("-"))
                {
                    value = arguments[i + 1].trim();
                }

                const auto on = value.isEmpty()
                             || value.equalsIgnoreCase("true")
                             || value == "1"
                             || value.equalsIgnoreCase("yes");

                GlobalSettings::getInstance().setPreReleaseChannelEnabled(on);
                juce::Logger::writeToLog(juce::String("PX3 update: pre-release channel ")
                                         + (on ? "ENABLED" : "disabled")
                                         + " by --debug");
                break;
            }
        }

        service.setPreReleaseChannelEnabled(
            GlobalSettings::getInstance().isPreReleaseChannelEnabled());
    });
}

} // namespace px3::update
