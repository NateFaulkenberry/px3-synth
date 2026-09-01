# PX3 v0.5.0

71 commits since v0.4.0. Two new control layers — MIDI Learn and a four-knob
macro strip — a rebuilt envelope editor, and the note-on click that had been
audible on every note since the breakpoint envelopes landed.

---

## New: MIDI Learn

Shift-click one or more knobs, move a hardware control, and it drives all of
them. No CC numbers to type, no dialog.

- **Any parameter knob is eligible**, and eligibility is a property of the
  control rather than a list somebody maintains: 140 of the editor's 159
  sliders carry a parameter ID and are mappable because of it.
- **Multi-select across panels.** Selection survives moving around the UI, so
  one CC can take a cutoff, a resonance and a reverb mix together.
- **Each destination gets a full sweep in its own units** — a cutoff in hertz
  and a resonance in 0–1 both travel their whole range.
- **Shift-click a mapped knob** to clear it and select it for reassignment.
- Mapped knobs show `CC21` under the pointer; selected knobs get a dashed
  amber ring.
- Mappings are saved in **DAW sessions and preset files**, and are unique per
  plugin instance. A preset carrying no mappings leaves yours alone, so
  auditioning factory sounds never costs you your controller setup.

The movement that *teaches* a mapping does not also jump the knobs, and a CC
arriving with nothing selected only drives existing assignments — it never
learns by itself.

## New: four macros

A persistent strip on the left of every panel — OSC, MOD, AMP, FLT, FX, MIX —
holding the same four knobs wherever you are.

- **Cmd-click a macro** to assign, then click knobs to add or remove them.
  Assignment survives switching panels, so one macro can collect an oscillator
  detune, a filter cutoff, a delay mix and a mixer send in one session.
- **Macros are control sources, not aliases.** A macro never becomes a
  parameter's value; it adds to it, the way an LFO does. A cutoff can be moved
  by its own knob, a DAW automation lane, a mapped CC, an LFO, an envelope and
  two macros at once, and the result is the sum of what each asks for.
- **Macros are parameters**, so they are automatable and MIDI-mappable with the
  same gesture as anything else — one hardware knob moving one macro moving a
  dozen parameters.
- Assignments **and** values are saved in presets and sessions, so a patch
  ships with the performance controls it was designed around. A preset load
  does not clear `CC 21 → Macro 1`: the preset says what the macro does, the
  instance says what moves it.
- Teal throughout, against MIDI's amber and modulation's purple, so the three
  are never confused.

## Rebuilt: the envelope editors

AMP ENV and ENV 1–3 were rebuilt from the design document.

- **Three handles for four parameters.** ATTACK and RELEASE are durations,
  pinned to the top and the bottom. DECAY / SUSTAIN is one handle moving in
  both axes, because they are the two coordinates of one point. The separate
  sustain-time bar is gone — its width stood for a duration the model does not
  have.
- **Handles may overlap.** Drag DECAY onto ATTACK and the stage has no length,
  which is what the graph and the DSP then both give you. The attack, decay
  and release parameters reach zero for that reason; they had floors of 1, 5
  and 10 ms that quietly handed back length the graph was not showing.
- **ATTACK, DECAY, SUSTAIN and RELEASE knobs** under each graph, bound both
  ways: a drag moves the knobs, a knob moves the curve, and turning a knob does
  not straighten a bend you drew.
- **A progress fill** shows how far the playing note has taken the envelope. It
  follows the drawn shape exactly, bends included, because the fill and the
  curve come from one sampler. It reads the voice's own position rather than a
  UI clock, so it cannot drift.
- The first and last points are anchored at silence, and on the ADSR skeleton
  the peak is anchored at the top.

---

## Fixed

- **A click on every note-on.** Eight rounds of investigation, and never
  reproducible offline: `SynthVoice::startNote` rebuilt the envelope from the
  ADSR parameters, clobbering the shape the user had drawn. Every offline test
  set the parameters directly, so parameters and shape agreed and the bug
  needed them to disagree — which only dragging the curve produces. Found with
  an in-process onset capture reading a 12 ms attack while the drawn envelope
  said 4 s.
- **A lock on the audio thread.** The on-screen keyboard's MIDI queue took a
  `CriticalSection`; it is a lock-free ring now.
- **An allocation at note-on.** `MidiBuffer` grows its storage on insertion, so
  every block carrying MIDI called the allocator on the audio thread — measured
  at 1.2 allocations per block, and invisible because the real-time check only
  measured blocks with an empty buffer.
- **A retriggered attack restarted from silence** instead of from where the
  envelope actually was.
- **Envelope shapes floated off the baseline.** Free-form editing unlocked the
  end points' levels; removing the added point put the ADSR skeleton back
  carrying levels it could never have been given directly. On screen the curve
  had come away from the bottom of its own graph; in the DSP it was a step at
  note-on and a note that never reached silence.
- **Handles drawn away from the point they mark.** Coincident controls were
  nudged apart on the time axis alone, which called the anchor and the ATTACK
  peak crowded on a short attack — they share a column and sit at opposite ends
  of the graph — and pushed the handle a full spacing off its corner.
- **A bypassed envelope's graph reset.** The graph drew what the *voice* runs,
  and a bypassed modulation envelope runs a neutral contour that gets out of the
  way. Bypass is a mute, not an edit.
- **Clicking the graph switched the card off** after the knob row moved the
  graph off the last cardInner row.
- **The mod panel could not fit its own cards.** The LFO row took half the
  panel and the ENV row then took its minimum, adding up to 50 px more than the
  content — so the envelope cards ran off the bottom with nothing to scroll to.
- **Keyboard messages were cut off.** The banner sized its box to the one fixed
  warning it was built for and then drew whatever string it was given.
- **Modulation flattened at the ends of a range.** Every source is scaled to
  the headroom its base value leaves, so full amount arrives at the boundary
  and turns around instead of being clamped — measured at 65.6% of every cycle
  pinned at an end before the fix.
- **Wavetable display sharpness**: inverted draw order and depth cueing were
  fading near frames to invisibility and painting far ones over them.

## Tooling

- `PX3Diag onsethunt` / `attackpop` / `hollowsiren` diagnostics for the note-on
  investigation, and an in-process onset capture that costs nothing when off.
- The release script signs the installer with a Developer ID Installer
  certificate rather than submitting it unsigned, discovers that identity
  itself, and refuses to submit an unsigned package. The notary preflight no
  longer misreports a working profile as revoked.
- Installer and uninstaller ship as one archive.
- **`PX3Diag regress` fixed**, not worked around: eleven `tail-overfiltered`
  failures were a metric counting silence. It measured how much of a release
  the tail lowpass was fully engaged over, as a fraction of all key-up samples —
  a fixed slice of release *progress*, so a large fraction of a 10 ms release
  and a negligible one of a 4 s release. The loudest sample it was engaged over
  sat at −55 dBFS. Gated at −60 dBFS the same releases score 3.9%, and the
  reproduced fault still scores 41% at −26 dBFS.

## Documentation

- **`docs/USER_MANUAL.md`** — the whole instrument for a musician: every panel,
  all twenty oscillator modes and what PARAM A/B/C do in each, the effects,
  macros, MIDI Learn, and a troubleshooting section.
- `docs/midi-mapping-design.md` and `docs/macro-system-design.md`, each with the
  repository research the design was drawn from and the decisions it forced.

---

## Testing

1055 component tests pass, and `PX3Diag rtsafety` reports 0 allocations per
block on the audio thread — including blocks carrying MIDI, which is where the
allocation fixed below was hiding.

Every test added this cycle was verified to fail against the fault it
describes. That found four tests of mine that proved nothing and had to be
rewritten: a padding test that moved three config keys at once, a highlight
test measuring a band the knob's own ring bled into, a "slots clear when
silent" check that sampled before anything had been written, and a banner test
that recomputed the drawing's arithmetic and so reproduced the very bug it
existed to catch.

---

## Known limitations

- **Apple Silicon only.** arm64; no Intel or universal build.
- **No CI.** Everything is measured on one machine.
- **MIDI is not sample-accurate.** A mapped CC reaches its parameter on the next
  UI tick, up to ~33 ms. This is a control layer, not a modulation path.
- **Mappings are per CC number, not per device.** A plugin receives every MIDI
  device merged into one stream; JUCE exposes no per-message device.
- **Macro depth is fixed** at full positive. The stored format carries a
  per-destination depth, so an editor for it can be added without breaking
  presets; bipolar mapping is expressible in the accumulator but not exposed.
- **One `PX3Diag regress` case still fails**, `N sine key-release, sustain=0`,
  at a note-off transient ratio of 7.7 against a threshold of 6.0. It predates
  this work — the identical failing set was measured at `699aa8c`. It is not
  the envelope (traced: sustain 0 held past decay moves by exactly zero across
  note-off), not an FX tail, and not silence being scored at −29 dBFS. Its
  absolute second difference matches the passing cases; only its local
  curvature baseline is lower. Unresolved, and the threshold has not been moved
  to hide it.
- **Oscillator aliasing** in the non-wavetable modes is unchanged.
- **Worst-case CPU at extreme polyphony** is unchanged from 0.4.0.
- **EQ spectrum visualiser** items from its design brief remain open.
