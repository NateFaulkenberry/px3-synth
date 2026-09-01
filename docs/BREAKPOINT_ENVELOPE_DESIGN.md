# Breakpoint envelope — research and architecture

Design document for replacing the ADSR envelopes with a breakpoint model.
Written before implementation, as the brief requires.

---

## A. Existing architecture

| Concern | Where |
|---|---|
| Settings | `EnvelopeSettings { attackSeconds, decaySeconds, sustainLevel, releaseSeconds }` — `Source/DSP/EnvelopeTypes.h`, 11 lines |
| Generic ENV 1/2/3 | `EnvelopeGenerator` — a `juce::ADSR` plus a linear output smoother |
| AMP ENV | `AmpEnvelope` — a `juce::ADSR`, a **one-pole** output smoother, an exponential release reshaping, and `getReleaseProgress()` |
| Settings flow | `currentAmpEnvelopeSettings()` / `currentModEnvelopeSettings(i)` → `voice->setAmpEnvelopeSettings(...)` once per block |
| Parameters | `ampAttack/Decay/Sustain/Release`, `env{1,2,3}Attack/...` — real `AudioParameterFloat`s, so a DAW may already be automating them |
| UI | `EnvelopeComponent` (33 KB) takes the four parameters by reference and drags three fixed handles |
| Modulation | `buildLfoAssignableTargets()` excludes `ampAttack…` and `env1Attack…` from being *destinations*; envelopes are *sources* via `modulationEnvelopeValues[]` |

Two properties of the existing code are load-bearing and must survive:

**`AmpEnvelope::getReleaseProgress()`** is what release-dependent processing
schedules off, deliberately, so its timing does not move when the envelope curve
changes. Anything that replaces the amp envelope has to keep producing it.

**AMP ENV and ENV 1/2/3 are separate classes on purpose** (§20). They share a
settings struct and nothing else. The new engine is shared as an immutable
utility; the two owners stay distinct.

---

## B. Curve representation — measured, not assumed

§12 says explicitly not to assume Bezier is superior. It is not. Prototype in
`docs/research/envelope-curve-prototype.cpp`:

```
  curve        finite  monotone  bounded   symmetry err    ns/sample
  power           yes       yes      yes       4.44e-16        18.17
  rational        yes       yes      yes       1.78e-15         4.92
  bezier          yes       yes      yes       2.99e-01       128.59
```

**Cubic Bezier is rejected on cost.** Constraining it to a single-valued
function of x (coincident control points, the standard trick) leaves x as a
*cubic in t*, so evaluating y at a given x needs that cubic inverted — per
sample, per voice. Measured at 128.59 ns/sample against 4.92 for the rational
curve: **26x**. At 64 voices and 48 kHz that is the difference between 15 ms and
0.6 ms of CPU per second of audio, for a curve shape nobody can distinguish by
ear. The asymmetry in the table is a property of the parameterisation and could
be fixed; the root-finding cost cannot.

**Power curves (`y = x^p`) are rejected on cost and on degeneracy.** 18.17
ns/sample, and the received wisdom that they produce NaN turns out to be wrong
in double — what they actually do is worse-behaved than that. Widening the
exponent range:

```
    range            finite   monotone   worst step
    1/8                 yes        yes       0.3546
    1/64                yes        yes       0.8785
    1/512               yes        yes       0.9839
    1/65536             yes        yes       1.0000
```

Everything stays finite and monotone, and the curve quietly becomes a **step
function** — a full-scale jump between adjacent samples, which is a click. The
range has to be limited by hand, and the limit is a tuning constant nobody can
justify.

**Chosen: the rational (Möbius) curve.**

```
    y = x / (x + r(1 - x))        r = exp(-k * c),  c in [-1, 1],  k = 3
```

- **Monotone and bounded by construction** for any r > 0. y(0) = 0 and y(1) = 1
  exactly, with no clamping and no special cases.
- **Cannot degenerate.** r is bounded to [1/20, 20] by the exponential map, so
  there is no setting at which the curve becomes a step. The power curve needs a
  hand-picked range to get the same guarantee; this one gets it from its shape.
- **Symmetric to 1.8e-15.** Bending up by c and down by c are exact mirror
  images through the diagonal, so "+50%" and "−50%" are the same amount of bend.
  That is what makes a single normalised control feel right (§14).
- **Three flops and one divide.** 4.92 ns/sample.
- **c = 0 is exactly linear** (r = 1 gives y = x), so a segment with no curve
  costs nothing conceptually and needs no separate code path.

The UI draws this same function sampled into a `juce::Path`, so there is one
curve, not a drawn one and an evaluated one (§16, §58).

---

## C. Data model

```
BreakpointEnvelope
    points[]            2..16
        timeSeconds     >= 0, non-decreasing
        value           0..1
        curveToNext     -1..+1
    sustainPoint        index of the point the envelope holds at
```

ADSR is a special case, not a separate concept:

```
    point 0   t=0                       v=0
    point 1   t=attack                  v=1        <- peak
    point 2   t=attack+decay            v=sustain  <- sustainPoint
    point 3   t=attack+decay+release    v=0
```

which is exactly what the four existing parameters already describe. Nothing
about the default envelope changes, which is what §22 asks for.

**Time is seconds, not normalised and not pixels.** Normalising would make a
point's meaning depend on the total length, so dragging the last point would
move every other point's real time. Points are stored in seconds and the view
scales to fit.

**Value is 0..1**, matching what the modulation system already consumes: an
envelope's output is summed as a normalised delta onto a destination parameter.

**Sustain is a designated point index**, not a marker between points and not a
time. It is the representation ADSR already implies, it survives points being
inserted before or after it (the index moves with it), and §25's loop points
become two more indices against the same array without touching anything else.

---

## D. Note-off

Release **starts from wherever the envelope actually is**, not from the sustain
point's value. Releasing during the attack of a slow envelope must not jump to
the sustain level first — that is an audible click and it is the single most
common way a multi-stage envelope sounds broken.

Implemented as an offset rather than a rewrite: on note-off the current value is
captured, and the first release segment is evaluated from that value to the next
point's value. Later release segments are untouched. No allocation, no
re-sorting, and the release *duration* is unchanged — which matters because
`getReleaseProgress()` is what other processing schedules against.

---

## E. Audio-thread representation

The editor works with points. The audio thread gets segments with the divisions
already done:

```
    DspSegment
        startValue, valueSpan
        startTime, invDuration        <- reciprocal precomputed
        r                             <- exp(-k * curve), precomputed
```

Evaluation per sample is a segment lookup (a forward walk, since time only moves
forwards within a note), one multiply for the normalised position, and the three
flops of the curve. No `pow`, no `exp`, no root-finding, no branches on curve
type.

Published the way wavetables are: built off the audio thread, handed over as an
immutable snapshot, never mutated in place. A fixed-capacity array rather than a
vector, so the snapshot is copyable into the voice without allocating.

---

## F. Persistence and migration

The four ADSR parameters **stay**, and stay authoritative for the four points
they describe. They are what a DAW automates, and removing them would break
every existing session (§18, §21, §42).

The envelope model is the one representation; the parameters are a projection
onto its first four points. Editing a parameter moves a point; dragging that
point writes the parameter back. Extra points and curve amounts have no
parameter and live in versioned state:

```xml
<envelopes version="2">
  <envelope id="amp" sustain="2">
    <point t="0" v="0" c="0"/>
    ...
  </envelope>
</envelopes>
```

An old preset has no `<envelopes>` node, so it loads as pure ADSR and sounds
exactly as it did. That is the migration: absence is a valid state, not an error
to detect.

---

## G. Answers to §62

1. **Bezier, Hermite, exponential or other?** Rational. Measured 26x cheaper
   than constrained Bezier and 3.7x cheaper than power, symmetric, and unable to
   degenerate into a step.
2. **One handle or two?** One, at the segment's midpoint. Two handles express
   shapes that a single-valued envelope segment cannot have.
3. **How is curvature normalised?** −1..+1, mapped through `exp(-3c)`. Symmetric
   by construction, so equal numbers are equal bends.
4. **Efficient evaluation?** Precomputed reciprocals and r per segment; three
   flops per sample.
5. **How does note-off interrupt?** From the current value, as an offset on the
   first release segment only.
6. **How is sustain represented?** A point index.
7. **How do old ADSR presets migrate?** They do not need to: absence of the new
   node means ADSR, and the parameters still describe it.
8. **How does it serialize?** A versioned child node, points only, parameters
   untouched.
9. **Automation/state restoration?** Unchanged — the same four parameters per
   envelope, not one per breakpoint.
10. **How does curve editing avoid discontinuities?** The curve is C0 at every
    breakpoint by construction (y(0)=0, y(1)=1 exactly). C1 is deliberately not
    forced, because §15 requires sharp corners to remain possible.
11. **Best interaction model?** Drag points; drag the curve between them to bend
    it; double-click to add; right-click or double-click a point to remove.
12. **What to emulate?** Vital's sustain-point-plus-loop-modes structure, which
    is why sustain is an index into the same array rather than a separate stage.

---

## Status

**Shipped in v0.4.0**, on all four envelopes, following the staged path in §57 —
model and DSP first, behind the existing parameters, so audio kept working
throughout.

One thing changed during the build and was then changed back. A HOLD stage was
added between ATTACK and DECAY, and removed again from both envelope types: it
defaulted to zero length, which put a fifth handle exactly on top of the attack
handle, and having two skeletons meant every piece of code touching an envelope
had to know which one it held. The shape is section C's four points, as
designed.

The editor gained a labelled time axis, and an explicit held stretch drawn
after the decay. That stretch is display-only - the envelope holds there for as
long as the key is down, which is not a duration the model has - and it exists
so the SUSTAIN level has a handle of its own. Without it the decay time and the
sustain level are the two coordinates of one breakpoint, which is one control
doing two jobs.

## Which representation is authoritative

This is the rule the whole design turns on, and getting it wrong produced the
worst bug in the project's history - an audible click on every note that took
eight rounds of investigation to find.

    the shape is a plain ADSR   ->  the four parameters are authoritative
    the shape is anything else  ->  the STORED SHAPE is authoritative

The switch happens the moment a curve is bent or a point is added, because at
that instant four numbers can no longer describe the envelope. From then on the
parameters are frozen at whatever they last held - the editor stops writing
them back, deliberately, since writing a lossy projection would destroy the
shape.

Everything that builds an envelope has to honour that. `SynthVoice::startNote`
did not: it rebuilt from the parameters unconditionally, so every note began on
a stale ADSR and was corrected one block later by the next settings push. With
a drawn four second attack and parameters left at twelve milliseconds, each
note opened at 77% level and then collapsed - which is what was heard.

Anything reading an envelope should go through `currentAmpEnvelope()` or
`currentModEnvelope()`, which apply this rule, rather than reading the
parameters directly.

Not implemented: the editor draws the curve but not a marker for where the
envelope currently is. `setLivePosition` existed with no caller and was removed
in the 0.4.0 cleanup rather than left looking wired.

## H. Showing where the playing note has got to

The AMP ENV graph fills the area under the part of the envelope the sounding
note has already traversed. Three decisions carry the design.

**One sampler, not two.** `buildCurvePath` takes an optional stopping time on
the display axis and an optional "close down to the baseline" flag. The curve
is that function with no stopping time; the fill is the same function stopped
part way and closed. The fill's upper edge therefore *is* the curve — including
every bend — rather than a second approximation that could be drawn from the
same numbers and still disagree. The alternative, a fill built from its own
walk of the points, is the kind of duplicate that stays correct until someone
changes one of the two.

At the stopping point the path lands on the exact interpolated value rather
than on the last sample before it. Without that, the fill's edge steps forward
one path sample at a time and visibly stutters against a curve that does not.

**Progress comes from the DSP, not from a clock.** `AmpEnvelope::Position` is a
read-only snapshot — active, in-release, held seconds, released seconds,
sustain time. `SynthVoice` exposes it along with the sequence number its note
started with; the processor picks the newest sounding voice once per block and
stores the five fields into atomics. The editor reads them on the UI timer.
A UI-side timer counting seconds alongside the envelope would be a second
implementation of the envelope's own timing, and would drift from it under load
or a change of buffer size — the same class of mistake as the two-samplers one.

Exposure is strictly read-only: no envelope, voice, or processor behaviour
changed, and the audio thread does five relaxed stores per block.

**The sustain bar is not a duration.** While the note is held the fill advances
in real time up to the sustain point and then stops, however long the key is
down. The held stretch drawn after the decay is display-only — the envelope
holds there for an unbounded time — so creeping across it would be inventing
time the envelope is not spending. The bar fills in at note-off, and the
release runs on from its far edge. That leaves one discontinuity, and it is
placed at note-off, a moment the picture is expected to change because the
player caused it, rather than at the arrival at sustain, where it would be an
unprompted jump of about a fifth of the graph's width.

A note released during the attack is treated the same way: it has left the held
part of the envelope behind, so the fill moves to the release segment.

Eight tests pin this (`AmpProgress_*` in `ComponentTests.cpp`), each verified to
fail against the fault it names: a progress that never advances, one that
resets per stage, a sustain that creeps, a release that restarts at zero, a
fill drawn from a flat approximation instead of the curve, an idle envelope
that still fills, and a retrigger that continues rather than starting over.
