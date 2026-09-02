# PX3 v0.6.0

31 commits since v0.5.0. The envelope work that 0.5.0 started is finished: a
breakpoint envelope is now a genuine time/level trajectory rather than an ADSR
with extra points, and AMP ENV is an ADSR and nothing else. Plus a SETTINGS
page, a fifth Macro, and the analog console switched on by default with a
control to choose its voicing.

---

## New: SETTINGS

A gear at the right of the top bar, between MENU and the master gain. It takes
its width from the preset selector, so nothing else in the bar moved.

SETTINGS is a view like the six panels — it persists across sessions and lights
its own button — but full width, with no Macro strip: the strip is a performance
surface and there is nothing on this page to assign a Macro to.

- **Enable animations** (on by default) gates the keyboard sparks, the sparkles
  around the pitch and mod wheels, and the logo's movement. It is gated at the
  source, not the draw: a keyboard still spawning particles nobody paints is
  still doing the work. This is an install preference — kept between sessions,
  never written into a preset, so loading somebody else's patch cannot change
  it.
- **Analog Engine** chooses the console profile: CLEAN, BRITISH, AMERICAN,
  TRANSFORMER or MODERN. Unlike the animation setting this *is* part of the
  sound — an automatable parameter that travels in sessions and presets.

**The analog console is now on by default.** It was off because nothing in the
UI could turn it on, which made "off" the only state a user could ever hear.

## New: MACRO 5

A fifth Macro, on the same strip. `kMacroCount` was already the only number the
product carried, so this was one line plus a compile-time assertion; the work
was in the tests, which had three fixed four-element arrays indexed by macro
(those segfaulted rather than failed) and hard-coded lists of four IDs, four CCs
and four destination counts. All of it is count-driven now.

The strip also lays out better: cells take their edges from the strip's height
by position rather than from a rounded-down cell height taken N times, which
used to pool the remainder at the bottom. Each knob now sits on its caption with
the slack above and below the *pair* — bottom-aligning them left every cell's
slack above the knob and the whole column sat low, 53 px of air over the first
macro against 10 under the last. It reads 31 and 32 now.

**Double-clicking a Macro knob arms it**, exactly as Cmd-clicking does. Both go
through one helper so they cannot drift apart, and every other knob in the synth
is untouched.

**The Macro knobs look like Macro knobs.** Every other knob in this synth is
dark on a dark panel; the Macros are a performance layer that sits outside the
panels, so they are drawn as a pale hardware knob — a light bezel drilled with a
ring of holes, a raised off-white cap, and a dark tick. The holes are the value
indicator: grey and recessed by default, lit from behind up to the value, so the
reading is a ring of lit dots rather than a drawn arc. The MIDI and Macro
indicators moved into shared code that both knob styles call, so a mapped Macro
knob says `CC21` exactly the way a filter cutoff does.

---

## Rebuilt: Breakpoint is a trajectory

A breakpoint envelope was played as an ADSR that happened to have more points:
the DSP ran the points up to the sustain index, froze there for as long as the
key was held, then played the remainder on a second clock at note-off. **A shape
with two peaks only ever produced the first one.** The graph drew a trajectory
the DSP never travelled.

It is `level = f(time)` on a single clock now. The key triggers it and does not
gate it, so it advances whether or not the key is down, ends at its last point —
anchored at silence — and retires the voice there. A note-off does not truncate
it, which matters most where the shape passes through zero: taken as a release, a
note-off at a zero level reads as "nothing left to release" and would have
retired the voice before a second strike ever sounded.

### Mode is the authority, not the point count

The reported symptom was that deleting points in Breakpoint mode made the editor
behave like a partly working ADSR. The cause was `isAdsrSkeleton()` — four
points with the sustain at index 2, with no mode in it. That is *exactly* the
shape seeding from an ADSR produces and exactly what deleting points lands back
on, so the coupling fired on the states users actually reach. Three consumers
turned it into behaviour:

- dragging a point wrote the four ADSR parameters;
- the next refresh rebuilt the shape from them — the two together are a loop;
- and `anyEnvelopeIsShaped` carried the same test, so a straight four-point
  Breakpoint envelope was **never handed to the voice at all** and played as an
  ADSR holding at its sustain.

Shape questions and semantic questions are now different questions, split at the
source rather than at each call site.

### AMP ENV is an ADSR and nothing else

No TYPE menu, and its four knobs span the whole row. A breakpoint envelope is a
one-shot — it plays its trajectory and the voice retires at the end, whatever the
key is doing — which is a modulation shape. As an *amplitude* envelope it means a
note whose length the keyboard does not control.

Enforced in the processor rather than the editor, in the place that matters:
state restore never goes through the UI's door, so the rule lives at the store.

### Also in the envelopes

- **The ADSR knobs are disabled in Breakpoint mode, not hidden.** A control that
  vanishes says nothing about why. Two independent things gate them — the card's
  bypass and the mode — and both are computed together in one place, because the
  refresh runs on a timer and would otherwise undo the mode state within a frame.
- **Three points is the floor**, for a Breakpoint reason: the first and last are
  anchored at silence, so at two there is nothing to drag and nothing the shape
  can say. Reduce below it and a point is put back at the middle of the line, on
  the line, so the sound does not change.
- **A 10 ms minimum duration.** A one-shot with no length plays nothing and
  retires, so an envelope dragged flat in time was a silent note that looked
  like a very short one.
- The ADSR knobs clear the TYPE selector by 16 px rather than sitting against it.

---

## Fixed

- **A Macro assigned to the AMP ENV knobs did not reach the sound.**
- **Envelope mode state leaked between the two modes**: returning to ADSR could
  derive four values from an arbitrary drawing instead of restoring what was
  stored, and the ADSR put aside behind a live drawing lost its curves across a
  save.
- **A second ADSR editor was still in the envelope card** — 534 lines painting an
  A / D-S / R handle path and dragging it into the parameters. It was dead twice
  over, and neither reason was visible from the call sites: the drawing half sat
  after an unconditional `return`, and the interaction half could never pick a
  handle because `log(seconds / minValue)` with the parameter floors removed is
  NaN. Deleted.
- **The graph showed a sustain region in Breakpoint mode** — a shaded band
  meaning "the envelope holds here", which is exactly what a trajectory never
  does.
- **Seeding Breakpoint mode ignored the ADSR parameters**, handing the editor a
  default-timed skeleton instead of the envelope you had set up.

## Engineering

- **Debug-only work came off the audio thread.** `processBlock` computed four
  modulation readouts every block that only the debug panel reads, and threw
  them away in a shipping build. CPU is unchanged within the benchmark's own
  run-to-run variance — this is about not running instrumentation in the audio
  callback, not a speed-up, and the numbers say so.
- **20 placeholder config keys removed.** `EnvelopeComponent::paint` read eight
  `visual.graph` keys for a graph it no longer draws and discarded all eight;
  four of them shipped in `UIConfig.json` and changed nothing when edited. A test
  now fails if a graph key appears that nothing reads.
- `ModPanel::refreshLfoFromParameters` took three arguments it ignored while
  reading the same values itself.
- The per-block LFO advance was a loop discarding its result through
  `ignoreUnused`, which reads like dead code and is the opposite — it is what
  makes the LFOs run. It is `advanceLfosForBlock` now.

## Developer tools

`PRESET / STATE TOOLS` in the debug console gains **Author** and **Category**
fields, and `DUMP PRESET` stays disabled until the name and author are both
filled in. The category comes from the library itself rather than a list written
in the panel, and a dump no longer inherits whatever preset happened to be
loaded.

## Documentation

`docs/USER_MANUAL.md` gains a SETTINGS chapter and moves the envelope TYPE
documentation to the modulation chapter, AMP ENV having no type to choose.
`docs/envelope-editor-design.md` carries the mode-isolation rule as a permanent
architectural constraint.

---

## Testing

**1197 component tests pass.** `PX3Diag rtsafety` reports 0 allocations per
block, including blocks carrying MIDI. Memory is byte-identical to 0.5.0.

Every test added this cycle was verified to fail against the fault it describes.
That discipline caught **nine** tests of mine that proved nothing, and the
pattern in most of them is the same: the test measured something the defect does
not move. A release test that released where the envelope was loud, so the guard
it existed to check was never reached. A curve test that read its expected value
back through the accessor it was asserting on, comparing 0 with 0. A step
measurement that skipped sample 0 — exactly where a note-on click lives — and
passed with the smoothing removed entirely. A pale-knob test that rendered
through a look-and-feel the test handed in, which says nothing about whether the
strip uses it. A recess test that measured the drilled edge rather than the hole.
And a preset test that could not distinguish a stripped property from one that is
merely ignored on load.

## Known limitations

- **Apple Silicon only.** arm64; no Intel or universal build.
- **No CI.** Everything is measured on one machine.
- **MIDI is not sample-accurate.** A mapped CC reaches its parameter on the next
  UI tick, up to ~33 ms.
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
