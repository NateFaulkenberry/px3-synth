# FX control surfaces — LUCY, DOOM and MOOD

Three effects, one change: the knobs stopped being a list of DSP parameters and
became a set of controls you can actually play.

None of the audio engines were rewritten. The STFT coders, the micro-loopers,
the packet models, the granular clouds, the filters, the reverbs — all of it is
the same DSP it was, in the same order. What changed is everything between your
hands and it.

---

## The problem

All three effects had the same shape of flaw, and it was invisible from outside.

The curves that give a knob musical meaning were written **inside the DSP**,
inline, at the point of use. LUCY's LOSS was converted into a masking depth in
one function, a coverage width in another, a quantiser step in a third and a
packet probability in a fourth. DOOM's TIME was squared into a decay inside
`soupFrame` and squared again into a delay inside `renderRelay`. MOOD's LENGTH
became a slice duration in one renderer and a grain size in another.

Three consequences followed from that:

- **"What does LOSS at 0.4 actually do?" could only be answered by reading four
  stages of DSP**, and only by someone willing to.
- **None of it could be tested without rendering audio**, so a mapping
  regression showed up as "it sounds different" rather than as a named failure.
- **Nothing stopped a conversion reaching across into another control's
  territory**, because a line of arithmetic inside a renderer can touch anything
  in scope.

## The change

Each effect gained a translation layer:

```
    USER CONTROLS        what you turn
          │
          ▼
    derive…Parameters()  one function, once a block
          │
          ▼
    DSP PARAMETERS       seconds, rates, thresholds, frame counts
          │
          ▼
    THE ENGINE           unchanged
```

`LucyControlModel`, `DoomControlModel`, `MoodControlModel`. No stage of any
engine reads a knob any more: `applyLoss` asks for a masking depth,
`applyPackets` for a probability, `renderRelay` for a delay in seconds and a tap
count. The curves are named functions — `mapLossToCoverage`,
`mapWetTimeToSoupT60`, `mapLoopModifyToTapeRateIndex` — and the tests call them
directly, without an engine.

**The curves themselves did not change.** This was a relocation, not a retune,
except where a curve was demonstrably wrong; those cases are called out below.

---

## LUCY

### GLOBAL was a wet/dry, and should never have been

It was literally `dry * (1 - g) + wet * g`. That is not what the control is for:
a crossfade can only ever change how much of a *fixed* wet signal you hear, so
turning it up could not make the character you had dialled in any stronger.

GLOBAL now scales the coder's depth and coverage, the packet probability, the
filter's amount and the freeze blend — each through its own exponent, so the
stages arrive in a deliberate order:

| arrives | stage | why |
| --- | --- | --- |
| early | FILTER | it shapes where artifacts live rather than making them |
| proportional | the coder, freeze, verb | |
| late | PACKETS | a dropout at a quarter intensity is not "subtle" |

A crossfade survives in the bottom 8% of travel and nowhere else, so the effect
can reach genuinely clean and the engine's idle path — which skips two FFTs per
hop — has a continuous way in and out.

### The bottom half of LOSS did nothing

Masking depth, quantisation *and* packet probability were all `loss²`. Stack
three squared curves and the first half of the knob is inaudible while the last
tenth does everything.

The five derived values now have five separately-tuned curves. Measured as how
much of each curve's total travel has happened by the halfway point:

| | masking | coverage | quantisation | packets |
| --- | --- | --- | --- | --- |
| travel at half turn | 33% | 45% | 25% | 19% |

Coverage and masking carry the character and are well under way by noon;
quantisation and packets are the destructive pair and are deliberately held
back — but not to nothing.

### SPEED was linear, and a frame count is a rate

16 frames down to 1, mapped linearly, put the middle of the knob at 8 and spent
most of its travel in a range that sounds the same. It is geometric now, so noon
is 4. Every temporal stage — coding decisions, the packet chain, slushy freeze
drift and JITTER's two rates — derives from that one control.

### The parameter schema, replaced outright

No compatibility shims. A test asserts the retired ids are *gone* rather than
aliased.

| was | is |
| --- | --- |
| `lucyFreeze` + `lucyFreezeSlushy` (two bools) | `lucyFreeze` — OFF / SOLID / SLUSHY |
| `lucyWeighting` (a bipolar float) | DARK / NEUTRAL / BRIGHT |
| `lucyFilterFreq` | `lucyFreq` |
| `lucyGateCutoff` | `lucyGateThreshold` |
| `lucyThreshold` | `lucyLimiterThreshold` |
| `lucyGain` | `lucyLossGain` |

Two booleans could express "slushy while not frozen", a state that meant
nothing. `lucyLimiterThreshold` is spelled out because the coder has a masking
threshold of its own — that one is derived, internal, and never a parameter.

Factory presets were updated to match. **Sessions saved before this load LUCY at
its defaults.**

---

## DOOM

`DoomSettings` was already named semantically, so this was the smaller job, and
the parameter ids did not change at all.

### The mode-dependent curves moved out

TIME is a decay in SOUP, a delay in RELAY, a lag in FLIP. LOOP MODIFY is a fill
sensitivity in BURST, a station scan in RADIO, a threshold in MASK. Each is now
a named function. Worth noting what they already got right, and what the tests
now pin:

- **RELAY's MODIFY counts repeats** — eight countable positions and one more
  that never stops, rather than a continuum of fractional repeats with no
  musical meaning.
- **RADIO's MODIFY scans** rather than selects: it returns two stations and a
  blend, so between centres both are audible and the static rises.
- **BURST's LENGTH is inverted** — more LENGTH is a faster sequence and so a
  shorter step, because what the knob sets is the pace.
- **MASK's threshold reaches a true zero**, which is the untouched loop.

### Modes became types

`loopModeIndex`, `wetModeIndex`, `routingIndex` and `crossSourceIndex` were bare
`int`s that a switch turned back into meaning. They are `DoomLoopMode`,
`DoomWetMode`, `DoomRouting` and `DoomCrossSource` now.

This is a C++ change, not a state change: **sessions and factory presets load
exactly as before.**

### RAMP is deliberately absent

The pedal uses MIX as a ramp-speed control when its ramping infrastructure is
engaged. A host already provides automation and this project already has a
modulation matrix, so a second internal ramp framework would be a worse version
of something you already have. It is host-provided by design, not an omission.

---

## MOOD

MOOD had no design document at all, so writing one came first. Three of its
findings were then acted on, and one is deliberately left open.

### It could not be seeded, and used the system RNG on the audio thread

`juce::Random::getSystemRandom()` was called from `processSampleFrame` — in
DEGRADE's noise floor and STRETCH's grain panning. That is a shared global, it
cannot be pinned, and it was the reason nothing about MOOD's output could be
asserted exactly, only its bounds.

It now uses the per-instance xorshift DOOM and LUCY already had, plus
`setSeed()`. Two engines at the same seed agree on all 24,000 samples of a
half-second render, and running one no longer disturbs another's sequence.

### ROUTING was a float

A three-way choice, stored as `index / 2`, smoothed per sample as though it were
audio, and recovered by comparing against 0.33 and 0.66. A comment in the source
records that two of the three settings were once wired to **each other's
labels** — which that representation made possible. `MoodRouting` makes it
unrepresentable. `wetModeIndex` and `loopModeIndex` went the same way, and so
did `bpm`, a field the engine never read.

### Still open, on purpose

MOOD's recycle path weights the two channels with a hardcoded `0.88` and `0.58`.
That is a fixed internal balance — the thing DOOM exposes as BALANCE. Whether to
document the ratio or expose it is a control-surface decision, so it is left as
a decision to take rather than taken quietly.

---

## The panels

DOOM and LUCY follow pedals where **each knob has a second function printed
underneath it**. Their cards now do the same: six large knobs, each with a
dimmed caption below the bold one, and a **MAIN / ALT** chip that switches which
of the pair the knobs drive.

| DOOM | | LUCY | |
| --- | --- | --- | --- |
| TIME | CROSS | FILTER | GATE |
| MODIFY *(wet)* | EQ | VERB | DECAY |
| LENGTH | FADE | FREQ | THRESHOLD |
| MODIFY *(loop)* | BLEND | SPEED | AUTO GAIN |
| CLOCK | GLUE | LOSS | LOSS GAIN |
| MIX | BALANCE | GLOBAL | FREEZER |

This replaced fourteen flat knobs on each card. Three things about how it works:

- **Both halves are always live.** Each is a real parameter with its own
  automation lane, attached whichever way the chip is set. Automating an
  alternate never depends on what the panel is showing.
- **ALT is not a parameter.** It is panel state, so it is not saved in presets,
  does not appear in your DAW's automation list, and switching it cannot change
  what you hear.
- **Under the hood** each pair is two sliders sharing one circle, with only one
  visible — not one control being repurposed.

Both cards' rows are now declared **once**, in `DoomCardLayout.h` and
`LucyCardLayout.h`, shared by the Synth's card and the standalone plug-in. They
were two copies that had to be edited together, which stopped being realistic at
twelve knobs.

MOOD's card was regrouped rather than repaired: nine knobs in one wrapping row
that interleaved its two channels became the channels' macros together, then the
machine's controls, then MIX alone at the foot of the card. Its knob rows now
size themselves from the card's width instead of a hardcoded pixel value — a
fixed width is either too small or, one pixel too large, wraps, and a wrapped
row halves its cell height, so overflowing rendered *smaller* knobs than fitting
did.

**Styling is untouched throughout** — same palettes, chips, knob look, artwork
and card treatment. Only the arrangement changed.

---

## Testing

**1433 assertions pass**, up from 1409 at the start of the branch.

The new ones need no audio, which is the point of the layer:

- Every LOSS curve rises monotonically, and the bottom half of the knob is not
  wasted.
- SPEED is geometric, and every temporal stage follows it together.
- GLOBAL is not a crossfade — its output blend is 1 across all but the bottom
  8%.
- DOOM's clock steps really are the eleven harmonised ratios, and SMOOTH sweeps
  the same span.
- MOOD's clock spans exactly three octaves, quantises to semitones, and its
  octaves are exact.
- **Each control owns one thing.** For each effect, every control is turned in
  turn and the whole derived structure is compared: LOSS GAIN never touches the
  coder, SPEED never changes degradation depth, GLUE never moves the clock, MIX
  and BALANCE stay orthogonal, DOOM's two MODIFYs never reach into each other's
  channel, and DEGRADE never transposes MOOD's engine.
- MOOD is reproducible from a seed, and its random state is per instance.

---

## Known behaviour changes

- **LUCY sessions and presets saved before this release load at defaults.** Its
  parameter schema was replaced with no compatibility layer, which was the
  explicit intent.
- **DOOM and MOOD are unaffected** — their parameter ids and stored values are
  unchanged.
- LUCY's rainbow ring is gone from every FX amount knob; the artwork carries the
  character now.
- MOOD's ROUTE dropdown shows `DRY-…` — the choice string `DRY->WET` is one
  character too long for the box. Shortening the three routing names would fix
  it, but that changes user-visible semantics and is a naming decision rather
  than a defect.
