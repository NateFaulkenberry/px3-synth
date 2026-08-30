# VU Meter — Research and Implementation

The needle on the bus compressor's meter looked like a number being animated
around a gauge. This documents why, what a real meter actually does, and what
replaced it.

---

## 1. Why the old one looked artificial

Four faults, all structural rather than cosmetic.

**The detector and the ballistics were the same operation.** A single one-pole
smoother in `FetCompressor` — `meterSmoothed += (reduction - meterSmoothed) *
coeff` with a 300 ms time constant — produced the number the GUI drew. A
one-pole is a first-order system: it can only approach its target
asymptotically. It cannot overshoot, cannot settle, and has no state
corresponding to momentum.

**The GUI had no physics at all.** It copied the DSP's value and mapped it
straight to an angle. Nothing about the needle had mass.

**Nothing used elapsed time.** The value only changed when the poll saw it
change, so the animation was implicitly tied to the timer rate.

**The needle was `g.drawLine`.** No taper, no pivot cap, no depth.

The result is the giveaway of a fake meter: the needle eases into position and
stops dead, exactly on target, every time.

---

## 2. What a real VU meter does

The behaviour is standardised, which means it can be derived rather than
guessed. ANSI C16.5-1942, now folded into IEC 60268-17:

| property | specification |
|---|---|
| rise | 99% of full-scale deflection in **300 ms** |
| overshoot | **not less than 1%, not more than 1.5%** |
| fall | same as rise, 300 ms — the movement is symmetric |
| detector | **full-wave average**, not peak and not RMS |
| reference | 0 VU = +4 dBu for an applied sine |

Two of those matter more than they look.

**It overshoots.** A specification that *requires* 1–1.5% overshoot is
describing a mechanism with mass and a spring, deliberately damped just short of
critical. That single fact rules out every first-order smoother.

**The detector averages.** A moving-coil movement is driven through a full-wave
rectifier and responds to the *mean* of the rectified signal, not its peak and
not its RMS. For a sine, mean|x| = (2/π)·A while RMS = A/√2, so a meter
calibrated to read a sine correctly applies a factor of π/(2√2) = 1.1107 to the
average. That factor is why a VU meter under-reads peaky material relative to a
true RMS meter — which is part of how it sounds "right" to read.

Sources: [Wikipedia/HandWiki VU meter](https://handwiki.org/wiki/Engineering:VU_meter),
[Elliott Sound Products — VU and PPM audio metering](https://sound-au.com/project55.htm),
[EDN — Analog VU meters, quick pointers](https://www.edn.com/analog-vu-meters-quick-pointers/).

---

## 3. The model, derived from the specification

A moving-coil movement is a damped harmonic oscillator:

```
ẍ + 2ζωₙẋ + ωₙ²x = ωₙ²·target
```

Both constants come out of the two published numbers, so neither is tuned by
eye.

**Damping ratio from the overshoot.** For a second-order step response,
Mp = exp(−πζ/√(1−ζ²)):

| overshoot | ζ |
|---|---|
| 1.0% | 0.8261 |
| 1.5% | 0.8007 |

Taking the middle of the permitted window, **ζ = 0.8134**, which gives 1.24%
overshoot — inside the spec by construction.

**Natural frequency from the rise time.** Solving the step response for the ωₙ
whose *first* crossing of 99% falls at 300 ms gives **ωₙ = 13.536 rad/s**
(f = 2.154 Hz). The resulting peak is 1.0124 at 399 ms, which is the mechanical
overshoot the standard describes.

Fall is the same movement, so the same constants serve both directions — no
separate release curve, and none is wanted.

---

## 4. Architecture

```
AudioProcessor
    │  full-wave average per block, VU-calibrated  (audio thread)
    ▼
std::atomic<float>            lock-free, one store per block
    │
    ▼
VuBallistics                  second-order physics, dt-driven, no JUCE, no GUI
    │  position / velocity
    ▼
VuMeterComponent
    ├── cached face image     scale, ticks, labels, lamps - repainted on resize
    └── vector needle         Path + AffineTransform about the true pivot
```

The four stages are separate on purpose: the processor answers "what level is
the audio", the physics answers "how would a movement respond to that", and the
renderer answers "what does that look like". Collapsing any two of them is how
the previous version ended up with no mass.

---

## 5. Calibration

**0 VU = −18 dBFS.** Stated here because it is a choice, not a constant: there
is no fixed digital equivalent of +4 dBu, and −18 dBFS is the common alignment
(EBU R68). A sine at −18 dBFS therefore parks the needle exactly on 0 VU, and
+3 VU is −15 dBFS.

The scale is linear in amplitude, not in decibels — see section 7 — which is why
−20 crowds against the left stop while 0 to +3 spreads across the last third.

---

## 6. What is measured

Full-wave average with the sine-calibration factor applied:

```
level = (1/N) · Σ|x|  ×  π/(2√2)
```

taken over each processed block and published through one atomic store. Not
peak, which would read transients the movement could never follow; not RMS,
which is not what the mechanism responds to.

Gain reduction is not a level and does not get this treatment: it is already a
control signal, and the needle physics is applied to it directly.

---

## 7. Rendering

**The face is cached.** Ticks, numbers, the two-colour arc, the lamp gradients
and the legend are drawn once into an `Image` at the display's scale and blitted
each frame. They do not change between frames, and rebuilding them at 60 Hz was
most of what a meter would otherwise cost. It is rebuilt on resize, on a mode
change and on a config reload — nothing else.

**The needle is vector geometry, not a line.** A `Path`: a tapered blade, widest
at the pivot and coming to a point, with a short counterweight behind the pivot
as a real movement carries. Under it a soft offset shadow, over it a thin
highlight and the pivot cap.

**The rotation pivot is the mechanism's pivot.** The path is built with its
pivot at the origin and rotated about it, then translated to the arc's pivot —
not rotated about a bounding-box corner or centre.

**Nothing is quantised.** Position, velocity, angle and the transform are all
`double`/`float` end to end. No integer angle, no rounded pixel position.

**High-DPI:** the face image is allocated at the display scale and drawn into a
scaled context, so the ticks and numbers stay sharp rather than being an
upscaled 1x bitmap. The needle is vector and scales for free.

---

## 8. Update strategy

The meter animates at **60 Hz on its own timer**, and steps its physics with
**real elapsed time** from a monotonic clock rather than an assumed 1/60. JUCE
timers are not guaranteed to fire on schedule; a simulation stepped with an
assumed interval runs at a different speed whenever the host is busy.

Long frames are handled twice over: a frame longer than 100 ms is treated as a
stall and capped, and any frame is sub-stepped at no more than 4 ms so one long
step cannot be a poor approximation of the curve. Integration is semi-implicit
Euler, which is stable for an oscillator where plain explicit Euler gains energy
each step and diverges.

The component repaints only when the needle has moved enough to change a pixel,
so a still meter costs nothing.

---

## 9. Performance

| | |
|---|---|
| one meter, cached face | **0.015 ms/frame** — 0.09% of one core at 60 Hz |
| physics step | below the timer's resolution to measure |

Eight meters would be roughly 0.7% of a core. The cost is the blit; the face
render happens on resize, not per frame.

Audio-thread cost is one `fabs`, one add and a counter per sample, with one
atomic store per 10 ms window.

---

## 10. Testing

Ten assertions in `PX3Tests vumeter`, all against the standard rather than
against the implementation:

| test | result |
|---|---|
| rise to 99% | 302 ms (spec 300 ms) |
| overshoot | 1.15% at 401 ms (spec 1–1.5%) |
| fall | 302 ms, matching the rise |
| settling | position 1.00000, velocity 0.00000 after 3 s |
| frame-rate independence | rise 333/317/311/308 ms at 30/60/90/120 Hz, peak spread 0.00014 |
| stalled GUI | finite and on-scale after 4 s, 30 s and an infinite frame |
| stops | driven between the stops for 33 s, never leaves them |
| detector | sine reads its RMS; a square over-reads by exactly 1.1107 |
| scale calibration | 0 error across −20/−10/−5/0/+3 VU; 0 VU at 70.8% of the sweep |
| GR scale | 0 dB at 100%, 6 dB at 50.1%, 20 dB at 10% |

The frame-rate test is the one that matters most: the same target sequence
produces the same trajectory at 30 and at 120 Hz, which is what separates a
physical model from "move X per frame". The spread in measured rise time is the
sampling of the measurement — a 30 Hz frame is 33 ms — not slop in the physics.

The physics is tested with no GUI at all, which is why `VuBallistics` has no
JUCE in it.

---

## 11. Known limitations

- **Not visually evaluated against live audio by me.** I cannot see the rendered
  panel; the physics, calibration and cost are measured, the *look* is not. It
  wants watching against pads, plucks, percussion and heavily compressed
  material before it is called finished.
- **No screenshot regression coverage.** The project has no image-comparison
  harness, so needle alignment is asserted through the mapping functions rather
  than against rendered pixels.
- **One meter exists**, so the multi-instance CPU figures are extrapolated from
  one rather than measured at 8.
- **The debug read-out of target/angle/velocity/dt is not built.** The physics
  exposes `needlePosition()` and `needleVelocity()` for it, but no console panel
  displays them yet.
- **0 VU = −18 dBFS is a declaration, not a measurement.** It is the common
  alignment, but this synth has its own gain structure and the choice deserves a
  listening check against material at a known level.
