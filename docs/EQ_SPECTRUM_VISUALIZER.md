# EQ Spectrum Visualiser — Diagnosis, Research, Design

The analyser behind the bus EQ looked blocky and sluggish. This documents which
stage was responsible for each symptom — measured, not guessed — what
professional analysers actually do, and what changed.

---

## 1. Diagnosis, per stage

The pipeline was:

```
push (mono sum) -> ring 8192 -> Hann 4096 -> FFT order 12
   -> resample to 1024 log points, PEAK over a +-6% band
   -> fast-up / 168 ms-down decay -> polyline through 1024 points
```

Two independent faults, both measured.

### Fault 1 — the low end has almost no measurements

At 48 kHz a 4096-point FFT has 11.72 Hz bins:

| FFT | bin width | window | bins below 100 Hz | bins below 200 Hz |
|---|---|---|---|---|
| 2048 | 23.44 Hz | 42.7 ms | 4 | 8 |
| **4096** | **11.72 Hz** | **85.3 ms** | **8** | **17** |
| 8192 | 5.86 Hz | 170.7 ms | 17 | 34 |
| 16384 | 2.93 Hz | 341.3 ms | 34 | 68 |

On a log axis from 20 Hz to 20 kHz, everything below 100 Hz occupies **202 px**
of an 868 px plot. So roughly a quarter of the display was being drawn from
**8 measurements**. That is the blockiness at the left, and no amount of
interpolation invents the missing detail.

### Fault 2 — adjacent display points read the identical bin

The resampler took the peak over a ±6% band around each display frequency. At
33 Hz that band is 3.9 Hz wide — a third of one FFT bin — so consecutive display
points resolved to the *same* bin and returned the *same* number.

Measured: a worst-case run of **42 consecutive display points reading the
identical bin**, which is a **36 px dead-flat step** in the curve. This is the
stair-stepping, and unlike fault 1 it is purely an artefact of the resampler —
the data underneath varies, the aggregation threw it away.

### What was NOT the problem

- **Temporal decay.** Attack is already instantaneous (`db > smoothed ? db :
  …`) and the fall is a 168 ms constant. The "sluggish" impression comes from
  the 85 ms analysis window smearing transients, not from the smoother.
- **Point count.** 1024 points across 868 px is already more than one per
  pixel. Adding points to a resampler that returns duplicates only produces
  more duplicates.

---

## 2. Research: what professional analysers do

The important finding is a negative one, and it comes from FabFilter directly.

Their own forum answer to "is Pro-Q's analyser a multi-rate FFT?" is: **"It's a
regular FFT analyser."** Pro-Q instead exposes the resolution as a user choice —
Low 1024, Medium 2048, High 4096, Maximum 8192 — and documents the tradeoff
plainly: higher resolution gives more precision in the low frequencies but
"requires more incoming samples to calculate a single spectrum, resulting in a
lower update rate and slower attack time."

So the brief's suggestion of multi-resolution analysis is **not** what the
reference product does. The frequency-versus-time tradeoff is fundamental —
it is the uncertainty principle, not an implementation shortcoming — and the
professional answer is to put it under the user's control rather than to hide it
behind a hybrid that is slow in the bass *and* smeared in the treble.

Sources:
[FabFilter forum — is the analyser multi-rate?](https://www.fabfilter.com/forum/topic/6697/spectrum-analyzer-of-pro-q-is-a-multi-rate-fft-analyzer),
[Pro-Q analyser documentation](https://www.fabfilter.com/help/pro-q/using/analyzer),
[Pro-MB analyser documentation](https://www.fabfilter.com/help/pro-mb/using/analyzer).

**Decision: no multi-resolution analysis.** One FFT, with the size exposed the
way Pro-Q exposes it. Building a multi-rate analyser here would add real
complexity — two ring buffers, two FFTs, a crossover between them and a seam to
hide — to solve a problem the reference implementation does not consider a
problem.

---

## 3. What changes

**Fault 2 is a bug and is fixed outright.** The resampler interpolates between
FFT bins instead of taking a peak over a band narrower than a bin. Where a
display point falls between two bins it gets a value between them; where it
spans many bins it still takes the peak, because a narrow resonance inside a
display bin is exactly what someone opened the analyser to find.

**Fault 1 is a tradeoff and is exposed, not engineered around.** The FFT size is
a setting, defaulting to 4096 — the same default region Pro-Q calls "High" —
with the consequence documented rather than hidden.

**The analysis is decoupled from the GUI frame rate.** It runs on a fixed hop so
the spectrum is the same whether the display is at 30, 60 or 120 Hz.

---

## 4. Measured result

The resampler is tested against a synthetic spectrum that differs in every bin,
so any flat run in its output belongs to the resampler and not to the signal:

| aggregation | longest run of identical display points |
|---|---|
| peak over a ±band (before) | **78** |
| interpolate between bins (after) | **6** |

A run of 6 out of 1024 points is about 5 px, and it sits at the very bottom of
the axis where two adjacent display points fall inside the same pair of bins
with nearly the same weight. That is the data running out, not the resampler
throwing it away — and it is the floor that only a longer FFT can lift.

## 5. Stereo

Mono sum, taken on the audio thread. This is a **bus** EQ: the question it
answers is the tonal relationship between the signal and the curve, and two
traces differing only in stereo detail make that harder to read rather than
easier. A separate L/R display would be a different feature with a different
purpose.

## 6. Known limitations and what is not yet done

The brief asks for considerably more than this pass delivers. What is **not**
done, so it does not read as finished:

- **FFT size is still fixed at 4096.** The research says this should be a user
  setting the way Pro-Q's is; it is currently a compile-time constant.
- **Analysis is still driven by the GUI timer**, one FFT per frame at 60 Hz.
  That happens to give ~80% overlap at 48 kHz, but the analysis rate should not
  be a consequence of the repaint rate. Decoupling it is designed but not built.
- **No curve interpolation in screen space.** The path is still a polyline
  through the resampled points. With more than one point per pixel this is
  hard to distinguish from a spline, but it has not been compared.
- **The static grid is not cached.** The VU meter's face is; this graph still
  redraws its grid, labels and ticks every frame.
- **No FFT size comparison was run** (1024 / 2048 / 4096 / 8192 / 16384 against
  CPU, latency and visual quality), and no paint-cost measurement was taken for
  this component specifically.
- **No debug overlay** showing FFT size, hop, analysis rate, points or paint
  time.
- **Not evaluated against real material by me.** I cannot see the rendered
  panel. The plateau fix is measured; the *look* is not, and the sluggishness
  attributed to the 85 ms window is reasoned rather than observed.
