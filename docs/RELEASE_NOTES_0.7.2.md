# PX3 v0.7.2

v0.7.1 shipped in-plugin updating that could find a release, download it, verify
its checksum, stage it and hand it to the helper — and then never install
anything. This release fixes that, and rebuilds the flow around it so the update
is one button rather than two.

The rest is the interface: a speech-bubble notice that points at the control it
is about, the same shape reused for the Macro depth panel, one close control
across every panel, and release notes you can actually read to the end.

---

## Fixed

- **The updater never installed.** Every visible step succeeded — download,
  checksum, extract, stage, handoff — and nothing was ever installed. The
  helper's own log file did not exist, even though writing it is its first act
  after parsing its arguments, so it was dead before it ran any of its own code.

  It was launched with JUCE's default `ChildProcess` stream flags, which `dup2`
  the child's stdout and stderr onto a pipe. The `ChildProcess` is a local: the
  read end closed the moment the launching function returned, and the helper's
  first write hit a pipe with no reader and took `SIGPIPE`.

  This is why it survived every check. Signature verification passed on both
  copies of the binary. Run from a terminal it worked perfectly — stdout was a
  terminal, and nothing closed it. The only symptom was an absence: a log file
  that was not there.

  Fixed on both sides, because a helper whose whole job is to outlive its parent
  should not be killable by its parent's file descriptors. The launcher passes
  no stream flags, so the child gets `/dev/null`; the helper ignores `SIGPIPE`
  regardless of who starts it. Reproduced against a closed reader before and
  after — no log, then a log.

- **The gear kept glowing after the notice had gone.** The update notice shows
  itself out after twenty seconds and the glow stayed on, which made it the
  permanent state of the window rather than a signal. They now end together.

  This reverses a rule the test suite asserted deliberately — the notice as a
  one-off, the glow as the standing signal — so that test now pins the opposite.
  SETTINGS still reports the update after both have gone quiet, and a newly
  opened editor announces it again, so this shortens how long the update shouts
  rather than hiding it.

- **The update notice swallowed nothing and blocked nothing.** It hangs over the
  header, so a click aimed at it landed on whatever was underneath — an
  oscillator's bypass, say. A click on a notice should do nothing, not something
  invisible. It now absorbs clicks; its close button still receives its own.

## Changed: the update is one button

**Install Update** now downloads, verifies, stages and hands off to the helper
without asking again. The message then reads *"Installation ready. Please save
your changes and close your DAW to complete the installation."* and the button
goes — there is nothing left to press.

The second button was only ever confirming what the first one requested. It sat
between the two halves of a job the user had already asked for, and the failure
above meant that for one release it was a button that did nothing at all.

If the download or staging fails you get the error and **Try Again**, which
retries the install rather than starting over with a check — the release is
still there, and the service already accepted being asked to prepare again from
a failed state.

No latch was needed to stop a second helper starting behind the first:
`launchInstaller` leaves the state at *installing* on success and *failed* on
error, so the condition the handoff is guarded on cannot still be true
afterwards.

## Changed: release notes you can read to the end

The Updates panel showed the first 400 characters of a release's notes in three
fixed lines. The cut existed because anything past three lines was invisible
anyway. The box now scrolls and the truncation is gone.

It is not a `TextEditor`. That was the obvious answer and it cost about **eight
milliseconds of every editor repaint** — measured against this project's own
repaint budget, and more than the entire rest of the interface put together.
The text is a `TextLayout` in a viewport instead: laid out when the text or the
width changes, and only drawn on the frames in between.

## New: the notice is a speech bubble

The "a new version is available" line was a plain label floating over the header
with nothing tying it to the button it refers to. It now points at that button.

The arrow is part of the background, not a second shape laid beside it: the
outline is walked as a detour in the edge, so the whole thing is one closed path
that gets one fill and one stroke. Drawn as two shapes the outline runs through
the join and the translucent fills double where they overlap.

**The Macro depth panel is the same bubble**, turned to point left. It already
pointed at its macro knob, but drew that pointer as a separate triangle and
could only hide the seam by leaving one of the triangle's three edges unstroked
— because stroking it drew a line across the arrow's mouth.

Both are configurable from `UIConfig.json`: colours, separate background and
border opacity, corner radius, border width, arrow size, arrow position and the
notice's offset. Opacity is its own property now rather than alpha buried in a
hex value, and multiplies into the colour so a colour carrying its own alpha
still means something.

## New: one close control

The circular X from the bus-insert sheets is now the close control on the update
notice, the Macro depth panel, the SETTINGS page and the preset browser, always
in the top-right corner. The text buttons along the bottom of the SETTINGS page
and the Macro depth panel are gone; the space they held is now used by what is
above them.

It moved out of `BusInsertOverlay` into `shared/`, since it is no longer the
sheets' button, and every site reads the same style keys — size, offsets, ring
and glyph widths, four colours — through one reader rather than each repeating
the same ten lookups. Anchoring to a corner rather than centring in a row is
what lets a header's height change without the button moving.

## New: SETTINGS in the menu

The preset menu has a **Settings** item below Import/Export, in its own section
because the items above it are all about the preset in front of you and this one
is not. It uses the same toggle the gear does, so it opens and closes from the
same place and returns you to the panel you came from.

## New: the uninstaller downloads on its own

The release workflow now publishes `PX3-Uninstaller-vX.Y.Z-macOS.zip` as a
separate asset, in both the beta and production paths.

It already ships inside the installer zip, which is the copy most people get.
This one is for the person who installed months ago, threw that zip away, and
now wants the plug-ins gone — without it the only way to get an uninstaller was
to download a full release.

Archived with `ditto` rather than `zip`, so the bundle's symlinks, extended
attributes and code signature survive, and verified by unpacking on every run:
an archive of an app that does not restore to a working app is a failure nobody
notices until they need it most.

## New: a debug toggle for the update affordances

The debug console's **UPDATE** section has a toggle that shows the notice and
lights the gear with no update in sight. Styling them otherwise meant waiting
for a real release to exist, which is not something you can arrange while
adjusting a corner radius.

It holds rather than fires once: while it is on the notice does not count down,
and opening SETTINGS does not put it away — the debug console is reached through
that panel, so honouring the usual rule would have shown the notice for exactly
as long as it took to go and look at it.

## Changed: the standalone is a required install

The updater helper lives inside `PX3 Synth.app`, and a plug-in updates itself by
handing a staged installer to that helper. Without the standalone there is
nothing to hand it to.

The installer now shows the standalone as a ticked, greyed row titled
"Standalone Application (required)". It is shown rather than installed silently
for the same reason the effects are listed: what lands on the machine should be
visible, even when there is no choice about it.

## Testing

The suite covers the new behaviour rather than describing it:

- The handoff survives a closed pipe — the exact condition that killed it —
  reproduced in both directions.
- The glow stops with the notice; a new window announces again; opening SETTINGS
  still stops it, on an editor of its own, because on the first one the timeout
  has already cleared the glow and the assertion would have passed whatever
  SETTINGS did.
- Closing the notice by hand ends the announcement, glow included.
- The debug preview shows the notice with no update available, does not time
  out, survives opening SETTINGS, and toggles back off and on again.
- Both bubbles are one closed subpath, arrow included; the left arrow tracks its
  knob, stays clear of the corner arcs, and draws plain when it has no target.
- Background and border opacity move independently, multiply into the colour,
  and clamp.
- The only button offered is Install Update, and a 2,400-character set of
  release notes reaches the panel whole.

## Known limitations

- **The installer is verified by reproduction, not by a full round trip.** The
  `SIGPIPE` fix is proven against a synthetic closed-pipe reproduction — the
  helper now writes its log where it previously wrote nothing. A live
  prepare/quit/relaunch cycle on a real release is still the thing that proves
  the whole path end to end.
- **A second helper can still be started across sessions.** Nothing stops a
  handoff if the panel is opened in a fresh session while an update is already
  staged and waiting. Within a session the state machine prevents it.
- **The preset browser keeps its old CLOSE button** alongside the new corner
  glyph.
- **The effect products still have no update checking.** The Updates panel lives
  in the Synth's SETTINGS; the registry knows all eight, but only the Synth has
  UI in front of it.
- **Windows is still unpackaged.** The release workflow builds it and says so.
