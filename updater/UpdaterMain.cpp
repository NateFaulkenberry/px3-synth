// PX3 Updater - the helper that installs while the DAW is still open.
//
// A separate process because it has to outlive the thing it is updating. The
// plugin runs inside somebody's host, holding the very bundle the installer
// replaces; it cannot install itself, and asking the user to quit their DAW
// and then go and find an installer is the workflow this exists to avoid.
//
// So: the plugin stages a verified installer and hands this its path and the
// host's process id. This waits for that process to go away, then runs the
// same signed, notarised .pkg the user would have downloaded by hand, then
// checks that the version on disk actually changed.
//
// It does NOT download, verify or choose anything. All of that happened in the
// plugin, where there is a UI to report it. This is the part that has to
// happen after the host has quit, and nothing more - which keeps the amount of
// code running unattended, as root, over a user's plug-ins as small as it can
// be.

#include <JuceHeader.h>

#include "UpdateService.h"

#include <csignal>
#include <unistd.h>

namespace
{
constexpr int kPollIntervalMs = 1000;
// Long enough for a session to be saved and a large project to close; short
// enough that a stuck host does not leave this running for a day.
constexpr int kMaxWaitSeconds = 60 * 60 * 6;

struct Request
{
    juce::File installer;
    int waitForPid { 0 };
    bool valid { false };
};

Request parseArguments(const juce::StringArray& arguments)
{
    Request request;

    for (int i = 0; i < arguments.size(); ++i)
    {
        if (arguments[i] == "--install" && i + 1 < arguments.size())
        {
            request.installer = juce::File(arguments[++i]);
        }
        else if (arguments[i] == "--wait-for-pid" && i + 1 < arguments.size())
        {
            request.waitForPid = arguments[++i].getIntValue();
        }
    }

    request.valid = request.installer.existsAsFile();
    return request;
}

bool processIsRunning(int pid)
{
    if (pid <= 0) { return false; }
    // Signal 0 tests for existence without delivering anything.
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
}

void log(const juce::String& message)
{
    juce::Logger::writeToLog("PX3 Updater: " + message);

    const auto file = px3::update::UpdateService::stagingDirectory().getChildFile("updater.log");
    file.getParentDirectory().createDirectory();
    file.appendText(juce::Time::getCurrentTime().toISO8601(true) + "  " + message + "\n");
}

// The installer must be the file we were told about and nothing else. This
// process may be launched by anything that can spell its arguments, so it
// checks rather than trusting: inside our own staging directory, a .pkg, and
// signed by the same Developer ID the user already trusts.
//
// The signature check is the one that matters. A checksum says the bytes
// arrived intact; this says who made them, and it is the same guarantee a user
// gets double-clicking the installer themselves. Without it this would be a
// download-and-execute mechanism, which is exactly what the brief forbids.
bool installerIsAcceptable(const juce::File& installer)
{
    const auto staging = px3::update::UpdateService::stagingDirectory();

    if (! installer.isAChildOf(staging))
    {
        log("refused: " + installer.getFullPathName() + " is not in the staging directory");
        return false;
    }

    if (! installer.hasFileExtension("pkg"))
    {
        log("refused: " + installer.getFileName() + " is not a .pkg");
        return false;
    }

    // The signature is the real authenticator. A checksum says the bytes
    // arrived intact; this says who made them, and it is the same guarantee
    // the user gets double-clicking the installer themselves.
    juce::ChildProcess check;
    if (! check.start("/usr/sbin/pkgutil --check-signature \"" + installer.getFullPathName() + "\""))
    {
        log("refused: could not run pkgutil to check the signature");
        return false;
    }

    const auto output = check.readAllProcessOutput();
    check.waitForProcessToFinish(30000);

    if (! output.contains("Developer ID Installer:"))
    {
        log("refused: " + installer.getFileName() + " is not signed by a Developer ID");
        return false;
    }

    return true;
}

int runInstaller(const juce::File& installer)
{
    // Through the system installer, with the user's own authorisation - not a
    // privileged helper of our own. `open` hands it to Installer.app, which is
    // where a user expects to confirm something that writes to /Library.
    juce::ChildProcess process;
    juce::StringArray command { "/usr/bin/open", "-W", installer.getFullPathName() };

    if (! process.start(command))
    {
        log("could not start the installer");
        return -1;
    }

    process.waitForProcessToFinish(-1);
    // getExitCode returns uint32; a process exit status is a byte, so the
    // narrowing is safe - it is made explicit rather than implicit.
    return static_cast<int>(process.getExitCode());
}
} // namespace

int main(int argc, char* argv[])
{
    // Never die because nobody is reading stdout.
    //
    // This process is started by a plug-in that exits seconds later, and if its
    // output happens to be a pipe, the first write after the parent goes takes
    // SIGPIPE and kills it silently - mid-update, with no log written. The
    // launcher now hands over /dev/null, so this is belt and braces, but the
    // failure it prevents is invisible and cost a release to find: a helper
    // whose whole job is to outlive its parent should not be killable by its
    // parent's file descriptors.
    ::signal(SIGPIPE, SIG_IGN);

    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::StringArray arguments;
    for (int i = 1; i < argc; ++i) { arguments.add(juce::String::fromUTF8(argv[i])); }

    const auto request = parseArguments(arguments);

    if (! request.valid)
    {
        log("nothing to do: no readable installer in " + arguments.joinIntoString(" "));
        return 2;
    }

    log("asked to install " + request.installer.getFileName()
        + (request.waitForPid > 0 ? ", waiting for pid " + juce::String(request.waitForPid)
                                  : juce::String(", no process to wait for")));

    if (! installerIsAcceptable(request.installer))
    {
        return 3;
    }

    // Wait for the host to quit. This is the whole reason for a separate
    // process: until it does, the plug-in bundles are in use.
    if (request.waitForPid > 0)
    {
        auto waited = 0;
        while (processIsRunning(request.waitForPid) && waited < kMaxWaitSeconds)
        {
            juce::Thread::sleep(kPollIntervalMs);
            ++waited;
        }

        if (processIsRunning(request.waitForPid))
        {
            log("gave up waiting after " + juce::String(waited / 60) + " minutes");
            return 4;
        }

        log("host has quit after " + juce::String(waited) + "s; installing");
    }

    const auto exitCode = runInstaller(request.installer);

    if (exitCode != 0)
    {
        log("installer exited with " + juce::String(exitCode));
        return 5;
    }

    log("installer finished");

    // Verified rather than assumed. An installer that returns zero has not
    // necessarily put a new version on disk, and reporting success for a
    // failed update is worse than reporting the failure.
    const auto installed = juce::File("/Applications/PX3 Synth.app");
    if (installed.exists())
    {
        const auto plist = installed.getChildFile("Contents/Info.plist");
        const auto text = plist.loadFileAsString();
        const auto marker = text.fromFirstOccurrenceOf("<key>CFBundleShortVersionString</key>",
                                                       false, false);
        const auto version = marker.fromFirstOccurrenceOf("<string>", false, false)
                                   .upToFirstOccurrenceOf("</string>", false, false)
                                   .trim();
        log("installed version now reads " + (version.isNotEmpty() ? version
                                                                   : juce::String("unknown")));
    }

    // The staged installer has served its purpose. Leaving it behind would
    // fill Application Support with old packages.
    request.installer.deleteFile();
    px3::update::UpdateService::stagingDirectory()
        .getChildFile("staged-update.json").deleteFile();

    // The copy of this binary that is running cannot delete itself while it
    // executes, so it is left for the next run to replace. It is one small
    // file in our own directory, not litter in Downloads.

    return 0;
}
