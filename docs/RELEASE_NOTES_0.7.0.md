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
