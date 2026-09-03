#include "GitHubReleaseProvider.h"

namespace px3::update
{
namespace
{
// Tokens that identify a build for something other than us. An asset naming
// one of these is not our installer, whatever else its name says.
const juce::StringArray kForeignPlatforms { "windows", "win64", "win32", "linux" };
const juce::StringArray kForeignArchitectures { "x86_64", "x64", "intel", "i386" };

bool namesForeignBuild(const juce::String& assetName,
                       const juce::String& platform,
                       const juce::String& architecture)
{
    const auto lower = assetName.toLowerCase();

    for (const auto& token : kForeignPlatforms)
    {
        if (lower.contains(token) && ! platform.toLowerCase().contains(token)) { return true; }
    }

    for (const auto& token : kForeignArchitectures)
    {
        if (lower.contains(token) && ! architecture.toLowerCase().contains(token)) { return true; }
    }

    return false;
}

juce::String findSha256(const juce::String& releaseNotes)
{
    // "SHA-256: <64 hex>", case-insensitive, anywhere in the notes. A
    // convention a human or a pipeline can both follow.
    auto index = releaseNotes.indexOfIgnoreCase("sha-256:");
    if (index < 0) { index = releaseNotes.indexOfIgnoreCase("sha256:"); }
    if (index < 0) { return {}; }

    const auto tail = releaseNotes.substring(index).fromFirstOccurrenceOf(":", false, false).trim();
    const auto candidate = tail.initialSectionContainingOnly("0123456789abcdefABCDEF");
    return candidate.length() == 64 ? candidate.toLowerCase() : juce::String();
}

juce::Time parseIso8601(const juce::String& text)
{
    // "2026-09-02T15:41:14Z". Not worth a dependency; a bad date costs a
    // display, not a decision.
    if (text.length() < 19) { return {}; }
    return juce::Time(text.substring(0, 4).getIntValue(),
                      juce::jmax(0, text.substring(5, 7).getIntValue() - 1),
                      text.substring(8, 10).getIntValue(),
                      text.substring(11, 13).getIntValue(),
                      text.substring(14, 16).getIntValue(),
                      text.substring(17, 19).getIntValue());
}
} // namespace

// The background half. Owns nothing the provider needs after cancellation:
// the cancelled flag is shared, so a job that outlives its provider notices
// and delivers nothing.
class GitHubReleaseProvider::LookupJob final : public juce::Thread
{
public:
    LookupJob(juce::URL url,
              juce::String productIdIn,
              juce::String platformIn,
              juce::String architectureIn,
              Fetcher transportIn,
              std::shared_ptr<std::atomic<bool>> cancelledIn,
              LookupCallback callbackIn)
        : juce::Thread("PX3 update lookup"),
          endpoint(std::move(url)),
          productId(std::move(productIdIn)),
          platform(std::move(platformIn)),
          architecture(std::move(architectureIn)),
          transport(std::move(transportIn)),
          cancelled(std::move(cancelledIn)),
          callback(std::move(callbackIn))
    {
    }

    ~LookupJob() override { stopThread(3000); }

    void run() override
    {
        auto reply = perform();

        if (cancelled->load()) { return; }

        // Delivered on the message thread, so the UI can act on it directly.
        auto callbackCopy = callback;
        auto flag = cancelled;
        juce::MessageManager::callAsync([callbackCopy, reply, flag]
        {
            if (! flag->load() && callbackCopy != nullptr) { callbackCopy(reply); }
        });
    }

    LookupResult perform()
    {
        UpdateResult transportResult;
        const auto body = transport(endpoint, transportResult);

        if (! transportResult.ok()) { return { transportResult, {} }; }

        return parseLatestRelease(body, productId, platform, architecture);
    }

private:
    juce::URL endpoint;
    juce::String productId, platform, architecture;
    Fetcher transport;
    std::shared_ptr<std::atomic<bool>> cancelled;
    LookupCallback callback;
};

GitHubReleaseProvider::GitHubReleaseProvider(juce::String repositoryOwner, juce::String repositoryName)
    : owner(std::move(repositoryOwner)), repo(std::move(repositoryName))
{
    transport = [](const juce::URL& url, UpdateResult& result) -> juce::String
    {
        int status = 0;
        auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                           .withConnectionTimeoutMs(15000)
                           .withStatusCode(&status)
                           .withExtraHeaders("Accept: application/vnd.github+json\r\n"
                                             "User-Agent: PX3-Updater\r\n");

        auto stream = url.createInputStream(options);

        if (stream == nullptr)
        {
            // No stream at all is indistinguishable from being offline here,
            // and "check your connection" is the more useful thing to say.
            result = UpdateResult::failure(UpdateError::noNetwork,
                                           "createInputStream returned null");
            return {};
        }

        // 404 from /releases/latest is not a failure. It is what GitHub
        // answers for a repository that has published nothing yet - a normal
        // state for a product before its first release, and for one whose
        // releases are all still drafts.
        //
        // Reporting it as an outage was wrong twice over: it told the user the
        // update service was down when it was answering correctly, and it hid
        // the actual situation, which is that there is nothing to update to.
        // An empty body is how that reaches the parser below.
        if (status == 404)
        {
            return {};
        }

        // 403 and 429 are how GitHub rate-limits an unauthenticated caller.
        // Genuinely "try again later", and the one case the phrase fits.
        if (status >= 400)
        {
            result = UpdateResult::failure(UpdateError::providerUnavailable,
                                           "HTTP " + juce::String(status));
            return {};
        }

        return stream->readEntireStreamAsString();
    };
}

GitHubReleaseProvider::~GitHubReleaseProvider()
{
    cancel();
    job.reset();
}

juce::URL GitHubReleaseProvider::latestReleaseUrl() const
{
    // The pre-release channel has to ask a different question. /releases/latest
    // will never return a pre-release however it is queried, so reaching one
    // means asking for the list and deciding here which is newest - which this
    // code can do properly, having a real version comparison.
    if (includePreReleases)
    {
        return juce::URL("https://api.github.com/repos/" + owner + "/" + repo
                         + "/releases?per_page=30");
    }

    // GitHub's own "latest", which is what a release is expected to be
    // published as. Note that this endpoint excludes anything flagged as a
    // PRE-RELEASE or a draft: a repository whose releases are all flagged that
    // way answers 404 here and reads as having published nothing. That is
    // handled below as "no release" rather than as an outage, but it is worth
    // knowing as the reason an updater can go quiet against a repository that
    // visibly has releases.
    return juce::URL("https://api.github.com/repos/" + owner + "/" + repo + "/releases/latest");
}

void GitHubReleaseProvider::cancel()
{
    cancelled->store(true);
    if (job != nullptr) { job->stopThread(3000); }
}

void GitHubReleaseProvider::fetchLatestRelease(const juce::String& productId,
                                               const juce::String& platform,
                                               const juce::String& architecture,
                                               LookupCallback callback)
{
    // A fresh flag per lookup, so cancelling an old one cannot silence a new.
    cancelled = std::make_shared<std::atomic<bool>>(false);

    auto next = std::make_unique<LookupJob>(latestReleaseUrl(), productId, platform,
                                            architecture, transport, cancelled, callback);

    if (synchronous)
    {
        const auto reply = next->perform();
        if (callback != nullptr) { callback(reply); }
        return;
    }

    job = std::move(next);
    job->startThread();
}

UpdateProvider::LookupResult
GitHubReleaseProvider::parseLatestRelease(const juce::String& jsonText,
                                          const juce::String& productId,
                                          const juce::String& platform,
                                          const juce::String& architecture)
{
    LookupResult out;

    // Nothing published. Not an error, and not a malformed response: an
    // invalid release with an ok() result is how "there is no release" is
    // spelled, and the service turns that into "you're up to date".
    if (jsonText.trim().isEmpty())
    {
        return out;
    }

    juce::var document;
    if (juce::JSON::parse(jsonText, document).failed())
    {
        out.result = UpdateResult::failure(UpdateError::malformedResponse,
                                           "response was not JSON");
        return out;
    }

    // An array is the list endpoint's answer; an object is /releases/latest's.
    // Both are accepted so the transport does not have to know which it asked.
    if (auto* releases = document.getArray())
    {
        LookupResult best;
        auto sawAnything = false;

        for (const auto& entry : *releases)
        {
            if (! entry.isObject()) { continue; }

            // A draft is not published, whatever channel the user is on.
            if (static_cast<bool>(entry.getProperty("draft", false))) { continue; }

            const auto candidate = parseRelease(entry, productId, platform, architecture);
            if (! candidate.result.ok() || ! candidate.release.isValid()) { continue; }

            // Newest by VERSION, not by the order the list arrives in - GitHub
            // sorts by creation date, which is not the same thing once a patch
            // to an older line is published after a newer one.
            if (! sawAnything || candidate.release.version > best.release.version)
            {
                best = candidate;
                sawAnything = true;
            }
        }

        return best;   // an invalid release if nothing usable was found
    }

    if (! document.isObject())
    {
        out.result = UpdateResult::failure(UpdateError::malformedResponse,
                                           "response was neither an object nor an array");
        return out;
    }

    return parseRelease(document, productId, platform, architecture);
}

UpdateProvider::LookupResult
GitHubReleaseProvider::parseRelease(const juce::var& document,
                                    const juce::String& productId,
                                    const juce::String& platform,
                                    const juce::String& architecture)
{
    LookupResult out;

    const auto tag = document.getProperty("tag_name", juce::var()).toString();
    const auto version = SemanticVersion::parse(tag);

    if (! version.isValid)
    {
        out.result = UpdateResult::failure(UpdateError::malformedResponse,
                                           "tag_name '" + tag + "' is not a version");
        return out;
    }

    const auto notes = document.getProperty("body", juce::var()).toString();

    const auto assetsVar = document.getProperty("assets", juce::var());
    const auto* assets = assetsVar.getArray();

    if (assets == nullptr)
    {
        out.result = UpdateResult::failure(UpdateError::malformedResponse, "no assets array");
        return out;
    }

    // WHAT COUNTS AS AN INSTALLER.
    //
    // Two shapes, because the build has produced both. Early releases attached
    // a bare .pkg; every release since attaches the distribution archive - a
    // zip holding the .pkg and the uninstaller - and nothing else. Matching
    // only .pkg meant a release with seven published versions reported "no
    // installer for your system".
    //
    // A bare .pkg is preferred where one exists, because it is one fewer step;
    // otherwise the archive, which the staging step opens.
    juce::String chosenName, chosenUrl;
    auto chosenIsArchive = false;
    auto chosenNamesOurBuild = false;

    for (const auto& entry : *assets)
    {
        if (! entry.isObject()) { continue; }

        const auto assetName = entry.getProperty("name", juce::var()).toString();
        const auto assetUrl = entry.getProperty("browser_download_url", juce::var()).toString();

        if (assetUrl.isEmpty()) { continue; }
        if (namesForeignBuild(assetName, platform, architecture)) { continue; }

        const auto isPkg = assetName.endsWithIgnoreCase(".pkg");
        // "-Installer.zip" specifically. The other zip each release carries is
        // the plug-in folder for a manual copy, which is not something to run.
        const auto isArchive = assetName.endsWithIgnoreCase("-installer.zip");

        if (! isPkg && ! isArchive) { continue; }

        const auto lower = assetName.toLowerCase();
        const auto namesOurs = lower.contains(platform.toLowerCase())
                            || lower.contains(architecture.toLowerCase());

        const auto better = chosenName.isEmpty()
                         || (isPkg && chosenIsArchive)
                         || (isPkg == ! chosenIsArchive && namesOurs && ! chosenNamesOurBuild);

        if (better)
        {
            chosenName = assetName;
            chosenUrl = assetUrl;
            chosenIsArchive = isArchive;
            chosenNamesOurBuild = namesOurs;
        }
    }

    if (chosenName.isEmpty())
    {
        out.result = UpdateResult::failure(UpdateError::noMatchingInstaller,
                                           "release " + tag + " has no installer for "
                                               + platform + "/" + architecture);
        return out;
    }

    out.release.productId = productId;
    out.release.version = version;
    out.release.releaseNotes = notes;
    out.release.downloadUrl = juce::URL(chosenUrl);
    out.release.installerFilename = chosenName;
    out.release.installerIsArchive = chosenIsArchive;
    // Either signal makes it a pre-release: GitHub's flag, or a suffix on the
    // tag. The flag is how a person marks one on the web; the suffix is how a
    // version says so itself. A release with either is labelled as one.
    out.release.isPreRelease = static_cast<bool>(document.getProperty("prerelease", false))
                            || version.isPreRelease();
    out.release.platform = platform;
    out.release.architecture = architecture;
    out.release.sha256 = findSha256(notes);
    out.release.releaseDate = parseIso8601(
        document.getProperty("published_at", juce::var()).toString());

    return out;
}

} // namespace px3::update
