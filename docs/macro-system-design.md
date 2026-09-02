# Macro control system

## Overview

Five knobs on the left of every panel, each able to move any number of
parameters anywhere in the synth. They are a performance layer: always in
reach, configured once, played constantly.

The design goal that decides everything else is that a Macro is a **control
source**, not an alias. It never becomes a parameter's value; it adds to it,
the same way an LFO does. That is what lets a cutoff be driven by its own knob,
a DAW automation lane, a MIDI CC, an LFO, an envelope and two Macros at once
without any of them destroying the others.

---

## Phase 1 findings — what this codebase already has

**Modulation is already a control-source layer.**
`applyModulationToNormalizedValue(parameter, baseNormalised)` walks the LFO and
envelope sources, accumulates a delta for any whose assignment matches this
parameter, and returns `clamp01(base + totalDelta)`. The base is untouched.
Each source's delta is

```
delta = depth × headroom × (signal × amount)
headroom = bipolar ? min(base, 1-base) : (amount >= 0 ? 1-base : base)
```

The headroom term exists because a source driving past the end of the range
gets clamped, which turns a sine into a square with rounded shoulders — measured
at 65.6% of every cycle pinned at an end. Scaling by the room the base actually
leaves means full amount arrives exactly at the boundary and turns around there.

**A Macro is a third source kind in that same loop.** Unipolar signal (0..1),
signed per-destination depth — which is exactly the envelope case. Nothing
about the model needs inventing.

**The knob is deliberately never moved by modulation.** There is a comment
saying so: driving it would fight the parameter attachment and write the
modulation back into the parameter. The moving ring on the knob is
`getModulatedNormalisedValue`. Macro influence joins the ring, not the knob.

**Parameters are raw `AudioParameterFloat*`** registered with `addParameter`,
each with a stable `getParameterID()` that `createParameterStateTree()` uses as
its serialization key. Anything that is a parameter is automatically
automatable, serialized into both session and preset state, and — since the
MIDI system maps parameter IDs — MIDI-mappable.

**Every panel is placed in one rectangle**, `panelViewportArea`. Carving a
strip off its left happens in one place and every panel narrows by
construction.

**Knobs are discoverable.** `attachParameterKnob` stamps the parameter's ID on
the slider; 140 of the editor's 159 sliders carry one. Eligibility is already
a property of the control.

---

## Design decisions (§40)

**1. How is the final value calculated?**
`clamp01(base + Σ deltas)` over LFOs, envelopes and Macros. One formula, one
accumulation loop, order-independent.

**2. Is Macro influence additive?**
Additive in normalised space, scaled by headroom. Not multiplicative: a
multiplicative Macro at 0 would silence every destination, which is not a
performance control.

**3. How do Macro and LFO/ENV combine?** They sum. A Macro raising the cutoff
while an LFO wobbles it gives a wobble around the raised point.

**4. Direct MIDI?** MIDI writes the BASE parameter. Macros add on top. So
`CC 22 → cutoff` and `Macro 1 → cutoff` coexist: the CC moves where the cutoff
sits, the Macro moves it from there.

**5/6. Multiple Macros on one parameter?** Yes, they sum, like any other
sources. The knob shows `M1+` and names them all on hover.

**7. Depth.** Every destination carries a signed depth, stored and persisted.
The assignment gesture creates it at +1.0. No depth editor in this version —
the data model has the field, so adding one changes UI, not format.

**8. Bipolar?** Not exposed. The accumulator already takes a `bipolar` flag per
source, so turning it on per destination later is a flag, not a redesign. A
unipolar Macro with a signed depth already covers "up" and "down", which is
what the brief's brightness example actually needs.

**9/10. Persistence.** Preset AND session carry Macro values (they are
parameters) and Macro destinations. Session additionally carries MIDI mappings,
which presets also carry now — see the MIDI design doc.

**11. MIDI vs preset.** A preset load restores Macro destinations. It does NOT
clear `CC 21 → Macro 1`, because a preset with no mappings leaves yours alone.
That is the arrangement the brief's §31 asks for, and it falls out of the rule
already in place.

**12/13. Sharing across panels.** The strip is a single component owned by the
editor, positioned OUTSIDE the panel rectangle. There is one instance, so
"shared across panels" is not something the code has to arrange.

**14. Cmd-click.** Unused in this UI. Shift is MIDI Learn; Cmd is Macro assign.
**Double-click a Macro knob does the same thing.** Cmd is the documented
gesture; double-click is the one that gets found without reading anything, and
it is free because Macro knobs carry no double-click-to-default value for it to
fight with. Both go through one helper so they cannot drift apart.

**15/16. Visuals.** See below.

**17. Missing IDs.** Dropped on load, the rest of the Macro survives.

**18. Automation.** A Macro is an automatable parameter. Its destinations move
because the Macro's value moved, exactly as a DAW automating an LFO rate moves
what the LFO does. Destination parameters keep their own automation lanes,
because the Macro never writes them.

**19. Isolation.** Every field is a non-static member of the processor.

**20. Threads.** See Thread safety.

---

## UI layout

`panelViewportArea` loses `editor.layout.macroStripWidth` (default **70 px**)
from its left edge before any panel is placed in it. Every panel — OSC, MOD,
FLT, FX, AMP, MIX — therefore has the strip to its left, in the same place, at
the same size, without any panel knowing about it.

Padding comes from `macro.strip.padX` (4) and `macro.strip.padY` (10), with
`macro.strip.captionHeight` (14), so the captions are not pressed against the
strip's edge and the sizes are one place rather than several.

The colour is **teal** (`macro.colors.accent`), and the choice matters: purple
is the modulation family's colour throughout this UI, so a purple macro
indicator read as a modulated knob. Amber is MIDI. Teal was unused — the
palette runs blue (OSC), red (FILTER), green (AMP), purple (LFO/ENV), amber
(MIDI).

## The macro knobs' own look

Every other knob in this synth is dark on a dark panel. The macros are a
performance layer that sits outside the panels and stays put while they change,
so `MacroKnobLookAndFeel` draws them as a pale hardware knob instead: a light
bezel carrying a ring of value dots, a raised off-white cap with a domed
highlight, a thin accent arc, and a tick cut into the cap near its edge. They
read as a different KIND of control before anything is read.

The indicators are NOT duplicated into it. The MIDI and macro overlays — the
dashed amber ring, the CC label, the assign-mode ring, the `M1+` chip — moved
out of the main knob look into `drawKnobOverlays`, which both looks call. Two
copies of that drawing is exactly how the pale knob and the dark one would
quietly stop agreeing about what "mapped to CC 21" looks like.

One thing does differ, and it has to: the CC label's bright amber is legible on
a dark knob and nearly invisible on a white one, so `drawKnobOverlays` takes a
`paleSubstrate` flag and uses a darker amber. Nothing else needs it — the macro
chip already carries its own light plate, and the two rings sit outside the knob
against the strip.

The value arc is drawn AFTER the cap, in the gap between it and the dots. Drawn
before, the cap covered its inner half and left a hairline.

The label inside a destination knob sits on a translucent pale plate with dark
text (`macro.colors.labelBackground`, `macro.colors.labelText`) rather than
being coloured text on the knob. A knob is busy and mostly dark, with a ring
and a pointer moving over it; a light chip reads as a tag stuck on top, which
is what it is.

The knob is `macro.strip.knobScale` (0.9) of the width its cell allows, rather
than filling it. Shrinking the disc this way leaves the strip's width alone,
which matters because that width is a layout budget every panel is placed
against: taking it off the strip instead would move all six panels.

The strip is 70 px wide including padding, holding five knobs stacked
vertically with an `M1`..`M5` caption under each. The knob takes the width the
padding leaves and the caption sits directly beneath it, touching — centring
the knob in what the cell had spare put a gap between a knob and the label it
belongs to, which reads as two things rather than one control.

The width started at 38 px, against the brief's "approximately 40 px or less".
It was widened on request: at that size the knobs were too small to play, and a
performance control that is awkward to reach for is not doing its job.

---

## Adding a Macro

`kMacroCount` is the only number. Parameter creation, the routing table, the
accumulator, serialization, the MIDI layer and the strip's layout are all
written against it, so raising it is a one-line change in the processor plus a
`static_assert` in `MacroStrip` — the strip's array cannot be sized from
`kMacroCount` directly because the processor is only forward-declared in that
header, so the assert fails the build rather than letting the two drift.

Going from four to five found nothing wrong in the product and five things
wrong in the **tests**: fixed `[4]` arrays indexed by macro (three of them,
which segfaulted rather than failed), and hard-coded expectation lists of four
IDs, four CCs and four destination counts. Those now derive from `kMacroCount`,
so the next one is a one-line change everywhere.

The strip lays out one cell per macro, each cell's edges computed from the
strip's height by position — `top + span * i / count` — rather than from a
rounded-down cell height taken N times, which pooled its remainder at the
bottom. Within a cell the knob sits directly on its caption with the slack
above and below the pair, not between them: bottom-aligning the pair left every
cell's slack above the knob and the whole column sat low, 53 px of air over the
first macro against 10 under the last.

## Macro component architecture

One `MacroStrip` component, owned by the editor, holding four
`juce::Slider`s bound to the `macro1`..`macro4` parameters through
`attachParameterKnob`. Because they are parameter knobs like any other, they
are MIDI-mappable through the existing system with no new code — which is the
whole of §22.

They are excluded from MACRO assignment eligibility (a Macro cannot target a
Macro), by parameter ID.

---

## Assignment mode

```
        NORMAL
          │  Cmd-click (or double-click) a Macro knob
          ▼
   ASSIGNING(macro N) ─── Cmd-click a different Macro ───► ASSIGNING(that one)
          │   │
          │   └─ click any eligible knob ──► toggle its assignment, stay in mode
          │
          ├─ click the active Macro knob ──► NORMAL
          ├─ Escape ───────────────────────► NORMAL
          └─ Shift-click anything (MIDI Learn) ──► NORMAL, then MIDI selection
```

While assigning, a **full-editor transparent overlay** takes the clicks. This
is what keeps a click from moving the knob it lands on: a mouse listener cannot
consume an event, so without the overlay every assignment click would also drag
the destination. The overlay hit-tests for the topmost parameter knob under the
cursor and toggles it.

**Panel changes preserve the mode** (§34, preferred). The strip and the overlay
both live above the panels and are untouched by switching, so this is the
default rather than something arranged.

**Escape exits and keeps what was already clicked.** Each click commits
immediately; there is no pending set to roll back.

**Only one learning mode at a time** (§36): entering Macro assign clears any
MIDI selection, and starting a MIDI selection leaves Macro assign.

---

## Parameter eligibility

A control is eligible if it is a `juce::Slider` carrying a parameter ID from
`attachParameterKnob`, and is not one of the Macro knobs themselves. That is the same
rule MIDI mapping uses, minus the Macros themselves. Nothing is hard-coded:
navigation, panel selectors and read-only displays are not parameter knobs and
so are never eligible.

---

## Control-source architecture

```
   base normalised value          ← the knob, DAW automation, direct MIDI CC
        +  Σ  LFO deltas
        +  Σ  ENV deltas
        +  Σ  MACRO deltas         ← depth × headroom × macroValue
        =  clamp01(...)            → DSP, and the knob's moving ring
```

Macro routes are held as a fixed array of slots, each carrying an atomic
parameter POINTER and an atomic depth, with an atomic count. The audio thread
compares pointers — no strings, no allocation, no lock, no container growth.
The message thread resolves a parameter ID to a pointer once, when the
assignment is made.

---

## Macro data model

```
macroN                        an AudioParameterFloat, 0..1, id "macro1".."macro4"
macroRoutes[N] = [ { parameterId, depth } ]
```

IDs are the stable strings, never indices or pointers, in persistence. Pointers
exist only in the runtime routing table, rebuilt whenever assignments change.

---

## Persistence

`createParameterStateTree()` gains a `macroRoutes` child: one `macro` node per
Macro carrying its index, with a `dest` child per destination holding
`param` and `depth`.

Macro VALUES need no work — they are parameters, so they are already in the
tree, in presets, and automatable.

The child is kept in `createPresetStateTree()`: presets ship their performance
controls (§28). It is applied on both restore paths. A state with no
`macroRoutes` leaves every Macro with no destinations, which is what an
older project or preset means (§45).

---

## Thread safety

| Thread | Does | Touches |
|---|---|---|
| Audio | reads route slots while accumulating deltas | atomics only |
| Message | edits assignments, rebuilds the route table, serializes | the table, parameters |

No UI is touched from the audio thread; no component tree is walked during DSP;
the route table is fixed-size and never allocates after construction.

---

## Failure handling

| Case | Behaviour |
|---|---|
| Destination names an unknown parameter | dropped on load, rest of the Macro survives |
| Macro index out of range in state | that node ignored |
| More destinations than slots | extras dropped, existing kept |
| State with no macroRoutes | every Macro empty, values default |
| Assignment to a Macro knob | refused |

---

## Testing strategy

Four logical Macros with stable IDs; the strip present and within its width
budget on all six panels; Cmd-click and double-click each entering assign mode
for each Macro, and double-click arming nothing on an ordinary knob;
eligible knobs highlighting distinctly from MIDI; click toggling assignment;
cross-panel assignment in one Macro; the Macro moving its destinations and the
DSP; unassigned parameters unaffected; two Macros on one parameter summing;
Macro plus LFO, Macro plus ENV, Macro plus direct MIDI; MIDI onto each of the
every Macro; the whole CC → Macro → parameter → DSP chain; preset and session
round trips for both values and assignments; two instances isolated; state
missing, unknown or malformed; and the existing suite unchanged.
