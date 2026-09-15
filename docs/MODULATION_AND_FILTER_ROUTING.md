# Modulation and Filter Routing (v0.7.5)

Design record and audit for the v0.7.5 modulation and filter routing work:
envelope time ranges, timed LFO ramps, key sync, the full-range modulation rule,
Pitch Mod destinations, and SERIES/PARALLEL filter routing.

Tests: `PX3Tests modupgrade` (43 checks) plus the updated modulation-rule
checks in `PX3Tests osc`.

---

## 1. Architecture as found

| Piece | Where it runs | Consequence |
| --- | --- | --- |
| LFO 1–3 | **Global**, one `LfoGenerator` each in the processor, advanced once per block and read at the block midpoint | Key sync can only be a global retrigger without a per-voice LFO rewrite |
| Modulation sum | **Global**, once per block, `applyModulationToNormalizedValue`, pushed to every voice | One rule governs every destination |
| ENV 1–3 | Per voice, averaged across active voices into the global modulation value | Envelopes already restart with each note |
| Filters | Per voice, per source, `sourceFilters[4][2]`, serial | Routing belongs in the voice's per-sample loop |

---

## 2. Why 100% modulation sounded subtle

Nothing was attenuating the signal: `lfoDepthForParameterId` returns 1.0 for every
destination, and the UI showed the true amount. The cause was the **swing rule**.
A bipolar source swung `min(base, 1 − base)` — the room on the *nearer* side of
the knob — and the sum was clamped.

That kept a sine from flattening against an end, but whenever a control sat near
either end of its range the nearer side was small, so even 100% barely moved it.
Filter cutoff defaults to 12 kHz, which is 0.867 normalised: a 100% LFO could move
it by ±0.133 — **7.5 kHz to 18 kHz, 0.68 of an octave**, all of it above where a
low-pass is audible.

A second, separate cause applied to pitch: the control named PITCH was a ±0.24 st
fine-tune. At 100% it could never be more than a quarter-semitone vibrato, and the
only wide pitch control, COARSE, moves in whole semitones.

### The new rule

```
bipolar (LFO):          delta = 0.5 × amount × signal
unipolar (ENV, Macro):  delta = amount × signal × (amount ≥ 0 ? 1 − base : base)
heard value             = fold(base + Σ deltas)        reflect back into 0..1
```

An LFO at 100% swings the whole range peak to peak, centred on the knob. Where
that passes an end, the value is **reflected** rather than clamped, so it keeps
moving and never holds still at a limit — the property the old rule existed to
protect. Envelopes and Macros are unchanged: they already reached the end of the
range in the direction of their amount.

The user chose to apply this to every destination, accepting that existing LFO
patches on off-centre controls become stronger. No factory preset uses an LFO,
so no shipped sound changes.

---

## 3. Destination audit

`ModRule_EveryDestinationSwingsFullyAtFullAmount` assigns a 100% square LFO to
every assignable parameter (194) and checks each swings a full half-range. None is
attenuated. Representative destinations, at their defaults, with a 100% sine:

| Destination | Base | Before v0.7.5 | v0.7.5 | Status |
| --- | --- | --- | --- | --- |
| Filter 1/2 Cutoff | 12 kHz (0.867) | 7.5–18 kHz, 0.68 oct | 1.1–17.8 kHz, 4.0 oct | **Corrected** (rule) |
| Filter 1/2 Resonance | Q 0.8 (0.282) | Q 0.25–1.35 | Q 0.25–1.78 | **Corrected** (rule) |
| Osc N Fine Tune (was "Pitch") | 0 st (0.5) | ±0.24 st | ±0.24 st | Unchanged at centre; renamed; the wrong control for vibrato |
| Osc N Pitch Mod | 0 st (0.5) | — | ±24 st | **New** |
| Sub Osc Pitch Mod | 0 st (0.5) | — | ±24 st | **New** |
| Osc N Coarse | 0 st (0.5) | ±24 st, stepped | ±24 st, stepped | Already full at centre; steps by design |
| Osc N Macro A/B/C | 0.5 | ±0.5 | ±0.5 | Already full at centre; stronger off-centre |
| Osc N WT Position | 0.5 | ±0.5 | ±0.5 | Already full at centre; stronger off-centre |
| Osc N / Sub level (mixer) | fader position | the nearer side of the fader only | whole range, folded | **Corrected** (rule) |
| Any control near an end of its range | ≈ 0 or ≈ 1 | ≈ no movement | whole range, folded | **Corrected** (rule) |

Pinned by tests: `ModRule_FullAmountSweepsTheDefaultCutoffOverOctaves`,
`Modulation_FullAmountLfoSwingsTheWholeRange`,
`Modulation_PastTheRangeFoldsBackInsteadOfClamping`,
`Modulation_FullAmountDoesDrivePastTheRange`,
`ModRule_PitchModAtFullAmountIsTwoOctaves`, `PitchMod_*`.

---

## 4. Envelope times

| | Before | v0.7.5 |
| --- | --- | --- |
| Attack range | 0–3 s | 0–40 s |
| Decay range | 0–4 s | 0–40 s |
| Release range | 0–5 s | 0–40 s |
| Knob law | skew 0.45 | skew 0.188: 25% ≈ 25 ms, 50% = 1 s, 75% ≈ 8.6 s |
| Envelope editor drag limit | 8 s | 40 s |
| AMP ENV defaults | 5 ms / 50 ms / 0.8 / 100 ms | **15 ms / 300 ms / 0.8 / 500 ms** |
| ENV 1–3 defaults | 20 ms / 120 ms / 0.7 / 220 ms | **250 ms / 600 ms / 0.7 / 1 s** |

The generators already honoured any time (only a 1 ms floor), so the ceiling was
purely the parameter range. 40 s gives headroom over the 30 s requirement while
the stronger skew keeps the fast end as precise as before. The new amp defaults
are immediate without clicking and tail off rather than stop; modulation
envelopes default slower, because they are usually sweeps. Sustain defaults are
unchanged.

---

## 5. Timed ramps and key sync

**RAMP UP / RAMP DOWN** are appended as waveform indices 4 and 5, so every stored
index keeps its meaning. A ramp is bipolar like every other shape (−1 → +1 or
+1 → −1), so it is subject to the same swing rule, and it holds its end value once
it arrives.

A ramp is driven by **elapsed audio time in seconds** (`rampElapsedSeconds`), not
by phase, samples or blocks. The block read is the ramp at the block midpoint,
the same convention as the cyclic shapes. It is therefore identical at any sample
rate and block size (`RampUpgrade_DurationDoesNotDependOn*`). TIME runs 0.05–60 s
and replaces RATE on the card while a ramp is selected.

### Trigger and reset semantics

| KEY SYNC | Cyclic shapes | Ramps |
| --- | --- | --- |
| Off (default) | Free-running, never reset (pre-0.7.5 behaviour) | Restart on the first note-on after every key is released; legato notes ride the same ramp |
| On | Phase reset to 0 on every note-on | Restart on every note-on |

- Note-ons are flagged while the block's MIDI is read and consumed at the top of
  `advanceLfosForBlock`, before the LFO advances. A restart lands within one block.
- The LFOs are global, so this is a **global retrigger**: the newest note restarts
  the LFO for every sounding voice (the user's choice over a per-voice rewrite).
- Note-offs never retrigger. Velocity-zero note-ons are note-offs.
- Transport start/stop and host loops do not reset LFOs.

---

## 6. Filter routing

Parameters `filterRouting` (SERIES / PARALLEL, default SERIES) and
`filterParallelBalance` (0 = Filter 1, 1 = Filter 2, default 0.5, modulatable).

The voice does not branch. With `p` the routing blend and `b` the balance, both
one-pole smoothed over 15 ms:

```
y1  = F1(x)
y2  = F2(y1 + (x − y1)·p)          p = 0: F2 hears F1   p = 1: F2 hears x
out = y2 + ((y1 + (y2 − y1)·b) − y2)·p
```

At `p = 0` this is exactly the previous serial chain; at `p = 1` it is exactly two
filters on one input blended by `b`. Every value between is continuous, so
switching mid-note does not click (`FilterRouting_SwitchingMidNoteDoesNotClick`).

The blend is **linear**, not constant-power: both filters receive the same signal,
so their outputs are correlated, and a constant-power law would add 3 dB for
matching filters. Linear returns unity (`FilterRouting_MatchingFiltersInParallelAddNoGain`,
measured −0.005 dB). A disabled filter still passes its input through its own
crossfaded bypass, in either routing.

The UI is a strip above the two filter cards: a SERIES/PARALLEL chip and a
BALANCE slider that greys out in SERIES.

---

## 7. Compatibility

DAW sessions and user presets store **normalised** parameter values, so widening
a range or growing a choice list silently changes what old values mean. State
version 12 handles it in `PluginProcessorState.cpp`:

| Change | Strategy |
| --- | --- |
| Envelope time ranges | States older than v12: decode with the old range (0–3/4/5 s, skew 0.45), re-encode with the new one. Seconds are preserved |
| LFO waveform list 4 → 6 | States older than v12: old normalised × 3 → index → re-encoded. Shapes are preserved. The LFO source child nodes also store the index directly |
| New parameters | States older than v12 set them to their defaults (SERIES, balance 0.5, key sync off, ramp 4 s, pitch mod 0), whatever the instance was set to |
| Parameter registration | New parameters are added at the end of the list, so host parameter indices do not move |
| "Pitch" → "Fine Tune" | Display name only; IDs `osc{1,2,3}Pitch` / `subOscPitch` unchanged |
| Factory presets | Defined in real units, converted with the current ranges; unaffected |
| INIT | Stored as a stateVersion 7 tree and migrated like any other old state |

Pinned by `StateUpgrade_*` and the preset round-trip tests. The one intended
audible change for old material is the modulation rule (§2).

Real-time safety: no allocation, locks or containers were added to the audio
path; the retrigger flags are plain audio-thread bools, and the ramp clock is a
capped double.

---

## 8. Summary

**Already present.** Assignable LFO and envelope destinations with a true 1.0
depth; per-note envelope restart; generators with no upper time limit; a
crossfaded filter bypass.

**Corrected.** The modulation swing rule (§2); envelope time ranges and the
editor drag limit; envelope defaults; the misleading PITCH label (now FINE TUNE).

**Newly implemented.** RAMP UP / RAMP DOWN with TIME up to 60 s; KEY SYNC on all
three LFOs; Pitch Mod destinations for Osc 1–3 and the sub; SERIES/PARALLEL
routing with BALANCE; state version 12 migration.

**Intentionally not added.**
- *A KEY SYNC switch on envelopes.* Every envelope already restarts with each
  note, so the switch would do nothing.
- *Key sync for filter modulation separately.* Filter modulation comes from the
  LFOs and envelopes above, so it inherits their behaviour.
- *Per-voice LFOs.* The user chose a global retrigger.
- *Tempo sync, ramp curve shapes, retrigger on transport.* Not requested; the
  ramp's linear travel can be shaped by the destination's own knob law.
- *A constant-power balance law.* It would add gain for correlated outputs (§6).
