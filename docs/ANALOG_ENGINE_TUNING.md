# AnalogEngine — Tuning

The internal constants, what each one does, and what the engine measures with
them at their current values. Every number below is output from
`PX3Tests analog`; nothing here is estimated.

Companion documents: `ANALOG_ENGINE_RESEARCH.md` (why this architecture),
`ANALOG_ENGINE_ARCHITECTURE.md` (where each stage sits).

---

## 1. The constants

None of these is a plugin parameter. They are compiled defaults, reachable at
runtime only through the debug console, and they revert on every construction.

| Key | What it does |
|---|---|
| `pairDrive` | Pre-gain into the transfer, shared by the channel and the buses that invert it. **Must be identical on both sides or the pair does not cancel.** |
| `masterDrive` | The master's own drive. Independent, because the master is a forward output stage rather than half of a pair. |
| `fxBusTrim` | Scales how much of the FX bus stage is mixed in — **not** its drive, which would break invertibility on the FX path. |
| `curveBlend` | The invertible pre-warp. 0 is a plain sine pair; 1 folds level into the argument. Moves the harmonic distribution. |
| `evenHarmonic` | Asymmetric `x²` term, forward side only. This is the second harmonic — the "warmth" axis. |
| `slewEnhance` | Arcsine slew on the channel, sine on the bus. Transient personality. |
| `hfRolloffHz` | Bandwidth. |
| `hfLevelDependence` | How much top the stage loses as it is driven. |
| `lfCornerHz` | Coupling capacitor. |
| `lfLevelTrim` | How far the LF corner rises with level — transformer behaviour. |
| `dcBlockHz` | DC blocker, on the inverse side and the master only. |
| `headroom` | Divides every drive; how far from the knee the profile sits. |
| `engineAmount` | Global wet mix for the engine. |
| `outputTrim` | Per-**stage** makeup, so an A/B is not a loudness comparison. |

## 2. Current values

| | CLEAN | BRITISH | AMERICAN | TRANSFORMER | MODERN |
|---|---|---|---|---|---|
| `pairDrive` | 0.85 | 1.05 | 1.15 | 1.10 | 0.90 |
| `masterDrive` | 0.70 | 0.88 | 0.92 | 0.95 | 0.75 |
| `curveBlend` | 0.10 | 0.30 | 0.62 | 0.20 | 0.45 |
| `evenHarmonic` | 0.0 | 0.022 | 0.008 | 0.030 | 0.004 |
| `slewEnhance` | 0.06 | 0.18 | 0.28 | 0.14 | 0.10 |
| `hfRolloffHz` | 21000 | 16000 | 18500 | 13500 | 20000 |
| `hfLevelDependence` | 0.10 | 0.38 | 0.22 | 0.52 | 0.14 |
| `lfCornerHz` | 6 | 18 | 14 | 26 | 8 |
| `lfLevelTrim` | 0.05 | 0.28 | 0.12 | 0.45 | 0.08 |
| `headroom` | 1.15 | 0.95 | 0.90 | 0.88 | 1.25 |
| `outputTrim` | 1.0060 | 1.0498 | 1.1928 | 1.0234 | 1.0781 |

`fxBusTrim` 0.70, `dcBlockHz` 5.0, `engineAmount` 1.0 across all profiles.

## 3. Measured behaviour

### The architecture

```
transfer pair is an exact inverse   worst |g-1(g(x)) - x| = 4.2e-7, at every blend
one channel, THD                    CLEAN 0.003%  BRITISH 0.601%  AMERICAN 0.327%
                                    TRANSFORMER 0.899%  MODERN 0.212%
1 channel -> 2 channels (CLEAN)     0.010%  ->  0.359%          a 36x step
busier mix (constant per-channel)   1ch 0.000%  2ch 0.063%  4ch 0.356%  8ch 6.768%
distributed vs two bus stages       1.290%  vs  2.947%
```

The single-channel figures are the colour, not the summing: with
`evenHarmonic` disabled BRITISH falls from 0.601% to **0.031%**. That is the
whole claim — the channel is transparent to the summing nonlinearity, and what
remains is the desk's tone.

The 1→2 channel step is where the architecture switches on. At constant *total*
level it does not keep climbing (N channels at x/N sum toward linearity), which
is why the busier-mix figure is measured separately: at constant *per-channel*
level, which is what adding parts to a mix actually does, it climbs hard.

### Profiles

```
THD, 4 channels     CLEAN 0.313%  BRITISH 1.290%  AMERICAN 3.869%
                    TRANSFORMER 0.855%  MODERN 2.629%
even/odd balance    CLEAN 0.000   BRITISH 0.128   AMERICAN 0.008
                    TRANSFORMER 0.381   MODERN 0.014
11 kHz retention    TRANSFORMER 0.642   MODERN 0.876
level match         within 0.17 dB across all five
```

All ten profile pairs differ in THD, even/odd balance or bandwidth. TRANSFORMER
carries the most even-harmonic content and the least bandwidth; CLEAN is
essentially only the summing behaviour, which is what it exists to demonstrate.

### The pre-warp

```
blend 0.00   H3 -32.5 dB   H5 -55.6 dB
blend 0.25   H3 -37.4 dB   H5 -42.7 dB
blend 0.62   H3 -21.8 dB   H5 -35.0 dB
```

A real change in harmonic *distribution*, not a gain — and the pair stays exactly
invertible at every value.

### Level and frequency

```
THD vs level    0.05 -> 1.328%   0.15 -> 2.798%   0.35 -> 3.947%   0.60 -> 4.074%
THD vs freq     100Hz 1.438%  500Hz 1.381%  1kHz 1.359%  5kHz 1.138%  10kHz 1.202%
```

### Safety

```
DC, all profiles          < 1e-6
silence, 44.1/48/88.2/96  exactly 0
extreme overload (x4 in)  bounded, peak 1.37 - 2.15
```

---

## 4. Oversampling: the decision and its number

```
worst fold-down at 1x   -142.5 dB
worst fold-down at 4x   -137.4 dB
audibility bar           -60 dB
```

**No oversampling.** The 1× fold-down sits 82 dB below the bar, and 4× measured
no better — within run-to-run noise of the same figure.

That is a stronger result than expected and it has a reason: the transfer pair
is smooth and bounded, the sources are already trimmed to −4 dB of headroom, and
the aggressive part of the curve is only reached by the *sum*, which is
band-limited by everything upstream of it. There is nothing here generating the
wideband harmonics that oversampling exists to catch.

Antiderivative antialiasing was considered (research document, §3) and is not
used: it introduces a half-sample delay and degenerates near stationary input,
for a problem that measures 82 dB below audibility.

`juce::dsp::Oversampling` would also have added plugin latency where the
instrument currently reports none. Paying that for an inaudible improvement
would have been a bad trade made to look rigorous.

---

## 5. What the measurements changed

Recorded because most of these were wrong first, and the numbers are what said so.

1. **Blending two curves is not invertible.** The inverse of a blend is not the
   blend of the inverses — 6.6e-4 of error at AMERICAN's setting. Replaced with
   an invertible pre-warp: 4.2e-7, at every blend.
2. **`channelDrive` and `busDrive` must be equal.** Separate constants looked
   like extra control and were a way to silently break the premise.
3. **Colour must sit outside the pair.** Filtering between the forward and
   inverse transfer breaks the cancellation: `g⁻¹(HP(g(x))) ≠ HP(x)`. This was
   the big one — single-channel THD 2.267% → 0.447%, and it is what turned the
   accumulation curve the right way up.
4. **The DC blocker was doing the same thing** one stage later. Moved to the
   inverse side and the master only: another 1.1% off AMERICAN.
5. **`x·|x|` produces no even harmonics.** It is half-wave symmetric, so its
   series is odd-only — the "even-harmonic bias" was generating none. `x²` does.
   Even/odd balance went from 0.000 everywhere to 0.381 on TRANSFORMER.
6. **A level detector at 18 Hz is a modulator.** It still rippled at twice the
   signal frequency, and a filter corner modulated at 2f amplitude-modulates.
   Slowed to 1.5 Hz.

Two of the measurements were also wrong before the DSP was:

- Comparing waveforms sample-by-sample reports the colour filters' **phase
  shift** as if it were distortion — 39% on AMERICAN, whose THD is 0.33%.
  Transparency is measured as THD.
- A DFT over a window containing the coupling filter's settling transient reads
  that transient as broadband THD. The measurement now discards it, and lets the
  1.5 Hz level detector settle too.

---

## 6. Tuning it by ear

The constants are not parameters, so the only way to reach them is the debug
panel — and the debug panel is **compiled out of the normal build**.

```bash
cmake -B build/tune -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPX3_DEBUG_PANEL=ON
cmake --build build/tune --target PX3Synth_Standalone
open "build/tune/PX3Synth_artefacts/RelWithDebInfo/Standalone/PX3 Synth.app"
```

Then: the plugin menu (top right) → **DEBUG**, and the window's left column,
**section D. ANALOG ENGINE**.

```
ANALOG ENGINE  (0 = off, 1 = on)      <- ships at 0; nothing is audible until this moves
PROFILE                               <- CLEAN / BRITISH / AMERICAN / TRANSFORMER / MODERN
RESET TO COMPILED DEFAULTS
Engine Amount, Pair Drive, Master Drive, FX Bus Trim, Headroom,
Curve Blend, Even Harmonic, Slew Enhance, HF Rolloff, HF Level Dependence,
LF Corner, LF Level Trim, DC Block, Output Trim
```

Every slider reads back its live value and, when it differs, the compiled
default beside it.

Three things worth knowing before turning knobs:

- **Changing PROFILE reloads that profile's whole tuning set**, discarding any
  edits. The sliders follow, so what you see is always what is running.
- **Nothing here is saved.** Not to presets, not to DAW state, not to
  UIConfig — by design, and there is a test that asserts it. Write values down
  or they are gone on the next launch.
- **The effect is quiet on a single sustained note by construction.** One
  channel is transparent; the character lives in the summing. Play chords, or
  a patch with several sources enabled, or it will seem to do nothing. That is
  the architecture working, not a fault.

A good first pass: enable it, set PROFILE to TRANSFORMER (the most coloured),
play a four-note chord with all four sources on, and A/B the enable slider.
Then compare profiles at the same material before touching any constant.

## 7. Future: JSON

Not implemented, deliberately. The intended shape:

```
shared/DSP/Analog/AnalogEngineConfig.json
    │   one block per profile, one key per constant
    ▼
AnalogEngine::setTuningValue on load and on hot-reload
    │
    ▼   compiled defaults remain the fallback for every key the file omits
```

It should reuse `UIConfigManager`'s file-watch so values can be tuned by ear
without recompiling. It must **not** become part of preset or DAW state: the
constants describe the instrument, not a patch. The current split — compiled
constants, debug console for experiments, nothing serialised — already enforces
that, and the JSON layer should preserve it.

---

## 8. Honest limitations

- **Nobody has listened to this.** Every claim above is a measurement. The
  profiles are demonstrably distinct in THD, harmonic balance and bandwidth;
  whether they are distinct *musically*, and whether the defaults are the right
  defaults, needs ears.
- **It ships disabled.** `analogEnabled` defaults false, so no existing patch
  changed. Turning it on by default is a one-line change once it has been heard.
- **The archetypes are informed by research into design families, not by
  measurement of specific hardware.** No claim of emulation is made anywhere.
- **The slew stage is not part of the invertible pair.** It is colour, and at
  the current values it contributes nothing measurable to single-channel THD
  (0.601% with and without). It is either too subtle to matter or wrong, and the
  measurements cannot currently tell which.
