# CHORUS — DSP Design

Four families of bucket-brigade (BBD) chorus, each built to the topology of the
hardware it is named after: the Roland SDD-320 Dimension D, a Solina-style
string ensemble, the BOSS CE-1, and the Roland Juno-60.

Rebuilt 2026-10-05 after an audit found the previous engine was one generic
anti-phase pair with a trapezoid LFO, presented under hardware names. The
problems it had are listed in §7 so they do not come back.

**The rule for every mode:** at the default control positions (RATE 0.35,
DEPTH 0.5, WIDTH 0.75, SPREAD 0.5, VINTAGE 0.5, LOW CUT 0, FEEDBACK 0, MIX 1)
and INTENSITY 1, the mode runs at its hardware's own figures. INTENSITY fades
from bypass to that hardware's wet/dry balance and stereo routing.

Every figure below is marked **[measured]** (taken from a recording or a
first-hand measurement of the hardware), **[documented]** (manufacturer or
peer-reviewed), or **[unverified]** (a modelling choice where no primary source
was found).

---

## 1. Shared structure

```
               ┌──────────────────────── one BBD line ─────────────────────────┐
 x ─► [HPF ─► pre-emph ─► 2:1 comp] ─► input LPF ─► (+fb) ─► tanh ─► delay(t) ─►
      └──────── Dimension only ───┘                         ▲   (cubic read)  │
                                                            └────── fb ◄──────┤
  ◄── [2:1 exp ─► de-emph] ◄── output LPF ◄─────────────────────────────────────┘
      └─ Dimension only ─┘
```

- **Delay.** A circular buffer read by a cubic (Catmull-Rom) interpolator. The
  delay follows the LFO **linearly in time**, as both published Juno models do.
  On the hardware the LFO drives the clock VCO, and delay is N/(2·f_clk). Whether
  the VCO's control law makes the delay linear in the LFO is **[unverified]**.
- **Filters.** Fixed, designed in `prepare()`, and stored per mode. Nothing is
  designed or `exp()`'d in the audio path. The user filters are a one-pole low
  cut, whose coefficient is computed per block and linearly smoothed per sample,
  and a tone tilt with a fixed coefficient.
- **Why not a clocked BBD simulation.** The Holters–Parker model clocks the BBD
  at 24–77 kHz (Juno-60) and needs no interpolation. Its only audible
  difference from fixed filters around an interpolated delay is aliasing from
  that clock, and the 5th-order input filter suppresses it (−33 dB at 20 kHz).
  The cost is several complex one-poles per BBD tick. A fixed-filter
  approximation reproduces the Holters–Parker magnitude response within 0.5 dB
  to 12 kHz (test `ChorusHw_JunoFiltersMatchHoltersParker`).
- **Saturation.** `tanh(k·v)/k` at the point the BBD samples its input, with
  k = 1.2 × VINTAGE. VINTAGE 0 is exactly linear; 0.5 (the default) is gentle.
  Measurements show the MN3009 rounds a sawtooth **[measured, pendragon]**; the
  amount is **[unverified]**.
- **Mode changes** crossfade two complete engines (two "slots") over 40 ms with
  a smoothstep (C1) gain. No delay, filter or routing switches in one sample.
  While the effect is inaudible, the mode changes outright.
- **The dry path is never filtered or delayed.** Latency: none.

---

## 2. JUNO-60 I / II / I+II

### Hardware

- One triangle LFO drives two 256-stage MN3009 lines, one for left and one for
  right. The right line's modulation is inverted (180°). Measurements from
  recordings **[measured, pendragon-andyh]**:

  | Mode | LFO rate | Delay range | Output |
  |---|---|---|---|
  | I | 0.513 Hz | 1.66–5.35 ms | stereo |
  | II | 0.863 Hz | 1.66–5.35 ms | stereo |
  | I+II | 9.75 Hz | 3.3–3.7 ms | ≈ mono |

  **I and II differ only in rate.** The Juno-60 service notes label the rates
  0.5 / 0.83 / "1" Hz; the Juno-6 notes label them 0.4 / 0.67 / 8.06 Hz
  (quoted by pendragon). The previous engine's 8 Hz for I+II came from the
  Juno-6 figure.
- Independent check (this project, against the jpcima and pendragon
  recordings): modulation periodicity 0.512 / 0.862 / ≈9.7 Hz. Saw L/R
  correlation: I 0.25, II 0.23, I+II 0.974, chorus off 1.000. The 0.974 shows
  that in I+II the wet has the **same polarity** on both sides and the lines
  move **in phase**.
- Outputs: L = dry + BBD_L and R = dry + BBD_R, both positive. The dry gain is
  0.83 against the BBD path **[unverified, jpcima's fit]**.
- Filters: 5th-order input and 5th-order output filters fitted to a real
  Juno-60 by Holters & Parker (DAFx-18) **[documented]**. Input filter −3 dB at
  6.5 kHz; output filter −3 dB at 8.8 kHz.
- **No compander** in the Juno-60 schematics **[measured, pendragon]**.
- The Juno-106 has I, II and I+II (both buttons pressed) **[documented, Roland
  JX-08 / SH-4d manuals]**. Its rates and circuit values were not obtained, so
  these modes are labelled JUNO-60.

### Model

- Two lines: line A fed from the left input, line B from the right. With a
  mono input this is identical to the hardware's single input.
- Rounded triangle LFO with corner fraction 0.01 for I and II. For I+II the
  corner is 0.08: jpcima hears the I+II LFO as "sine-like" **[unverified]**.
- Line B phase: 0.5 cycle for I and II, 0 for I+II, plus (SPREAD − 0.5).
- Centre 3.505 ms ± 1.845 ms for I and II; 3.5 ± 0.2 ms for I+II.
- Holters–Parker pole/residue filters, impulse-invariant, as parallel real
  sections (one first-order, two second-order), DC-normalised to the analogue
  gain. Bilinear was rejected: it warps the 7–10 kHz poles and darkens 10 kHz
  by 4–5 dB at 44.1 kHz.
- Output: L = 0.83·x + A, R = 0.83·x + B at INTENSITY 1. WIDTH scales (A − B);
  0.75 is the hardware.
- No compander, emphasis, low cut, feedback or random wander. LOW CUT and
  FEEDBACK remain as user extras, off by default.

---

## 3. DIM 1–4, 1+4, 2+4, 3+4 (SDD-320 Dimension D)

### Hardware

- Two independent BBD lines with compander and pre/de-emphasis; block diagram
  COMP → DELAY → EXP "for noise reduction"; MODE 1 softest, MODE 4 strongest;
  mono input feeds both channels **[documented, owner's manual]**.
- Sweep direction opposite on the two channels; out-of-phase signal fed to the
  opposite side **[documented, Home & Studio Recording review, 1984]**.
- LFO "almost perfectly triangular with only the tiniest amount of dwell"; two
  speeds (~2 s and ~4 s cycles) × two depths; delays ≈ 5 / 5.5 ms on the two
  sides (except button 1); ~8 kHz steep filtering after the lines; bass
  roll-off before the compander **[measured, one first-hand report (Fractal
  forum)]**.
- Mode 1: 8–12 ms **[measured, Klark Teknik BBD-320 thread]**.
- Combination buttons 1+4, 2+4 and 3+4 exist **[documented, Roland]**. What
  they do in the circuit is **[unverified]**.

### Model

| Mode | Rate | Centre (A / B) | Swing at DEPTH 0.5 |
|---|---|---|---|
| DIM 1 | 0.25 Hz | 9.5 / 10.5 ms | ±2.0 ms |
| DIM 2 | 0.25 Hz | 4.99 / 5.51 ms | ±2.5 ms |
| DIM 3 | 0.5 Hz | 4.99 / 5.51 ms | ±1.5 ms |
| DIM 4 | 0.5 Hz | 4.99 / 5.51 ms | ±2.5 ms |
| DIM 1+4 | 0.75 Hz | 9.5 / 10.5 ms | ±2.5 ms |
| DIM 2+4 | 0.75 Hz | 4.99 / 5.51 ms | ±2.5 ms |
| DIM 3+4 | 1.0 Hz | 4.99 / 5.51 ms | ±2.5 ms |

- Rates and the 2 × 2 structure follow the measurement. Which button gets
  which depth, and the depth values, are **[unverified]**, chosen so that
  detune rises from DIM 1 to DIM 4 as the manual requires.
- Combinations are switch states on **the same pair** (the unit has two BBDs).
  The model treats the two buttons' timing resistors as parallel, so the rates
  add; it takes the deeper depth and the lower button's centre delay
  **[unverified]**.
- The previous engine ran a second pair, which the hardware cannot do.
- One rounded-triangle LFO (corner 0.02, about 92% of each cycle at constant
  detune). Line B at +0.5 cycle.
- Per line: 1st-order HPF at 150 Hz; pre-emphasis shelf (zero 2 kHz, pole
  6 kHz, +9.5 dB) and its exact inverse; NE570-style 2:1 compander; 4th-order
  Butterworth at 8 kHz before and after the delay. Corners and orders are
  **[unverified]**.
- **Compander.** The compressor's rectifier is on its output (gain = ref /
  env(out), so out ∝ √(ref·in)). The expander's is on its input (gain =
  env(in) / ref). The detectors are matched (2 ms attack, 20 ms release, ref
  0.25, gain at most 20 dB). The pair is exactly inverse across a pure delay,
  and departs from it only where the filters and saturation between them
  change the signal. That departure is the colour.
- Output: L = dry + 0.5(A − B), R = dry − 0.5(A − B). The wet is pure side, so
  the mono sum is exactly the dry. The cross-feed gain of 1 (full cancellation)
  is **[unverified]**.

---

## 4. ENSEMBLE (Solina-style string ensemble)

### Hardware

- Three BBD lines, each clock VCO driven by the sum of a slow "chorus" and a
  fast "vibrato" three-phase generator, 0/120/240° **[documented, J. Haible]**.
- Rates: chorus ~0.6–0.8 Hz, vibrato ~6–6.4 Hz **[measured, coarse: forum
  scope traces; SS-30 blog]**.
- Mono output.

### Model

- Three lines from the mono sum. Slow sine at 0.7 Hz, ±1.6 ms; fast sine at
  6.3 Hz, ±0.12 ms; centre 6 ms. Line i at i/3 cycle on both generators
  (SPREAD scales the spacing). Swings and centre are **[unverified]**.
- 4th-order Butterworth at 7 kHz before and after **[unverified]**.
- WIDTH pans the lines L / centre / R with a sum-preserving law: gL = (1 − p),
  gR = (1 + p), each × 1/√3. The mono sum is always the original's mono output,
  dry + Σ line/√3. WIDTH 0 is the original.

---

## 5. CE-1 (BOSS CE-1 Chorus Ensemble)

### Hardware

- One 512-stage MN3002 **[documented]**. Clock 60–200 kHz, i.e. 1.3–4.3 ms
  **[unverified, forum]**.
- Output A (mono) carries direct + chorus. In stereo, output A carries **chorus
  only** and output B **direct only** **[documented, BOSS CE-2W manual, CE-1
  mode]**.
- Chorus INTENSITY is the only chorus control; the rate is fixed.

### Model

- One line from the mono sum; sine LFO 0.7 Hz; centre 2.8 ms, ±0.75 ms at
  DEPTH 0.5; 4th-order Butterworth at 6.5 kHz either side. All of these are
  **[unverified]**. No compander.
- Routing: s = INTENSITY × min(1, WIDTH / 0.75).
  - L = (1 − s)·dryL + wet
  - R = (1 − s)·(dryR + wet) + s·mono
- At the defaults this is the CE-1's stereo output (L chorus, R direct). WIDTH
  0 is its mono output, with direct + chorus on both sides.

---

## 6. Controls

| Control | Meaning | Hardware at |
|---|---|---|
| INTENSITY (`amount`) | bypass → the hardware's wet/dry and routing | 1 |
| RATE | ×2^((rate − 0.35)·3) on every LFO | 0.35 |
| DEPTH | swing ×(2·depth), never closer than 0.5 ms to zero delay | 0.5 |
| WIDTH | Juno/DIM: (A − B) × width/0.75; ENSEMBLE: pan; CE-1: spatial amount | 0.75 (ENSEMBLE: 0) |
| PHASE (`spread`) | pair: line B phase + (spread − 0.5); ENSEMBLE: spacing × 2·spread | 0.5 |
| VINTAGE (`character`) | BBD drive, 0 linear | 0.5 |
| TONE | wet tilt, one-pole split at 1.4 kHz | 0 |
| LOW CUT | extra wet HPF 20–420 Hz | 0 (20 Hz) |
| FEEDBACK | line output into its input, ≤ 0.55 | 0 |
| DRY/WET (`mix`) | final crossfade against the input | 1 |

---

## 7. What the previous engine got wrong (do not reintroduce)

1. **Juno: one line, inverted on R.** Both ears heard the same pitch
   trajectory and the mono sum cancelled the chorus. The hardware has two lines
   moving in opposition, added with the same polarity.
2. **Juno I and II differed in depth**, and I could not reach the hardware
   swing. On the hardware they differ only in rate.
3. **Juno I+II at 8 Hz, stereo.** It is 9.75 Hz and near mono.
4. **A trapezoid LFO** justified as "flats are steady detuning". Pitch is the
   derivative of delay, so a flat is *zero* detune. Its sine-warped ramps also
   left a slope step at every corner.
5. **DIM combinations as two stacked pairs at different rates.**
6. **ENSEMBLE had one slow LFO.** Its output L+R contained the third path
   while the comments claimed the mono sum cancelled.
7. **CE WARM was dry ± wet.** The CE-1's stereo is chorus on one side and
   direct on the other.
8. **The "compander" expander divided by the compressor's current gain** —
   computed on the undelayed signal, so it was not an inverse.
9. **`exp()` per sample** for every one-pole coefficient. A ±5% random rate
   and depth wander that no hardware had.

---

## 8. Tests (`PX3Tests chorus`)

The hardware tests (`ChorusHw_*`) measure the engine only from its stereo
output, so they compile against any version of it.

- Delay trajectories come from an impulse train. The wet peak after each
  impulse samples d(t).
- Juno: rates, swing, opposition / in-phase, wet polarity, the stereo image
  against the recordings, and the filter response against Holters–Parker.
- Mono sum per family.
- CE-1: chorus left / direct right.
- Dimension: rates and swings, triangle plateau share, one pair in the
  combinations, compander round trip at −20 and −6 dBFS.
- Ensemble: both generators, 120° spacing.
- Mode-change crossfade, and cost per sample.

---

## Sources

- pendragon-andyh, Juno60 chorus analysis (MIT): https://github.com/pendragon-andyh/Juno60/blob/master/Chorus/README.md
- jpcima, rc-effect-playground (ISC): https://github.com/jpcima/rc-effect-playground
- M. Holters, J. D. Parker, "A Combined Model for a Bucket Brigade Device and its Input and Output Filters", DAFx-18: https://www.hsu-hh.de/ant/wp-content/uploads/sites/699/2018/09/Holters-Parker-2018-A-Combined-Model-for-a-Bucket-Brigade-Device-and-its-Input-and-Output-Filters.pdf
- Roland JX-08 reference manual, JUNO-106 chorus modes: https://static.roland.com/manuals/jx-08_reference/eng/17811878.html
- Roland SDD-320 owner's manual: https://archive.org/stream/SDD-320_owners_manual/SDD-320_owners_manual_djvu.txt
- Home & Studio Recording, June 1984, Roland Dimension D review: https://www.muzines.co.uk/articles/roland-dimension-d/4054
- Dimension D first-hand measurement (Fractal Audio forum): https://forum.fractalaudio.com/threads/roland-dimension-d.21886/
- BOSS CE-2W owner's manual (CE-1 output routing): https://www.fullcompass.com/common/files/31565-BOSSCE2WChorusPedalOwnersManual.pdf
- J. Haible, String Ensemble / Triple Chorus: http://jhaible.com/legacy/triple_chorus/triple_chorus.html
- SS-30 chorus notes: http://ss30m.blogspot.com/2016/08/altogether-now-chorus-stereo-chorus.html
- Panasonic MN3009 (256 stages, t = N / 2 f_clk): https://xvive.com/audio/product/mn3009/
