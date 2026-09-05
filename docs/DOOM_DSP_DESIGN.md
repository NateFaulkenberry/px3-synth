# DOOM — DSP Design

A BAD MOOD-inspired two-channel ambient processor for the P(X3) synthesiser.

DOOM is **not** a port of the existing `Mood` engine. The two share a control
philosophy — a micro-looper, a wet channel, and one clock that ties them
together — because Chase Bliss built BAD MOOD around the MOOD MKII control
scheme. Everything under those controls is different, and deliberately so:
Chase Bliss describe BAD MOOD as *"a fresh approach to the concepts found in
MOOD, a batch of brand new feelings"*, not a MOOD revision.

Throughout this document: **BAD MOOD exhibits X; DOOM reproduces X using Y.**
Nothing here claims to describe Chase Bliss's implementation.

---

## 1. What BAD MOOD publicly documents

From the official Chase Bliss BAD MOOD manual (27pp) and product page.

### Two channels that are aware of each other

> "BAD MOOD is a two-channel multi-effect. One half samples and loops brief
> moments, the other is a collection of real-time spatial effects. […] What
> makes BAD MOOD unique is that its two channels are aware of each other and
> can interact in a variety of ways."

### CLOCK

> "The CLOCK knob controls everything. Specifically, it sets BAD MOOD's sample
> rate. […] **WET CHANNEL** — the quality and time of the effects.
> **MICRO-LOOPER CHANNEL** — the length and resolution of the loops. It's tone,
> length, and quality, all in one. What makes it interesting is that it moves in
> musical, harmonized steps. For example, lowering the sample rate from 64k to
> 32k will halve the speed of your micro-loop as well as your Wet Channel
> effect."

A `SMOOTH` dip switch "removes the harmonized stepping effect from the CLOCK
knob for fluid adjustment."

### Micro-Looper Channel — always listening

> "It's an 'always listening looper'… It is continuously recording when
> bypassed, and then you turn it on and see what you get. Instead of manually
> setting the length like a typical looper, it's set by the CLOCK position."

Three states: **Recording** (bypassed) / **Playing** / **Overdubbing**. The
bypassed state doubles as a *replace* function — "as soon as the channel is
bypassed it starts to erase the existing loop and record the input audio in its
place."

Three modes:

| Mode | Documented behaviour | LENGTH | MODIFY |
|---|---|---|---|
| **BURST** | "takes your loops and turns them into rhythmic patterns of up to 8 steps… Wherever a 'unique' sound is detected in the loop, Burst creates a step. It then cycles through those slices of audio to create a pattern." Patterns "dynamically react to your instrument when in playback, to create randomizing 'fills'". | speed of pattern / size of each step | sensitivity of the envelope detector |
| **RADIO** | "contains five distinct loopers that take the same recording and interpret it into different genres spread across various stations. You can scan freely between the stations, introducing interference and combining the different loops." Stations: TAPE (speed/direction), AMBIENT (pitch-preserving slowdown, "cinematic blur"), ORCHESTRAL (# of voices "that come in and out"), SHOEGAZE ("frozen moments that last forever… stacked layers"), DANCE (rotates half/double/normal speed). | station-dependent | scans the stations |
| **MASK** | "takes the loud parts of your loop and turns them into something new. Any sound over the volume threshold is changed in a way of your choosing." MODIFY fully down = "the pure micro-loop recording". | character of the mask | threshold |

### Wet Channel

| Mode | Documented behaviour | TIME | MODIFY | Freeze |
|---|---|---|---|---|
| **SOUP** | "a spectral reverb that resynthesizes your playing. It analyzes and recreates whatever passes through it, creating unnatural ambience that is a distant memory of your instrument." "Sounds best when CLOCK is rolled back, but turning it up will introduce sparkling artifacts." | decay time | character — "rotate clockwise to emphasize Soup's synthetic nature" | ambient pad |
| **RELAY** | "a delay that doesn't fade out. Unlike a traditional delay where you control the amount of feedback, Relay lets you select a precise number of repeats that each share the same volume." At max, "repeats are stable and will pile up like a looper." | delay time | number of repeats | looping echo |
| **FLIP** | "a pitch shifter that creates layered harmonies to support your playing, but allows you to spread the different notes across time." | lag time between notes | "a variety of different arrangements of 4ths, 5ths, and octaves, going both up and down. The higher the knob is set, the more notes will be present." | repeating chord |

### ROUTING

Three positions, and it "only has an effect when both channels are on":
INPUT ONLY / INPUT + MICRO-LOOPER / MICRO-LOOPER ONLY. Critically:

> "When the Micro-Looper Channel is in its always-listening state (bypassed), it
> will record the sounds from the Wet Channel regardless of the routing setting."

and

> "By default, any micro-loops routed through the Wet Channel will become 100%
> wet… But you can blend some of the clean micro-loop back in" (the BLEND hidden
> option).

### CROSS

> "BAD MOOD contains a unique form of modulation that dynamically interferes
> with both **pitch and loudness**. It can modulate itself or be modulated by
> your playing. The intensity of this modulation is set by CROSS. The source of
> this modulation is set by INPUT MOD."

INPUT MOD chooses: your input modulates both channels, or one channel modulates
the other.

### GLUE

> "a hidden global effect that's tucked behind the CLOCK knob… GLUE comes right
> at the end of the pedal's signal chain, and applies to both channels. Set it
> low to warm up and gel the two channels, set it high to completely thrash
> everything passing through the pedal."

### Other documented controls

- **EQ** — "a two-way global EQ. Rotate the knob clockwise to remove low
  frequencies; rotate counter-clockwise to remove high frequencies. At noon the
  EQ will have no effect." (i.e. a tilt.)
- **BALANCE** — "the relative loudness of the two channels."
- **BLEND** — clean micro-loop blended back when routed through the wet channel.
- **FADE** — "loops will gradually fade while overdubbing… or the ability to
  treat the Micro-Looper Channel like a delay."
- **SPREAD** — "turns on stereo processing. Each mode has its own unique
  approach to generating a stereo image."
- **HALF** — "cuts the loop length in half, matching the response of the
  original MOOD."

Sources:
- Chase Bliss — BAD MOOD manual (PDF, 27pp), <https://www.chasebliss.com/manuals>
- Chase Bliss — BAD MOOD product page, <https://www.chasebliss.com/bad-mood>
- Chase Bliss — MOOD MKII manual (PDF), <https://www.chasebliss.com/manuals>

---

## 2. What can reasonably be inferred

These are **inferences**, flagged as such, not documented facts.

1. **"Sample rate" is literal.** The manual's own worked example (64k → 32k
   halves both the loop speed and the wet effect) is exactly what a single
   engine sample rate does. So DOOM runs its whole engine on an internal clock,
   rather than scaling a set of independent time constants.
2. **"Harmonized steps" implies a ratio table, not a continuous sweep.** A
   continuous rate would not be describable as steps, and there would be no need
   for a `SMOOTH` switch to remove them. The ratios are almost certainly simple
   (halving is named explicitly).
3. **Soup being "spectral" and "resynthesizing" implies an analysis/synthesis
   pass**, not a delay network. A feedback delay network reverberates the signal;
   it does not "analyze and recreate" it, and it does not become *more*
   synthetic as a character knob rises.
4. **Relay's "precise number of repeats that each share the same volume" cannot
   be a feedback loop.** Feedback produces a geometric decay by construction.
   Equal-level, countable repeats implies parallel taps.
5. **Burst's steps come from onset detection.** "Wherever a *unique* sound is
   detected" is the language of a novelty/onset function, not of a fixed grid.

---

## 3. The control model

Between the knobs and the engine there is one translation layer:
`shared/DSP/Doom/DoomControlModel.{h,cpp}`.

```
    USER CONTROLS          DoomUserParameters      six knobs, two functions each
          │
          ▼
    deriveDoomParameters()                         one function
          │
          ▼
    DSP PARAMETERS         DoomDerivedParameters   a delay in seconds, a tap
          │                                        count, a harmony index
          ▼
    THE ENGINE             px3::Doom
```

**The four macros mean different things in different modes**, and that is the
whole control scheme rather than an inconsistency. TIME is a decay in SOUP, a
delay in RELAY and a lag in FLIP; LOOP MODIFY is a fill sensitivity in BURST, a
station scan in RADIO and a threshold in MASK. Each of those is a named mapping
function, so the engine is handed a quantity rather than a knob position:

| control | mode | derives |
|---|---|---|
| TIME | SOUP | `mapWetTimeToSoupT60` — squared, 0.25…14 s |
| TIME | RELAY | `mapWetTimeToRelayDelay` — squared, 0.03…0.9 s |
| TIME | FLIP | `mapWetTimeToFlipLag` |
| WET MODIFY | RELAY | `mapWetModifyToRelayTaps` — a **count**, 1…8, plus infinite at the top |
| WET MODIFY | FLIP | `mapWetModifyToFlipHarmony` — an index into a widening table |
| LENGTH | BURST | `mapLengthToBurstStep` — **inverted**: more LENGTH is a faster sequence and so a shorter step |
| LOOP MODIFY | BURST | `mapLoopModifyToBurstSensitivity` |
| LOOP MODIFY | RADIO | `mapLoopModifyToStation` — a **scan**, returning two stations and a blend |
| LOOP MODIFY | MASK | `mapLoopModifyToMaskThreshold` — reaches a true zero, which is the untouched loop |
| CLOCK | — | `mapClockToRatio` — the harmonised table, or SMOOTH's continuous sweep over the same span |

The curves themselves are unchanged: this was a relocation, not a retune. The
per-sample stages still map their own smoothed knob through the same functions,
so automating a macro glides rather than stepping at block boundaries; the
block-rate `DoomDerivedParameters` is the same model seen once a block, and is
what the control-model tests assert against.

### Six controls, twelve functions

The panel pairs each primary with its alternate, as the pedal prints them. Both
are real parameters, attached and automatable whichever the ALT switch is
showing:

| primary | alternate | the question each answers |
|---|---|---|
| TIME | CROSS | how long does the wet thing go / how much do the channels interfere |
| WET MODIFY | EQ | what kind of wet thing is it / where does the whole thing sit tonally |
| LENGTH | FADE | how does the loop behave / how quickly does it evolve |
| LOOP MODIFY | BLEND | how does the loop transform / how much clean loop remains |
| CLOCK | GLUE | how fast and degraded is the machine / how much do I warm or destroy it |
| MIX | BALANCE | how much DOOM do I hear / which channel dominates |

**RAMP is deliberately absent.** The pedal uses MIX as a ramp-speed control
when its ramping infrastructure is engaged. A plug-in host already provides
automation and this project already has a modulation matrix, so a second
internal ramp framework would be a worse version of something the user already
has. It is host-provided by design, not an omission.

**ALT is not a parameter.** It selects which function the six paired knobs
display, which is a property of the panel rather than of the sound.

### Ownership

Each control owns one thing, and `DoomControl_EachControlOwnsOneThing` pins it:
GLUE never moves the clock, CLOCK never moves GLUE or MIX, MIX and BALANCE are
orthogonal, and the two MODIFYs never reach into each other's channel.

---

## 4. Architecture

```
                        INPUT (host rate, stereo)
                                 │
                    ┌────────────┴────────────┐
                    │                         │ dry
                    ▼                         │
            ┌──────────────┐                  │
            │ CLOCK        │  decimate        │
            │ box-average  │  (anti-alias)    │
            └──────┬───────┘                  │
                   │  internal rate           │
    ┌──────────────┴────────────────────┐     │
    │        DOOM ENGINE (internal)     │     │
    │                                   │     │
    │   ┌───────────────────────────┐   │     │
    │   │ MICRO-LOOPER              │   │     │
    │   │  always-listening history │   │     │
    │   │  BURST / RADIO / MASK     │   │     │
    │   └────────┬──────────────────┘   │     │
    │            │        ▲             │     │
    │      ROUTING│        │ record     │     │
    │            ▼        │             │     │
    │   ┌───────────────────────────┐   │     │
    │   │ WET CHANNEL               │   │     │
    │   │  SOUP / RELAY / FLIP      │   │     │
    │   └────────┬──────────────────┘   │     │
    │            │                      │     │
    │      ┌─────┴─────┐                │     │
    │      │  CROSS    │ ◄── env of     │     │
    │      │  AM + FM  │     input or   │     │
    │      └─────┬─────┘     other chan │     │
    │            ▼                      │     │
    │      BALANCE · BLEND · SPREAD     │     │
    │            │                      │     │
    │            ▼                      │     │
    │          EQ tilt                  │     │
    │            │                      │     │
    │            ▼                      │     │
    │          GLUE                     │     │
    └────────────┬──────────────────────┘     │
                 │                            │
                 ▼  hold + reconstruction LPF │
              ┌──────┐                        │
              │ MIX  │◄───────────────────────┘
              └──┬───┘
                 ▼
              OUTPUT
```

---

## 5. Subsystem designs

### 5.1 CLOCK — musical sample-rate stepping

**BAD MOOD exhibits** a single sample-rate control that changes loop length,
loop pitch, wet time and wet quality together, in harmonised steps.

**DOOM reproduces this** with a decimated internal engine. The whole engine runs
at `internalRate = hostRate * ratio`, where `ratio` is quantised to:

```
1, 3/4, 2/3, 1/2, 3/8, 1/3, 1/4, 3/16, 1/8, 1/12, 1/16
```

These are the simple-integer ratios — octaves (1/2, 1/4, 1/8, 1/16), fifths
(2/3, 1/3), fourths (3/4, 3/8, 3/16) and a twelfth (1/12). Because a buffer
recorded at one rate and replayed at another changes speed and pitch together,
each step is a musical interval on everything the engine holds. `SMOOTH`
(exposed as a parameter) bypasses the quantiser for continuous sweeps.

Down-conversion uses a **box average** over each internal step rather than
point sampling: point sampling folds everything above the internal Nyquist
straight into the band, which is noise rather than character. Up-conversion is
a zero-order hold followed by a one-pole reconstruction filter at the internal
Nyquist — the hold's imaging is a large part of what makes low clock settings
sound *digital* rather than merely dull, so it is tamed, not removed.

*References:* sample-rate conversion and imaging — Smith, **Physical Audio
Signal Processing** / **Spectral Audio Signal Processing** (CCRMA),
<https://ccrma.stanford.edu/~jos/>.

### 5.2 MICRO-LOOPER — always listening

**BAD MOOD exhibits** a looper that records continuously while bypassed, so
engaging it captures audio that has *already happened*.

**DOOM reproduces this** with a circular history buffer written on every
internal step regardless of state. Engaging playback latches
`loopStart = writePos − loopLength`; no recording is started, because the
material is already there.

`loopLength` is held in **internal samples**, so lowering CLOCK lengthens the
loop in real time and drops its pitch — one control, both consequences, as
documented. `HALF` halves it.

Loop boundaries are read through a **short equal-power crossfade** across the
splice rather than a hard pointer wrap. A wrap that steps from the end of the
loop to its start is a click once per loop, which is a defect rather than a
character. Intentional digital artifacts are produced by the clock and by GLUE,
where they are controllable.

Fractional read positions use **cubic Lagrange (Catmull-Rom) interpolation**,
matching the existing `Delay` and `CombResonator` precedent in this codebase.
Allpass interpolation is rejected for the same reason as there: it is recursive,
so it produces transients under modulation, and the read head here is modulated
constantly.

*References:* fractional delay — Välimäki & Laakso, *Principles of Fractional
Delay Filters*; CCRMA on delay-line interpolation.

#### BURST — onset-sliced sequencer

Slices are found by an **onset detector** over the captured loop: a
half-wave-rectified spectral-flux-style novelty function computed from a
short-window energy envelope, peak-picked against an adaptive median threshold,
capped at 8 slices and floored at a minimum slice spacing. Wherever the loop has
a "unique sound", there is a step — as documented.

The sequencer then cycles those slices at a rate set by LENGTH. Live input above
the MODIFY threshold **scrambles** the step order: the permutation is drawn from
a seeded generator, so the fills are reproducible under test while sounding
unrepeatable in use. Steps are enveloped with a short attack/decay so slice
boundaries do not click.

*Why this rather than a fixed grid:* a grid divides the loop; onset detection
divides the *music* in the loop. The documented behaviour is the latter.

#### RADIO — five stations with interference

MODIFY scans a 1-D station axis. Each station has a centre; the output is a
**crossfade between the two nearest stations plus a static bed** whose level
rises with distance from any centre and falls to zero at a centre. That gives
the documented "clean, pure version of each one" at the centres and
"interference and combining" in between. The static is band-limited noise
gated by the loop's own envelope, so it behaves like interference on a signal
rather than a noise generator running underneath.

| Station | DSP |
|---|---|
| TAPE | variable-rate playback, LENGTH bipolar over −2×…+2× including reverse |
| AMBIENT | **granular time-stretch**: overlapping Hann grains read at unity rate while the grain origin advances slowly. Pitch preserved, time dilated — the documented "cinematic blur". |
| ORCHESTRAL | N granular voices at harmonic intervals (unison, ±octave, ±fifth, ±fourth), each with its own slow amplitude envelope so voices "come in and out". LENGTH sets N. |
| SHOEGAZE | short windows are captured and sustained indefinitely by overlap-adding a held grain; LENGTH selects which moment, and layers stack. |
| DANCE | a three-position rotator between ½×, 2× and 1× playback, crossfaded at each change; LENGTH sets rotation speed. |

#### MASK — threshold-driven substitution

An envelope follower on the loop drives a smooth gate. Above threshold
(MODIFY), the loop is crossfaded into a *disguise* selected by LENGTH:

```
ring modulation → reversal → pitch displacement → resonant excitation
```

MODIFY at zero yields the untouched loop, as documented. The crossfade is
smoothed so the mask "turns on and off" musically rather than switching.

### 5.3 WET CHANNEL

#### SOUP — spectral resynthesis reverb

**BAD MOOD exhibits** a reverb that "analyzes and recreates" the input, whose
character control emphasises its synthetic nature, and that becomes "sparkling"
at high clock rates.

**DOOM reproduces this** with an **STFT magnitude-decay reverberator** rather
than a feedback delay network. Per frame, per bin:

```
M[k] ← max(M[k] · g[k], |X[k]|)      g[k] = 10^(−3·hop / (T60(k)·rate))
```

The magnitude accumulator decays exponentially toward silence and is re-excited
by the input — this is frequency-domain reverberation by spectral magnitude
decay, which gives independent decay control per bin and, unlike an FDN,
genuinely *resynthesises* the signal rather than reflecting it.

Phase is what MODIFY controls, and this is the crux of the "synthetic" axis:

- MODIFY low — phase advances by the bin's expected per-hop phase increment
  from the input's own phase, so partials stay coherent and the tail sounds like
  a (strange) room.
- MODIFY high — phase is progressively **randomised** per frame and the
  magnitude spectrum is **blurred across neighbouring bins**. Randomised phase
  destroys the transient structure and leaves only the spectral envelope, which
  is exactly "a distant memory of your instrument".

`T60(k)` is tilted with frequency (highs decay faster) so the tail darkens
naturally. Because SOUP runs at the internal rate, low CLOCK settings shrink
the analysis band and lengthen every frame in real time — dark and slow — while
high CLOCK settings leave the top octaves in, which is where the documented
"sparkling artifacts" live.

Configuration: **512-point FFT, hop 128 (75% overlap), Hann analysis and
synthesis**. Hann at 75% overlap satisfies COLA under the analysis×synthesis
window product, so reconstruction is unity without a normalisation table. 512 at
the internal rate is a deliberate compromise: long enough for a usable bin
spacing, short enough that the frame is a handful of milliseconds and DOOM does
not need host latency reporting.

*References:*
- Frequency-domain reverberation by spectral magnitude decay —
  <https://www.sfxmachine.com/docs/FDReverbSpectralMagDecay.pdf>
- Phase-vocoder phase handling and phase randomisation (the Paulstretch
  approach) — <https://github.com/oramics/dsp-kit/blob/master/docs/phase-vocoder.md>
- STFT applications and modification — Smith, *Spectral Audio Signal
  Processing*, <https://www.dsprelated.com/freebooks/sasp/Applications_STFT.html>

#### RELAY — countable, non-decaying repeats

**BAD MOOD exhibits** "a delay that doesn't fade out… a precise number of
repeats that each share the same volume", piling up like a looper at maximum.

**DOOM reproduces this** with **parallel taps on a single write buffer**, not a
feedback loop. Feedback produces geometric decay by construction; parallel taps
do not. MODIFY selects N ∈ 1…8; taps sit at `k · delayTime` for k = 1…N and all
share one gain. The sum is normalised by `1/√N` so adding repeats does not add
level, and the whole bank passes through a soft saturator so a pile-up
compresses instead of clipping. At maximum, a ninth "hold" tap recirculates at
unity through that saturator, which is what makes it behave like a looper while
remaining bounded — the saturator is the energy sink that a plain `feedback =
1.0` does not have.

*References:* nonlinear feedback and energy management in recursive delay — the
project's existing `CombResonator` (Jot's T60 rule, always-saturated loop) and
`Delay` normalisation rules.

#### FLIP — harmonies spread across time

**BAD MOOD exhibits** a pitch shifter producing layered harmonies, spread across
time by a lag control, with more notes as MODIFY rises, drawn from 4ths, 5ths
and octaves in both directions.

**DOOM reproduces this** with a **granular pitch shifter** — overlapping
Hann-windowed grains read at a rate ratio, 4 grains per voice at 25% offsets so
the overlap is constant-power — one voice per harmony note, each voice delayed
by `k · lag`. Harmonies come from a table indexed by MODIFY, widening as it
rises:

```
0:  +12
1:  −12
2:  +7            3:  −5
4:  +7, +12       5:  −5, −12
6:  +5, +7, +12   7:  −12, −5, +7, +12
```

Granular rather than phase-vocoder shifting: the artifacts of short-window
granular shifting are the *point* of this mode ("try turning up TIME a bit to
replicate the laggy character of older pitch shifters"), and it costs a fraction
of the CPU.

*References:* granular pitch shifting, grain overlap and windowing —
<https://documentation.dspconcepts.com/awe-designer/8.D.2.6/granular-synthesis-module>,
and Smith, *Spectral Audio Signal Processing* on time-scale modification.

### 5.4 CROSS — signal-dependent interference

**BAD MOOD exhibits** modulation that "dynamically interferes with both pitch
and loudness", sourced either from the input or from the opposite channel.

**DOOM reproduces this** with an **envelope-follower cross-modulation network**,
explicitly not a random-number generator:

```
source (input | opposite channel)
   │
   ▼
full-wave rectify → fast-attack / slow-release follower
   │
   ▼
slew limiter (bounded d/dt)          ← makes it organic rather than jumpy
   │
   ├─► AM: gain = 1 − depth·env       (loudness interference / dropouts)
   └─► FM: read-rate = 1 + depth·k·(env − ⟨env⟩)   (pitch interference)
```

Two properties make it musical rather than cheap:

1. **It is correlated with the music**, because its source is the music. The
   result "squiggles" where you play, which is what the documentation
   describes.
2. **The slew limiter bounds the derivative**, so parameters never jump. A jump
   in read-rate is a click; a bounded ramp is a bend.

The FM term is driven by the follower's deviation from its own long-term mean,
so a sustained pad does not simply detune — it wavers.

Stability: depth is bounded, the follower is a one-pole (unconditionally
stable), and read-rate is clamped to a musical range. The cross path never
carries audio, only a control value, so no audio feedback loop is created even
when each channel modulates the other.

*References:* envelope following and dynamic parameter modulation — standard
practice; slew limiting as the difference between "organic" and "jumpy" is the
same rule this codebase already applies to user-facing gains.

### 5.5 GLUE — end-of-chain saturator/destroyer

**BAD MOOD exhibits** a global end-of-chain effect spanning "warm up and gel"
through "completely thrash everything".

**DOOM reproduces this** as a **three-region drive** rather than one `tanh`:

| GLUE | Behaviour |
|---|---|
| 0 | unity, bit-transparent |
| low | asymmetric soft saturation — a small even-harmonic term, which is what "warm" means spectrally |
| mid | symmetric soft clip, rising harmonic density, gentle high-tilt |
| high | **wavefolding** past the first fold — this is where it stops being distortion and starts being destruction |
| max | folding plus aggressive downward compression |

Asymmetry is deliberate and small: a purely odd-symmetric shaper generates only
odd harmonics and sounds sterile at low drive. Output is level-compensated per
region so turning GLUE up changes *character*, not loudness, and the stage is
DC-blocked after folding because asymmetric shaping generates DC by definition.

*References:* soft clipping, asymmetric waveshaping, wavefolding and harmonic
generation — standard nonlinear-processing literature; the project's existing
`Vibe` saturation stage.

### 5.6 EQ, BALANCE, BLEND, FADE, SPREAD

- **EQ** — a **tilt filter**: a matched low-shelf/high-shelf pair moving in
  opposition around a 700 Hz pivot. Documented as two-way with no effect at
  noon, which is a tilt, not two cuts.
- **BALANCE** — equal-power crossfade between the two channels' contributions.
- **BLEND** — dry micro-loop mixed back when the loop is routed through the wet
  channel, exactly as the hidden option documents.
- **FADE** — a per-lap multiplier applied to the loop buffer during overdub, so
  loops evolve and the looper can behave as a delay.
- **SPREAD** — per-mode stereo. Each mode decorrelates differently (grain pan
  by voice, tap pan by index, spectral bin-phase decorrelation in SOUP) rather
  than one global width control, matching "each mode has its own unique approach
  to generating a stereo image".

---

## 6. Intentional approximations

1. DOOM has no footswitches, so the looper's state (recording / playing /
   overdubbing) is a parameter rather than a gesture, and FREEZE is a toggle.
2. Presets, MIDI, ramping, CV and the dip switches are the host's job here —
   the synth already has a preset system, a modulation matrix and automation.
   Reimplementing them inside one FX would be a second framework.
3. `TRAILS`, `DRY KILL`, `MISO` and true bypass are properties of a pedal in a
   signal chain, not of an FX inside a synth's FX bus.
4. RADIO's five stations are DOOM's reading of five documented *genres*. The
   genre names are documented; the algorithms behind them are not, and these are
   independently designed.
5. `SYNC` (locking one channel's time to the other) is folded into the clock
   rather than exposed, to keep the control count honest.

---

## 7. Parameters

The identifiers below are implementation names and are unchanged by the control
refresh - what changed is that the engine no longer reads them directly, and
that the panel groups them into six pairs. Modes and routing became typed
enums rather than bare ints, which is a C++ change and not a state change:
saved sessions and factory presets load exactly as they did.

| Parameter | Range | Default | Purpose |
|---|---|---|---|
| `doomEnabled` | bool | true | bypass (true bypass semantics) |
| `doomMix` | 0…1 | **0.0** | dry ↔ DOOM, both channels. Zero by default, matching `reverbAmount`: adding an effect must not change what existing patches sound like. |
| `doomClock` | 0…1 | 1.0 | engine sample rate, harmonised steps |
| `doomClockSmooth` | bool | false | disable the harmonised quantiser |
| `doomRouting` | INPUT / INPUT+LOOP / LOOP | INPUT | what the wet channel processes |
| `doomLoopActive` | bool | false | playing (true) vs always-listening (false) |
| `doomLoopMode` | BURST / RADIO / MASK | RADIO | micro-looper mode |
| `doomLoopLength` | 0…1 | 0.45 | mode-dependent (see §5.2) |
| `doomLoopModify` | 0…1 | 0.50 | mode-dependent (see §5.2) |
| `doomLoopHalf` | bool | false | halve the loop length |
| `doomOverdub` | 0…1 | 0.0 | overdub amount into the loop |
| `doomFade` | 0…1 | 1.0 | loop retention per lap while overdubbing |
| `doomWetActive` | bool | true | wet channel on |
| `doomWetMode` | SOUP / RELAY / FLIP | SOUP | wet channel mode |
| `doomWetTime` | 0…1 | 0.45 | mode-dependent (see §5.3) |
| `doomWetModify` | 0…1 | 0.40 | mode-dependent (see §5.3) |
| `doomFreeze` | bool | false | freeze the wet channel |
| `doomCross` | 0…1 | 0.0 | cross-modulation intensity (off by default, as documented) |
| `doomCrossSource` | INPUT / CHANNEL | INPUT | cross modulation source |
| `doomGlue` | 0…1 | 0.15 | end-of-chain saturator/destroyer |
| `doomEq` | −1…+1 | 0.0 | tilt: −1 darker, +1 brighter |
| `doomBalance` | 0…1 | 0.5 | micro-looper ↔ wet channel |
| `doomBlend` | 0…1 | 0.0 | clean micro-loop blended past the wet channel |
| `doomSpread` | 0…1 | 0.5 | stereo processing depth |

---

## 8. CPU

- The engine runs at the **internal** rate, so every subsystem gets cheaper as
  CLOCK falls. Worst case is CLOCK at maximum.
- SOUP is the dominant cost: two 512-point real FFTs per hop per channel, hop
  128 → 2 × (2 FFTs / 128 internal samples). At unity clock and 48 kHz that is
  ~750 FFT pairs/second, which is a fraction of one core.
- SOUP's FFT scratch, the history buffer, all grain state and every delay line
  are allocated in `prepare()`. `processSampleFrame` allocates nothing, locks
  nothing, and contains no unbounded loops.
- Grain counts are fixed maxima (`kMaxGrains`), not dynamic.
- Only the active wet mode and the active loop mode run per sample.

## 9. Testing strategy

A dedicated `doom` suite covering: construction and defaults; preparation at
44.1/48/88.2/96 kHz and several block sizes; silence, impulse and sine
stability; every clock step; every loop mode and every radio station; every wet
mode; capture / playback / reverse / half-speed / double-speed / freeze /
overdub; cross at four intensities; glue at five; hostile combinations;
per-parameter automation sweeps; state and preset round-trip; and FX ordering.
Stochastic behaviour is driven by a seeded generator so tests are deterministic.
