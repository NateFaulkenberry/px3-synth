# MIDI parameter mapping (MIDI Learn)

## Overview

Shift-click one or more knobs, move a hardware control, and that control now
drives all of them. The point is that the user never types a CC number, never
opens a dialog, and never leaves the surface they are already looking at.

The feature is additive. With no mappings stored, nothing about the synth
changes: no extra work on the audio thread beyond reading MIDI it already
reads, no change to any parameter, no change to the drawn UI.

---

## Phase 1 findings — what this codebase already has

Everything below was read out of the repository before any design was chosen.

**There is no APVTS.** Parameters are raw `juce::AudioParameterFloat*` members
of the processor, registered with `addParameter`, and enumerable through
`getParameters()`. Every one is an `AudioProcessorParameterWithID`, so
`getParameterID()` is a stable string — and `createParameterStateTree()`
already uses exactly that string as its serialization key. That is the stable
identifier a mapping should hold. Nothing else in the codebase identifies a
parameter more durably.

**Knobs bind through `juce::SliderParameterAttachment`**, constructed in six
places: `PluginEditor`, `MixPanel`, `ModPanel`, `BusInsertOverlay`,
`FxCardComponent` and `EnvelopeComponent`. There is no single choke point and
no common knob subclass — every knob is a plain `juce::Slider`.

**Per-slider metadata already travels in `Component::getProperties()`.** The
rotary look-and-feel reads `modulatedPos`, `knobBypassed`, `psychedelicFx`,
`psychedelicBypassGray` and `isMixerPanKnob` from there and draws accordingly.
This is the existing mechanism for "this knob has an extra thing to say", and
it is what the MIDI label extends.

**MIDI is scanned in exactly one place.** `updateActiveNotesFromMidi`, called
once per block from `processBlock`, walks the `MidiBuffer` on the audio thread
and already branches on `isController()` for CC 1 (mod wheel) and CC 121
(reset all controllers). Both the plugin and the standalone build reach it
through the same `processBlock`, because the standalone build uses JUCE's
stock wrapper — there is no custom standalone MIDI code in this repository.
One hook therefore covers both, which is the answer to "do not assume the two
architectures are identical": here they genuinely are, and the reason is
recorded rather than assumed.

**Audio-to-UI hand-off is by atomic.** `lastMidiNote`, `modWheelNormalized`,
`pitchBendActivity` and the envelope-progress slots are all written on the
audio thread and read on the message thread through `copyXxx()` accessors.
The MIDI-learn hand-off uses the same pattern rather than inventing one.

**Session state versus preset state is already a solved problem.**
`createParameterStateTree()` is the canonical tree for DAW projects AND preset
files; `createPresetStateTree()` takes that tree and REMOVES the properties
that belong to the session rather than to the sound — the open panel, the
loaded preset's name, category, author and path. `applyParameterStateTree`
takes a `restoreUiSessionState` flag, and `PresetManager` passes `false`.

That mechanism is what the mapping persistence hangs off. Note that the brief's
§10 asked for mappings to be session-only, and that was reversed during
implementation: they are now written into BOTH the session tree and preset
files. The `restoreUiSessionState` flag still matters, because it is what tells
the two restore paths apart — see Persistence.

**Modulation is applied at read time, not written into parameters.**
`applyModulationToNormalizedValue(&param, param.getValue())` layers LFO and
envelope contributions on top of whatever the parameter currently holds, and
`getModulatedNormalisedValue` is what the UI draws as the moving ring. The
knob itself is deliberately never moved by modulation — there is a comment
saying so, and the reason is that driving it would fight the parameter
attachment and write modulation back into the parameter.

This settles §9 by construction: MIDI mapping writes the parameter, modulation
reads it. They compose the way a hand on the knob and an LFO compose today.

**The keyboard already has a notice banner**, drawn by `PianoKeyboard` from
`WarningStyle` — but only while `silenced`. Select Mode needs the same banner
over a keyboard that is still playable, so the banner gains a second trigger
rather than a second implementation.

**Shift is free on knobs.** The only `isShiftDown` uses in the UI are in
`BreakpointEnvelopeEditor`, for fine adjustment while dragging a curve. No
knob uses shift for anything, so shift-click is available.

---

## Phase 2 findings — technical constraints and risks

**Parameter writes belong on the message thread.** `setValueNotifyingHost`
calls listeners and, in a plugin, calls into the host. Doing that from the
audio thread is the classic way to get a lock or an allocation in a real-time
path. The design therefore never writes a parameter from the audio thread.

**Consequence, stated plainly:** a MIDI CC reaches the parameter on the next
message-thread tick rather than sample-accurately. At the 30 Hz tick this
synth already runs, that is up to ~33 ms. For a control gesture on a knob this
is not perceptible; for anything wanting sample accuracy it would be wrong,
and this feature is explicitly not that. Recorded as a limitation.

**MIDI device identity is not available.** In a plugin, every input device is
merged into one `MidiBuffer` before `processBlock` sees it; JUCE exposes no
per-message device. So a mapping cannot be device-scoped, and §14's "MIDI
device" question has a factual answer rather than a design answer: not
possible at this layer. Recorded as a limitation.

**Feedback loops.** If the timer wrote every mapped parameter on every tick,
it would fight the user's own hand on the knob and any DAW automation. The
timer therefore writes only when a CC's value has actually CHANGED, tracked by
a sequence number the audio thread bumps.

**Test-thread reality.** The processor drives its own `juce::Timer`. Under a
test harness with no message loop, mappings would never apply, so the apply
step is a public method (`applyPendingMidiMappings`) that the timer calls and
a test can call directly. The timer is a caller, not the mechanism.

---

## User workflow

```
Shift-click a knob            →  it joins the selection, keyboard shows a notice
Shift-click more knobs        →  they join too, anywhere in the UI
Move a hardware control       →  every selected knob is mapped to that CC
                                 selection clears, knobs show their CC
```

To remove: shift-click a mapped knob. Its mapping is dropped immediately and
it joins the selection, ready to be given a new CC — or left unmapped by
pressing Escape.

That is §6's stated behaviour, and it is destructive by design. It is not
*silent*: the CC label vanishes from the knob at the instant of the click, so
the change is visible in the same gesture that causes it. The alternative
considered was deferring the clear until the selection resolves, which would
have made shift-click non-destructive but would also have made "unmap this"
impossible to express — there would be no gesture that ends with a parameter
unmapped. Inspection does not need a click, because a mapped knob shows its CC
at all times.

---

## UI behaviour

**Selected knobs** draw a dashed amber ring just outside the knob, from the
shared rotary look-and-feel, so every knob in the synth gets it without
per-panel code. Outside rather than over: the value and the modulation ring
both still have to be readable while the user is choosing what to assign.

**The keyboard notice** reads:

> **Select knobs, then move a MIDI control to assign**

Chosen over the suggested "Assign parameters to a MIDI CC source" because it
names both halves of the interaction — what the user is doing now, and what
ends it. The banner uses the existing `WarningStyle` geometry and colours, so
it sits where the "engage an oscillator" message sits. The keyboard is NOT
silenced during Select Mode: the user can still play while assigning.

**Mapped knobs** draw `CC21` inside the knob, under the spindle, in the same
look-and-feel. Small and quiet: it has to say "this is on a controller" at a
glance without competing with the value the knob is showing.

---

## Architecture

```
   MIDI in (plugin or standalone, same MidiBuffer)
        │
        ▼
   updateActiveNotesFromMidi          [audio thread]
        │  records CC value + bumps a sequence
        ▼
   ccValues[128] / ccSequence[128] / lastTouched      atomics, no locks
        │
        ▼
   applyPendingMidiMappings()         [message thread, 30 Hz timer]
        │
        ├── learn armed?  →  assign every selected parameter to the touched CC
        │
        └── for each CC that moved  →  for each mapped parameter
                                       setValueNotifyingHost(cc / 127)
        │
        ▼
   Parameter  →  DSP, DAW automation, and the knob's own attachment
        │
        ▼
   UI moves because the attachment moves it - there is no second value
```

The last line is the load-bearing one. Nothing in this feature renders a value
of its own. The knob moves because its `SliderParameterAttachment` observes the
parameter, exactly as it does when a DAW automates it.

**Extensibility toward macros.** The mapping layer's input is "a normalised
value from a source" and its output is "write these parameter IDs". A macro
knob would be another source feeding the same apply step. The source is
identified by a small tagged struct rather than by a bare CC number, so adding
a second source kind does not change the destination side or the persistence
format beyond a new tag value. The macro system is NOT implemented here.

---

## Data model

```
MidiMapping
{
    int  ccNumber;          // 0..127, the identity
    int  learnedChannel;    // 1..16, recorded but not matched on
    StringArray parameterIds;
}
```

Held as a `std::vector<MidiMapping>` on the processor instance. A parameter
appears in at most one mapping; assigning it to a new CC removes it from any
other, so "which CC drives this knob" always has one answer.

**Channel:** matching is channel-agnostic; the channel that taught the mapping
is recorded. A controller that sends on channel 1 while the DAW routes on
channel 2 would silently do nothing under strict matching, and "silently does
nothing" is the worst failure mode for a feature whose whole appeal is that it
just works. Recording the learned channel means turning matching on later is a
behaviour change, not a data migration.

**Ranges:** the first version maps the CC's full 0..127 onto the parameter's
full normalised range. The mapping struct is a struct rather than a bare pair
precisely so a per-destination range can be added later without touching the
persistence shape or the apply path.

**Never a pointer.** Destinations are parameter ID strings. A mapping survives
the editor being closed, because it never knew about the editor.

---

## Instance isolation

Every piece of state added by this feature is a non-static member of
`PX3SynthAudioProcessor`: the mapping vector, the CC atomics, the learn state,
the timer. There are no file-scope variables, no singletons, and no global
MIDI listener — MIDI arrives through the processor's own `processBlock`, which
is per-instance by definition.

Two instances in one project therefore cannot see each other's mappings, and
the test suite asserts it rather than trusting the reasoning.

---

## Persistence

`createParameterStateTree()` gains a `midiMappings` child holding one
`mapping` node per CC, each with `cc`, `channel` and one `dest` child per
parameter ID. Both the DAW-session path and preset files carry it: saving a
patch saves the hardware layout it was designed around.

The two paths restore it differently, because they mean different things.

**A DAW session is the whole truth for that instance**, so it is applied whole.
A session saved with no mappings restores none.

**A preset is a sound that may bring a layout with it.** One that carries
mappings replaces what is there; one that carries none leaves your controller
alone. The alternative — absent meaning "clear" — would have every factory
preset wipe the assignments of anyone who merely auditioned one, and there is
no gesture in the UI that would put them back.

The cost of that choice, stated: a preset cannot express "this sound uses no
controller". Removing every assignment is done in the UI, by shift-clicking the
mapped knobs, which is where the user is already looking.

A destination naming a parameter that no longer exists is dropped on load; the
rest of the mapping survives. A mapping left with no destinations is dropped.

---

## MIDI handling

Detection happens inside the existing `isController()` branch. For each CC
message the audio thread stores the value into `ccValues[cc]` and increments
`ccSequence[cc]`, and records the CC number and channel as "most recently
touched" with its own sequence. Nothing is allocated, nothing is locked, and
no message is consumed — the buffer is passed on to the synth untouched, so
note input, mod wheel and pitch bend behave exactly as before.

Normalisation is `value / 127.0`, applied to the destination's normalised
range, so each destination maps into its own units: a cutoff in hertz and a
resonance in 0..1 both receive a full sweep.

CC 1 and CC 121 keep their existing meanings AND can be mapped, because a user
who wants the mod wheel to drive a cutoff is asking for something reasonable.
The two effects add rather than conflict, since one writes a parameter and the
other feeds the modulation matrix.

---

## Thread safety

| Thread | Does | Touches |
|---|---|---|
| Audio | records CC values and the last-touched CC | 128 + 3 atomics, relaxed/release |
| Message | reads them, writes parameters, edits mappings | the mapping vector, parameters |

The mapping vector is only ever read or written on the message thread, so the
audio thread never walks it, never takes a lock, and never allocates. This is
why the mapping lookup can be a plain `std::vector` rather than something
lock-free: the audio thread does not participate in it at all.

---

## Select Mode state machine

```
        NORMAL
          │  shift-click an eligible knob
          ▼
       SELECTING ──── shift-click a selected knob ────► remove it from the set
          │   ▲                                          (empty set → NORMAL)
          │   └──── shift-click another knob ─────────── add it
          │
          ├──── Escape ──────────────────────────────►  NORMAL, nothing assigned
          │
          └──── a CC moves ──────────────────────────►  assign all, then NORMAL
```

- **Shift-click an already-selected knob** removes it. Emptying the set leaves
  Select Mode.
- **Shift-click a mapped knob** clears its mapping and selects it.
- **A selection spanning several existing CCs** is fine: each parameter's old
  mapping was already cleared as it was selected, so the new CC is applied to
  a clean set.
- **A plain click anywhere** does not end Select Mode. The user needs to move
  around the UI to reach knobs in other panels, and losing the selection on
  the way would make cross-panel assignment impossible.
- **Escape** ends Select Mode with nothing assigned.
- **A CC arriving with nothing selected** applies existing mappings only. It
  never learns.
- **Note data** is untouched by all of this.
- **Closing and reopening the editor** loses the selection (it is UI state) and
  keeps the mappings (they are processor state).

---

## Failure cases

| Case | Behaviour |
|---|---|
| Loaded state names an unknown parameter | that destination is dropped, the rest of the mapping loads |
| A mapping ends up with no destinations | the mapping is dropped |
| The same parameter is assigned to a second CC | it is removed from the first; one parameter, one CC |
| A CC arrives with no mapping and no selection | ignored |
| MIDI arrives while the editor is closed | mappings still apply; the timer lives on the processor |
| Two mappings for one CC in loaded state | merged into one |
| CC number out of 0..127 | impossible from `getControllerNumber`, but clamped anyway |

---

## Testing strategy

Unit and integration, in `ComponentTests` alongside the rest of the suite:

- single and multi-parameter learn
- CC movement changes the parameter value, and the DSP output with it
- CC movement moves the UI knob, through the attachment
- two CCs driving different parameters at once
- one CC driving destinations with different ranges
- clearing by shift-click
- two processor instances, same CC, different destinations, no leakage
- state round trip: save, reload, mappings return
- preset round trip: a preset carries its mappings, loading one brings them
  in, and a preset carrying none leaves existing assignments alone
- unknown parameter ID in loaded state degrades gracefully
- with no mappings, parameters and rendered audio are bit-identical to before
- note input still works while mappings exist

Each test is verified to fail against the fault it describes, as everything
else in this suite is.
