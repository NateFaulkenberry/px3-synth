#include "UpdateModel.h"

namespace px3::update
{

juce::String describe(UpdateState state)
{
    switch (state)
    {
        case UpdateState::notConfigured:   return "Updates are not configured";
        case UpdateState::idle:            return "";
        case UpdateState::checking:        return "Checking for updates";
        case UpdateState::upToDate:        return "You're up to date";
        case UpdateState::updateAvailable: return "Update available";
        case UpdateState::downloading:     return "Downloading";
        case UpdateState::verifying:       return "Verifying download";
        case UpdateState::readyToInstall:  return "Ready to install";
        case UpdateState::installing:      return "Installing";
        case UpdateState::updated:         return "Updated";
        case UpdateState::failed:          return "Update failed";
    }
    return {};
}

juce::String describe(UpdateError error)
{
    // Each of these is what the user is told. No status codes, no JSON, no
    // file paths - those go to the log, where they are useful and where they
    // cannot alarm somebody who just wanted a new version.
    switch (error)
    {
        case UpdateError::none:
            return {};
        case UpdateError::noNetwork:
            return "Couldn't check for updates. Check your internet connection and try again.";
        case UpdateError::providerUnavailable:
            return "The update service is unavailable right now. Try again later.";
        case UpdateError::malformedResponse:
            return "The update information couldn't be read. Try again later.";
        case UpdateError::noMatchingInstaller:
            return "This release has no installer for your system yet.";
        case UpdateError::unsupportedArchitecture:
            return "This release does not support your processor.";
        case UpdateError::downloadFailed:
            return "The download didn't finish. Check your connection and try again.";
        case UpdateError::checksumMismatch:
            return "The download was incomplete or damaged, and has been discarded.";
        case UpdateError::stagingFailed:
            return "The update couldn't be saved. Check that there is free disk space.";
        case UpdateError::installerLaunchFailed:
            return "The installer couldn't be started.";
        case UpdateError::installerFailed:
            return "The installer didn't finish successfully.";
        case UpdateError::verificationFailed:
            return "The update finished but couldn't be verified.";
        case UpdateError::cancelled:
            return "Update cancelled.";
        case UpdateError::alreadyInProgress:
            return "An update is already in progress.";
    }
    return {};
}

} // namespace px3::update
