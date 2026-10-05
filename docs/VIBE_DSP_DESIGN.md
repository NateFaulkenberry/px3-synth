# VIBE — Uni-Vibe model

`shared/DSP/Vibe/UniVibe.{h,cpp}`. A stage in the Synth's FX chain and the PX3
Vibe plug-in. Parameters (`fx.vibe.*`): `enabled`, `speed`, `intensity`, `mode`
(CHORUS / VIBRATO), `level` (±12 dB), `stereo` (LINKED / INVERTED).

Until 0.8.0 "VIBE" was two unrelated things on one card: a phaser labelled
Uni-Vibe, and the per-voice analog drift. The drift is now **ANALOG**
(`shared/DSP/AnalogDrift`, bit-identical to before — `PX3Tests analoggolden`).
This document is the Uni-Vibe.

## 1. The hardware

Sources:

* **[K]** R.G. Keen, *The Technology of the Univibe*,
  http://www.geofex.com/article_folders/univibe/univtech.htm
* **[D]** C. Darabundit, R. Wedelich, P. Bischoff, *Digital Grey Box Model of
  the Uni-Vibe Effects Pedal*, DAFx-19,
  https://www.dafx.de/paper-archive/2019/DAFx2019_paper_31.pdf
* **[P]** PerkinElmer, *Photoconductive Cell Application Notes*
* Clone BOMs (Aion Straylight, NeoVibe) for part values the sources omit.

Signal path [K], [D] §2:

1. **Input** — two jacks mixed by 22 k / 47 k to ground (×47/69, a third of the
   level lost).
2. **Preamp** — three transistors forming a discrete op-amp, gain ≈ 1 + 3.9k/1.2k
   ≈ 4.25. A phase splitter gives equal and opposite outputs that drive the first
   phase network, and its emitter is the **dry** feed to the mixer.
3. **Four phase networks** — each is a capacitor Cp from the previous stage's
   collector, and a 1 µF coupling capacitor in series with an LDR (and a fixed
   resistor) from its emitter, meeting at the next bootstrapped Darlington
   splitter. Cp = **15 nF, 220 nF, 470 pF, 4.7 nF** — no progression at all [K].
   The fourth stage is a buffer whose output is the **wet** signal.
4. **Mixer** — CHORUS: dry and wet through 100 k each into one node (an equal
   sum). VIBRATO: wet alone through a divider. Then the volume pot [K].
5. **LFO and lamp** — a darlington phase-shift oscillator, diode-limited, whose
   amplitude rises with speed (a first-order compensation for the lamp averaging
   at speed [K]). INTENSITY scales the drive into a lamp driver that also sets
   the lamp's idle current ("dim orange, about halfway") [K]. One incandescent
   lamp, four CdS cells, one reflective box.

What makes it not a phaser [D] §1, §3:

* **Unmatched stages.** With one lamp, all four LDRs move together, so the stage
  frequencies keep fixed ratios set by the capacitors: f3 ≈ 10·f4 ≈ 32·f1 ≈
  470·f2. In the mid band only two stages move a notch at a time: effectively
  one main notch, a weaker one in the bass ([K]: "a two-notch phaser, the second
  notch never raises its head"; [D] Fig. 9).
* **The stages are not all-passes.** The splitter's non-inverting and inverting
  gains differ (α, β) and the coupling capacitor matters:
  `H = α κe ω0/(ω0 + jω) − β (κc ω0 + jω)/(ω0 + jω)`, κc = Cp/(Cp+C_DC),
  κe = C_DC/(Cp+C_DC) [D] eq. 9. Each stage is a shelf: about +1 dB up top for
  three of them, and the 220 nF stage's κc = 0.18 gives a ~5 dB bass shelf that
  sweeps with the lamp — the throb [D] §3.1.
* **Lamp and photocells.** The light → resistance law is a power law
  (R ∝ L^−γ, γ < 1 for CdS) [P]; cells brighten faster than they darken, faster
  at high light [P], and the sweep's asymmetry grows with intensity [D] §4.2.

Measured cell resistances, one unit [D] Table 1:

| cell | Cp | bright (min) | dark (max) | α | β |
|---|---|---|---|---|---|
| 1 | 15 nF | 12.7 k | 2.79 M | 1.01 | 1.11 |
| 2 | 220 nF | 6.86 k | 2.59 M | 0.98 | 1.09 |
| 3 | 470 pF | 7.69 k | 3.32 M | 0.97 | 1.10 |
| 4 | 4.7 nF | 6.22 k | 4.16 M | 0.95 | 1.09 |

## 2. The model

### Stages — circuit model, [D] eq. 16–21

Each stage is one first-order section per channel, discretised with the bilinear
transform pre-warped at its ω0, recomputed every control tick:

```
T  = tan(π f0 / fs),  f0 = 1 / (2π (R_LDR + R6) Cp·C_DC/(Cp+C_DC))
b0 = α κe T − β(κc T + 1),  b1 = α κe T − β(κc T − 1),  a0 = T + 1,  a1 = T − 1
```

Note: [D] eq. 20 omits T from the non-inverting leg's numerator; the continuous
form (eq. 15) requires it, and with it the DC gain is ακe − βκc as it must be.

### Lamp and photocells — parametric, calibrated to [D] Table 1

[D] used 640 measured resistance curves per cell. We do not have the
measurements, so — as [D] §6 suggests — a parametric model, run every 16
samples (shared by both channels: there is one lamp):

```
lfo      = tanh(1.5 sin 2πφ) / tanh(1.5)                    diode-limited sine
A        = 0.8 · INTENSITY · (0.7 + 0.6 · SPEED)             rises with speed [K]
i        = max(0, 0.55 + A · lfo)                            idle current + drive
θ       += (i² − θ) · (i² > θ ? heat(8 ms) : cool(22 ms))    filament temperature
L        = θ³                                                light
ln g    += (0.9 ln L − ln g) · rate(τ)                       CdS, in the log domain
            τ = 4 ms brightening, 12 ms + 40 ms·(1 − L) darkening
g_sat    = g / (1 + g/1.5)
R_cell   = 1 / (g_sat / R_bright + 1 / R_dark)               per cell, Table 1
```

The coefficients are interpolated linearly per sample between ticks (a convex
combination of stable first-order sections is stable). No `pow`, `exp`, `tan`
or allocation runs per sample.

### Transistor stages

The preamp and the three Darlington splitters each clip with a biased, C1 soft
curve `x(27+x²)/(27+9x²)` (unity slope at zero), headroom 3.4 V [K], as [D]
§3.2.1 does with tanh. The dry and the wet each pass a 5 Hz DC blocker ([D]: the
bias pushes DC through as the stages approach DC; the original clicks there).

### Mixer, level, switches

CHORUS = (dry + wet) · 0.625, VIBRATO = wet · 0.86, then LEVEL. The two makeups
only level-match the modes against the input on pink noise; they do not change
what is mixed. INVERTED takes the right channel's wet with opposite polarity (the
last stage's collector — the stereo mod in [K]). Enable, mode and stereo
crossfade over 20 ms with smoothstep (C1); LEVEL is a 15 ms one-pole. Switched
off and faded out, VIBE returns its input bit for bit and resets.

### Constants and their status

| constant | value | status |
|---|---|---|
| Cp | 15 n, 220 n, 470 p, 4.7 n | verified [K], [D] |
| C_DC | 1 µF | verified [K], clone BOMs |
| α, β per stage | Table 1 | measured, one unit [D] |
| R_bright, R_dark per cell | Table 1 | measured, one unit [D] |
| R6 (series resistor) | 4.7 k | **unverified** (clone BOMs) |
| input pad, preamp gain | 47/69, 4.25 | verified [K] |
| headroom | 3.4 V | [K] (estimate from bias) |
| volts per full-scale sample | 0.5 V | **unverified** (choice) |
| clip bias | 0.15 | **unverified** ([D] gives none) |
| lamp idle / swing / speed rise | 0.55 / 0.8 / 0.7 + 0.6·speed | **unverified**, calibrated |
| heat / cool | 8 ms / 22 ms | **unverified**, calibrated |
| light exponent, CdS γ | 3, 0.9 | **unverified** (γ typical of CdS [P]) |
| CdS rise / decay | 4 ms / 12 + 40·(1−L) ms | **unverified** (within [P]'s 5–100 ms) |
| speed range | 0.5 – 8 Hz | [D]: 0 – 7.6 Hz on the pedal |
| mixer makeups | 0.625, 0.86 | level matching, measured |

Calibration (`PX3Tests vibecal`), intensity 1 at 2 Hz: cell 1 sweeps 11 k – 2.79 M
(Table 1: 12.7 k – 2.79 M), still more than a decade at 8 Hz; INTENSITY 0 holds
cell 1 at 294 k.

### Deliberate departures from the pedal

* No cancel switch and no speed/intensity coupling ([D] §2.1: the intensity pot
  loads the oscillator and drifts its rate). The coupling is a flaw of the
  circuit, not a feature anyone sets out to get.
* VIBRATO is level-matched to CHORUS; the pedal's divider ratio is unverified.
* No light-history (long-term memory) term: no measurement to calibrate it.
* No oversampling ([D] did not need it either: the clipping is gentle).

## 3. What the old VIBE was

Four ideal unit-magnitude all-passes with invented, monotonic, log-spaced ranges
(70–1000, 190–2300, 520–5200, 1100–9500 Hz), a sine LFO through one asymmetric
one-pole and `pow(·, 1.8)`, mapped linearly to log frequency — clamped, so above
INTENSITY 0.6 the stages parked at their ends for 45–72 % of every cycle — a
12 % "lamp bleed" tremolo on the wet, and a second LFO a quarter cycle ahead for
the right channel. A generic phaser with a Uni-Vibe label.

Measured with the same black-box tests (`tests/Tests/VibeRegression.h`):

| | old VIBE | new VIBE | test threshold |
|---|---|---|---|
| sweep rise fraction (0.5 = symmetric) | 0.47 | 0.39 | < 0.42 |
| more lopsided with intensity | no (0.47 vs 0.46) | yes (0.39 vs 0.41) | yes |
| frames with two deep notches | 0.62 | 0.12 | < 0.30 |
| VIBRATO bass level swing | 1.1 dB | 17.9 dB | > 6 dB |
| VIBRATO treble-over-bass tilt | +0.02 dB | +1.24 dB | > 0.5 dB |

## 4. Verification

* `PX3Tests vibe` — 30 checks: defaults, bit-transparency off, idle lamp at
  INTENSITY 0, capacitor stagger, non-all-pass stages, the character table above,
  VIBRATO pitch wobble, CHORUS vs VIBRATO, level match, LEVEL, transistor
  character, stereo modes, click-free switches and automation, silence, DC,
  finite at 22.05–192 kHz and blocks 1–1024, determinism, block-size
  independence, sample-rate independence, reset.
* `PX3Tests fxproducts` — PX3 Vibe as a plug-in: stereo, state, rates and
  blocks, bypass, card parity with the Synth.
* `PX3Diag vibe-renders <dir>` — guitar, chords, drums, pad, bass and a mix
  through off / low / high / slow / fast / chorus / vibrato, plus a synth chord
  clean / ANALOG / VIBE / ANALOG+VIBE, with rise fraction, notch range, step
  ratio and level per render.
* `PX3Bench vibefx` — CPU: the class alone, and 16 voices with VIBE on and off.
