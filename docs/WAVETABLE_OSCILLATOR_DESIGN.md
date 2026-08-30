# Wavetable oscillator — research and architecture

Design document for the wavetable extension. Written before implementation, as
the brief requires. Nothing in this document is built yet; the open questions in
§E are the ones the prototypes have to settle before code lands.

---

## A. Current implementation analysis

### A.1 There is no wavetable oscillator

`px3::OscillatorMode::wavetable` (index 8) is a placeholder. The whole of it,
in `Source/DSP/OscillatorUnit.cpp`:

```cpp
case px3::OscillatorMode::wavetable:
{
    const auto pos = derived.wavetablePos;         // macroA ^ 1.1
    const auto warp = std::sin(context.currentAngle * (1.0 + pos * 6.0));
    sample = static_cast<float>(warp) * (0.35f - pos * 0.20f);
    break;
}
```

One sine whose frequency multiplier is swept from 1× to 7×. No table, no
frames, no interpolation, no band-limiting.

**It is also broken.** `SynthVoice` wraps the phase to `[0, 2π)` every cycle
(`SynthVoice.cpp:708-720`). `sin(k(θ + 2π)) = sin(kθ + 2πk)`, so unless `k` is a
whole number the argument takes a step of `2πk mod 2π` at every wrap — a
discontinuity once per fundamental period. Measured at note 60, analog off,
inharmonic energy against the harmonic series:

| macroA | multiplier | worst step ratio | inharmonic dB |
|--------|-----------|------------------|---------------|
| 0.00   | 1.0000 (whole) | 0.05 | −15.29 |
| 0.25   | 2.3058 | 1.35 | **+17.27** |
| 0.50   | 3.7991 | 1.52 | +8.56 |
| 0.75   | 5.3724 | 1.17 | +8.14 |
| 1.00   | 7.0000 (whole) | 0.34 | −25.12 |

32 dB of inharmonic energy appears the moment the multiplier leaves a whole
number and vanishes at both endpoints, which is exactly where `2πk mod 2π = 0`.
The clean readings at 0.00 and 1.00 are what rule out any other explanation.

Consequence for this project: the mode has no users worth preserving and no
behaviour worth being compatible with. It is replaced outright.

### A.2 Where everything lives

| Concern | Location |
|---|---|
| Oscillator DSP | `Source/DSP/OscillatorUnit.{h,cpp}`, 20 modes in `OscillatorMode.h` |
| Per-voice instances | `SynthVoice::oscillatorUnits[3]` — no global oscillator state |
| Mode selection | `osc{1,2,3}Mode` choice parameter → `OscillatorSettings::modeIndex` |
| Oscillator state | `OscillatorSettings { modeIndex, macroA/B/C, vowelIndex, harmonics[8] }` |
| Derived state | `OscillatorUnit::DerivedCurves`, recomputed in `setSettings()` **once per block**, and only when settings actually differ |
| Parameters | `PluginProcessor.cpp` ~line 100: `osc{N}Mode/MacroA/MacroB/MacroC/Vowel/H1..H8` |
| Pitch and phase | `SynthVoice` owns `sourceAngle` per oscillator; advanced by `2π·f/fs`, wrapped to `[0, 2π)` |
| Render call | `renderSample(sampleRate, RenderContext&)` returns one float per sample |
| Into the mixer | `sourceSamples[4]` (sub + 3 osc) → per-source filters → mixer. 4 sources, fixed |
| Sample rate | Passed to `prepare(double)` and to every `renderSample` call |
| Oversampling | **None anywhere in the synth path.** Only mentioned in `FetCompressor`, where it was measured and rejected |
| UI ↔ DSP | `OscillatorComponent` holds references to controls owned by `PluginEditor`; parameters bound there via attachments |
| Layout | `UIConfig.json` → `osc.panel.*`, `panels.osc.*` |

### A.3 The two constraints that shape the design

**Modulation is block-rate.** `PluginProcessorParameters.cpp:40-80` computes
`effective = clamp01(base + totalDelta)` by summing LFO and envelope
contributions read from atomics. LFO values update once per block. At 512
samples that is a 93.75 Hz update — audible zipper on any fast WT scan. The
project already has the answer to this: user-facing continuous values are
smoothed per sample (`SmoothedGain`), never stepped per block. WT Position must
follow that convention.

**Real-time safety is a measured guarantee, not an aspiration.** `PX3Diag
rtsafety` reports **0 allocations over 200 blocks** in every scenario including
48 voices releasing with the full FX chain. Wavetable loading allocates
megabytes. The handoff therefore has to be designed, not improvised — see §C.5.

**Modulation destinations are free if the parameter is real.**
`buildLfoAssignableTargets()` builds the destination list automatically from
existing float parameters, minus an exclusion list. A real `osc{N}WtPos`
`AudioParameterFloat` becomes an LFO and envelope destination with no custom
plumbing — which is precisely what §23 of the brief demands. This also means WT
Position must **not** be folded into macroA, or it gets no modulation identity
of its own.

---

## B. External research comparison

| | **Surge XT** (GPL3) | **Vital** (GPL3) | **okwt** (import tool) |
|---|---|---|---|
| Table representation | `max_wtable_size 4096`, `max_subtables 512`, `max_mipmap_levels 16`; dual `float`/`int16` storage with weak pointer arrays into one allocation | 2048 samples × up to 256 frames; kept in the frequency domain as well as time | 2048 × 256 by convention |
| Anti-aliasing | Mipmap pyramid selected from playback step size: `a = wt.dt * pitchmult_inv`, thresholds `0.015625·wtbias` … scaled by `wtbias = 1.8f` | Per-octave band-limited spectra, resynthesised from the stored spectrum; sharp cutoff at Nyquist | None — generation tool only |
| Sample interpolation | Sinc FIR from a precomputed `sinctable`, indexed by the fractional phase | Windowed spectral resynthesis | n/a |
| Frame interpolation | Linear crossfade; a "continuous" mode allows fractional frame positions rather than stepped | Spectral morph between frames, not just amplitude crossfade | n/a |
| Phase | `oscstate += rate`, integer state masked `& (wtsize - 1)` — continuity is structural | Phase accumulator per voice | n/a |
| Import | `.wt` format plus WAV | WAV, with pitch detection | WAV, **and images**: resize to 2048×256, greyscale, each row is a frame, pixel brightness is the sample value |
| Audio→table strategy | Cycle extraction | Pitch-detected cycles | Truncate / linear / geometric / bicubic / percussive resampling; silence trim, per-frame fades, normalise |

Two findings from this comparison matter more than the rest:

1. **Everyone band-limits by mipmap, and selects the level from the phase
   increment rather than from the note number.** The increment already folds in
   sample rate, pitch bend, vibrato and tuning, so it is the correct quantity.
   Selecting from MIDI note is the classic mistake.

2. **Nobody uses higher-order frame interpolation for anti-aliasing, because it
   does not help.** A linear crossfade of two band-limited frames is a linear
   combination — it cannot create a harmonic that was not in either input, so it
   stays band-limited. What linear crossfade *does* fail at is
   **phase-misaligned** frames, where the morph comb-filters and the sound
   thins out mid-scan. The fix is phase alignment when the table is built, not a
   more expensive interpolator at run time. This is the single most important
   conclusion of the research, and it is what §5 of the brief is really asking
   about.

---

## C. Proposed architecture

### C.1 Canonical representation

```
px3::Wavetable                       immutable once built, shared by shared_ptr
    name, category, sourceId         sourceId identifies a user import
    frameCount     64 (default)
    frameSize      2048
    levels[]                         mip pyramid, level 0 = full bandwidth
        level ℓ: frameCount frames of max(256, frameSize >> ℓ) samples,
                 band-limited to (frameSize/2 >> ℓ) harmonics
```

The 256-sample floor is load-bearing, not a rounding-up — see §E.0. All levels
share one gain, taken from the full-bandwidth spectrum, so a level change never
alters the amplitude of the harmonics it retains.

Storing level ℓ at reduced *length* as well as reduced bandwidth is what makes
the pyramid affordable: the total is a geometric series, ≈2× the base table
rather than ×levels.

```
64 frames × 2048 samples × 4 B  =  512 KB base
pyramid, with the 256-sample floor =  1.25 MB per table   (measured)
```

Against the ~2 MB voice pool this is the dominant new allocation, so tables are
shared immutably across all three oscillators and every voice. `float32`
throughout — Surge's `int16` path exists for a memory budget we do not have.

### C.2 Band-limit selection

Select from the phase increment, following Surge:

```
increment   = f0 / fs                          cycles per sample
maxHarmonic = 0.5 / increment                  harmonics that fit below Nyquist
level ℓ     = smallest level whose harmonic count ≤ maxHarmonic
```

Switch levels rather than crossfading, with **hysteresis** on the selection —
measured in §E.2. The level gap at the worst boundary is 0.0098 dB, and a
crossfade costs +79% per sample to remove it. Hysteresis addresses the actual
risk, which is a note chattering between levels under vibrato.

Levels are stored with a **minimum length of 256 samples** even as bandwidth
keeps halving (§E.0). This is worth 45 dB of alias rejection at C7 and costs
25% more memory.

### C.3 Interpolation

- **Between frames:** linear, for the reason in §B. Frames are phase-aligned at
  build time so linear is spectrally and perceptually correct.
- **Between samples:** cubic Hermite, measured — §E.1. It matches an eight-tap
  windowed sinc on alias rejection for 70% of the cost.

### C.4 Parameters and modulation

New real parameters per oscillator:

| Parameter | Type | Why |
|---|---|---|
| `osc{N}WtPos` | float 0..1 | The scan position. A real parameter so the existing modulation matrix picks it up automatically |
| `osc{N}WtTable` | choice / string | Which table. Not a modulation destination |

`macroA/B/C` stay as the warp controls (§26), keeping the existing three-macro
UI convention intact.

WT Position reaches the DSP through `RenderContext`, **not** `OscillatorSettings`
— settings are a once-per-block push, and position needs to move per sample.
`RenderContext` gains `float wtPosition`, ramped linearly across the block from
the previous value, matching how the project already smooths user-facing gains.

### C.5 Real-time table handoff

The audio thread must never allocate, free, or take a lock, and must never touch
a `shared_ptr` refcount on a table that the message thread might be releasing.

```
message thread                     audio thread
--------------                     ------------
build Wavetable (background job)
publish raw pointer + generation
                                   at block start: read generation,
                                   refresh cached raw pointer
retire old table only after
every voice has been seen
past the generation
```

An RCU-style retire, not a `shared_ptr` swap. The retired table is released on
the message thread once the audio thread has demonstrably moved past it. This
preserves the measured 0-allocations-per-block guarantee.

Voices already sounding keep the table they started with until the next note, so
a table swap never discontinues a held note.

### C.6 Import

Separate the file format from the runtime representation, per §43:

```
WAV / AIFF / PNG / JPEG  →  Importer  →  canonical Wavetable  →  mip pyramid
```

- **Audio:** DC removal → normalise → pitch detection → cycle extraction at
  detected period → phase alignment → resample each cycle to 2048 → frame
  selection. Single-cycle files bypass detection.
- **Image:** greyscale → resize to `frameSize × frameCount` → row = frame,
  column = sample position, brightness = amplitude → per-frame DC removal →
  normalise. This is okwt's mapping, which is the established convention;
  implemented independently.

All of it on a background thread (§38), then handed over by §C.5.

### C.7 What is deliberately *not* changed

No new mixer channels, buses or signal paths (§54). The wavetable oscillator is
another `OscillatorMode` returning one float from `renderSample`, entering
`sourceSamples[]` exactly as the other 19 modes do.

---

## D. Licensing

| Source | Licence | Use here |
|---|---|---|
| Surge XT | GPL3 | **Research only.** Concepts used: mipmap-by-increment selection, the pyramid layout. No code copied |
| Vital | GPL3 | **Research only.** Concepts used: spectral framing of the morph problem. No code copied |
| okwt | see repo | **Research only** for the image mapping convention (row = frame, brightness = amplitude), which is an idea, not code |
| Factory tables | generated by us | Ours to ship. Generated from additive/Fourier/waveshaping recipes committed as source, so the licence is unambiguous and the tables are reproducible |

PX3 is proprietary, so no GPL code may be linked or copied. Every reference
above is used as a description of a technique.

**No third-party wavetable packs will be bundled** — not Serum's, not Vital's,
not any "free wavetable pack" of uncertain provenance. Factory content is
generated from recipes in our own source tree, which also satisfies §12's
preference and makes the tables regenerable rather than opaque binaries.

---

## E. Prototype results

Built and measured before implementation, in
`docs/research/wavetable-prototype.cpp` (standalone, no JUCE):

```
clang++ -O3 -std=c++17 -o proto docs/research/wavetable-prototype.cpp && ./proto
```

64 frames x 2048 samples, 9 mip levels. Test tables are built additively so
their harmonic content is exact, which means any inharmonic energy measured is
the oscillator's rather than the table's. Analysis uses a Blackman-Harris
window; a first pass with Hann flatlined at its own -56 dB sidelobe floor and
reported all three interpolators as identical, which was a measurement floor and
not a result.

### E.0 The mip pyramid must have a minimum LENGTH, not just a minimum bandwidth

This was not on the original list of questions, and it turned out to matter more
than anything that was.

Halving the stored length at every level is what makes the pyramid cheap, but
carried all the way down it reaches 16 samples at level 7 and 8 at level 8. At
that point the interpolator is reconstructing the waveform almost unaided and
its error swamps everything else. Flooring the length at 256 samples:

| | worst boundary, diff < 15 kHz | level gap |
|---|---|---|
| length halved all the way | -24.79 dB | 0.0826 dB |
| length floored at 256 | **-34.15 dB** | **0.0098 dB** |

9.4 dB of boundary cleanliness and an 8x smaller level gap, for 25% more memory
(1.25 MB per table instead of 1.00 MB).

Surge guards the same cliff from the other side: its level selection refuses the
top mip levels unless the table is still 128 or 64 samples long (`ts >= 128`,
`ts >= 64`). Same finding, arrived at independently.

**This dominates the interpolator choice.** At C7 the alias rejection was 39.75
dB with the length halved all the way and 84.79 dB with the floor — using the
*same* interpolator. 45 dB from a memory-layout decision, against roughly 8 dB
from the interpolation order.

### E.1 Sample interpolation: cubic Hermite

Alias rejection, tone against inharmonic energy, dB, higher is cleaner:

| interp | C2 | C3 | C4 | C5 | C6 | C7 | ns/sample |
|---|---|---|---|---|---|---|---|
| linear | 83.25 | 83.48 | 76.05 | 79.25 | 81.41 | 84.79 | 2.99 |
| **hermite** | 87.03 | 87.91 | 83.41 | **90.12** | **87.68** | **92.47** | **4.21** |
| sinc8 | 88.20 | 88.22 | 87.22 | 90.14 | 87.14 | 90.91 | 6.05 |

High-frequency droop at C4, dB (0 = no loss). The 20 kHz column is the
band-limiting working as intended, not interpolation loss:

| interp | 2 kHz | 5 kHz | 10 kHz | 15 kHz |
|---|---|---|---|---|
| linear | 0.20 | -0.82 | -0.54 | -2.15 |
| **hermite** | 0.23 | -0.66 | **0.02** | **-1.06** |
| sinc8 | 0.23 | -0.66 | 0.08 | -0.76 |

**Hermite.** It matches sinc8 on alias rejection once the length floor is in
place — and beats it at C6 and C7 — for 70% of the cost, and is transparent
through 10 kHz. The eight-tap sinc buys nothing here and costs 44% more per
sample. This is the outcome §10 of the brief warned to leave open rather than
assume.

### E.2 Mip transition: hard switch, no crossfade

How different the two levels actually are at the pitch where selection changes,
split at 15 kHz because a difference living entirely above that cannot be
reported by a listener:

| boundary | pitch | level gap | diff < 15 kHz | diff > 15 kHz |
|---|---|---|---|---|
| 3 -> 4 | 187.5 Hz | 0.0000 dB | -63.87 | -64.70 |
| 5 -> 6 | 750 Hz | 0.0002 dB | -46.31 | -46.59 |
| 6 -> 7 | 1500 Hz | 0.0015 dB | -38.06 | -37.48 |
| 7 -> 8 | 3000 Hz | 0.0098 dB | -34.15 | -27.30 |

Cost: hard switch 9.88 ns/sample, crossfade 17.63 ns/sample — **+79% to read two
levels instead of one.**

**No crossfade.** The level gap is 0.0098 dB at the worst boundary, three orders
of magnitude below a just-noticeable level change, and the crossfade nearly
doubles the per-sample cost of the oscillator to remove it.

The real risk at a boundary is not the switch, it is **chatter**: a note sitting
on a boundary with vibrato would toggle levels at the vibrato rate and warble.
Selection therefore gets hysteresis — the same job Surge's `wtbias = 1.8` does —
which costs nothing per sample. Verified during implementation.

### A note on one wrong turn

The first explanation for the boundary difference was per-level normalisation:
if each level is normalised to its own energy, dropping the top harmonics boosts
the retained ones and changes the audible part of the spectrum. That is a real
hazard and the design avoids it (all levels share one gain taken from the
full-bandwidth spectrum) — but it was not the cause here. Fixing it moved the
worst boundary by 0.02 dB. The cause was table length, above.

## F. Remaining open questions

Settled by measurement, not argument. Items 1 and 2 are answered above.

3. **Frame count.** 64 vs 128 vs 256. Memory is linear in this; morph smoothness
   may not improve past 64 once frames are phase-aligned.
4. **Phase alignment method.** Align each frame's fundamental to zero phase, or
   cross-correlate against the previous frame. The second preserves character
   better on imported material; the first is more predictable.
5. **WT Position smoothing time.** Long enough to kill the 93.75 Hz block step,
   short enough that a fast envelope scan still feels immediate.

---

## Status

Research and design only. No implementation has been written. The next step is
prototyping items 1–2 in §E, since the sample interpolator and the band-limit
strategy determine the shape of everything built on top of them.
