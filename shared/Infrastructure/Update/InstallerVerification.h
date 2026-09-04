#pragma once

#include <JuceHeader.h>

namespace px3::update
{

// How the updater decides an installer is genuine, kept apart from the helper's
// main() so it can be tested. The helper is an executable the test suite cannot
// link, and this is the check that has silently refused every correctly signed
// release twice now - once because the helper died before running it, and once
// because it was handed a path it could not open.

// The pkgutil invocation, as ARGUMENTS rather than as one command string.
//
// This is the whole fix. ChildProcess does not run a shell, so quotes written
// into a command string are passed through as literal characters: pkgutil was
// asked about a file named `"/Users/.../PX3.pkg"`, quotes included, replied
// "Package does not exist", and the caller read the absence of a certificate
// name as proof the package was unsigned. It also exits 0 for a missing file,
// so no status check would have caught it either.
//
// Separate arguments need no quoting and cannot be re-split, which is what
// makes a path containing spaces or parentheses - `~/Library/P(X3)/` is both -
// survive intact.
inline juce::StringArray signatureCheckCommand(const juce::File& installer)
{
    return { "/usr/sbin/pkgutil", "--check-signature", installer.getFullPathName() };
}

// Whether pkgutil's answer actually names a Developer ID Installer certificate.
//
// Deliberately a positive match on the certificate rather than an exit status
// or an absence of errors: pkgutil reports a missing file, an unsigned package
// and a correctly signed one all with status 0, so the certificate line is the
// only thing that distinguishes them.
inline bool signatureOutputNamesADeveloperID(const juce::String& pkgutilOutput)
{
    return pkgutilOutput.contains("Developer ID Installer:");
}

} // namespace px3::update
