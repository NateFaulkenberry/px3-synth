# Oscillator DSP — research, measurements and architecture decisions

The design record for the oscillator remediation pass. Every decision here was
measured before it was implemented, in `docs/research/oscillator-prototype.cpp`
(standalone, no JUCE):

```
clang++ -O3 -std=c++17 -o oscproto docs/research/oscillator-prototype.cpp && ./oscproto
```

**Measurement method.** Every test tone is snapped to an exact FFT bin and the
waveform is exactly periodic in the 65,536-sample frame, so no window is used.
The harmonic grid bin is odd and the frame a power of two, so a folded component
can never land on a harmonic and hide there. "Alias" is every bin off the
harmonic grid, relative to the fundamental: in total, below 15 kHz, and as the
worst single bin. The three broken measurements recorded in
`WAVETABLE_OSCILLATOR_DESIGN.md` apply unchanged. A fourth was caught here: at
96 kHz a 2^14-sample pink-noise segment left the 25 Hz band a single bin wide,
and its variance read as filter ripple.

Unless stated otherwise, figures are at 48 kHz.

---

## 0. Decisions at a glance

| Area | Decision | Deciding measurement |
|---|---|---|
| Phase | `double` accumulator in cycles, [0, 1), one per oscillator; pitch changes move only the increment | float32 drifts up to 0.032 ct in 10 s; double is 1e-10 ct |
| Saw, square, pulse, triangle | 4-point PolyBLEP / PolyBLAMP (integrated cubic B-spline), with a 3-tap passband compensation | saw at C5: −65.7 dB of alias below 15 kHz, against −46.1 for 2-point and −19.2 naive |
| Output latency | Every oscillator mode and the sub lag their phase by exactly **5 samples** | the longest intrinsic latency (the FM decimator) sets it; the rest are padded to match |
| PWM | Same line, both edges corrected independently; DC removed analytically | the mean is exactly 2w − 1 at every width |
| Super Saw | 7 band-limited saws, continuous drift, 1/√7 normalisation | — |
| Hard sync | Reset at the exact fractional time, PolyBLEP4 on reset and slave wrap | C5, ratio 2.37: −52.3 dB below 15 kHz, against −6.6 sample-quantised |
| FM | 2× oversampled, 23-tap halfband, index tapered by Carson bandwidth near Nyquist | C6, ratio 3.5, index 10: in-band alias +5.4 dB → −61.5 dB |
| tanh stages | First-order ADAA with a tabulated antiderivative, at 1×; no oversampling | costs one tanh; the voice's two cascaded tanh become one ADAA curve: 15 dB cleaner for 40% of the CPU |
| Additive, organ, ISAAC | One phase accumulator per partial; partials faded out across a guard band below Nyquist | — |
| Wavetable | Unchanged: mip pyramid, hysteresis, Hermite (measured in `WAVETABLE_OSCILLATOR_DESIGN.md`) | level gap at a mip boundary 0.0098 dB |
| White noise | SplitMix32 per oscillator per voice, seeded by a hash of voice, note and oscillator | shipped LCG: correlation 0.9999 between voices; hashed: 0.0027 (floor 0.0029) |
| Pink noise | Kellet's refined coefficients, kept; colour filter made sample-rate aware | slope −3.02 dB/oct, ripple ≤ 0.25 dB at all four rates |
| DC | Removed at the cause where there is one; a 5 Hz blocker only on modes that measure DC after that | — |
| Mode switch | 5 ms crossfade between two independently rendered modes | — |
| Smoothing | Macros, tuning and Pitch Mod ramp per sample across the block; mode and bit depth are structural | — |

---

## 1. The research gate

**1. Why does a naive saw alias?** Stilson and Smith (§2): the ideal sawtooth is
not band-limited. Its harmonics fall at only 6 dB per octave and never stop, so
sampling it folds everything above fs/2 back into the band. Equivalently, the
discontinuity is rounded to the sample grid, which is pitch-period jitter.

**2. Why does a naive square alias?** For the same reason. It has two
discontinuities per period, and its odd harmonics also fall at 6 dB per octave.

**3. Why does a triangle still alias?** A triangle is continuous, but its slope is
not. Its harmonics fall at 12 dB per octave, so the folded energy is smaller but
still unbounded. Measured at MIDI 96: square −16 dB below 15 kHz, triangle −44 dB.

**4. How does (Poly)BLEP correct a discontinuity?** The ideal step is replaced by
a band-limited step: the running integral of a lowpass impulse response. Only the
*residual*, band-limited step minus ideal step, is added to the samples around the
discontinuity, at its exact fractional time. PolyBLEP approximates the lowpass
kernel with a polynomial B-spline, so the residual is closed-form with no table
(Välimäki & Huovilainen 2007; Välimäki, Pekonen & Nam 2012). A linear B-spline
touches the sample either side, which is the classic 2-point PolyBLEP. A cubic
B-spline touches four, so it needs two samples of lookahead.

**5. What happens under rapid frequency change?** The correction assumes the
discontinuity time computed from the current increment. If the increment changes
inside the kernel's support, the time estimate is off by a second-order amount in
Δinc/inc. For control-rate pitch modulation ramped per sample, as PX3 does, that
is negligible. It is a real limitation under audio-rate FM *of* a BLEP oscillator,
which PX3 does not offer.

**6. Why is PWM harder than a static square?** Both edges move independently.
The falling edge's time depends on the width at that instant. The edges can come
closer than the kernel's width, where corrections must *superpose* rather than
override. The DC offset moves with the width. Measured: at MIDI 96 a 5% pulse
reaches −28 dB of alias relative to its harmonic power, against −43 dB at 50%.

**7. Why is hard sync harder than a saw?** The resets land at arbitrary
fractional times, with a step height set by wherever the slave's phase happened
to be. They coexist with the slave's own wraps. Measured: moving the reset to the
exact time *without* a correction gains nothing (−7.1 against −6.6 dB). The
correction at the exact time is what gains 45 dB.

**8. Why can FM of two sines alias?** `sin(φ + I·sin θ)` is a nonlinear function
of the modulator. It generates sidebands at fc ± k·fm with Bessel amplitudes
J_k(I), significant out to k ≈ I + 1 (Carson's rule). Clean carrier and modulator
say nothing about where those land. Measured: MIDI 84, ratio 3.5, index 10 puts
its Carson top at 41 kHz, with in-band alias at +5 dB.

**9. Why does tanh alias?** A memoryless curve expands bandwidth. Its power
series generates every odd harmonic and intermodulation product without limit,
and whatever lands above Nyquist folds.

**10. Why does oversampling help a nonlinearity?** The generated products land
below a higher Nyquist, and the decimation filter removes them before the rate is
reduced.

**11. Why doesn't it fix oscillator aliasing?** A naive discontinuous source is
not band-limited at *any* rate, so its harmonics still fold. Measured: a naive saw
at 2× improves only 6 to 7 dB. Oversampling also cannot remove aliasing already
in its input, and it costs CPU and latency.

**12. When is a band-limited wavetable preferable?** For arbitrary and user
waveforms, whose discontinuity structure is not known analytically, and when
spectra are stored rather than computed. For a few known discontinuities under
continuous modulation (PWM, sync), an analytic correction is better, with no
memory.

**13. How should additive partials be handled near Nyquist?** Compute each
partial's frequency from the current pitch. Never generate one at or above fs/2,
and fade amplitudes to zero across a guard band below it. Otherwise a partial pops
in or out in one sample as pitch crosses the threshold (Stilson & Smith §3.8).

**14. How should fractional ratios be represented?** One phase accumulator per
partial, advanced by ratio·f/fs. Never a wrapped master phase multiplied by a
non-integer: that jumps a part-cycle at every wrap. This was the ORGAN, ISAAC, ROB
and DIGITAL bug.

**15. How do lo-fi modes tell intentional aliasing from accidental?** By
construction. Intentional artifacts are the ones the algorithm introduces as
controlled, sample-rate-independent quantities: bit depth, hold *time*, fold
count. Anything that changes with sample rate, comes from a wrap discontinuity,
is a DC bias from asymmetric quantisation, or depends on an unexplained constant,
is not. The check is invariance of the character across sample rates.

---

## 2. Classic waveform techniques compared

Quality columns are measured where PX3 measured them, and taken from the
literature otherwise (marked †).

| Method | CPU | Quality (saw, C5, <15 kHz) | Modulation | PWM | Sync | Complexity | PX3 suitability |
|---|--:|--:|---|---|---|---|---|
| Naive | 2.4 ns | −19 dB | trivial | poor | poor | trivial | no |
| Naive at 2× + halfband | ~2× + filter | −25 dB | trivial | poor | poor | low | no: the source is still unbounded |
| BLIT-SWS † | medium–high | tunable, ~90 dB low-band | good, but integrator state and DC transients on change (S&S §4.3) | good (BP-BLIT) | awkward | high | no |
| Table BLEP † | medium | high | good | good | good | medium (tables, lookahead) | no: PolyBLEP4 reaches the target without tables |
| minBLEP † | medium | high | good, zero latency | good | good | high | no: minimum-phase edges, and PX3 can afford 5 samples |
| DPW2 | ~3 ns | −35 dB | 1/(4·dt) gain: fragile at low pitch and under fast pitch change | via two DPW saws | poor | low | no: below PolyBLEP2 |
| PolyBLEP 2-point | 2.6 ns | −46 dB, −1.2 dB at 10 kHz | good, no latency | good | good | low | considered |
| **PolyBLEP 4-point + comp** | **3.9 ns** | **−62 dB, +0.2 dB at 10 kHz** | **good, 2 + 1 samples of lookahead** | **good** | **good** | **low** | **chosen** |
| Band-limited wavetable | medium, 2.25 MB/table | 68–98 dB (measured for WAVETABLE) | good | needs a table set | poor | medium | WAVETABLE mode only |

---

## 3. Architecture decision records

### Saw
- **Technique:** 4-point PolyBLEP. The residual of the integrated cubic B-spline
  is added at the exact wrap time, followed by a 3-tap passband compensation.
- **Reference:** Välimäki, Pekonen & Nam 2012 (integrated B-spline PolyBLEP);
  Stilson & Smith (why it aliases).
- **Problem it solves:** The discontinuity at the wrap.
- **Why it applies to PX3:** Three oscillators plus a sub, polyphony up to 64,
  continuous pitch modulation.
- **Advantages:** No tables, cost independent of pitch, exact at any fractional
  time, linear-phase.
- **Disadvantages:** The kernel rolls the passband off as sinc⁴(f/fs): −2.5 dB at
  10 kHz at 48 kHz. It also needs 2 samples of lookahead.
- **CPU implications:** 3.9 ns/sample against 2.4 naive.
- **Modulation implications:** The correction is exact for control-rate pitch
  change. It is second-order wrong under audio-rate FM of the saw itself, which is
  not offered.
- **Chosen approach:** 4-point with a symmetric `[−a, 1+2a, −a]` compensation,
  a = 0.25. That costs 3–5 dB of the 4-point's advantage but restores 10 kHz to
  +0.2 dB. It still beats 2-point by 15 dB in band *and* is flatter. The
  compensation and one pad sample give 3 samples of latency, and the line is padded
  to the common 5.
- **Rejected:** 2-point (15 dB worse in band), DPW2 (below 2-point), naive at 2×
  (−25 dB), table BLEP and minBLEP (no gain over this for PX3's needs).
- **Reason:** Measured.

| saw, 48 kHz, alias <15 kHz | MIDI 36 | 60 | 72 | 96 | 108 |
|---|--:|--:|--:|--:|--:|
| naive | −28.3 | −22.2 | −19.2 | −13.0 | −9.8 |
| DPW2 | −43.8 | −37.9 | −35.0 | −28.2 | −24.2 |
| PolyBLEP 2 | −54.8 | −48.9 | −46.1 | −38.8 | −34.2 |
| PolyBLEP 4 | −74.0 | −68.1 | −65.7 | −57.4 | −51.9 |
| **PolyBLEP 4 + comp 0.25** | **−69.8** | **−64.0** | **−61.6** | **−53.4** | **−48.1** |

At 96 kHz the chosen form reaches −96.9 dB at MIDI 72.

### Square
- **Technique, reference, problem:** As the saw, with a step of +2 at the wrap and
  −2 at the half cycle.
- **Chosen:** The same line, so the square is a pulse at w = 0.5.
- **Measured:** MIDI 72: naive −22.3, 2-point −49.6, 4-point −69.6, with
  compensation −66.3 dB below 15 kHz. The mean is 0.0000.

### Triangle
- **Technique:** 4-point PolyBLAMP: the integral of the BLEP residual, scaled by
  the slope change (±8·dt per sample).
- **Reference:** Välimäki et al. 2012 (BLAMP); DaisySP (integrated square, the
  alternative).
- **Problem it solves:** The slope discontinuities at the corners.
- **Advantages:** No integrator, so no leak, DC drift or low-frequency amplitude
  error (S&S §4.3).
- **Disadvantages:** The same sinc⁴ passband as the saw, compensated the same way.
- **Chosen:** PolyBLAMP 4 + compensation.
- **Rejected:** Leaky integration of a PolyBLEP square. It measures 29 dB worse in
  band at MIDI 96 (−57.8 against −86.7) and carries a leak-rate amplitude error.

### PWM
- **Technique:** A pulse on the PolyBLEP4 line. Rising edge at the wrap, falling
  edge where the phase crosses the width, each corrected at its own fractional
  time, so corrections superpose when the edges are close.
- **Problem it solves:** Two moving discontinuities, narrow pulses and moving DC.
- **Chosen:** Width is smoothed per sample. The analytic mean 2w − 1 is
  subtracted, so the pulse is zero-mean at every width. This is intentional:
  otherwise the width knob is also a DC offset knob. Width stays clamped to
  8–92%, as before.
- **Measured:** Alias relative to harmonic power at MIDI 60 is −49.7 dB (50%),
  −44.3 (10%) and −42.2 (5%), against naive −23.5 / −19.0 / −16.3.
- **Limitation:** At MIDI 108 a 5% pulse is narrower than a sample
  (dt = 0.087). Alias is −21 dB there, against +0.3 naive.

### Super Saw
- **Technique:** Seven saws on individual PolyBLEP4 lines, symmetric detune
  offsets, each softened by its own ADAA tanh.
- **Problem it solves:** It used to be seven naive saws. Each frozen random drift
  offset was chosen at note-on, so the "drift" never moved.
- **Chosen:**
  - Drift is a continuous, independent random walk per saw, in hertz, low-passed
    to a sub-hertz rate and scaled by SPREAD.
  - Seeded deterministically from the voice and note.
  - The sum is normalised by 1/√7, the level of uncorrelated saws, so level no
    longer depends on detune.
- **CPU:** 7 × (3.9 + 4.3) ns ≈ 58 ns/sample, against 7 × (2.4 + 4.4) ≈ 48 shipped.

### Hard Sync
- **Technique:** The master wraps at fractional time t. The slave's phase at t is
  computed exactly, the slave restarts from zero for the remaining 1 − t of the
  sample, and a step of −2·(slave phase at t) is corrected on the line. The
  slave's own wraps inside the same sample are corrected too.
- **Reference:** Stilson & Smith (discontinuity timing); the BLEP sync technique
  as described in the minBLEP and PolyBLEP literature.
- **Problem it solves:** The reset used to land on the sample.
- **Chosen:** PolyBLEP4 line, then the DRIVE stage through ADAA.
- **Measured** (alias below 15 kHz):

| | ratio 1.5 | 2.37 | 4.6 | 7.9 |
|---|--:|--:|--:|--:|
| MIDI 48 sample-reset | −23.0 | −13.1 | −3.4 | +14.0 |
| MIDI 48 exact PolyBLEP4 | −68.4 | −59.0 | −48.7 | −32.2 |
| MIDI 96 sample-reset | −11.1 | +1.6 | +27.3 | +0.3 |
| MIDI 96 exact PolyBLEP4 | −57.2 | −46.4 | −43.1 | −12.2 |

- **Limitation:** At high ratios on high notes the slave itself is at 16 kHz and
  above, and still aliases.

### FM
- **Technique:** 2× oversampling. The pair of points is taken at t = n and
  t = n + ½, and decimated by a 23-tap Kaiser halfband whose latency is exactly
  5 samples. The index is tapered so the Carson bandwidth
  fc + (I+1)·fm stays below the sample rate: below the *oversampled* Nyquist,
  where the decimator can still remove it.
- **Reference:** Chowning (FM, sideband structure); Carson's rule; JUCE
  Oversampling (the up/process/down architecture and FIR/IIR tradeoff).
- **Problem it solves:** Sidebands beyond Nyquist.
- **Advantages:** Musical behaviour is unchanged until the sidebands would fold.
  The taper engages only there and only as far as needed.
- **Disadvantages:** About 2.5× the CPU of 1× FM.
- **Measured** (alias below 15 kHz):

| | 1× | 2× FIR63 | **2× FIR23** | 2× FIR15 | 4× |
|---|--:|--:|--:|--:|--:|
| MIDI 84, r 3.5, I 5 | −29.5 | −123.6 | **−94.0** | −70.6 | −123.6 |
| MIDI 84, r 3.5, I 10 | +5.4 | −90.8 | **−61.5** | −38.9 | −90.8 |
| MIDI 96, r 1, I 10 | −39.6 | −145.8 | **−101.4** | −76.3 | −145.8 |
| MIDI 96, r 3.5, I 5 | +7.5 | −53.7 | **−53.5** | −41.7 | −91.4 |
| MIDI 96, r 3.5, I 10 | +8.5 | −3.8 | **−3.8** | −3.8 | −95.1 |

- **Chosen:** 2× with FIR23, plus the Carson taper, which removes the last row's
  case.
- **Rejected:**
  - 4×: it only wins where the taper already acts.
  - FIR63: its 15-sample latency would force every mode to 15.
  - FIR15: 23 dB worse at index 10.

### Additive (and ISAAC)
- **Technique:** One accumulator per partial at `ratio·f`, amplitudes computed
  once per block and ramped per sample. A raised-cosine fade takes each partial
  to zero between 0.42·fs and 0.48·fs of *its own* frequency.
- **Problem it solves:** Partials read `sin(angle·ratio)` from a wrapped angle,
  which is only valid for integer ratios, and nothing prevented a partial past
  Nyquist.
- **ROLL:**
  - It did nothing. ADDITIVE's static harmonic set ignored C.
  - It is now a spectral window rolled through the series. At C = 0.5 all partials
    pass. Towards 0 the upper partials roll off; towards 1 the lower ones do.
  - The set is renormalised by its RMS, so ROLL changes timbre, not level.
- **ISAAC:**
  - Stretched ratios h·(1 + 0.03·inh·h) now have real independent phases.
  - The half-frequency "shimmer" partial has its own accumulator, instead of a
    discontinuity at every wrap.
  - Labels: SPREAD → TILT, ROLL → STRETCH, matching what they do.

### Organ
- **Technique:** Nine drawbars at 0.5, 1.5, 1, 2, 3, 4, 5, 6, 8 × f, each with its
  own accumulator and a Nyquist fade.
- **Problem it solves:** The 16′ and 5⅓′ drawbars were `sin(angle·0.5)` and
  `sin(angle·1.5)` on a wrapped angle. That is not a sub-octave and a quint; it is
  a waveform that jumps a half cycle every period.
- **Chosen:**
  - Registrations unchanged.
  - The key click's decay is expressed in seconds.
  - The click noise comes from the oscillator's own seeded stream.

### Wavetable
- **Technique:** The v0.4.0 design is kept: mip pyramid selected by increment
  with hysteresis, 4× Nyquist headroom per level, cubic Hermite, linear frame
  crossfade, zero-phase-aligned imports.
- **Evidence:** `WAVETABLE_OSCILLATOR_DESIGN.md` §E. 68–98 dB of alias rejection
  across the keyboard. A 0.0098 dB level gap at the worst mip boundary, where a
  crossfade would cost +79% per sample.
- **Changed:** Only the plumbing. Phase comes from the oscillator's own
  accumulator, and the output joins the common 5-sample latency.
- **Rejected:** Crossfaded or continuous mip interpolation, on the measured gap
  above.

### Nonlinear processing (tanh)
- **Technique:** First-order ADAA (Parker, Zavalishin & Le Bivic 2016):
  y = (F(x) − F(x₋₁)) / (x − x₋₁), with F the antiderivative of the curve,
  falling back to the curve at the midpoint when |x − x₋₁| < 10⁻⁵.
  - F is *tabulated*: 2,049 nodes over ±12, cubic Hermite through exact values and
    exact slopes (F′ is the curve itself). The error is O(h⁴) and C¹ across cells.
  - Any static curve can be anti-aliased by building its table once.
- **Problem it solves:**
  - Every PX3 source passed through two tanh stages in series: the oscillator's
    `tanh(x)`, then the voice's `tanh(0.92·x)`.
  - Several modes add their own inside.
- **Why it applies to PX3:** The cascade is one static curve,
  `tanh(0.92·tanh(x))`, so one table anti-aliases both.
- **Measured** (alias below 15 kHz, and cost):

| | 1× | ADAA exact | ADAA table | 2× | 4× | 2× + ADAA |
|---|--:|--:|--:|--:|--:|--:|
| saw MIDI 72, drive 0.92 | −46.4 | −62.0 | −62.0 | −59.8 | −59.8 | −61.1 |
| saw MIDI 72, drive 3.3 | −31.6 | −47.6 | −47.6 | −50.9 | −54.5 | −55.8 |
| saw MIDI 72, drive 8 | −27.7 | −42.8 | −42.8 | −38.2 | −51.8 | −54.3 |
| ns/sample | 4.4 | 17.7 | **4.3** | 25.2 (FIR63) | ~50 | ~30 |

| voice source stage (two tanh → one ADAA curve) | MIDI 36 | 60 | 84 | 108 |
|---|--:|--:|--:|--:|
| input saw | −74.0 | −68.1 | −61.8 | −51.9 |
| shipped two tanh | −53.7 | −47.9 | −42.2 | −36.3 |
| **one ADAA curve** | **−70.3** | **−64.3** | **−58.4** | **−52.7** |
| level change | −0.01 | −0.02 | −0.10 | −0.52 dB |

- **Chosen:** Table ADAA at 1× for the voice source stage, SUPER SAW's edge
  softening, HARD SYNC's drive, ISAAC, FORMANT, ORGAN, DIGITAL, PHYSICAL, ROB and
  PX3.
- **Advantages:** Same cost as the tanh it replaces. The half-sample averaging
  also damps what aliasing was already in the input.
- **Disadvantages:** A half-sample delay, uniform across sources. A gentle top
  roll-off: −0.6 dB at 10 kHz, −0.5 dB of level at MIDI 108.
- **Rejected:**
  - Exact log-cosh ADAA: 4× the cost for identical numbers.
  - 2× and 4× oversampling of tanh: they only beat ADAA at drive ≥ 3, by 3–9 dB,
    for 6–12× the cost.
  - Replacing tanh with a curve that has a cheap closed antiderivative: it changes
    the sound.

### Oversampling
- **Decision:** Used in exactly one place, FM (and PX3's FM component), at 2×.
- **Why only there:** Phase modulation is the one nonlinearity in the oscillators
  that ADAA does not apply to, and the one whose products go furthest past
  Nyquist.
- **Filter:** Kaiser-windowed halfband, 23 taps, β = 6, as a streaming polyphase
  decimator. Verified sample-exact against the reference filter (worst difference
  8.9e-16).

---

## 4. Conventions

### Phase
- **Representation:** `double`, in cycles, wrapped to [0, 1) by subtraction. One
  wrap per sample at most, because the increment is clamped below 0.5.
- **Increment:** f / fs, recomputed every sample from the current frequency.
- **Continuity:** A pitch change changes the increment and never the phase. Pitch
  bend, vibrato, VIBE drift, Pitch Mod and tuning all arrive as frequency.
- **Ratios:** Each partial, detuned saw, slave or modulator owns its own
  accumulator at ratio·f / fs.
- **Reset:** Only at note start, and at a hard-sync master wrap, where it happens
  at the exact fractional time.
- **Phase modulation:** Added to the phase at read time and never stored, so the
  accumulator stays the pitch reference.
- **Note start:** All three oscillators and the sub start at the same phase, a
  hash of voice and note sequence. They used to start 120° apart, and the sub
  always at 0.

### Latency
Every oscillator mode and the sub produce their output exactly
`kOscillatorLatencySamples = 5` samples after the phase that generated it
(0.10 ms at 48 kHz):

| family | intrinsic | padding |
|---|--:|--:|
| PolyBLEP/BLAMP line + compensation | 3 | 2 |
| FM (2× + FIR23) | 5 | 0 |
| everything else | 0 | 5 |

So two oscillators at the same pitch stay phase-aligned whatever their modes, a
mode crossfade mixes aligned signals, and the sub stays aligned with the
oscillators. ADAA stages add a further half sample, uniformly across sources.

### Parameter rates

| Class | Parameters | Treatment |
|---|---|---|
| Immediate | enable (has its own fade), wavetable table (swapped at a block, per its design), vowel choice (ramped as coefficients) | at block start |
| Smoothed | macros A/B/C (all derived curves), harmonic trims, PWM width, FM ratio/index, sync ratio/drive, coarse/fine/Pitch Mod ratio, mode gain | linear ramp across the block, per sample |
| Audio-rate | phase accumulators, FM modulator, pitch bend and mod wheel (one-pole, time-constant based), vibrato, VIBE drift | per sample |
| Structural | oscillator mode, sub waveform (5 ms crossfade); DIGITAL bit depth and hold (stepped by design) | a controlled transition |

The wavetable scan keeps its measured 3 ms one-pole.

### Sample-rate independence
Every per-sample constant below was a hidden sample rate. Each is now derived
from a time or a frequency, keeping its 48 kHz value, the rate PX3 is tested and
tuned at. The one-pole rule is c(fs) = 1 − (1 − c₄₈)^(48000/fs), the decay rule is
k(fs) = k₄₈·48000/fs, and counts scale with fs.

| Where | Was | Now |
|---|---|---|
| Voice pitch-bend smoother | 0.06 per sample | 0.06 at 48 kHz, as a time constant |
| Voice mod-wheel smoother | 0.045 per sample | same rule |
| PWM mod-wheel width | unsmoothed target | the smoothed wheel |
| NOISE / PINK colour low-pass | coefficient 0.02–0.48 / 0.01–0.30 | same, as a time constant |
| ORGAN key-click decay | 0.0006–0.0036 per sample | per second |
| DIGITAL hold | 1–52 samples | the same time at 48 kHz |
| PHYSICAL damping and strike | 0.9995–0.9957 per sample; 10-sample burst | T60 in seconds; strike in seconds |
| ROB transient and onset | per-sample decay; 10–96 samples | seconds |
| ADDITIVE shimmer, PX3 movement | `noteAge · 0.0007` | hertz |
| Voice onset guard | 8–96 samples | the same time at 48 kHz |
| Voice release-tail smoother | 0.02–0.20 per sample | time constants |
| VIBE hiss pinking | Kellet economy coefficients | poles held at their 48 kHz frequencies |

### DC
Removed at the cause first:
- PWM's mean is subtracted analytically.
- DIGITAL's quantiser rounds symmetrically; `floor` biased it by half a step.
- ISAAC/ADDITIVE's half-frequency partial no longer jumps at the wrap.
- PHYSICAL's leaky state no longer integrates a DC transient.

Anything still measuring DC after that goes through a 5 Hz one-pole blocker on
that mode's output, and only that mode's. The list is recorded by the `oscdc`
diagnostic. VIBE's 12 Hz coupling capacitor is no longer relied on.

### Mode switching
When the mode changes, the old and new modes both render for 5 ms. Their outputs
share the common latency, so they are aligned, and they are crossfaded linearly.
Each mode owns its state, so the two renders do not interfere. A change that
arrives mid-fade waits for the fade to finish. A mode entering mid-note starts
from the oscillator's current phase.

---

## 5. The experimental modes

| Mode | Intentional | Mathematical requirement | Was unintended, now fixed |
|---|---|---|---|
| DIGITAL | bit crushing, phase quantisation, sample-and-hold aliasing, fold | pitch from the accumulator; hold is a time; zero-mean quantiser | hold in samples (character changed with rate); `floor` DC bias; fold read a non-integer multiple of a wrapped phase |
| PHYSICAL | inharmonic, bell-like partial structure; strike and ring | fundamental at the played pitch; decay in seconds | MATERIAL scaled the fundamental too (detuned the note); DECAY changed level, not decay; random phases and a 10-sample noise burst at note-on |
| ROB | nested phase modulation, transient smack, chaos | CHAOS = 0 contributes nothing; components band-limited by frequency | non-integer multiples of a wrapped phase (a discontinuity every cycle); 2% chaos noise at CHAOS = 0; per-sample decays |
| ISAAC | stretched, shimmering partials | independent partial phases; Nyquist fade | wrapped-phase partials; the shimmer partial jumped at every wrap (DC-heavy) |
| PX3 | FM + ISAAC + saturated saw, with slow movement | band-limited saw; bandwidth-aware FM; one meaning per knob | naive saw; A drove both morph *and* FM ratio *and* additive tilt |

PHYSICAL is a **synthetic modal resonator**, not a physical model. It is a small
bank of inharmonic sine modes with struck-and-held envelopes.

PX3's knobs are now defined as:
- **MORPH:** the FM ↔ ISAAC balance.
- **CHAR:** the aggression of the whole voice, meaning saw drive and FM index together.
- **MOVE:** depth and rate of the slow movement.

DIGITAL's aliasing from hold and phase quantisation is **intentional** and is
not band-limited. It is the sound of the mode.

---

## 6. Noise
- **White:**
  - SplitMix32, one stream per oscillator per voice, seeded at note start from a
    hash of voice index, note-start sequence and oscillator index.
  - Rendering stays reproducible: nothing reads `juce::Random::getSystemRandom()`,
    so sessions render identically.
  - Voices are independent; the shipped LCG gave every voice the same noise
    (correlation 0.9999).
  - Cost 0.5 ns against 1.0.
- **Pink:**
  - Kellet's refined filter, kept.

    | fs | slope (ideal −3.01 dB/oct) | ripple 25 Hz–16 kHz |
    |--:|--:|--:|
    | 44.1 kHz | −3.025 | 0.25 dB |
    | 48 kHz | −3.025 | 0.21 dB |
    | 88.2 kHz | −3.026 | 0.24 dB |
    | 96 kHz | −3.021 | 0.23 dB |

  - Its fixed coefficients are already sample-rate robust in the audible band. A
    1/f slope is scale-invariant, and the filter's top-octave shaping only moves
    further above 16 kHz as the rate rises.
  - Rejected, measured:
    - A refit at the running rate, least squares against Kellet's own response
      (−3.034 at 96 kHz): no better.
    - Pole mapping (−3.090): worse.
    - Alternating pole-zero cascades: 1.0 dB of ripple at 44.1 kHz, 5× the cost.
  - The part of the NOISE and PINK modes that *was* sample-rate dependent, the
    COLOR low-pass, is fixed.

---

## 7. Licensing

| Source | Licence | Use |
|---|---|---|
| Stilson & Smith 1996; Välimäki & Huovilainen 2006, 2007; Välimäki, Pekonen & Nam 2012; Parker, Zavalishin & Le Bivic 2016 | publications | Methods only. Residual polynomials derived here from the B-spline integrals |
| DaisySP | MIT | Read for structure (phase, width, reset). No code copied |
| JUCE `dsp::Oversampling` docs | JUCE licence | Architecture reference. PX3's decimator is its own |
| Kellet pink filter | posted to music-dsp as public domain | Coefficients used, as before |
| Pirkle, *Designing Audio Effect Plugins in C++* | book | Practical realtime reference |

No GPL code is used.

---

## 8. References

- T. Stilson, J. O. Smith, "Alias-Free Digital Synthesis of Classic Analog Waveforms", ICMC 1996. https://freeverb3-vst.sourceforge.io/doc/blit.pdf
- V. Välimäki, A. Huovilainen, "Oscillator and Filter Algorithms for Virtual Analog Synthesis", *Computer Music Journal* 30(2), 2006. https://research.aalto.fi/en/publications/oscillator-and-filter-algorithms-for-virtual-analog-synthesis/
- V. Välimäki, A. Huovilainen, "Antialiasing Oscillators in Subtractive Synthesis", *IEEE Signal Processing Magazine* 24(2), 2007. https://research.aalto.fi/en/publications/antialiasing-oscillators-in-subtractive-synthesis/
- V. Välimäki, J. Pekonen, J. Nam, "Perceptually informed synthesis of bandlimited classical waveforms using integrated polynomial interpolation", *JASA* 131(1), 2012.
- Aalto Acoustics Lab, Virtual Analog Synthesis and Audio Effects. https://www.aalto.fi/en/department-of-signal-processing-and-acoustics/virtual-analog-synthesis-and-audio-effects
- J. Parker, V. Zavalishin, E. Le Bivic, "Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution", DAFx-16.
- DaisySP oscillator. https://github.com/electro-smith/DaisySP/blob/master/Source/Synthesis/oscillator.h
- JUCE `dsp::Oversampling`. https://docs.juce.com/master/classjuce_1_1dsp_1_1Oversampling.html
- W. Pirkle, *Designing Audio Effect Plugins in C++*, Routledge.
- P. Kellet, pink noise filter, music-dsp archive. https://www.firstpr.com.au/dsp/pink-noise/
