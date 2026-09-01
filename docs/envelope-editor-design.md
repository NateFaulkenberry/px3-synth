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

## Sustain and release

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

**ADSR → Breakpoint** keeps the shape exactly: the same four points, the same
curves. The user can then add points.

**Breakpoint → ADSR** cannot keep an arbitrary shape — four points cannot
express sixteen. Rather than choose between destroying the work and refusing the
switch, the envelope does both things that matter:

1. The full breakpoint shape is **retained**, stored alongside the active one.
2. The active shape is reduced to a four-point ADSR that preserves what can be
   preserved: the attack reaches the first peak, the sustain takes the sustain
   point's time and level, the release ends where the envelope ended, and the
   three curves are taken from the corresponding segments.

Switching back to Breakpoint restores the retained shape exactly. The reduction
is never destructive, because the original is still there.

The retained shape is serialized, so the round trip survives saving and
reloading.

> This is §12's "store the advanced representation and restore it if the user
> switches back", combined with its "reduce to an ADSR-compatible
> representation" — the two are not alternatives, and doing both costs one extra
> stored shape per envelope.

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

## Persistence and migration

The `envelopeShapes` node gains a `mode` property per envelope and an optional
retained-shape child. The version goes to **4**.

**Migration from version 3 and earlier**, which is every preset and project that
exists today: an envelope with no recorded mode takes the mode its shape
implies — four points holding at index 2 means ADSR, anything else means
Breakpoint.

That is exactly what the implicit rule did, so **every existing preset loads
with the behaviour it had before**, now stated rather than inferred. A preset
whose envelope was a plain ADSR is still skipped by the writer and rebuilt from
the parameters; it loads in ADSR mode.

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
