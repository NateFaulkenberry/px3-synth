# Envelope editor: ADSR and Breakpoint modes

## The problem

The editor has one representation and two behaviours, and it switches between
them without being asked. A four-point shape is treated as an ADSR: the knobs
write the parameters, the parameters drive the points' times and level, and the
stored shape contributes only its curves. Add a point and the shape silently
becomes authoritative — the knobs stop being written and stop meaning anything,
while still sitting under the graph showing whatever they last held.

Nothing tells the user this has happened. The knobs do not grey out, do not
move, and do not say they have been retired. They are simply wrong from that
moment on.

The fix is to make the two behaviours **two modes the user chooses**, so which
one is active is visible, deliberate, and saved.

---

## Phase 1 findings — what already exists

**The canonical representation is already there.** `px3::BreakpointEnvelope` is
up to 16 points, each `{timeSeconds, value, curveToNext}`, plus a `sustainPoint`
index. Both envelope kinds consume it: `AmpEnvelope::setEnvelope` and
`EnvelopeGenerator::setEnvelope` take one. `setSettings(EnvelopeSettings)` is
the same call with `fromAdsr` in front of it.

**The curve maths is already shared.** `BreakpointEnvelope::shape(x, curve)` is
a static function implementing a rational (Möbius) curve,
`y = x / (x + r(1−x))` with `r = exp(−3c)` — monotone and bounded by
construction, symmetric, exactly linear at zero.

The editor's path builder calls it. `BreakpointEnvelope::valueAt` calls it. And
`Snapshot::evaluate`, which is what the audio thread runs per sample, calls it.

That answers §9 before it is asked: there is no second approximation to unify.
The graph and the DSP are drawing the same function from the same coefficient.

**The DSP already follows the whole shape.** `Snapshot::rebuild` flattens the
envelope into fixed-size segments carrying `startTime`, `invDuration`,
`startValue`, `valueSpan` and `curve`. The reciprocal is taken once at rebuild
rather than as a divide per sample. Zero-length segments are marked by
`invDuration == 0` and skipped, so a segment of no duration is an instant jump
in the DSP exactly as it is in the model.

So §8 is largely satisfied by construction. What it lacks is proof: there are
no tests comparing DSP output against the mathematical definition across a
range of shapes. This work adds them.

**Serialization** writes a `envelopeShapes` child at version 3, holding one node
per envelope with its sustain index and every point's time, value and curve.
Envelopes that are a plain ADSR are skipped entirely — they are reconstructed
from the four parameters on load.

**The implicit rule today**, stated plainly: `isAdsrSkeleton()` (four points,
sustain at index 2) means the parameters own times and level while the shape
owns curves; anything else means the shape owns everything.

---

## The two modes

### ADSR

Exactly four points: the anchor at silence, the peak, the sustain point, and the
end at silence. Three draggable handles, because the decay time and the sustain
level are the two coordinates of one point.

- The four parameters own the times and the sustain level.
- The stored shape owns the three curves.
- Points cannot be added or removed. Double-clicking empty space does nothing;
  double-clicking a point does nothing.
- The four knobs are shown, and they and the graph are two views of one thing.

Curves are fully available. An ADSR-mode envelope can be far from a straight-line
ADSR — every segment bends — but its **topology** is fixed at four points.

### Breakpoint

Up to 16 points, any of which may be moved in time and level.

- The shape owns everything. The parameters are not written and not read.
- Points can be added and removed, subject to the structural rules below.
- The four knobs are **hidden**. They no longer describe the envelope, so
  showing them would be the original defect wearing a different hat.

Entering Breakpoint mode starts from exactly the shape ADSR mode was showing,
curves included.

---

## Canonical representation

```
BreakpointEnvelope
  ├── mode          adsr | breakpoint
  ├── points[0..15] { timeSeconds, value, curveToNext }
  └── sustainPoint  index into points
```

One structure, one source of truth, both modes. ADSR mode is that structure with
a topology constraint applied; Breakpoint mode is the same structure without it.

The mode lives on the envelope rather than beside it, so it travels with the
shape through every path that already carries one — the editor, the processor's
slot, serialization, and mode switching — without a second thing to keep in step.

## Structural points

| Point | Rule | Reason |
|---|---|---|
| First | Pinned at time 0, value 0. Never removable. | The note-on instant. An envelope that starts above zero is a step. |
| Peak (ADSR mode only) | Pinned at value 1. | ATTACK is a duration; its handle moves in time along the top. |
| Sustain | Never removable. | The envelope holds here; without it there is nothing to release from. |
| Last | Value pinned at 0. Never removable. | Silence after the release. |

`anchorStructuralPoints()` enforces these on every path into the model —
during a drag, and on adds, removes and anything restored from saved state.

In Breakpoint mode the peak is **not** pinned: point 1 is an ordinary point, so a
drawn envelope can rise to any level.

## Segments, curves and zero length

A segment runs between consecutive points. Its value at normalised position
`x ∈ [0,1]` is

```
value = a.value + (b.value − a.value) × shape(x, a.curveToNext)
```

with `shape` the shared function described above. `curveToNext` is 0 for a
straight line; positive and negative bend in opposite directions by equal
amounts.

**Zero-length segments are legal and deterministic.** When two points share a
time, the segment between them has no duration and is an instant jump to the
later point's value. Both the model (`valueAt` returns `b.value`) and the DSP
(`invDuration == 0`, skipped) implement this the same way.

**Points may share a time.** `setPoint` holds a point between its neighbours
rather than re-sorting, so dragging one onto another produces coincident points
rather than renumbering everything under the cursor. Dragging further does not
cross: the point stops.

**Ordering is by index, not by time.** Two points at the same time keep their
order, so the envelope's traversal is always defined.

## The ADSR assumptions found inside Breakpoint mode

Traced in the code before anything was changed. Breakpoint mode was not an
arbitrary time/level envelope; it was the ADSR machinery with the point count
relaxed.

| Where | The assumption |
|---|---|
| `BreakpointEnvelope` | `sustainPoint` is an intrinsic index, and `canRemovePoint` protects it |
| `Snapshot` | Split in two at `sustainSegment`, with `sustainSeconds`, `sustainValue`, `releaseSeconds` |
| `AmpEnvelope` / `EnvelopeGenerator` | `heldSeconds` **stops advancing** at the sustain point; note-off flips `inRelease` and starts a *second* clock, `releasedSeconds` |

In the one-shot path the single clock keeps running and `inRelease` is never
consulted, because the one-shot branch is taken first. That, not the guard in
`noteOff`, is what keeps a released note travelling — mutating the guard away
changes no observable behaviour in `EnvelopeGenerator`. The guard stays so that
no release state is written which nothing will read; a stale anchor sitting
behind a live `inRelease` flag is the kind of thing that returns once something
else moves. In `AmpEnvelope` the guard *is* load-bearing: without it, a
note-off taken where the shape passes through zero reads as "nothing left to
release" and retires the voice, so a double-strike envelope released in the
silent gap would never sound its second peak.
| `AmpEnvelope` | The release is reshaped to constant dB/second unless the user bent it |
| Editor | `roleLabelFor` returns ATTACK / DECAY / SUSTAIN / RELEASE |
| Fill animation | `progressDisplayTime()` is built from those two clocks, not from elapsed time |

The consequence: a six-point breakpoint envelope did not play its trajectory. It
played points 0–2, froze at point 2 for as long as the key was held, and then
played points 2–5 on a different clock with a different curve law. The graph
showed a shape the DSP never traversed.

## Two models, stated

**ADSR** is a semantic four-stage envelope. The user edits attack time, decay
time, sustain level and release time; the points are a *rendering* of those four
parameters. The key gates it: it holds at sustain until note-off.

**Breakpoint** is `level = f(time)`. An ordered sequence of points, each with a
time, a level and a curve to the next. There is no attack, no decay, no sustain
and no release — those are ADSR's vocabulary. The key **triggers** it; it does
not gate it.

## Breakpoint note lifecycle

One clock. `t` starts at zero on note-on and advances while the voice lives.
`level = f(t)`. That is the whole model, and every question the brief asks falls
out of it rather than needing a rule of its own:

| Situation | Behaviour |
|---|---|
| The envelope reaches its final point while the key is held | It is finished. The last point is anchored at zero, so the voice reaches silence and retires. Holding longer does not extend it. |
| The key is released before the envelope finishes | The trajectory continues to its end. Breakpoint is one-shot: the key triggers, it does not gate. |
| Retrigger before completion | `t` resets to zero and the envelope starts again **from the level it had reached**, using the same anchor that keeps an ADSR retrigger from clicking. |
| Legato | Nothing special. Each note gets its own voice; there is no per-voice glide to interact with. |

The last point stays anchored at zero in Breakpoint mode, and that anchor is
what makes the model safe: `f(t)` always ends at silence, so a voice always
retires and no "release" concept is needed to stop it.

> This is a deliberate choice, and it is the one that follows from
> `level = f(time)`. A gated breakpoint envelope would need a point to hold at —
> which is a sustain point, which is the ADSR model wearing different words.

## ADSR sustain and release

The **sustain point** is one index into the points. Everything before it is
traversed while the key is held; everything after it is the release.

- **Note-on** starts at point 0 and advances in real time.
- On reaching the sustain point, the envelope **holds at that point's value**
  for as long as the key is down. Its time coordinate is where holding begins.
- **Note-off** begins the release from wherever the envelope currently is —
  which may be mid-attack if the key was short — and traverses the segments
  after the sustain point.
- **The release may contain several segments.** Nothing restricts the sustain
  point to being second-to-last; a breakpoint envelope can have a multi-stage
  release.

In ADSR mode the sustain point is index 2, so there is exactly one release
segment. In Breakpoint mode it can be any index from 1 to `pointCount − 2`.

## Mode switching

The two modes keep **separate state**. Switching is a change of which one is
active, never a conversion of one into the other.

```
Envelope slot
  ├── mode              adsr | breakpoint
  ├── adsr shape        four points + curves, driven by the four parameters
  └── breakpoint shape  arbitrary points, initialised once
```

**ADSR → Breakpoint, the first time**, seeds the breakpoint shape from the
current ADSR so the user starts from something familiar rather than an unrelated
default. The seeded points carry no ADSR identity — they are simply where the
first arrangement came from.

**ADSR → Breakpoint, afterwards**, restores the breakpoint shape as the user
last left it. Seeding happens once, tracked by an explicit
`breakpointInitialised` flag; it is not re-derived on every switch, or a user's
work would be overwritten every time they looked at the other mode.

**Breakpoint → ADSR** restores the stored ADSR parameters unchanged. No attempt
is made to derive four values from an arbitrary shape: that would be lossy,
unpredictable, and would quietly rewrite settings the user did not touch.

Editing in one mode never mutates the other. Repeated switching is lossless in
both directions.

## Parameter synchronisation

**In ADSR mode** the four parameters and the four points are the same numbers.
A knob writes its parameter; the card applies the parameters to the stored
shape's times and level while keeping its curves. A drag writes the shape and
the card writes the four values back. Neither can go stale because neither is a
copy — they are two views resolved on every refresh.

**In Breakpoint mode** the parameters are not written and not read. They keep
whatever they held, and their knobs are hidden. Switching back to ADSR mode
restores the parameters from the reduced shape, so they are correct again the
moment they are visible again.

This is what removes the original defect. Stale knobs were possible only because
the knobs stayed on screen after they stopped meaning anything.

## Mode State Isolation

**ADSR and Breakpoint are two independent envelope representations sharing one
editor and one component. The active mode selects the representation. Point
count never determines semantic mode.**

This is a permanent architectural rule. A Breakpoint envelope with two points is
a Breakpoint envelope. So is one with three, four, or sixteen. There is no count
at which ADSR meaning returns.

### What is shared, and what is not

| Shared infrastructure | ADSR-only | Breakpoint-only |
|---|---|---|
| the active mode | attack, decay, sustain, release **parameters** | the point list: time, level, curve per point |
| graph geometry and the row it lives in | the four knobs and their readouts | arbitrary point count, 2..16 |
| mouse framework, hit testing, selection, drag | stage roles (ATTACK / DECAY / SUSTAIN / RELEASE labels) | elapsed-time playback position |
| curve rendering and `shape()` | the sustain region and its marker colour | one-shot lifecycle: no hold, no separate release clock |
| the fill renderer | the peak pinned to 1.0 | |
| serialization framework | the sustain index as a *stage* boundary | |
| `Snapshot` and the DSP evaluator | | |

The **sustain index** is the subtle one. In ADSR mode it is a stage boundary: the
envelope holds there. In Breakpoint mode it is bookkeeping — the model keeps the
field so `Snapshot` has one shape to build from, but nothing in Breakpoint mode
may read it as meaning. It must not protect a point from deletion, must not be
drawn, and must not be consulted by the evaluator, which takes the one-shot path
and ignores it.

### The coupling that was found, and how it behaved

`isAdsrSkeleton()` was `pointCount == 4 && sustainPoint == 2` — a pure shape
test with no mode in it. That is *exactly* the shape seeding from an ADSR
produces, and exactly the shape deleting points lands back on, so the coupling
fired on the states users actually reach rather than on some corner. Three
consumers turned it into behaviour:

1. **The edit write-back** (`AmpEnvelopeComponent`, `ModPanel`): dragging a point
   wrote `ampAttack` / `ampDecay` / `ampSustain` / `ampRelease` from the drawn
   points.
2. **The refresh** (same two files): the stored shape was rebuilt from those
   parameters, discarding the drawn times and levels.

   Together these are a loop. Drag a point in Breakpoint mode; the drag writes
   the ADSR parameters; the next refresh rebuilds the shape from them. The
   editor becomes a partially working ADSR editor, which is the reported
   symptom.
3. **The DSP gate**: `anyEnvelopeIsShaped` is `! isPlainAdsr()`, and
   `isPlainAdsr()` carried the same count test. A straight four-point Breakpoint
   envelope reported itself a plain ADSR, so `setAmpEnvelopeShape` was never
   called and the voice ran the ADSR path — holding at the sustain point. The
   shape was not merely mis-drawn; it was not played.

A fourth site applied ADSR constraints to Breakpoint editing: `canRemovePoint`
protected the sustain index and floored the count at `kMinPoints + 1`, so
deletion stopped at three points and could not reach two.

### The rule as code

Shape questions and semantic questions are now different questions:

- `hasAdsrSkeletonShape()` — pure geometry, four points with the sustain at
  index 2. Used **only** by `impliedModeFor`, which infers a mode for states
  saved before modes existed. That is the one legitimate place a count may
  suggest a mode, and it runs only when no mode was recorded.
- `isAdsrSkeleton()` and `isPlainAdsr()` — semantic, and therefore
  `mode == Mode::adsr &&` the geometry. Every consumer above asks a semantic
  question, so every consumer is now mode-gated at the source rather than at
  each call site, where one missed site reintroduces the bug.

`toAdsr()` stays a pure converter: it is named for what it does, and its only
production caller is already behind `isAdsrSkeleton()`.

### Minimum points is a Breakpoint rule

Two, because a function of time needs a start and an end. Not because an ADSR
editor needs four. Deleting reaches two, and two is a valid Breakpoint envelope
in every sense the mode rule cares about: it stays in Breakpoint mode, plays on
the one-shot clock, fills by elapsed time, and persists exactly.

It is however **flat**, and that is worth stating plainly rather than leaving to
be discovered. The first and last points are anchored at silence in both modes —
an envelope begins and ends at rest, or a note clicks on and never reaches
silence — so at two points *both* points are the anchored ends and there is
nothing left to shape. **Three** is the smallest count that can carry a shape.

This is an anchoring rule, not ADSR meaning returning at a low count: a
two-point envelope is still a Breakpoint envelope throughout, and the tests
show it. If the anchoring should be relaxed — a modulation envelope that ends
at a non-zero level is musically reasonable in a way an amplitude one is not —
that is a separate decision about anchoring, and it would want to distinguish
AMP ENV from ENV 1-3 rather than change what a mode means.

### Seeding reads the parameters, not the stored shape

In ADSR mode the four values live in the **parameters** and the stored shape
carries only the curves. Seeding Breakpoint from the raw stored shape therefore
handed the editor a default-timed skeleton and discarded the envelope the user
had set up. The seed applies the parameters first, so it starts from the ADSR
the user can see — which is what "initialise from the current ADSR shape" has to
mean in an architecture where the shape is not where those numbers live.

### The card's second ADSR editor, removed

`EnvelopeComponent` carried an older ADSR editor of its own, predating
`BreakpointEnvelopeEditor`: an A / D-S / R handle path it painted in `paint()`,
with `mouseDown`/`mouseDrag`/`mouseDoubleClick` writing those handles straight
into the four parameters. None of it was mode-gated, and it has been deleted —
534 lines, along with `DragHandle`, `pickHandle`, the two hit-test helpers, the
handle drawing and readout, the log-scaled time↔pixel mapping and the drag
application. `Geometry` is now just the graph's rectangle, which is all
`graphBounds()` ever wanted from it.

It was dead, in two independent ways, and the reasons are worth recording
because both were invisible from the call sites:

1. **The drawing half sat after an unconditional `return;`** in `paint()`, so it
   had not been executed for some time.
2. **The interaction half could never pick a handle**, in either mode:
   `timeToVisualNorm` computes `log(seconds / minValue)`, and the attack, decay
   and release parameters had their floors removed so they can reach zero. With
   `minValue == 0` that is `log(inf) / log(inf)` — NaN. Every distance test in
   `pickHandle` compared against NaN and was false.

So the code was inert by accident rather than by design, and a single
arithmetic change would have woken a second ADSR editor underneath the real one,
in both modes. Deleting it is what actually removes that.

What remains is a regression test that drags thirty times around the graph's
edge and asserts no ADSR parameter moves — verified against a `mouseDown` made
to write one — so the card cannot quietly acquire parameter-writing powers
again.

Removing it also corrected `updateCursorFor`, which asked for
`rowCount() - 1` — the last row, which became the KNOB row when the knobs were
added. The graph therefore showed a pointing-hand cursor claiming a drag that
did not exist, while the knob row, which does toggle bypass on a background
click, showed a plain arrow. It uses `graphRowIndex()` now, matching `mouseUp`.

### Mode switching

Unchanged by this work and restated here because it is part of the same rule:
Breakpoint seeds from the ADSR on the **first** entry only, tracked by an
explicit flag; returning to ADSR restores the stored ADSR and never derives it
from the drawing; returning to Breakpoint restores the drawing and never
re-seeds. A two-point drawing comes back as two points.

## Persistence and migration

The `envelopeShapes` node carries, per envelope: the active `mode`, the active
shape's points and curves, and a `retained` child holding the shape of the mode
that is **not** active, tagged with which mode that is. The version is **4**.
Whether the breakpoint shape has been initialised is carried by the presence of
a breakpoint-tagged shape — active or retained — rather than inferred from the
points, because a default `BreakpointEnvelope` is itself a valid four-point
ADSR and so cannot be told apart from an untouched one by inspection.

The retained child has to be symmetric, and the first implementation was not:
it always held the breakpoint shape. Saving while a drawing was live therefore
wrote a second copy of the active shape and left the stored ADSR's curves with
nowhere to go, so they reloaded straight. A `retained` child with no mode tag
is a breakpoint shape, which is all it used to hold.

**Both modes' state is saved**, not only the active one. A preset saved in ADSR
mode with a seven-point breakpoint envelope behind it reopens with both, and
switching reveals exactly what was stored.

**Migration from version 3 and earlier**, which is every preset and project that
exists today: an envelope with no recorded mode takes the mode its shape
implies — four points holding at index 2 means ADSR, anything else means
Breakpoint.

That is exactly what the implicit rule did, so **every existing preset loads
with the behaviour it had before**, now stated rather than inferred. A preset
whose envelope was a plain ADSR is still skipped by the writer and rebuilt from
the parameters; it loads in ADSR mode.

## What Breakpoint mode does not draw

The **sustain region** — the shaded band from the sustain point to the right
edge — is ADSR-only. It means "the envelope holds here for as long as the key
is down", which is exactly what a breakpoint envelope never does; drawn there it
describes a stage the DSP does not have. The sustain point also loses its
distinct marker colour in Breakpoint mode.

The model still keeps a sustain index in Breakpoint mode, because that point is
structural and may not be deleted. That is bookkeeping, and bookkeeping is not
something to put on screen.

Role labels are likewise blank: a breakpoint has a time, a level and a curve,
and calling one of them RELEASE would name a model it does not have.

## Fill animation

**ADSR mode** keeps its stage-aware progress: held time clamped at the sustain
point, then release time from there. That works and is left alone.

**Breakpoint mode** is driven by elapsed time and nothing else. The fill's right
edge is at `t`, found by the same `buildCurvePath` walk that draws the curve —
so it stops on the curve, part way through a curved segment if that is where `t`
falls, rather than at a point boundary or a percentage of the width. Crossing a
breakpoint is not an event; it is just a value of `t` like any other.

Both are fed from the DSP's own position, published once per block into
atomics — never a UI clock counting alongside the audio.

## Thread safety and real-time behaviour

Unchanged, and deliberately so. The editable envelope is a message-thread
object. It is flattened into `Snapshot` — fixed-size, no allocation, reciprocals
precomputed — and the audio thread reads only that. Adding a mode adds one enum
to the editable side and nothing at all to the snapshot: the DSP does not need
to know which mode produced the points it is playing.

No allocation, no locks, no per-sample rebuilding.

## Shared by both envelope kinds

AMP ENV and ENV 1–3 use the same model, the same editor component, the same
serialization and the same DSP evaluation. They differ only in what consumes
the result: AMP ENV drives the amplitude of every voice; ENV 1–3 are assignable
modulation sources.

The four slots are separate `BreakpointEnvelope` values in one array, with
separate parameters and separate mode flags. Nothing is shared between
instances.

---

## Testing strategy

**Model** — ADSR mode rejects added and removed points; Breakpoint mode allows
both up to 16 and refuses a 17th; structural points resist removal in both;
curves survive every operation.

**DSP shape** — the audio-thread `Snapshot` is compared against the model's own
`valueAt` across a dense sweep, for linear segments, curved segments in both
directions, very short segments, zero-length segments, coincident points and a
multi-point envelope. This is the test the brief asks for and the one that was
missing: not "audio came out", but "the DSP traverses the shape that was drawn".

**Sustain and release** — note-off at several positions, including mid-attack;
a multi-segment release; holding at the sustain point.

**Mode switching** — ADSR → Breakpoint preserves the shape; Breakpoint → ADSR
reduces predictably and retains the original; switching back restores it
exactly, including after a save and reload.

**Persistence** — mode, points, curves and the retained shape round-trip through
both a session and a preset; a version-3 state migrates to the mode its shape
implies.

**Independence** — each of the four slots keeps its own mode, shape and
parameters.

Every test is verified to fail against the fault it describes.
