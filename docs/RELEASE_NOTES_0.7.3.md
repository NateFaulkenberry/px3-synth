# PX3 v0.7.3

v0.7.2 fixed the updater helper dying before it could run. With it running, its
log showed what that crash had been hiding: it was refusing every installer as
unsigned — including the correctly signed, notarised ones this project
publishes.

This release fixes that, and adds the thing whose absence let both bugs ship: a
way to run the helper locally, the way the plug-in launches it.

---

## Fixed

- **The updater refused every correctly signed installer.** The helper reached
  its signature check, rejected the package, and stopped:

  ```
  asked to install PX3-v0.7.2-macOS.pkg, waiting for pid 19437
  refused: PX3-v0.7.2-macOS.pkg is not signed by a Developer ID
  ```

  The package was fine. `pkgutil` reports it as Developer ID signed and trusted
  by the Apple notary service, and the release workflow staples a ticket to it.

  The check built its command as one string with quotes around the path.
  Nothing runs a shell, so `pkgutil` received the quote characters as part of
  the filename, replied `Package does not exist: "/Users/..."`, and the absent
  certificate name was read as proof the package was unsigned. `pkgutil` exits
  **0** for a missing file, so a status check would not have caught it either.

  The path is one unquoted argument now. That the staging directory is
  `~/Library/P(X3)/` made a path with parentheses the case that always
  mattered.

- **The refusal message asserted a cause it had not established.** It said "is
  not signed by a Developer ID" when what had actually happened was that
  `pkgutil` was never given a path it could open. It now reports what `pkgutil`
  said, which would have identified this in one line instead of across two
  releases.

## New: running the updater locally

`scripts/test-updater-locally.sh <installer.pkg> [--run]`

Without `--run` it changes nothing: it stages the package and reports the three
things the helper requires — in the staging directory, a `.pkg`, and Developer
ID signed — including `pkgutil`'s actual verdict.

With `--run` it launches the helper **the way the plug-in does**: detached, with
its output going to `/dev/null`, waiting on a process id. That reproduction is
the point. Both failures in this path were invisible from the plug-in and
immediately obvious here, and neither could be reproduced by running the helper
from a terminal — with a terminal on stdout, the v0.7.1 crash does not happen at
all.

## Testing

The two halves of the check moved into `InstallerVerification.h`, because the
helper is an executable the test suite cannot link and this check had by then
silently refused every signed release twice. The suite pins:

- A path containing a space and parentheses survives as one unquoted argument.
- `pkgutil`'s "Package does not exist" is never mistaken for a signature.
- A genuine certificate line is accepted; an unsigned package is refused.

1366 assertions pass.

## Known limitations

- **An update install still has not been watched end to end.** The refusal is
  fixed and verified — the same helper that rejected a package in 10 ms now
  passes it and proceeds to wait — but no full prepare → quit DAW → relaunch
  cycle has been run. The local harness above is what makes that testable, and
  it is the next thing to do rather than something this release claims.
- **A second helper can still be started across sessions** if the panel is
  opened in a fresh session while an update is already staged. Within a session
  the state machine prevents it.
- The preset browser keeps its old CLOSE button alongside the corner glyph.
- Windows is still unpackaged.
