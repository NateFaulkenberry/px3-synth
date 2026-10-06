# REVERB - DSP design

`shared/DSP/Reverb/` - one reverb, six algorithmic types, used by the Synth's FX
chain and by PX3 Reverb.

| File | What is in it |
| --- | --- |
| `Reverb.h/.cpp` | The shared path: pre-delay, type switching, sleep, WIDTH, MIX |
| `ReverbEngines.h` | The six types, and the 16-line FDN three of them share |
| `ReverbPrimitives.h` | Delay line, allpass, three-band decay filter, random LFO, filters, Hadamard |
| `ReverbMapping.h` | What a knob position means per type (seconds, scale, ms) |
| `ReverbParameters.h` | Parameter ids, defaults, value text, which controls each type reads |
| `ReverbPresets.h` | Type-specific presets |
| `shared/UI/Fx/ReverbCard.h` | The card's per-type slot, captions, dimming and PRESET menu |

## Why it was rebuilt

The previous reverb had four types. ROOM, HALL and CLOUD were one 8-line FDN at
three scales, all fed a mono sum; PLATE was a Dattorro port. It also had an
IR-import mode and a SHIMMER control shown on every type.

An audit, measured on a Python port and with `PX3Tests reverbmetrics`, found:

- **DAMPING was inverted on every type.** Turning it up made the tail
  *brighter*. HALL at DAMPING 0 had an 8 kHz RT of 0.45 s; at DAMPING 1 it was
  1.14 s.
- **PLATE at DAMPING 0 had no tank tail.** Dattorro's 0.0005 belongs on the
  feedback term of the damping one-pole. The port put it on the input term,
  which made the tank a 4 Hz lowpass.
- **Low echo density.** Mixing times (echo density at least 0.9) were
  113 / 207 / 211 / 416 ms (ROOM / PLATE / HALL / CLOUD).
- **ROOM's early field was discrete taps.** Nine single-sample taps gave a
  5 ms-frame crest of 12-15; Gaussian noise reads about 3.
- **Metallic tails.** The worst peak in the late tail stood 7.9 dB (ROOM) and
  6.4 dB (HALL) above decaying noise of the same RT.
- **The output stage was nonlinear and adaptive.** It had tanh saturation, a
  0.16-per-sample slew limiter that distorted loud high frequencies, a block
  level-match that pumped the tail, and a 12 % dry floor at MIX 1.
- **SIZE and PRE-DELAY stepped once per block.** Every reverb control is a
  modulation destination, so this produced clicks.
- **Stale tails replayed.** An inactive type's lines froze and replayed when
  switched back to.
- **Plate damping and bandwidth were per-sample constants**, so the plate got
  brighter at 96 kHz.

The goal of the rebuild was a set of distinct, deliberately designed algorithms.
IR import was removed: no research reason to keep it, and the user asked
against it. SHIMMER now exists only on CLOUD, where it is real pitch-shifted
regeneration.

## Signal path (all types)

```
in -> NaN guard -> 28 Hz DC block -> PRE-DELAY (0-250 ms; a change crossfades
two taps over 20 ms, equal power - never a pointer slide) -> TYPE ->
WIDTH (mid/side, side x 0..1: the mono sum never changes) ->
per-type level x (T_default / T)^0.25 -> equal-power MIX
```

- **No saturation, no slew limiter, no adaptive gain.** The reverb is linear:
  `Reverb_IsLinear` measures a -300 dB residual.
- **MIX** uses sin/cos gains and lands exactly on 0 and 1. MIX 0 is the input,
  bit for bit, so an untouched knob cannot colour the FX bus.
- **MIX law (`px3::FxMixLaw`).** The standalone PX3 Reverb crossfades: MIX 1
  is fully wet. In the Synth the reverb is on the FX send, whose return is
  `(stage − send)`, so a crossfade took `(1 − cos)·send` out of the mix as MIX
  rose. There it is **additive**: `in + wet·sin(πMIX/2)` - the wet keeps its
  equal-power curve, so MIX 1 is the whole reverb on top of an intact dry and
  small MIX settings sound as they did. MIX 0 is still bit-exact.
- **Control rate is 32 samples.**
  - Every normalised control goes through a one-pole of about 60 ms.
  - Delay lengths glide per sample with a 120 ms time constant.
  - Moving SIZE is therefore a Doppler sweep, never a step.
  - Settled lengths are whole samples, read directly. Only while gliding is a
    read interpolated, and then with Lagrange-3.
- **Why not linear interpolation:** a fractional delay read linearly is a
  lowpass, and inside a loop it acts on every pass. It was measured: the HALL
  tail's flatness fell from 0.54 to 0.34, and DAMPING 0 was no longer flat.
- **Storage:** one arena sized for the largest type: 1.01 MB at 48 kHz,
  4.02 MB at 192 kHz.
- **Type switch, in order:**
  1. The old wet fades out over 50 ms (smoothstep).
  2. The arena is zeroed on the audio thread, 32 KB per control tick.
  3. The new type binds to the arena.
  4. Its *input* fades in over 10 ms. Audio arriving mid-note into an empty
     network is a step, and a reverb answers a step with a click.
  - Nothing allocates (`PX3Diag rtsafety`).
- **Sleep:** at MIX 0, or after 0.3 s with both input and wet below -120 dB,
  the engine clears itself and stops running. It wakes on the first input
  sample. A tail therefore never replays, and an idle reverb costs nothing.

## The types

Constants are in seconds at SIZE scale 1 and scaled to the running rate.

### Shared decay rule

Every recirculating path uses the same rule:

- Per line, `g = 10^(-3 M / (T fs))` (Jot), computed for three bands: low,
  mid and high.
- The bands are realised as gMid × a first-order low shelf × a first-order
  high shelf. Each shelf is monotone, so |H| ≤ max(gLow, gMid) < 1 at every
  frequency.
- The mixing matrices, the allpasses and the Lagrange interpolator all have
  |H| ≤ 1, so every loop is stable at any setting.
- **LOW** sets the low band to ×0.5..×2 of the mid band.
- **DAMPING** sets the high band to ×1..×0.2, from a corner that moves from
  ~10 kHz down to 1.2 kHz (zita-rev1's "HF half-time", folded into one knob).

### Fdn16: the network ROOM, HALL and CLOUD share

```
in L -> lines 0-7, in R -> lines 8-15 (balanced random +-1 injection)
line i: Lagrange read (random-modulated, gliding) -> DecayFilter
[ROOM: 8 slowly rotating Givens pairs - a time-varying lossless matrix]
-> 16x16 Hadamard -> + input -> in-loop allpass -> line i
out: two ORTHOGONAL balanced +-1 pickups over all 16 lines
```

Three rules came out of prototyping. All three are in code comments.

- **Injection and pickup vectors must not be Hadamard rows.** H times one of
  its own rows is a single line, so all the energy recirculates through one
  delay - a comb.
- **The pickups must be orthogonal.** Non-orthogonal random pickups measured
  an IACC of -0.25.
- **In-loop allpasses must be short or low-gain.** An allpass's group delay
  swings from 0.3× to 3.4× its length at g = 0.54, and the long side rings.

### ROOM - "put this inside a space"

- **Early reflections:** image-source arrivals (Allen & Berkley, orders 1-2)
  in a 5.3 × 4.1 × 2.9 m shoebox scaled by SIZE (×0.35..1.6), 20 per ear.
  - All reflections are same-sign.
  - Arrivals closer than 0.6 ms are merged. Two equal taps a fraction of a
    millisecond apart are a comb with teeth kilohertz apart.
  - The taps are scattered in four groups, each through its own allpass pair,
    and only then summed, so the reflections are not exact copies of each other.
- **Late field:** Fdn16 with 14-62 ms lines and 3-5 ms in-loop allpasses at
  g 0.2-0.45.
  - Random modulation of 0.1-0.6 ms.
  - The rotating matrix: angles of 0.3-1.2 rad at 1-2 Hz.
- **The late field is fed 0.25 × reflections + 0.8 × the diffused input.** Fed
  from the reflections alone, every tail carried their comb as a fixed
  metallic colour (measured: ringing 9 dB over noise; 2 dB at this mix).
- **EARLY** trades the reflections against the tail.
- DECAY range 0.15-3 s.

### HALL - a large, smooth late field

- Six-stage stereo input diffusion, 1.3-15.7 ms, different on L and R.
- Fdn16 with 32-97 ms lines (×0.55..1.6 by SIZE) and 4-9 ms in-loop allpasses.
- Random modulation of 0.05-0.6 ms.
- True stereo in.
- A 14-reflection image-source cluster (×2.2 time scale) for the front of the
  hall, set by **EARLY**.
- DECAY range 0.8-16 s.

### PLATE - Dattorro, corrected

- Mono in, which is authentic for a plate (EMT 140). The paper's 7+7 output
  pickups make it stereo.
- 13 kHz input bandwidth.
- **Four short dense diffusers** (0.43-1.67 ms) ahead of the paper's four. A
  plate is dense within milliseconds; with the paper's four alone, the
  prototype took 324 ms to mix.
- The paper's input diffusion coefficients scale with DIFFUSION; 0.75 / 0.625
  at the default.
- **The tank decay comes from seconds and the half-loop length, so SIZE no
  longer changes the decay time.**
- One DecayFilter per half, damping in Dattorro's sense.
- Excursion of 0-1 ms (sine plus random) from MOD.
- Lagrange pickups that glide with SIZE.
- DECAY range 0.4-8 s.

### CLOUD - an ambient wash, with SHIMMER

- **Bloom:** four long, slowly modulated allpasses per channel, 23-89 ms,
  ±2.5 ms at 0.13-0.33 Hz. They turn attacks into swells.
- **Network:** Fdn16 with 80-234 ms lines and long modulated in-loop
  allpasses.
- Pickups are side-weighted (±45 %), so a panned source keeps its side. The
  mirrored weights keep the pickups orthogonal.
- DECAY range 2-60 s.

**SHIMMER is regeneration inside the loop:**

- **Source:** four lines per side are summed.
- **Band-limit:** a 160 Hz highpass, then an **8th-order Butterworth** at
  min(6 kHz, 0.2 fs). The doubled signal cannot alias. A 4th-order filter
  measured only -30 dB of alias at 44.1 kHz; this one measures -60 dB or better.
- **Shifter:** a two-head delay-line shifter at ratio 2.
  - sin² / cos² crossfades, which sum to exactly 1.
  - Windows of 95 and 113 ms.
  - Each head's read offset is re-drawn at random while that head is silent,
    which breaks up the comb without a step in either read.
- **Re-entry:** crossfaded into four lines' feedback as
  `sqrt(1-a²) y + a × 1.15 × shifted`.
  - Equal power, not additive. The ×1.15 restores the crossfade's -1.25 dB.
  - `a = 0.55 s^1.3`, gliding per sample.
- **Governor:** backs `a` off if the loop's energy grows with no input.
- Each pass climbs another octave. The band-limit removes the top, so the climb
  fades upward.
- When SHIMMER is 0 the shifters keep listening, so turning it up starts from
  audio rather than from silence.

### SPRING - Välimäki, Parker & Abel 2010

- Two springs of different transit time (36 and 42 ms × SIZE). The input is
  summed to mono, as in a tank.
- **Each spring's loop:**
  - 128 stretched allpasses `(a + z^-K)/(1 + a z^-K)`, K = fs / (2 fc), with
    fc = 2.8 kHz.
  - A randomly modulated delay.
  - A 4th-order lowpass at fc in the loop, and another on the input. The
    stretched allpass repeats every fs/K, so its chirp has images at 3fc, 5fc...
- Group delay rises with frequency below fc, so every echo is an upward chirp,
  and the highs fall further behind every round trip.
- **DRIP** sets a (0.4-0.7).
- **DIFFUSION** (shown as SPLASH) sets a 10-stage first-order allpass "splash".
- fc was lowered from the paper's 4.3 kHz to 2.8 kHz to put the dispersion in
  the audible midrange at this cost. With 4.3 kHz, matching the paper's spread
  would have taken about 400 sections.
- The two chains are interleaved in one loop for instruction-level parallelism.
- DECAY range 0.6-6 s.

### GATED - a shaped burst

- No feedback at all, so it is unconditionally stable.
- **Per channel:**
  1. Two short diffusion chains (0.7-3.6 ms). Long allpasses ring on past the
     gate.
  2. 48 irregular taps over LENGTH (DECAY: 80-700 ms). Their gains draw
     **SHAPE**: 0 is a reverse swell, the middle a flat gate, full is falling.
     The taps alternate between the two chains; all taps reading one diffused
     signal would be an FIR of identical copies, a fixed comb.
  3. Three short post-allpasses, then a DAMPING lowpass.
- The last 15 % of the envelope is a smoothstep, so the gate closes without a
  click.

## Parameters

| Control | Id | Range (per type) | Default |
| --- | --- | --- | --- |
| MIX | `fx.reverb.amount` | equal power, 0 dry .. 1 wet (Synth: dry + wet·sin) | Synth 0, PX3 Reverb 0.35 |
| MODE | `fx.reverb.algorithm` | ROOM, PLATE, HALL, CLOUD, SPRING, GATED | ROOM |
| PRE-DELAY | `fx.reverb.pre.delay` | 0-250 ms (squared) | 10 ms |
| DECAY | `fx.reverb.decay` | log, per type (see above) | 0.45 |
| SIZE | `fx.reverb.size` | per type | 0.5 |
| DAMPING | `fx.reverb.damping` | flat .. dark | 0.35 |
| LOW | `fx.reverb.low` | x0.5 .. x2 | x1 |
| DIFFUSION | `fx.reverb.diffusion` | grainy .. smooth | 0.7 |
| MOD | `fx.reverb.mod` | depth+rate macro, per type | 0.35 |
| WIDTH | `fx.reverb.width` | mono .. natural | 1 |
| EARLY | `fx.reverb.early` | ROOM, HALL | 0.5 |
| SHIMMER | `fx.reverb.shimmer` | CLOUD | 0 |
| DRIP | `fx.reverb.spring.drip` | SPRING | 0.5 |
| SHAPE | `fx.reverb.gate.shape` | GATED | 0.5 |

- **DECAY's range follows the type,** so halfway means "medium for this space".
  Value text shows seconds (or metres / ms for SIZE), computed per type.
- **Every continuous control is a modulation destination,** and all are smoothed.
- **Inert controls are dimmed on the card** and pinned bit-exact by
  `Reverb_InertControlsAreReallyInert`:
  - MOD and LOW on GATED;
  - LOW on SPRING;
  - EARLY outside ROOM / HALL;
  - SHIMMER outside CLOUD;
  - DRIP and SHAPE outside their types.
- Removed parameters (no migration, by decision): `mod.depth`, `mod.rate`,
  `cloud.feedback`, `cloud.diffusion`, and the IR state.

### Card

MODE, then PRESET, each on its own row, then:

- SIZE, DECAY, PRE-DELAY and the **type slot**. The slot is one cell holding
  EARLY, SHIMMER, DRIP and SHAPE; it shows the current type's and is empty on
  PLATE (`FxCardComponent::addKnobSlotToLastRow`).
- DAMPING, LOW, DIFFUSION.
- MOD, WIDTH.
- MIX.

Captions follow the type: DECAY reads LENGTH on GATED; SIZE reads LENGTH and
DIFFUSION reads SPLASH on SPRING. `ReverbCard.h` does this for both products.

## Presets

Presets are type-specific and named by use; there is no shared list.

- **Choosing one** (message thread) writes the type and its controls as host
  gestures. It never touches MIX or the bypass.
- **Applying is click-free,** because everything it writes is smoothed.
- **"Name*"** is derived: the menu compares the live controls with the
  preset's.
- **The selection** ("TYPE/Name") is a state property, not a parameter. It is
  saved with the session, in patches and in PX3 Reverb.
- **After a type change** the menu shows the placeholder "PRESET" and nothing
  else changes.
- Factory patches that use the reverb now use these presets.

| Type | Preset | Purpose | Sets |
| --- | --- | --- | --- |
| ROOM | Tight Room | reflections, no audible tail | 0.3 s, 0 ms, size .30, damp .45, early .75, low x0.8 |
| ROOM | Synth Room | all-purpose room for leads/polys | 0.75 s, 6 ms, size .5, damp .35, early .5, low x0.9, mod .3 |
| ROOM | Small Studio | treated live room for plucks/keys | 0.5 s, 3 ms, size .35, damp .6, early .6, low x0.75, diff .8 |
| ROOM | Wide Room | bigger, wider, for stereo synths/arps | 1.0 s, 10 ms, size .65, damp .3, early .3, width .9, mod .4 |
| ROOM | Dark Room | warm and close; tames bright saws/FM | 1.1 s, 4 ms, size .55, damp .8, early .5 |
| ROOM | Drifty Room | a moving tail for static pads/drones | 1.4 s, 8 ms, size .65, damp .4, early .35, mod .75 |
| HALL | Synth Hall | the default hall for leads/chords | 2.4 s, 20 ms, size .55, damp .35, early .4, mod .35 |
| HALL | Wide Pad | spacious late field + full width behind pads | 4.5 s, 35 ms, size .8, damp .4, diff .85, mod .5, early .2, low x0.9 |
| HALL | Lead Throw | long pre-delay keeps articulation | 2.0 s, 60 ms, size .6, early .25, low x0.8 |
| HALL | Dark Hall | big and warm | 3.5 s, 25 ms, size .7, damp .75, low x1.15, early .4 |
| HALL | Bright Hall | open, for dull or filtered sounds | 2.8 s, 18 ms, size .6, damp .12, low x0.8, early .45 |
| HALL | Epic Synth | huge and long, for swells | 8 s, 45 ms, size 1, damp .45, low x0.85, mod .55, early .3 |
| PLATE | Short Decay | dense, immediate, under a second | 0.7 s, 0 ms, size .4, damp .2, low x0.75, diff .8 |
| PLATE | Synth Plate | the classic plate | 1.8 s, 10 ms, size .5, damp .25, low x0.85 |
| PLATE | Vocal-ish Synth | the vocal-plate recipe for formant patches | 2.2 s, 30 ms, size .55, damp .3, low x0.7, mod .45 |
| PLATE | Bright Plate | sparkling, for dark sources | 2.0 s, 8 ms, damp .05, low x0.8 |
| PLATE | Dark Plate | damped, for bright buzzy sources | 2.5 s, 10 ms, size .55, damp .7, low x0.9 |
| PLATE | Long Plate | a large plate held long | 5 s, 15 ms, size .85, damp .35, low x0.8, mod .5 |
| PLATE | Percussive Plate | small, tight, wide; FM percussion | 1.0 s, 0 ms, size .25, damp .3, low x0.6 |
| CLOUD | Soft Cloud | gentle wash, no shimmer | 6 s, 20 ms, size .45, damp .45, diff .75, mod .45, low x0.85 |
| CLOUD | Synth Cloud | long, evolving | 10 s, 25 ms, size .6, damp .4, diff .8, mod .55, low x0.8 |
| CLOUD | Shimmer Wash | the classic shimmer pad | 12 s, 30 ms, size .6, damp .35, diff .8, shimmer .55, low x0.7 |
| CLOUD | Octave Bloom | heavy shimmer, full bloom | 18 s, 40 ms, size .75, damp .3, diff .9, shimmer .8, low x0.6 |
| CLOUD | Ethereal Pad | light shimmer, long moving tail | 20 s, 35 ms, size .7, diff .85, mod .75, shimmer .3, low x0.75 |
| CLOUD | Infinite | 60 s sustain for drones | 60 s, 20 ms, size .8, damp .5, diff .85, mod .6, low x0.6 |
| CLOUD | Dark Cloud | long, dark, low wash | 14 s, 20 ms, size .65, damp .85, diff .8, mod .45 |
| SPRING | Surf Tank | drippy amp tank | 2.5 s, size .5, damp .3, drip .85, splash .6 |
| SPRING | Dub Spring | long and dark | 4.5 s, size .75, damp .55, drip .65, splash .5 |
| SPRING | Short Spring | twang without a wash | 1.2 s, size .3, damp .35, drip .4, splash .5 |
| SPRING | Bright Twang | open and splashy | 2.2 s, size .5, damp .1, drip .9, splash .8 |
| SPRING | Dark Tank | muffled, sits under a mix | 3.0 s, size .6, damp .75, drip .6, splash .4 |
| GATED | 80s Gate | flat burst that stops dead | 300 ms, size .5, damp .3, diff .8, shape .5 |
| GATED | Tight Gate | thickens percussion, no tail | 150 ms, size .4, damp .35, diff .8, shape .55 |
| GATED | Reverse Swell | rising swell into each hit | 450 ms, size .6, damp .35, diff .9, shape 0 |
| GATED | Big Snap | longer falling burst for snares/claps | 500 ms, size .7, damp .25, diff .85, shape .75 |

### Screening

`PX3Diag reverb-renders <dir>` renders every preset, and every type at four
lengths, on synthesised material: drums, a vowel voice, a mono lead, bass,
piano-like, plucked guitar, a pad, an arpeggio, FM percussion, a sparse melody
and a dense mix. The renders are at MIX 35 %. A preset is rejected for any of:

| Reason | Rule |
| --- | --- |
| Low build-up | Wet energy below 150 Hz, relative to broadband, more than 3 dB over the dry's |
| Smears transients | Onset sharpness of the mix below 0.6 (median over the percussive sources) |
| Muddy / harsh | Wet / dry spectral centroid outside 0.45-1.6 |
| Narrow | Mean wet inter-channel correlation above 0.5 (non-bass sources) |
| Phasey in mono | A mono-sum loss beyond -6 dB |
| Rings | Tail ringing more than 5 dB over decaying noise |

- **Rejected and revised: Wide Room v1** (SIZE .75, WIDTH 1, EARLY .45). A
  sparse melody lost 6.3 dB in mono: inter-ear reflection timing cancelled
  single tones. v2 takes its width from the late field instead.
- **Dropped at design time as duplicates:** "Frozen Sky" (= Infinite) and
  "Wide Plate" (= WIDTH).
- **All 35 final presets pass.**
- **Open, outside the presets:** PLATE at 3.4 s and longer measures -6.2 and
  -7.4 dB mono loss on the sparse tones. That is the paper's pickup signs; the
  old plate measured -6.8 dB.

## Measurements (48 kHz, default controls unless noted)

| | ROOM | PLATE | HALL | CLOUD | SPRING | GATED |
| --- | --- | --- | --- | --- | --- | --- |
| Mixing time, ms, before -> after | 113 -> 43 | 207 -> 12 | 211 -> 73 | 416 -> 339 (bloom) | - | 14 |
| Tail ringing over noise, dB, before -> after | +7.9 -> +1.4 | +2.1 -> +2.9 | +6.4 -> +0.8 | +2.3 -> +1.0 | +2.1 | (burst) |
| Spectral flatness, before -> after | 0.046 -> 0.40 | 0.075 -> 0.50 | 0.12 -> 0.54 | 0.083 -> 0.52 | - | - |
| 1 kHz RT60 vs DECAY target (DECAY 0.25 / 0.7) | -9 % / +3 % | +1 % / +1 % | +1 % / -2 % | 0 % / -1 % | -1 % / -15 % | - |
| 1 kHz RT60 deviation 44.1 / 96 / 192 kHz vs 48 kHz | ≤ 2.3 % | ≤ 0.7 % | ≤ 1.5 % | ≤ 0.7 % | ≤ 1.8 % | - |

- **Before** figures come from the Python port at decay 0.6 and from the
  pre-change `PX3Tests reverbmetrics`.
- **CLOUD's mixing time** is the intended bloom; its echo density is 0.95 at
  400 ms.
- **SHIMMER at 0.6:**
  - 880 Hz rises from -61.2 to -7.1 dB against 440 Hz.
  - 1760 Hz rises from -75.7 to -19.1 dB.
  - 660 Hz, which is not an octave, stays at -49.5 dB.
  - The tail still falls 29.7 dB from second 2 to second 7.
  - The 4.1 kHz fold of a 20 kHz input at 44.1 kHz is -108 dB.
- **SPRING** echo period: 49.5 ms at 500 Hz, 69.5 ms at 2 kHz. First arrival:
  14.5 ms at 300 Hz, 34.8 ms at 2 kHz.
- **SPRING's long decays read about 15 % short:** the loop gain uses the
  chirp's DC group delay as the round trip.

**CPU,** class alone, stereo noise, µs per 512-sample block at 48 kHz (median
of 5; before -> after):

| ROOM | PLATE | HALL | CLOUD | CLOUD + SHIMMER | SPRING | GATED |
| --- | --- | --- | --- | --- | --- | --- |
| 85 -> 135 | 53 -> 68 | 71 -> 126-132 | 72 -> 137 | 78 -> 150 | 172 | 113 |

- Each figure is about 1.1-1.6 % of one core.
- The old FDNs called `pow` and `sin` 8 times per sample, but had half the
  lines and no interpolation in the loop.
- An idle or MIX-0 reverb now costs nothing.

**Memory:**

- Reverb storage was ~1.53 MB per instance at 48 kHz; it is now 1.11 MB
  (arena 1.01 MB + pre-delay 0.10 MB).
- At 192 kHz it was ~6.1 MB; it is now ~4.4 MB (arena 4.02 MB).
- `scripts/memory-benchmark.sh`, per instance:
  - stress scenario: 16.6 -> 16.0 MB;
  - default: 2.9 -> 3.0 MB, within the tool's 0.1 MB resolution and noise.

## Tests and tools

- `PX3Tests reverb`: 164 checks. They cover:
  - transparency and linearity;
  - per-type diffusion, ringing, flatness, decorrelation, balance and smooth
    decay at two decay lengths;
  - DECAY as a time at every rate;
  - DAMPING and LOW direction;
  - stability at every extreme at 44.1 / 48 / 96 / 192 kHz;
  - NaN / Inf, denormals, sleep, reset;
  - no stale-tail replay;
  - determinism;
  - no zipper or click on any control or on type switches;
  - stereo, WIDTH and the mono sum;
  - PRE-DELAY;
  - level match;
  - inert and live controls;
  - SHIMMER pitch, cleanliness, decay and alias floor;
  - SPRING dispersion;
  - GATED gate and shape;
  - arena size;
  - presets and the card.
- `PX3Tests reverbmetrics` - the measures table per type and decay.
- `PX3Diag reverb-metrics [rate]` - impulse measures and calibration levels.
  Override any control with `PX3_RV_<NAME>=value`.
- `PX3Diag reverb-renders <dir>` - the listening set and the preset screen.
- `PX3Diag reverb-zip <type> <control>` and `reverb-onset` - click and onset
  hunting.
- `PX3Diag rtsafety` - includes type switches, preset applies and MIX
  sleep/wake: 0 allocations.
- `PX3Bench reverb` - CPU per type.

## Known limits

- **ROOM's early pattern colours the sound,** as a real room's does: a fixed
  comb from its reflections, about 2-3 dB over noise in the tail.
- **Linear interpolation remains** on the ROOM / HALL reflection taps and
  GATED's taps. These are outside any loop.
- **Echo density is a per-sample statistic,** so mixing times read longer at
  96 / 192 kHz (HALL 73 -> 115 ms at 192 kHz) for the same decay. RT60s match.
- **SPRING** is a parametric model, not a calibrated tank. Its dispersion is
  in the right direction, rising with frequency (period ratio 1.4 between
  500 Hz and 2 kHz), but it is not fitted to a specific unit.
