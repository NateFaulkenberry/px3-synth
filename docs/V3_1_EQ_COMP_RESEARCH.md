# v0.3.1 — Bus EQ and FET Compressor: Research

Research phase for two bus inserts on the Dry Bus and the FX Bus. Written
before any implementation code, as the brief requires. Its purpose is to reach
a design that can answer "why is it built this way?" with a reason rather than
a preference.

---

## Why these two processors, on these two buses

The stated goal is control over **how the dry and FX buses blend**. That framing
constrains the design more than "add an EQ and a compressor" would:

- The problems when two buses fight are almost always **low-mid build-up** and
  **the FX return swamping transients**. That is a broad tonal EQ and a
  fast, coloured compressor — not a surgical notch tool and not a transparent
  mastering limiter.
- Both processors sit on a **sum**, not on a source. Bus processing wants gentle
  slopes, forgiving Q, and defaults that do nothing until asked.

---

## EQ Research

### Filter topology candidates

| topology | verdict |
|---|---|
| **RBJ biquad (Direct Form I / TDF-II)** | Derived from analog prototypes through the bilinear transform, with documented frequency warping and a bandwidth definition specifically intended for audio. Cheap, well understood, and already the structure the project's `VoiceFilter` uses via `juce::dsp::IIR`. |
| SVF (Chamberlin / TPT) | Excellent under fast modulation and already used in the project for the tape head bump. But EQ bands are not modulated at audio rate; the advantage does not apply, and the peaking/shelf forms are less standard. |
| Cascaded first-order sections | Cheap but cannot produce a peaking bell without extra structure. |
| FIR / linear phase | Rejected: latency on a bus insert would misalign the dry and FX paths against each other, which is the exact thing this feature exists to control. Pre-ringing is also wrong for transient-heavy synth material. |
| Analog circuit modelling (Neve/API-specific) | Rejected as scope. The musical properties worth having — proportional Q, gentle shelves — can be obtained from the cookbook forms without claiming to be a specific desk. |

**Chosen: RBJ biquads**, one per band, Transposed Direct Form II, coefficients
built through the same `juce::dsp::IIR::ArrayCoefficients` path already proven
in `FilterResponse.h`.

### Constant-Q vs proportional-Q — the decision that matters most

This is the single most important musical choice in an EQ and it is not a
matter of taste:

- **Constant-Q**: the bandwidth in Hz stays fixed as gain changes. A 2 dB boost
  and a 12 dB boost have the same width. This is the surgical, "digital"
  behaviour, and it is what a corrective EQ wants.
- **Proportional-Q**: the bandwidth narrows as gain increases. A small boost is
  broad and gentle; a large boost focuses. Classic console EQs behave this way,
  and it is most of why they are described as musical — a modest move sounds
  like a tone control rather than like a filter.

The RBJ peaking form defines bandwidth at the **half-gain point** (dBgain/2)
rather than at −3 dB. That definition gives proportional-Q behaviour: the
skirt of a gentle boost is wide, and it tightens as gain rises, without the Q
control itself having to move.

**That is the behaviour a bus EQ should have**, so the cookbook form is used as
published rather than "corrected" toward constant-Q.

### Band arrangement

The brief proposes low shelf / bell / bell / high shelf, and asks whether that
is best. It is, with one addition:

| band | default | reasoning |
|---|---|---|
| 1 | **Low shelf**, 100 Hz | Bus low-end shaping is almost always a shelf, not a bell. A high-pass would be more useful still for the FX return — so band 1 is **switchable between low shelf and high-pass**. |
| 2 | **Bell**, 300 Hz | The mud region. This is where two buses collide most. |
| 3 | **Bell**, 3 kHz | Presence and definition. |
| 4 | **High shelf**, 8 kHz | Air. Switchable to **low-pass**, which is the single most useful move on an FX return that is too bright. |

Making bands 1 and 4 switchable costs one enum per band and turns a tone
control into something that can also clean up a send. Bands 2 and 3 stay bells
because a mid shelf on a bus is rarely what anyone reaches for.

### Shelf design

RBJ shelves are parameterised by shelf slope S, where S = 1 is the steepest
slope that remains monotonic. Anything steeper overshoots — it produces a dip
before the shelf, which on a bus reads as a phase problem rather than as tone.
**S is fixed at 1** and the Q control on a shelf band is repurposed to shelf
slope over a restricted range that cannot overshoot.

### Stability, sample rate, smoothing

- Frequencies are clamped below Nyquist as the existing `VoiceFilter` does; a
  cookbook biquad is unconditionally stable for positive Q within that bound.
- Coefficients are rebuilt only when a parameter actually changes, not per
  sample.
- **Coefficient interpolation is the wrong smoothing strategy.** Interpolating
  between two sets of biquad coefficients can pass through unstable
  intermediate states. The safe approach is to smooth the **parameters**, then
  rebuild coefficients from smoothed values — every intermediate state is then
  a valid filter by construction. This is the same reasoning that made
  `VoiceFilter` rebuild from smoothed cutoff and Q.
- Coefficient rebuilds are throttled the way `VoiceFilter` already throttles
  them (measured there at 1.7x the local slope on a fast sweep — i.e. no
  stepping), rather than being done every sample.

### CPU

Four biquads per channel per bus, two buses, stereo = 16 biquads. A biquad is
about five multiplies and four adds. This is negligible against the measured
340 µs/block the FX chain already costs, and it will be verified rather than
assumed.

### Oversampling

**Not required.** The EQ is linear. Oversampling a linear filter changes
nothing except cost and latency.

---

## 1176 Research

### Architecture

The 1176 is a **feedback** compressor: the detector senses the signal **after**
gain reduction, not before. This is not a detail — it is the source of much of
its character:

- The loop is self-limiting, so the effective ratio is softer than the nominal
  one at low gain reduction and approaches it as reduction increases.
- Threshold becomes **program dependent** rather than a fixed number. The 1176
  has no threshold control at all; Input drives the signal into a fixed
  threshold, which is why "turn Input up until it sounds right" is the actual
  workflow.

Gain reduction is performed by a **FET used as a variable resistor**, shunting
signal to ground as its gate voltage changes. The FET's control law is
non-linear and is partly linearised in the circuit by feeding a fraction of the
drain signal back to the gate; what that correction leaves behind is a
significant part of the audible harmonic signature.

### Detector and sidechain

The detector is a **full-wave rectifier** (CR2/CR3 in the schematic) feeding a
smoothing capacitor — a peak-ish detector, not an RMS one. That matters: an RMS
detector would ignore exactly the fast transients the 1176 is prized for
catching.

**The critical finding for "all buttons in":** the ratio buttons do not select a
ratio in a gain computer. They **apply a bias level to the detector diodes**,
and that bias sets the threshold of limiting. Ratio and threshold are therefore
the same control in the hardware, moved together.

### Ratio behaviour and All Buttons In

Because the buttons are bias networks rather than switch positions, engaging all
four puts all four networks in parallel — a bias state that no single button
produces. The documented consequences:

- The ratio is no longer a single number. It varies roughly **12:1 to 20:1** and
  **changes across the circuit** with level.
- The unit compresses at the selected ratio **on the transient**, and the ratio
  then **increases after the transient**. That lag is the sound.
- The **knee hardens** at high ratio/threshold and is softer at low.
- **Distortion increases** markedly.

So all-buttons-in must be modelled as: a different threshold, a ratio that rises
after the initial transient with its own time constant, a harder knee, and more
drive into the nonlinearity. Setting ratio to 20:1 reproduces none of that.

### Timing

- **Attack: 20 µs to 800 µs.** Genuinely fast — 20 µs is under one sample at
  48 kHz, which has an implementation consequence noted below.
- **Release: 50 ms to 1.1 s.**
- Both are **program dependent**: the release in particular behaves as a dual
  time constant, fast for short excursions and slower for sustained ones, which
  is what stops it pumping on dense material.
- Both front-panel controls are **reversed** — fully clockwise is fastest. The
  UI should reproduce that, because it is part of the workflow people know.

### Nonlinearity — what to model and what not to

Every nonlinear stage needs a reason:

| stage | reason | keep? |
|---|---|---|
| FET residual nonlinearity | The gain element itself distorts, and it distorts *more* as it works harder. This is the compressor's own voice and it is program dependent for free. | **Yes** |
| Input stage | Drives the signal into a fixed threshold; a Class A stage with real headroom limits. | **Yes**, gentle |
| Output transformer | Low-frequency saturation and a small high-frequency limit. Audible on bass-heavy material, which this bus will see. | **Yes**, gentle |
| Detector nonlinearity | The diode rectifier is not a perfect absolute-value function; its knee softens low-level detection. | **Yes** — it is cheap and it *is* the knee |
| Generic "analog warmth" saturation | No engineering reason. | **No** |

### Meter

The hardware meter is a VU movement showing gain reduction, with its own
ballistics — roughly 300 ms to full deflection. A digital meter that follows the
gain reduction sample-accurately looks wrong and reads worse. The meter is
therefore given **VU-like ballistics**, and the value is published to the UI
through an atomic written once per block — never a lock, never a queue.

### Oversampling

The nonlinear stages generate harmonics, and harmonics above Nyquist fold back.
But this is a **bus insert on a synth**, where the material is already
band-limited by the instrument, and the nonlinearities chosen are gentle
(soft-knee saturation, not hard clipping).

**Decision: measure first, then decide.** The AnalogEngine faced the same
question and its aliasing was measured below the audible bar at unity rate,
so it ships without oversampling. The same measurement will be made here, and
2x oversampling will be added **only around the nonlinear stages** if the
measurement calls for it.

---

## Alternatives Considered and Rejected

1. **Feed-forward compressor with a threshold control.** Simpler, more
   predictable, and what most modern compressors do. Rejected: it removes the
   program-dependent threshold that defines the unit, and it would make the
   Input knob a mere trim instead of the primary control.
2. **RMS detector.** More "accurate" level sensing. Rejected: it cannot catch
   the transients the fast attack exists for, and the hardware is a full-wave
   peak rectifier.
3. **JUCE's stock `dsp::Compressor`.** A clean feed-forward design with a
   fixed knee. Useful as a **baseline for tests**, not as the implementation —
   it has no feedback loop, no program dependency and no nonlinearity.
4. **Ratio as a continuous control.** Rejected: the four discrete ratios plus
   all-buttons *are* the instrument. A continuous control would make
   all-buttons-in incoherent.
5. **All-buttons-in as ratio = 20:1.** Explicitly rejected by the research: the
   documented behaviour is a rising ratio, a harder knee, a shifted threshold
   and more distortion.
6. **Linear-phase EQ.** Rejected: latency on one bus and not the other is
   precisely the misalignment this feature exists to prevent.
7. **Constant-Q EQ.** Rejected for a bus: gentle moves would be too narrow.
8. **A shared "insert" that is one fixed EQ+comp block.** Rejected in favour of
   a generic insert chain (below), because the brief asks for extensibility.

---

## Extensibility

The brief asks that this be extendible to future buses and channels. The design
is therefore:

- `BusInsertChain` — owns an EQ and a compressor, exposes `prepare`, `reset`
  and `processSample(float& l, float& r)`, and knows nothing about which bus it
  is on.
- Each bus owns one instance. Adding the same inserts to a source channel or to
  the master later means constructing another instance and adding its
  parameters — no changes to the processors themselves.
- Parameter IDs are namespaced by bus (`dryEq…`, `fxEq…`), so a third bus adds
  a prefix rather than a new scheme.

---

## Signal Flow — where the inserts go, and why

Determined by reading the existing `processBlock`, not assumed.

**Dry bus:** sources sum → dry channel strip (gain, polarity, mute/solo, pan) →
`AnalogEngine::dryBus` → **EQ → COMP** → master sum.

The inserts must come **after** the AnalogEngine bus stage. The channel stage
runs a forward transfer and the bus stage runs its exact inverse; anything
inserted between them is inside the pair and breaks the cancellation. This is
not theoretical — the same mistake with `outputTrim` was measured at 0.045% THD
on a channel that must be transparent by construction.

**FX bus:** send sum → `AnalogEngine::fxBus` → FX chain → **(stage − send)** →
**EQ → COMP** → return gain/pan/polarity → master sum.

The return is a *difference*, because each effect passes its own dry through and
subtracting the send recovers only the wet contribution. The inserts therefore
go **after** the difference — placed before it, they would have the untouched
send subtracted back out of them. They are placed **before** the return fader so
that adjusting the dry/FX blend does not change how hard the compressor works,
which is the entire point of the feature.

---

## Final Recommendation

**EQ** — four RBJ biquad bands per channel, TDF-II, proportional-Q by virtue of
the cookbook's half-gain bandwidth definition. Bands 1 and 4 switch between
shelf and pass filters; bands 2 and 3 are bells. Shelf slope fixed at S = 1.
Parameters smoothed and coefficients rebuilt from the smoothed values, never
interpolated. No oversampling. Flat by default.

**Compressor** — a feedback FET model: detector after the gain element, full-wave
peak detection with a soft diode knee, program-dependent dual-time-constant
release, the four ratios implemented as bias/threshold pairs rather than as
divisions, and an all-buttons mode that shifts threshold, hardens the knee and
raises the ratio *after* the transient. Gentle input, FET and output-transformer
nonlinearities, each justified above. VU-ballistic gain-reduction metering
published through an atomic. Oversampling decided by measurement.

**Both** — bypassed by default, click-free on bypass, fully persisted in presets
and DAW state, no internal tuning constants serialised.

---

## References

- [Universal Audio — 1176LN Limiting Amplifier manual and schematic](https://media.uaudio.com/assetlibrary/1/1/1176ln_manual.pdf)
- [Universal Audio — 1176 Classic FET Compressor manual](https://help.uaudio.com/hc/en-us/articles/34530260482324-1176-Classic-FET-Compressor-Manual)
- [Austin Moore — *All Buttons In: An investigation into the use of the 1176 FET compressor in popular music production*, Journal on the Art of Record Production (University of Huddersfield repository)](https://eprints.hud.ac.uk/id/eprint/27391/1/Journal%20on%20the%20Art%20of%20Record%20Production%20%C2%BB%20All%20Buttons%20In_%20An%20investigation%20into%20the%20use%20of%20the%201176%20FET%20compressor%20in%20popular%20music%20production.pdf)
- [Journal on the Art of Record Production — All Buttons In](https://www.arpjournal.com/asarpwp/all-buttons-in-an-investigation-into-the-use-of-the-1176-fet-compressor-in-popular-music-production/)
- [Inside Blackbird — The 1176 Compressor](https://blog.insideblackbird.com/the-1176-compressor)
- [Abbey Road Institute — The 1176](https://abbeyroadinstitute.nl/blog/the-1176/)
- [Pulsar Audio — The History of All-buttons-in Mode](https://pulsar.audio/blog/the-history-of-all-buttons-in-mode/)
- [Robert Bristow-Johnson — Cookbook Formulae for Audio EQ Biquad Filter Coefficients (W3C mirror)](https://webaudio.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html)
- [musicdsp.org — RBJ Audio EQ Cookbook](https://www.musicdsp.org/en/latest/Filters/197-rbj-audio-eq-cookbook.html)
- [Elliott Sound Products AN012 — Peak, RMS and averaging circuits](https://sound-au.com/appnotes/an012.htm)
- [GroupDIY — FET gain elements and their control law](https://groupdiy.com/threads/that-was-fet-compressors.42515/)
- [Airwindows](https://github.com/airwindows/airwindows) — studied for envelope and nonlinearity decisions; the console channel/bus pairing already informs this project's AnalogEngine.
- [Signalsmith Audio DSP](https://github.com/signalsmith-audio/dsp), [DSPFilters](https://github.com/vinniefalco/DSPFilters), [Faust](https://github.com/grame-cncm/faust) — compared for filter parameterisation and compressor structure.
