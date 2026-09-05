# MOOD — DSP Design and Evaluation

MOOD is the last of the three "two channels and a shared clock" effects in this
project to get a design document. DOOM and LUCY have had one since they were
written; MOOD has been shipping without one, which is why this document is as
much an **evaluation** as a description. §8 is a list of findings, and one of
them is a real defect.

MOOD reproduces the documented control philosophy of a two-channel
micro-looper-plus-effects pedal using its own DSP. It does not reproduce, and
does not claim to reproduce, any proprietary implementation.

---

## 1. The shape

Structurally MOOD is DOOM's sibling: two channels that hear each other, a
routing selector deciding what the second one is fed, and one clock underneath
both.

```
                        INPUT (stereo)
                             │
              ┌──────────────┴───────────────┐
              │                              │ dry
   ═══════════▼══════════ internal clock ══  │
              │                              │
      ┌───────▼────────┐                     │
      │  MICRO-LOOPER  │  ENV / TAPE /       │
      │ always listening  STRETCH            │
      └───────┬────────┘                     │
              │                              │
        ┌─────▼──────┐  ROUTING              │
        │  DRY→WET   │  what the wet         │
        │  LOOP→WET  │  channel is fed       │
        │  PARALLEL  │                       │
        └─────┬──────┘                       │
      ┌───────▼────────┐                     │
      │   WET CHANNEL  │  REVERB / DELAY /   │
      │                │  SLIP               │
      └───────┬────────┘                     │
              │                              │
              ├──────────► history writeback │
              │            + DEGRADE          │
   ═══════════▼═══════ zero-order hold ════  │
              │                              │
              ▼            MIX               │
            ┌──────────────────┐◄────────────┘
            │       MIX        │
            └────────┬─────────┘
                     ▼
                  OUTPUT
```

The two channels are summed, not crossfaded: `wet + loop`. Which one dominates
is a consequence of the modes and FEEDBACK rather than of a balance control —
this is where MOOD differs from DOOM, which has an explicit BALANCE.

---

## 2. The clock

CLOCK is the engine's sample rate, and it is what ties the channels together:
audio recorded at one rate and played back at another changes **speed and pitch
at the same time**, so dropping the clock an octave half-speeds the micro-loop
and the wet channel alike.

```
    steps    = round((1 - clock) * 36)        36 steps = three octaves
    divider  = 2^(steps/12)                   clamped to 1 … 8
    increment= 1 / divider
    rate     = hostRate / divider
```

Quantised to **semitones**, so the transposition lands on musical intervals.
Where DOOM quantises to a table of eleven harmonised ratios, MOOD quantises to
the chromatic scale over three octaves — a finer grid over a wider span, which
suits a control that is used as a pitch/speed instrument rather than as a
rhythmic divider.

Between internal steps the output is held (**zero-order hold**) on the way back
up to the host rate. The aliasing that leaves behind at low clock settings is
the character of the control, not an artifact to be filtered away — the same
decision DOOM makes.

`DEGRADE` deliberately does **not** divide the engine clock. It holds samples of
its own, so roughening the loop does not also transpose it. Transposition is
CLOCK's job and only CLOCK's.

---

## 3. The micro-looper

Always listening: a four-second circular history is written once per internal
step, whether or not anything is playing it back. That single write carries the
input **plus** the recycled channels, so FEEDBACK and DEGRADE compound each
pass round the loop rather than sitting as a fixed layer on the output:

```
    recycled = loop * fb * 0.88 + wet * fb * 0.58
    history <- degrade(input + recycled)
```

The two coefficients are the only unexplained constants in the engine; see §8.

### ENV — envelope-gated slice capture

MODIFY is **sensitivity**, so the threshold falls as the knob rises
(0.14 → 0.002). The range is set against the levels this actually sees: a
one-pole average of |x| for a sine of amplitude A settles at 0.64A, and on the
FX bus that puts the envelope around 0.16 on ordinary material. A threshold of
0.35 would mean the detector never fires and the mode does nothing at any
setting a player would use.

The captured slice gets **its own buffer**. Holding a position in the circular
history cannot work: the write head keeps running and laps the region being
held, so a long hold reads audio being overwritten underneath it.

### TAPE — variable-speed heads

LENGTH is 0.05 … 2.2 s. MODIFY selects a playback speed from a table of eight
(−4× to +4×, through the useful fractions), so the transpositions are octaves
and fifths rather than arbitrary ratios.

Its stereo behaviour is asymmetric by specification: a **second head runs the
same loop backwards on the LEFT channel only**. The reverse head is never read
on the right and is not computed there.

### STRETCH — granular cloud

Up to 16 grains. Panning speed follows MODIFY — the further from frozen, the
faster the image moves.

Grains carry a `sourceBalance` as well as a `pan`: with SPREAD down a grain
hands back what it found, left to left and right to right, so a source that
arrived hard left leaves hard left. Summing to mono and panning to centre —
which is what a pan of 0.5 amounts to — throws the incoming image away before
SPREAD has been asked whether it wanted it changed.

---

## 4. The wet channel

### REVERB

Four allpass diffusion stages (331/457/619/797 samples) with **static** tap
positions. They were once modulated by `sin(0.13 * writePosition)`, which is not
a modulator at all: the write position advances one per sample, so that
expression is a ~1 kHz oscillator frequency-modulating the taps at audio rate.

SPREAD does two things here, and both are necessary: it diverges the two sides'
tap times **and** cross-feeds reflections between them. Without the cross-feed
the mode is two independent mono reverbs, and a source hard to one side produces
no reflections at all on the other.

### DELAY

TIME is 0.03 … 1.6 s. MODIFY is feedback, and the top of the control is **unity**
— repeats are stable and pile up like a looper, held there by a saturator rather
than by a coefficient below one.

Length changes are made by **crossfading between two taps** rather than by
sliding one, so moving TIME does not pitch-bend the echoes already in flight.
That is what `CrossfadeTap` exists for, and it is the right call: a slid tap is
a tape effect, and this mode is not one.

At SPREAD 0 each channel feeds itself and the incoming image is kept; at SPREAD 1
the paths cross fully and every repeat lands on the other side.

### SLIP

A window of 0.05 … 0.55 s replayed at a speed quantised to **semitones**, an
octave down through neutral to an octave up, in each direction.

The input is written to the wet buffer so that ROUTING reaches this mode. It
previously read the history directly and ignored its arguments entirely, which
meant routing the micro-loop into it did nothing at all.

---

## 5. DEGRADE

Three artifacts behind one control, applied in the feedback path so they
compound over repeats:

1. **Bit reduction**, 16 down to about 3 bits — quantised against a *nominal
   level*, not against the sample itself. Quantising to a fixed step regardless
   of level is what makes a lo-fi effect sound like a broken gate rather than an
   old sampler.
2. **Sample-rate reduction** on top. Bit-crushing alone only ever adds a noise
   floor; it is the downsampling that produces the aliased, metallic ring that
   reads as a degraded recording.
3. **A rising noise floor**, lowpassed so it is hiss rather than fizz, and gated
   by how much signal is actually passing. Ungated it is a permanent layer.

---

## 6. Parameters

| Parameter | Range | Default | Purpose |
|---|---|---|---|
| `moodEnabled` | bool | true | bypass |
| `moodMix` | 0…1 | 0.35 | dry ↔ MOOD |
| `moodClock` | 0…1 | 1.0 | engine sample rate, semitone-quantised over three octaves |
| `moodRouting` | DRY→WET / LOOP→WET / PARALLEL | DRY→WET | what the wet channel is fed |
| `moodWetMode` | REVERB / DELAY / SLIP | REVERB | wet channel mode |
| `moodWetTime` | 0…1 | 0.40 | mode dependent (§4) |
| `moodWetModify` | 0…1 | 0.45 | mode dependent (§4) |
| `moodLoopMode` | ENV / TAPE / STRETCH | ENV | micro-looper mode |
| `moodLoopLength` | 0…1 | 0.28 | mode dependent (§3) |
| `moodLoopModify` | 0…1 | 0.50 | mode dependent (§3) |
| `moodFeedback` | 0…1 | 0.35 | how much of both channels is recycled into the loop |
| `moodFreeze` | bool | false | stop writing the history |
| `moodSpread` | 0…1 | 0.50 | per-mode stereo treatment |
| `moodDegrade` | 0…1 | 0.20 | bit + rate reduction + noise floor |

---

## 7. Real-time safety

Every buffer is allocated in `prepare()` — history, wet, the ENV slice store and
the four diffusion lines. Nothing in `processSampleFrame` or
`processInternalStep` allocates, locks, or touches the filesystem. Grains are a
fixed `std::array<Grain, 16>`, not a container.

Smoothing is per sample on all ten continuous controls, at 25 ms (30 ms for
the enable ramp).

---

## 8. Evaluation

What follows is an assessment of the architecture as it stands, not a list of
planned work.

### 8.1 The architecture is sound

The two-channel-plus-shared-clock structure is the right one, and the clock is
genuinely a clock — it changes length, pitch and bandwidth together, which is
what makes the control musical rather than a sample-rate slider. Several
comments in the source record a previous version where it was `1/rate` used as a
sample-and-hold count that never exceeded a couple of samples: a decimator, not
a clock, changing neither loop length nor pitch. That has been fixed properly.

The per-mode stereo treatments are a strength. Each mode images differently
(diffusion cross-feed, ping-pong repeats, grain source balance, a left-only
reverse head), which is far better than a single width control bolted on the
end, and `Mood_SpreadOffPreservesTheIncomingImage` pins the property that
matters: SPREAD at zero must not destroy an image that arrived already stereo.

### 8.2 There is no control-model layer — the same gap DOOM and LUCY had

MOOD maps its four macros inline, in each renderer, from the raw smoothed knob:
`jmap(loopLength, 0.05f, 2.2f)` inside `renderLoopTape`,
`jmap(wetTime, 0.03f, 1.6f)` inside `renderWetDelay`, and so on. That is exactly
the arrangement DOOM and LUCY were refactored out of. The consequences are the
same:

- "What does LENGTH at 0.4 mean in TAPE" can only be answered by reading DSP.
- The curves cannot be tested without rendering audio.
- Nothing prevents a mapping from quietly reaching across into another
  control's territory.

The fix is the one already applied twice: a `MoodControlModel` with
`MoodUserParameters → deriveMoodParameters() → MoodDerivedParameters` and named
mapping functions. MOOD is the natural third.

### 8.3 ROUTING is a float, and that is a defect

`MoodSettings::routing` is a **float** carrying `index / 2`, compared against
thresholds of 0.33 and 0.66:

```cpp
if (currentSettings.routing > 0.66f)        // PARALLEL
else if (currentSettings.routing > 0.33f)   // LOOP→WET
```

A three-way choice is being stored as a continuous value, smoothed per sample as
if it were audio, and recovered by threshold. This is the clearest problem in
the file. It is also demonstrably error-prone: a source comment records that the
two branches were once **the wrong way round**, so LOOP→WET fed the wet channel
the input as well as the loop and PARALLEL fed it the loop alone — each doing
what the other's label said. A `MoodRouting` enum would have made that
unrepresentable. `Mood_RoutingMatchesItsLabels` now guards the behaviour, but
the guard exists because the type does not.

`wetModeIndex` and `loopModeIndex` are bare `int`s for the same reason and
should become enums alongside it.

### 8.4 MOOD cannot be seeded, and calls the system RNG from the audio thread

`Mood.cpp:385` and `Mood.cpp:601` call `juce::Random::getSystemRandom()` — in
`applyDegradation`'s noise floor and in STRETCH's grain panning. Both are on the
audio path. Two consequences:

- **MOOD has no `setSeed`.** CHORUS, DOOM, LUCY and VIBE all have one
  specifically so their stochastic behaviour is reproducible in tests — MOOD is
  the only stochastic engine in the project without one. A test can therefore
  assert bounds but never an exact result, which is why the existing suite
  checks stability and width rather than output identity.
- `getSystemRandom()` returns a **shared global**. Calling it from the audio
  thread is a shared-state access from a context that should not have one.

The fix is small and matches the neighbours: a per-instance xorshift and a
`setSeed`, as `Doom::nextRandom` and `Lucy::nextRandom` already provide.

### 8.5 The feedback coefficients are unexplained

```cpp
const auto recycledL = loop.l * loopFeedback * 0.88f + wet.l * loopFeedback * 0.58f;
```

`0.88` and `0.58` are the only magic numbers in the engine without a comment
saying where they came from. They set the relative weight of the two channels in
the recycle path — effectively a fixed internal balance — and they are the one
place a reader cannot reconstruct the intent. Either they are a tuned ratio and
should say so, or that ratio wants to be a control (DOOM exposes exactly this as
BALANCE).

### 8.6 Test coverage is good on stability, thin on identity

Eighteen MOOD checks cover the things that break loudly: runaway feedback, DC,
denormals, bypass tails, reset, silence-to-transient, every mode pair for spread,
the clock's transposition of a captured loop, and the routing labels. What is
missing is anything asserting *what a mode does* — there is no equivalent of
LUCY's `Lucy_PacketRepeatFillsTheGapsRatherThanEmptyingThem`. §8.4 is the reason:
without a seed, "the same input produces the same output" is not assertable.

### 8.7 Summary

| | |
|---|---|
| Architecture | Sound. The right structure, and the clock is a real clock. |
| Stereo design | A strength. Per-mode imaging, correctly preserved at SPREAD 0. |
| Real-time safety | Clean. No allocation, locks or containers on the audio path. |
| Control model | **Missing.** The inline-mapping arrangement DOOM and LUCY were refactored out of. |
| Type safety | **Weak.** Routing is a float; both modes are bare ints. |
| Determinism | **Absent.** No seed, and the system RNG is called from the audio thread. |
| Documentation | This document is the first. |

Ranked by cost to fix against value returned: §8.4 (seed) is small and unlocks
the identity tests in §8.6; §8.3 (routing enum) is small and removes a class of
bug that has already happened once; §8.2 (control model) is the largest and
brings MOOD into line with its two siblings.
