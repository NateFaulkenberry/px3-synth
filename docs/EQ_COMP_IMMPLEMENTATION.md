# v0.3.1 — Bus EQ and FET Compressor: Implementation Report

Companion to `docs/V3_1_EQ_COMP_RESEARCH.md`, which was written first and which
settled the topology questions. This document records what was built, what was
measured, and what was decided by measurement rather than by preference.

Every number here was produced by a test or a benchmark in this repository and
can be reproduced with the command given beside it. Where something was not
tested, it says so.

---

## 1. What shipped

Two insert processors, available on two buses:

| | |
|---|---|
| **ParametricEQ** | 4 bands. Bands 1 and 4 switch between shelf and pass filter; bands 2 and 3 are bells. RBJ cookbook biquads, transposed direct form II. |
| **FetCompressor** | Feedback FET compressor. INPUT/OUTPUT/ATTACK/RELEASE, five ratio positions including all-buttons, stereo link, and a parallel MIX blend. |

Both are hosted by `BusInsertChain`, which runs EQ then compressor and knows
nothing about which bus it is on.

Files added:

```
Source/DSP/BusInsertTypes.h      settings structs shared by both processors
Source/DSP/ParametricEQ.{h,cpp}
Source/DSP/FetCompressor.{h,cpp}
Source/DSP/BusInsertChain.h      the pair, per bus
Source/UI/BusInsertOverlay.{h,cpp}   both sheets
Source/UI/ModalBackdrop.{h,cpp}      the shared sheet backdrop
```

---

## 2. Signal flow

### Dry bus

```
sources → dry channel strip (gain, phase, gate, pan)
        → AnalogEngine::dryBus (the console's inverse half)
        → EQ → COMPRESSOR
        → dryBusBuffer → master sum
```

The insert is the last stage on the dry bus. It is **post-fader**, which is not
a preference: the console engine is an invertible pair split across the channel
and the bus, and putting anything between the two halves breaks the property the
whole engine depends on.

### FX bus

```
sends → AnalogEngine::fxBus → FX chain
      → (stage − send) difference → headroom scale
      → EQ → COMPRESSOR
      → return gain → pan → gate → master sum
```

The insert sits on the **recovered wet signal**: after the difference that
separates wet from the send, and before the return fader. Two consequences,
both intended:

- the inserts see wet only, never the dry signal that was sent in;
- riding the return blend does not change how hard the compressor works.

`Source/DSP/PluginProcessor.cpp` — search for `busInserts[0].processSample` and
`busInserts[1].processSample`.

---

## 3. EQ

### Topology

RBJ Audio EQ Cookbook biquads via bilinear transform, transposed direct form II.
Coefficients are rebuilt from **smoothed parameters** and never interpolated
between coefficient sets — interpolating filter coefficients can walk a stable
pair into an unstable one, and the resulting instability does not show up until
the sweep passes through it.

Bandwidth is defined at the **half-gain point**, which is the cookbook's own
definition and produces proportional-Q behaviour: a small boost is broad and a
large boost narrows. That is the behaviour of the analogue designs the research
document surveys, and it is why a 3 dB move sounds like tone shaping and a 12 dB
move sounds like a correction.

Measured (`PX3Tests businserts`):

| claim | measured |
|---|---|
| flat is a wire | 0.000 dB across 40 Hz – 15 kHz |
| low shelf hits its gain at the corner | +3.00 dB at the corner for a +6 dB shelf (half-gain definition) |
| high pass corner and slope | −3.0 dB at the corner, 12 dB/oct below it |
| proportional Q | skirt holds 66% of a 3 dB boost, 64% of a 12 dB one |
| stability at extremes | finite at 4 sample rates across the full frequency, gain and Q ranges |
| sweeping does not step | worst sample jump is 1.4× the local slope, sweeping frequency, gain and Q end to end |

### Band arrangement

| band | type | default |
|---|---|---|
| 1 | low shelf **or** high pass | low shelf, 100 Hz |
| 2 | bell | 300 Hz |
| 3 | bell | 3 kHz |
| 4 | high shelf **or** low pass | high shelf, 8 kHz |

The inner bands have **no type parameter at all**. A one-entry choice has a
zero-width range and normalising against it divides by zero — that reached the
state tests as `dryEqType2=nan` on every malformed-payload case before it was
removed.

Ranges: 20 Hz – 20 kHz (logarithmic), ±18 dB, Q 0.30 – 8.0.

---

## 4. Compressor

### Why it is not a generic compressor

Three things carry the character, and none of them is a flavour setting:

1. **Feedback topology.** The detector senses the signal *after* the gain
   element. The loop is self-limiting, so the effective ratio is softer than the
   nominal one at light reduction and approaches it as reduction deepens, and
   the threshold becomes program dependent. This is why the hardware has no
   threshold control: INPUT drives the signal into a fixed threshold.
2. **The ratio buttons bias the detector.** Ratio and threshold are one control,
   so each position has its own threshold as well as its own slope.
3. **All-buttons is a different circuit, not ratio = 20.** The unit compresses
   at the selected ratio *on the transient*, and the ratio then rises, with its
   own time constant. That lag is the sound.

Measured (`PX3Tests businserts`):

| claim | measured |
|---|---|
| below threshold it is a wire | −40 dB in → −39.91 dB out |
| ratios are ordered and distinct | 4:1 −8.04, 8:1 −9.52, 12:1 −10.63, 20:1 −11.73, ALL −16.45 (output dB at 0 dB in) |
| all-buttons keeps tightening | ALL settles 1.67 dB further after onset, against 20:1's 0.60 dB |
| attack ordering | 10 ms in, fastest attack has reached −13.9 dB against the slowest's −12.4 dB |
| no DC accumulation | mean output 0.000226 |
| finite everywhere | silence, denormals, normal and overload input across all five ratios |

Attack 20 µs – 800 µs, release 50 ms – 1.1 s, both **reversed on the panel** as
the hardware is: fully clockwise is fastest.

### MIX

A parallel blend: 0% is the bus untouched, 100% the compressor alone. The dry
side of the blend is the signal **as it arrived, before input gain**, so the
control blends "this bus" against "this bus compressed" rather than against a
different level.

Measured: mix 0 → 0.07 dB, mix 0.5 → −3.96 dB, mix 1.0 → −11.73 dB.

The EQ has no equivalent control, as specified.

---

## 5. Oversampling: decided by measurement

The brief required this be settled by measurement. It was, and the measurement
changed the answer twice.

**Step 1 — is there aliasing?** An 11 kHz tone at 0.7 was driven through the
compressor and every harmonic landing above Nyquist was folded back and summed.
11 kHz at 48 kHz is the honest worst case: h2 stays in band, but h3 folds to
15 kHz, h4 to 4 kHz and h5 to 7 kHz, none of them maskable by the fundamental.

| input drive | images vs fundamental |
|---|---|
| unity | −43.8 dB |
| +6 dB | −36.8 dB |
| +30 dB | **−15.2 dB** |

**Step 2 — from where?** Linearising the FET curve and the transformer sent the
same measurement to −77.6 dB, and changing the attack setting made no difference
at all. That put every dB of it in the waveshaper and ruled out the detector
loop, which had been the other candidate.

**Step 3 — what fixes it?** 2× oversampling was rejected, and **not on CPU
cost**. A halfband pair adds roughly 15 samples of group delay. These are
per-bus inserts, so enabling the compressor on the dry bus alone would slide the
dry path against the FX return and comb the two together at the master sum. The
alternative — running the oversampler permanently on both buses to keep the
latency constant — pays the cost even when both inserts are off.

First-order **antiderivative antialiasing** was used instead: it integrates the
curve across the segment between consecutive samples rather than point-sampling
it, which is the average the band-limited version would have taken. No filters,
no delay, nothing to align.

That required one change to the curve. The FET's asymmetry became a **gate
bias** instead of an added squared term. A bias is what the device actually has,
and — decisively — it keeps the curve integrable in closed form. Ratio spread,
mix law, attack ordering and DC all measure the same after the change.

| input drive | before | after |
|---|---|---|
| unity | −43.8 dB | **−61.6 dB** |
| +6 dB | −36.8 dB | **−54.3 dB** |
| +30 dB | −15.2 dB | **−28.6 dB** |

−28.6 dB under maximum abuse is the accepted residual. It requires a
near-full-scale tone above 10 kHz driven 30 dB into a bus compressor to produce.
The thresholds in `BusComp_AliasingStaysBelowAudibility` pin both the absolute
level and the ~13 dB improvement, so a regression in either shows up.

---

## 6. CPU

`build/diag/PX3Bench_artefacts/RelWithDebInfo/PX3Bench`, shipping configuration
(`PX3_DIAGNOSTICS=0`), 400 blocks × 5 sweeps per scenario, 48 kHz.

Each row is read against its own baseline: the dry rows against
"16 voices, all 4 sources", the FX rows against "16 voices + FX send, no
inserts". Reading the FX rows against the dry baseline would charge the inserts
for the delay.

| scenario | mean µs | p99 µs | vs baseline | CPU |
|---|---|---|---|---|
| 16 voices, all 4 sources *(baseline)* | 925.2 | 1039.4 | — | 8.67% |
| 16 voices + inserts bypassed | 933.1 | 1051.8 | +7.9 | +0.07% |
| 16 voices + dry EQ | 948.8 | 1121.1 | +23.6 | +0.22% |
| 16 voices + dry COMP | 984.7 | 1112.8 | +59.5 | +0.56% |
| 16 voices + dry EQ + COMP | 990.4 | 1122.1 | +65.2 | +0.61% |
| 16 voices + FX send, no inserts *(baseline)* | 1150.0 | 1297.1 | — | 10.78% |
| 16 voices + FX EQ + COMP | 1216.5 | 1413.4 | +66.5 | +0.62% |
| 16 voices + both buses | 1284.0 | 1478.0 | +134.0 | +1.26% |
| 64 voices, everything on + both buses | 8351.7 | 9103.9 | ~0 | 78.30% |

A bypassed insert costs 0.07% — within run-to-run noise, which is the intended
result: a disabled chain returns early rather than running an identity filter.
At 64 voices with everything on, adding both buses' inserts is indistinguishable
from not having them (8351.7 against that scenario's own 8417.1).

### Read p99, not max

**The `max` column of this harness is dominated by OS scheduling, not by DSP,
and should not be quoted.** The benchmark is an ordinary userspace process with
no thread pinning, so a single descheduling lands in whichever block was being
timed. In the run above, "64 voices, EVERYTHING on" reported a max of
**420,402 µs** — 3941% of the block budget. A 420 ms block is not work; it is
the process being taken off the CPU.

Outliers of 2.5× to 3.7× the mean appear on rows with no inserts at all - the FX
chain, mod envelopes, long release tails - and they move to a different row on
every run. `p99` stays at 1.1 to 1.2× the mean everywhere, which is the tail
statistic that actually describes the DSP.

This matters because an earlier version of this document quoted a 10.4 ms max on
"both buses" and attributed it to the spectrum analyser. That was wrong twice:
the analyser's FFT runs on the message thread from a `juce::Timer`, never on the
audio thread, and PX3Bench does not construct an editor at all, so no analyser
was running during any of these numbers. The figure did not reproduce on a
re-run.

### The analyser's actual audio-thread cost

`BusAnalyser::push` is the whole of it. Inactive, it is one relaxed load and a
branch. Active, it is that load plus a store and a release store. No allocation,
no lock, no unbounded work - and it is inactive unless a sheet is open, which is
why "inserts bypassed" measures as free.

**Not measured:** GPU cost. The sheets are ordinary JUCE software rendering like
the rest of the UI, and no GPU path exists in this plugin to measure.

---

## 7. Parameters and persistence

46 parameters, 23 per bus, namespaced `dry*` and `fx*`:

```
<bus>EqEnabled
<bus>EqType1  <bus>EqType4                       (bands 2 and 3 have no type)
<bus>EqFreq1..4  <bus>EqGain1..4  <bus>EqQ1..4
<bus>CompEnabled  <bus>CompInput  <bus>CompOutput
<bus>CompAttack   <bus>CompRelease  <bus>CompRatio
<bus>CompMix      <bus>CompLink
```

All are registered with `addParameter`, so they reach the DAW's automation list,
the session state and preset files through the same `getParameters()` walk as
every other parameter. No separate serialisation path was added, and none is
needed.

**No internal tuning constant is serialised.** The FET's drive law, the bias,
the detector's high-pass corner, the transformer's corner and the meter's
ballistics are implementation details and stay compiled in, exactly as the
AnalogEngine's constants do.

Defaults are chosen so that switching an insert on changes nothing until a
control is moved: every band starts at 0 dB, the compressor at unity input and
full wet. A processor that colours the sound the moment it is enabled cannot be
A/B'd.

Verified by `BusInsert_EveryParameterIsRegistered` (46 of 46 found on the
processor) and `BusInsert_ParametersRoundTripThroughState` (worst normalised
drift 0.0000000 across a full save/restore).

---

## 8. UI

### Strip buttons

`EQ` and `CMP`, square, in the bottom corners of the DRY and FX strips — EQ
bottom-left, CMP bottom-right. They reuse `MixerToggleButton`, so they wear the
same cap, border and lamp as MUTE, SOLO and PHASE.

`MixerToggleButton`'s lamp now comes from a virtual `isLit()` rather than
reading `getToggleState()` directly. The insert buttons are not toggles — they
open something — and they light with their **insert's enable state**, polled
with the meters so automation and preset loads move them too.

### The sheets

Both are bus agnostic: one EQ sheet and one compressor sheet exist, and each is
retargeted with `setBus`, which rebuilds its attachments. Opening from the FX
strip and opening from the dry strip use the same component.

They share the preset browser's **backdrop, scrim and click-outside-to-close**,
extracted into `Source/UI/ModalBackdrop.{h,cpp}`, and they wear the plugin's
**card frame** — border, a padding gap over a translucent background — taken
straight from the Card system, with the mixer palette. Where a card puts a
two-part gloss, a sheet puts a single solid panel: the *inner overlay*, which is
what the controls sit on. It is declared per sheet at
`cards.<key>.innerOverlay`.

Each sheet takes its accent from the mixer strip that opened it, so a DRY sheet
and the DRY strip are visibly the same channel.

**The EQ graph** (`Source/UI/BusEqGraph.{h,cpp}`) is the primary control, not a
picture of one. It follows the ADSR graph's interaction model — pick the nearest
handle, wrap the edit in a host gesture, double-click to restore the parameter's
own default — with one addition: a band has three values and a pointer has two,
so **Q is on the wheel** over the handle it belongs to. A drag sets frequency
and gain together; a band switched to a pass filter has no gain at all, so its
handle rides the 0 dB line and only moves horizontally.

Axes are labelled: a log frequency ruling at 20/50/100/200/500/1k/2k/5k/10k/20k
with unlabelled minor ticks between, and a dB ruling at ±12, ±6 and 0 against
the parameters' own ±18 dB range, so a band at its limit sits on the edge of the
plot rather than somewhere arbitrary inside it.

**The analyser** behind the curve is a live spectrum of the bus, fed by
`px3::BusAnalyser` — a fixed ring the audio thread writes single samples into
with one relaxed store, no allocation and no lock. The read is allowed to race:
a display is not worth synchronising for, and the ring is twice the window so
the writer cannot lap the reader inside a frame even at 192 kHz.

Three decisions worth recording:

- **Tapped pre-EQ.** Post-EQ would draw the same shaping twice, once as the
  curve and once as the trace, which is exactly when an analyser stops helping
  you decide a cut.
- **Nothing is written unless a sheet is open.** The tap is switched on with the
  overlay's visibility, so a sheet nobody has opened costs the audio thread one
  relaxed load per sample and nothing else. That is what keeps the "an insert
  you are not using is free" property true, and it is asserted rather than
  assumed.
- **Resampled onto the log axis, peak per display bin.** A linear bin walk puts
  nine tenths of its points in the top octave and draws the bass from three
  bins; an average across a display bin buries the narrow resonance someone
  opened the analyser to find.

**The compressor face** is the same frame with a silver inner overlay: a solid
fill with a fixed pixel-derived grain over it, the five large controls, the five
ratio buttons and a moving-coil gain reduction meter whose needle falls as the
unit works.

### UIConfig

Every property listed is read by code and changes behaviour. Nothing here is a
placeholder.

`mix.insertButton` — the button's own style, the same shape as `mix.mute`:
`size.width`, `size.height`, `cornerRadius`, `textSize`, and the seven colours.

`mix.inserts.eq` and `mix.inserts.comp` — independent placement per button:
`size`, `offsetX`, `offsetY`. Offsets are measured from the strip's bottom-left
and bottom-right corners respectively. A card style block may override these per
bus at `cards.<key>.inserts.<eq|comp>`.

`busInserts` — shared by both sheets: `headerHeight`, `headerGap`,
`buttonWidth`, `enableWidth`.

`busInserts.eq` — `widthFraction`, `heightFraction`, `innerPadding`,
`graphHeight`, `graphGap`, `columnGap`, `knobSize`, `typeHeight`, `gridColor`,
`zeroLineColor`, `axisLabelColor`, `spectrumColor`.

`busInserts.comp` — `widthFraction`, `heightFraction`, `innerPadding`,
`meterWidth`, `meterHeight`, `meterGap`, `knobSize`, `knobGap`, `ratioGap`,
`ratioHeight`, `ratioButtonGap`, `grainAmount`, `meterFaceColor`,
`meterInkColor`, `meterNeedleColor`.

`cards.busInsertEq` and `cards.busInsertComp` — the frame, in the same shape as
every other card block, plus `innerOverlay.{margin, radius, color}` for the
solid panel that replaces the gloss.

The sheets are sized as a fraction of the window rather than in pixels so they
hold their proportion at any window size.

---

## 9. Extensibility

Adding the same inserts to a third bus — a source channel, the master, anything —
is three things and no new code in either processor:

1. one more entry in `PX3SynthAudioProcessor::busInserts` and
   `busInsertParams` (raise `kBusInsertCount`);
2. one more `createBusInsertParameters(bus, "<prefix>", "<Label>")` call and one
   `busInserts[n].processSample(l, r)` at the point in `processBlock` where the
   insert belongs;
3. a pair of buttons on that strip via `addInsertButtons(channel, bus)`.

The sheets need no change: they are constructed once and retargeted. Neither
`ParametricEQ`, `FetCompressor` nor `BusInsertChain` knows which bus it is on.

---

## 10. Test coverage

39 assertions in `PX3Tests businserts`, in five groups:

- **BUS EQ** — flat response, shelf and pass behaviour, proportional Q,
  stability at four sample rates and every extreme, and continuity while
  sweeping every control end to end.
- **BUS COMPRESSOR** — the wire below threshold, ratio ordering, all-buttons
  behaviour after the transient, the mix law, attack ordering, finiteness from
  silence to overload, DC, and the aliasing measurement.
- **BUS INSERTS / INTEGRATION** — a disabled chain is *exactly* a wire; loaded
  but disabled settings change the rendered output by less than 0.02 dB; the dry
  EQ reaches the dry bus; the FX insert does not touch the dry path with the
  sends closed and does reach the return with them open; the compressor narrows
  dynamic range in the plugin; every parameter is registered and round-trips.
- **BUS INSERTS / UI** — exactly two EQ and two CMP buttons exist across six
  strips; opening a sheet covers the UI behind it; the sheet edits the bus whose
  button was pressed and leaves the other bus alone; the shipping config
  declares both sheets and styles them as cards.
- **BUS INSERTS / EQ GRAPH** — dragging a band moves its frequency and gain and
  leaves the other three alone; the wheel over a handle sets its Q; a double
  click restores that band's defaults; a pass filter's handle has no gain to
  drag; the analyser tap runs only while the sheet is open; and the ring itself
  returns the most recent window in order with no seam, and writes nothing at
  all while inactive.

Full suite: **680 assertions, 0 failures**. Release build clean, no warnings.

### Two measurement notes worth keeping

Both cost time here and are recorded in the test comments so they cost less next
time.

**Processor output is not bit-comparable across renders.** A global note-start
sequence advances per note, so two identical renders already differ by 0.25 —
the first version of the "disabled changes nothing" test was measuring that, not
the inserts. The exact claim is now made on the chain itself, where it can be
made exactly, and the processor-level claim is made on level.

**DOOM and LUCY default to enabled** and draw from the shared system random,
which `Random::setSeed` cannot pin. Any test comparing rendered samples has to
switch them off first.

---

## 11. Known limitations

- **Aliasing residual.** −28.6 dB at maximum abuse, as measured and accepted in
  section 5. Not audible on program material; measurable on a slammed
  high-frequency tone.
- **Not tested on hardware other than arm64 macOS.** No CI exists for this
  repository, so the suite is run by hand.
- **Meter ballistics are not calibrated to a VU standard.** They are a smoothed
  gain-reduction readout with roughly VU timing, which is what the hardware's
  meter shows, but no standards-based calibration was performed or claimed.
