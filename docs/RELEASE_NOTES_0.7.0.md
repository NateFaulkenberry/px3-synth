# PX3 v0.7.0

In progress.

---

## Fixed

- **A mapped MIDI CC no longer waits for the interface.** It reached its
  parameter on the next UI tick — up to ~33 ms at the 30 Hz timer this synth
  runs — so a controller sweep arrived as a staircase rather than a sweep. It
  now drives the DSP from the block it arrives in.

  `setValueNotifyingHost` is two things bolted together: an atomic store into
  the parameter, and the call that tells the host. Only the second is unsafe
  on a real-time thread, so the audio thread now does the first directly when
  the CC arrives, and the message-thread pump still does both on its own
  schedule. Both write the same normalised number, so applying it twice is
  applying it once.

  Writing the parameter itself, rather than intercepting each DSP read, is
  what makes this reach every reader — the modulation seam, the direct reads
  in the effects, and anything added later. There is no per-parameter list to
  forget a parameter from.

  **Automation recording is unaffected**, which was the hard requirement:
  Logic's Touch, Latch and Write read the host notification, which still comes
  from the message thread exactly as before. JUCE's `setValueNotifyingHost`
  has no early-out on an unchanged value, so the audio thread's earlier store
  cannot swallow it — verified in JUCE's source rather than assumed, because
  the whole design rests on it.

  A CC remains **authoritative, not additive**: it sets the parameter's base
  value and contributes no delta, so the modulation accumulator is untouched
  and an LFO, envelope or Macro still sums on top of whatever the controller
  last asked for.

- **The uninstaller now shows its own icon.** It was being generated — the
  app's icon with a red X struck through it — and copied into the bundle, and
  then ignored. `osacompile` also writes an `Assets.car` holding the stock
  AppleScript scroll and sets `CFBundleIconName`, which macOS resolves through
  that catalog in preference to the `applet.icns` the build had just written.
  Measured rather than assumed: before the fix the shipped uninstaller's icon
  matched the stock one to four decimal places. The build now removes both the
  key and the catalog, and refuses to finish if either comes back.
- **The EQ analyser's grid is drawn once instead of sixty times a second.** It
  was redrawing its gridlines, labels and zero line every frame behind a trace
  that actually changes — measured at 49% of the component's entire paint cost,
  201 us a frame. The VU meter's face was already cached; this now is too.

## New: separate Dry and FX outputs

- **The mixer's two buses, offered to the host as stereo pairs.** A plain
  instance is unchanged — one stereo output carrying the full mix. Enable the
  second pair in the host and **1/2 carries Dry, 3/4 carries FX**, as
  independent stems rather than one four-channel output.

  The synth already kept the two apart all the way to the final copy, so this
  exposes buffers that existed rather than adding a parallel signal path:
  measured at **0.7% worst-case CPU change**, most cases under 0.3%. The stems
  carry the fixed output boost but not the analog master stage or the output
  ceiling — those act on the sum and cannot be divided between two stems, so
  summing 1/2 and 3/4 in the host is close to the stereo output rather than
  identical to it.

  Bus configuration is the host's, not the sound's: it is not written into
  presets or plugin state, so auditioning a patch never changes your routing.

## New: Macro depth

- **Every Macro → parameter assignment has its own depth.** Cmd-click (Ctrl on
  Windows) a Macro knob to open a panel listing everything it drives, each with
  its own slider and a signed percentage. Depth is per *route*: the same
  parameter driven by two Macros has two depths, and one Macro's several
  destinations each have their own.

  New assignments are full depth, so existing patches sound exactly as they did.
  The stored preset format already carried a per-destination depth — this
  exposes it rather than changing the format.

- **Assignment mode is easier to leave.** Double-click still arms it. It now
  finishes on the Macro knob, on a click on background, or on **Enter** — all
  three through one commit path, so they cannot mean three different things.
  The keyboard notice names the Macro and says how to finish.

## New: separate Dry and FX outputs

- **The mixer's two buses, offered to the host as stereo pairs.** A plain
  instance is unchanged — one stereo output carrying the full mix. Enable the
  second pair in the host and **1/2 carries Dry, 3/4 carries FX**, as
  independent stems rather than one four-channel output.

  The synth already kept the two apart to the final copy, so this exposes
  buffers that existed rather than adding a parallel path: **0.7% worst-case
  CPU change**, most cases under 0.3%. The stems carry the fixed output boost
  but not the analog master stage or the output ceiling — those act on the sum
  and cannot be divided between two stems.

  Bus configuration is the host's, not the sound's: it is never written into
  presets or plugin state.

## New: update checking

- **SETTINGS now has an Updates section.** It shows the installed version — read
  from the build, not a string kept in the UI — and checks the project's GitHub
  Releases for a newer one.

  **Prepare Update** downloads and verifies the signed installer while your DAW
  stays open. Nothing is installed from inside the plugin: a helper shipped
  inside the standalone waits for the host to quit and then runs the same
  notarised `.pkg` you would have run by hand, and checks the version actually
  changed. You never have to close your DAW and go looking for an installer.

  Built around a product registry and a provider interface rather than around
  this synth: PX3 Synth is the first product registered, and GitHub is one
  provider behind an abstraction that can be replaced without touching the UI
  or the installation path.

## Measured

- **`PX3Diag eqspectrum`** — the two measurements the EQ spectrum brief asked
  for and never got. FFT size against CPU, window length and resolution
  (1024–16384, at 48 and 96 kHz), and the component's paint cost. It settles
  one open question: CPU is not the constraint at any FFT size — the largest
  costs half a percent of a frame — so the case for making the size
  configurable is about window length alone. See `docs/EQ_SPECTRUM_VISUALIZER.md`
  section 7.

---

## Testing

Nine tests added, every one verified to fail against the fault it describes.
Six fail with the fix removed; the three that are invariant guards were each
mutated toward the bug they name — a learn gesture that jumps the knob, a
route left behind by a cleared mapping, and dropping the host notification on
the grounds that the audio thread already wrote the value.

That found one test of mine that proved nothing:
`TheTeachingMoveDoesNotDriveTheDspEither` could not reach the learn path at
all, and its mutant was caught by a test that already existed. It was replaced
with a check that clearing one knob's mapping stops that knob and leaves the
one sharing its CC still moving — the shift-click-to-unmap path, which is the
only place a route can outlive the mapping it came from.

1227 component tests pass. `PX3Diag rtsafety` reports 0 allocations per block,
including blocks carrying MIDI, which is where the new audio-thread work sits.

---

## Known limitations

- **Apple Silicon only.** arm64; no Intel or universal build.
- **No CI.** Everything is measured on one machine.
- **MIDI CC is block-accurate, not sample-accurate.** `processBlock` samples
  its parameters once, before rendering, so one block is the floor. True
  sample accuracy would mean splitting the render at CC timestamps — a rewrite
  of the mixer and FX chain, not a change to this path. The CC is read before
  that sampling, so it is early within its block rather than late.
- **Mappings are per CC number, not per device.**
- **Macro depth is fixed** at full positive; the stored format carries a
  per-destination depth, so an editor for it changes UI, not format.
- **A zero-duration ADSR is still reachable** by setting all four times to zero.
  Unlike the Breakpoint case this is not a trap — an ADSR holds at its sustain
  rather than retiring — so no floor was added.
- **One `PX3Diag regress` case still fails**, `N sine key-release, sustain=0`,
  at a note-off transient ratio around 7-8 against a threshold of 6.0. It
  predates this work — the identical failing set was measured at `699aa8c` — and
  the threshold has not been moved to hide it.
- **The debug panel's preset dump cannot be fully tested**: the file chooser is a
  modal async dialog nothing headless can drive. The mapping from fields to
  metadata is tested; the chooser itself is not.
- **EQ spectrum visualiser** items from its design brief remain open.
