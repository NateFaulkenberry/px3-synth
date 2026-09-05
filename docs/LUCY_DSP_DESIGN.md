# LUCY — DSP Design

A Lossy-inspired spectral degradation instrument for the P(X3) synthesiser.

Chase Bliss's **Lossy** was built with **Goodhertz**. It is not a bitcrusher: its
own documentation says the Loss modes "create their effect by manipulating the
frequency spectrum", offers a hidden control that switches between equal and
**psychoacoustic** frequency weighting, and describes the result as "lossy data
compression reminiscent of a low bit-rate digital MP3". LUCY is built to that
reading.

Throughout: **Lossy publicly documents X; LUCY reproduces X using Y.** Nothing
here claims to describe Chase Bliss's or Goodhertz's implementation.

---

## 1. What Lossy publicly documents

From the official Chase Bliss / Goodhertz Lossy manual (22pp) and product page.

### The three blocks and the signal flow

> "The pedal can be broken into three main blocks: A **loss** section that
> deconstructs the audio. A **filter** section to shape and emphasize the
> artifacts. A **reverb** section to feed or diffuse the loss."

The manual's own signal-flow page gives the order:

```
INPUT → VERB (PRE) → LOSS → PACKETS → FREEZE → FILTER → VERB (POST) → GATE → LIMITER → OUTPUT
```

with the reverb **at the front by default**:

> "The reverb comes at the very front of the signal flow by default. This feeds
> the reverb into Lossy's other parts, exciting and enhancing their effect. It
> makes for a more cohesive and expansive sense of degradation."

and `PRE/POST` moving it to the back for "a more traditional reverb that
steadily trails out, regardless of whatever else Lossy is doing."

### GLOBAL

> "Sets the overall amount of processing **in place of a Mix control**. You can
> think of GLOBAL like a macro knob that increases the intensity of everything."

### LOSS

> "Controls the depth of the Loss and Packet effects. This will control both the
> **strength** of the effects, as well as **which frequencies are affected**."

and

> "Each mode starts by affecting a **narrow strip** of the frequency spectrum and
> then gradually **spreads** as the LOSS knob is turned up."

### SPEED

> "Controls the rate of the Loss and Packet effects, as well as the **update rate
> of the Freeze**. Slower speeds introduce **spectral smearing** and leave space
> between packets, while faster speeds become more **garbled and textural**."

### Loss modes

| Mode | Documented behaviour |
|---|---|
| **STANDARD** | "Lossy data compression reminiscent of a low bit-rate digital MP3, with a **darker** sound that is **stuffed full of chiming spectral harmonics**." |
| **INVERSE** | "A counterpart that plays **everything stripped away in Standard mode**, revealing a **brighter, thinner** sound with a **moving, feathery** quality." |
| **JITTER** | "Simulates inaccuracies in **phase and timing** due to imperfect clocking. Useful for introducing digital sizzle, noise, and irregularities." |

### Packet modes

| Mode | Documented behaviour |
|---|---|
| **PACKET LOSS** | "**Randomly** generates **brief** audio drop-outs — moments of silence that replicate the **skips and spaces of a bad connection**." |
| **CLEAN** | "Removes the Packet effect for a steadier, more stable sound." |
| **PACKET REPEAT** | "Follows the same idea as Packet Loss, but instead of silence the spaces are **filled with spectral smears**." |

> "The Packet effects also **randomly alternate left and right** when SPREAD is
> engaged."

### Filter

> "It's a **band-pass** filter made up of three parts: FILTER sets the **width**.
> FREQ sets the **position**. SLOPE sets the **slope**."

with SLOPE documented as **6 dB** ("gentle, tone-knob-like"), **24 dB**
("balanced, mildly-resonant") and **96 dB** ("intense, highly-emphasized"), and
FILTER at minimum giving **no filtering at all**. `INVERT` turns it into a
band-reject.

### Verb

> "Lossy's reverb is as **digital as could be**, with a warm character and unique
> decay that **fizzles and sputters**. Rather than creating realistic-sounding
> spaces, it's instead reminiscent of the reverbs used by electronic musicians in
> the **early 1990s**."

### Freeze

Two states, both documented:

> "**Solid state** — the current sound will infinitely sustain like an ambient pad."

> "**Slushy state** — the freeze is able to **update itself** and become a
> **shifting spectral copy** of your input audio. You can use this to 'refill' the
> sound of the freeze… The SPEED knob will adjust how quickly it updates."

`FREEZER` (hidden) "adjusts the balance between the live and frozen signal."

### Gate

> "All audio quieter than the cutoff will be silenced… It can create
> **sputtering effects like a distant radio station** at lower settings.
> Medium settings work well as a performance gesture to introduce **momentary
> failure**. And higher settings will consume your sound, only letting through
> **little blips of audio**."

### Limiter and gain

- `THRESHOLD` — "sets the threshold of a built-in limiter — the lower the
  threshold, the more limiting. The limiter helps keep the output levels
  consistent and also **brings out the details** of Lossy's various modes."
- `AUTO GAIN` — "gradually introduces automatic gain compensation for the Loss
  modes. **The Loss modes create their effect by manipulating the frequency
  spectrum**, so this helps keep the perceived volume the same."
- `WEIGHTING` — "controls whether frequencies are weighted **equally** or if they
  use a **psychoacoustic model** for the Loss modes." DARK ← NEUTRAL → BRIGHT.
- `LOSS GAIN` — ±36 dB on the wet signal.

### SLOW

> "Captures the classic sound of the Lossy plugin — **bigger, darker, slower, and
> with more latency**."

Sources:
- Chase Bliss / Goodhertz — Lossy manual (PDF, 22pp), <https://www.chasebliss.com/manuals>
- Chase Bliss — Lossy product page, <https://www.chasebliss.com/lossy>
- Goodhertz — <https://goodhertz.com/>

---

## 2. What can reasonably be inferred

Flagged as inference, not documented fact.

1. **A perceptual coder is the model, not a bitcrusher.** "Lossy data
   compression", "manipulating the frequency spectrum", and a **psychoacoustic
   weighting** option together describe masking-based coding. A bitcrusher has no
   spectrum and nothing to weight.
2. **INVERSE is the residual, not one minus an amount.** "Everything stripped
   away in Standard mode" is a subtraction in the same domain the stripping
   happened in. The residual of a masking coder is the discarded quiet bins plus
   the quantisation error — which is exactly "brighter, thinner… moving,
   feathery".
3. **SPEED is a decision rate.** One control setting the rate of Loss, Packets
   *and* Freeze, where slow gives "spectral smearing" and "space between
   packets", is a control over **how often the engine makes a new decision** —
   not three separate rate multipliers.
4. **Packet loss is bursty.** "Skips and spaces of a bad connection" is burst
   behaviour, not independent per-sample dropout.
5. **PACKET REPEAT is error concealment.** Filling a gap with the previous
   material is precisely what a codec's packet-loss concealment does, and
   "spectral smears" says it is done in the spectral domain.

---

## 3. The control model

Between the knobs and the engine there is one translation layer:
`shared/DSP/Lucy/LucyControlModel.{h,cpp}`.

```
    USER CONTROLS            LucyUserParameters      what a person turns
          │
          ▼
    deriveLucyParameters()                           one function, once a block
          │
          ▼
    DSP PARAMETERS           LucyDerivedParameters   coverage, step sizes,
          │                                          frame counts
          ▼
    THE ENGINE               px3::Lucy
```

**No stage of the engine reads a knob.** `applyLoss` asks for a masking depth,
not for LOSS; `applyPackets` asks for a probability, not for LOSS and SPEED. The
transfer curves used to live inline in those four functions, which meant the
answer to "what does LOSS at 0.4 actually do" was spread across four stages of
DSP and could not be tested without running audio. They are named functions now
— `mapLossToCoverage`, `mapLossToQuantisation`, `mapSpeedToDecisionFrames`,
`applyGlobalIntensity` — and the control-model tests call them directly.

### The three macros

**GLOBAL** is an *intensity* macro, not a wet/dry. It scales the coder's depth
and coverage, the packet probability, the filter's amount and the freeze blend —
each through its own exponent, so the stages arrive in a deliberate order:

| exponent | arrives | which |
|---|---|---|
| 0.7 | early | FILTER — it shapes where artifacts live rather than making them |
| 1.0 | proportional | loss depth, coverage, quantisation, freeze, verb |
| 1.6 | late | PACKETS — a dropout at a quarter intensity is not "subtle" |

A crossfade survives in the bottom **8%** of GLOBAL's travel and nowhere else,
so the effect reaches genuinely clean at zero and the idle path (which skips two
FFTs per hop) has a continuous way in and out. Above that region the dry term is
gone. This is a discontinuity guard, not the mechanism: a crossfade cannot keep
the character recognisable as intensity rises, because it only ever changes how
much of a *fixed* wet signal you hear.

**LOSS** drives five derived values through five separately-tuned curves. Using
one curve for all five is what made the bottom half of the knob do nothing and
the top tenth do everything:

| derived | curve | why |
|---|---|---|
| coverage | `0.05 + 0.95·loss^1.15` | early LOSS works a narrow strip, widening to the whole spectrum |
| masking depth | `0.02 + 1.75·loss^1.6` | obvious digital-compression character by half travel |
| discard ratio | `0.35 + 0.65·loss^0.8` | at the bottom only clearly-inaudible bins go, so LOSS thins before it gouges |
| quantisation | `0.05 + 2.40·loss^2.0` | the harshest artifact, deliberately late |
| packet probability | `0.50·loss^2.4` | later still; a knob that stutters at a quarter turn has no usable bottom half |

**SPEED** is one temporal control. Nothing else in the engine picks its own rate:
the coding decision interval, the packet state interval, the slushy-freeze drift
and JITTER's two rates all derive from it. All four are **geometric**, because
each is a rate — a linear map from a knob to a frame count spends most of its
travel in a range that sounds the same. Decision hold runs 16 frames down to 1,
with the middle of the knob at 4 rather than the 8 a linear map gives.

SLOW is orthogonal and multiplies with it: doubling the transform halves the
frame rate, so SLOW + low SPEED is substantially slower than either alone.

### Ownership

Each control owns one thing, and the tests in §8 pin the separation:

| control | owns |
|---|---|
| GLOBAL | how strongly the whole effect is expressed |
| LOSS | degradation depth and spectral coverage |
| SPEED | temporal evolution, for every stage |
| FILTER / FREQ / SLOPE | where the artifacts live |
| VERB / DECAY | the reverb |
| LOSS GAIN | wet level, and nothing else |

---

## 4. Architecture

Following the documented signal flow.

```
                         INPUT (stereo)
                              │
              ┌───────────────┴────────────────┐
              │                                │ dry (undelayed)
              ▼                                │
      ┌──────────────┐                         │
      │ VERB  (PRE)  │  feeds the loss         │
      └───────┬──────┘                         │
              │                                │
   ═══════════▼═══════════ STFT analysis ══    │
              │                                │
      ┌───────▼──────┐                         │
      │ LOSS ENGINE  │  masking model          │
      │  STANDARD    │  + spectral quantiser   │
      │  INVERSE     │  + coverage band        │
      │  JITTER      │                         │
      └───────┬──────┘                         │
      ┌───────▼──────┐   Gilbert-Elliott       │
      │  PACKETS     │   burst model           │
      └───────┬──────┘                         │
      ┌───────▼──────┐                         │
      │   FREEZE     │  solid / slushy         │
      └───────┬──────┘                         │
   ═══════════▼═══════ STFT synthesis ═════    │
              │                                │
      ┌───────▼──────┐                         │
      │ FILTER       │ band-pass, 6/24/96 dB   │
      └───────┬──────┘                         │
      ┌───────▼──────┐                         │
      │ VERB (POST)  │  optional               │
      └───────┬──────┘                         │
      ┌───────▼──────┐                         │
      │    GATE      │                         │
      └───────┬──────┘                         │
      ┌───────▼──────┐                         │
      │   LIMITER    │                         │
      └───────┬──────┘                         │
              │                                │
              ▼            GLOBAL              │
            ┌──────────────────┐◄──────────────┘
            │       MIX        │
            └────────┬─────────┘
                     ▼
                  OUTPUT
```

---

## 5. Subsystem designs

### 5.1 Spectral engine

`StftEngine` (shared with DOOM's SOUP). Hann analysis **and** synthesis, hop =
size/4, so the Hann-squared product sums to a constant at 75% overlap and
reconstruction is unity with no normalisation table. A frame the callback leaves
untouched comes back out unchanged — which is the property the "LOSS at zero is
transparent" test rests on.

**Size:** 512 normally, **1024 in SLOW**. 512 at 48 kHz is a 94 Hz bin and a
10.7 ms frame — coarse enough that partials of a synth's low notes share bins
(which is where the coder's "chiming" character comes from) and short enough
that a pluck's transient survives. 1024 doubles the frequency resolution and the
frame length, which is precisely the documented "bigger, darker, slower, and
with more latency".

**Latency, honestly:** the wet path is one frame late — 10.7 ms, or 21 ms in
SLOW. The **dry path is not delayed**, so no host latency is reported and no comb
is created against the FX bus's dry sum. On a degradation effect whose wet signal
deliberately bears little resemblance to its input, a frame of offset reads as a
short pre-delay. SLOW's doubling of it is a documented characteristic of the
mode, not a defect.

**Phase:** magnitude-only manipulation wherever possible, with the input's own
phase carried through. Spectral processing that rewrites phase carelessly turns
chords into mush; the modes that *do* touch phase (JITTER, FREEZE, PACKET
REPEAT) do so deliberately and are the only ones that should smear.

### 5.2 LOSS — a masking coder, not a bitcrusher

Per frame, per bin:

**1. Masking threshold.** Bin magnitudes are grouped into ~24 **critical bands**
(Bark scale), each band's energy is spread into its neighbours by an asymmetric
**spreading function** (steeper downward than upward in frequency, as masking
actually is), and the result is the masking threshold. This is the core of every
perceptual coder.

**2. Weighting.** The threshold is tilted by `WEIGHTING`: NEUTRAL uses the
psychoacoustic curve, DARK biases it to preserve lows, BRIGHT to preserve highs.
Documented as exactly this choice.

**3. Coverage.** LOSS also sets **which** frequencies are touched. A coverage
window starts as a narrow strip and widens with LOSS, so at low settings only a
band is degraded and at maximum the whole spectrum is — documented verbatim.

**4. Discard and quantise.** Inside the coverage window, bins below the
threshold are **discarded**; bins above it are **quantised** on a logarithmic
magnitude grid whose step widens with LOSS. Discarding is what makes it darker
(quiet high partials go first). Coarse quantisation of what survives is what
produces the "chiming spectral harmonics" — the error is concentrated at the
partials that remain.

**STANDARD** outputs the coded spectrum.

**INVERSE** outputs the **residual**: `X − coded`, bin by bin. Not `1 − amount`;
the complement of a spectral operation in the spectral domain. Everything
STANDARD threw away — the sub-threshold bins and the quantisation error — is
what remains, and it is brighter and thinner because that is what a coder
discards, and it moves and feathers because which bins fall below threshold
changes every decision.

**JITTER** perturbs **phase and timing**, as documented:
- per-bin phase offset following a bounded **random walk** (not white noise:
  clock error is correlated in time, and a random walk is what an unstable clock
  actually produces),
- plus a time-domain **fractional-delay wobble** on the input, driven by a
  smoothed random signal and read with cubic Lagrange interpolation.

Both, because "phase **and** timing" is two things.

**AUTO GAIN** compares frame energy before and after coding and applies the
ratio, smoothed across frames. Documented as existing precisely because the Loss
modes work by manipulating the spectrum.

### 5.3 SPEED — one decision rate

The engine holds its decisions for `N` frames, where `N` falls as SPEED rises.
Held decisions are the masking pattern, the packet state and the freeze target.

- **Slow** — one decision spans many frames, so the same bins stay discarded
  across time. That *is* spectral smearing, and it is also what leaves long
  spaces between packets.
- **Fast** — a new decision every frame, so the pattern changes constantly:
  garbled and textural.

One mechanism, three documented consequences, from one control.

### 5.4 PACKETS — Gilbert-Elliott bursts

Packet loss on a real link is **bursty**: losses cluster. The standard model for
this is the **Gilbert-Elliott** two-state Markov chain — a GOOD state and a BAD
state with transition probabilities between them. LUCY uses it directly, with
LOSS setting the probability of entering BAD and SPEED setting how long a state
lasts.

That is what makes this sound like a broken connection rather than a tremolo: a
tremolo is periodic, independent per-sample randomness is hiss, and a
two-state chain produces runs — a few frames fine, then a cluster gone.

- **PACKET LOSS** — frames in BAD are zeroed, with a short spectral fade at the
  edges so a drop-out is a gap rather than a click.
- **PACKET REPEAT** — frames in BAD re-emit the last GOOD frame's magnitudes with
  phase advanced by the expected per-hop increment. This is **packet loss
  concealment by frame repetition**, which is what a codec does, and advancing
  phase rather than repeating it is what turns a repeat into a smear.
- **CLEAN** — the chain does not run.

With SPREAD, the chain is run **independently per channel** with a shared
probability, so drop-outs alternate sides — documented behaviour, and it keeps
the two channels related rather than independent.

*Reference:* Gilbert–Elliott burst-error channel model; packet-loss concealment
by frame repetition, standard in speech and audio codecs.

### 5.5 FREEZE — spectral, not a looper

Magnitudes are latched per bin; phase advances by the bin's expected per-hop
increment with a small bounded random component, so a frozen chord sustains
without becoming a static metallic tone.

- **Solid** — the latch holds.
- **Slushy** — the frozen magnitude is continuously interpolated toward the live
  input at a rate set by SPEED, so it becomes "a shifting spectral copy of your
  input audio" and can be refilled by playing a new chord.

`FREEZER` crossfades the frozen spectrum against the live one.

This is a spectral freeze, not "record 100 ms and loop it": the frozen sound has
no period, no loop point, and can be filtered and re-degraded as a spectrum.

### 5.6 FILTER

A band-pass whose **width** is the primary control, matching the documentation:
at minimum there is no filtering at all, and turning it up narrows the band
around FREQ.

Implemented as cascaded 2-pole state-variable sections — 1 section for 6 dB,
2 for 24 dB, 8 for 96 dB — with resonance rising with the slope, because the
documented descriptions ("mildly-resonant", "highly-emphasized") are about
resonance as much as steepness. `INVERT` subtracts the band-pass from the input
to give the band-reject.

It sits **after** the spectral engine, per the documented signal flow: its job is
to "shape and emphasize the artifacts", which means it must act on the artifacts.

### 5.7 VERB — pre by default

A 4-line **feedback delay network** with a Hadamard mixing matrix, modulated
delay lengths and one-pole damping in the loop. Its "as digital as could be"
character comes from a deliberate **coarse quantiser inside the feedback path**,
so the tail loses resolution as it recirculates and "fizzles and sputters" —
early-90s digital reverb behaviour, and the reason it is not the project's
existing `Reverb`.

**PRE** (default) puts it in front of the loss, so the loss codes the reverb's
tail as well as the input — which is what "exciting and enhancing their effect"
means, and it is the routing that makes the degradation cohesive rather than
decorative. **POST** puts it after, for a tail that trails out regardless.

### 5.8 GATE

Envelope follower, **hysteresis** (the open threshold sits above the close
threshold, so a signal hovering at the cutoff does not chatter), and separate
attack/release. Low cutoff sputters, medium fails momentarily, high passes
blips — the three documented behaviours fall out of one threshold.

### 5.9 LIMITER

A lookahead peak limiter: a short delay so the gain reduction is in place before
the peak arrives, with attack/release smoothing. Necessary here rather than
decorative — freeze, reverb feedback and coarse spectral quantisation can each
produce peaks the input never had. Lowering the threshold increases limiting and
"brings out the details", as documented.

---

## 6. Intentional approximations / adaptations

1. Footswitch gestures become parameters: freeze state, gate on/off.
2. Presets, MIDI, ramping and CV are the host's job; the synth has them already.
3. `ALL WET`'s true-analog dry thru has no meaning inside a plugin's FX bus.
4. `TRAILS`, `MISO` and `DRY KILL` are pedal-in-a-chain concerns.
5. The critical-band model is a simplified Bark-scale spreading function, not a
   full ISO/IEC psychoacoustic model — enough to produce coder-like behaviour at
   a fraction of the cost.

---

## 7. Parameters

Six primary knobs, each carrying a second function, as the pedal prints them.
The panel's **ALT** switch selects which of a pair is displayed; both are real
parameters, attached and automatable whichever way it is set.

| primary | alternate |
|---|---|
| FILTER | GATE |
| GLOBAL | FREEZER |
| VERB | DECAY |
| FREQ | THRESHOLD |
| SPEED | AUTO GAIN |
| LOSS | LOSS GAIN |

| Parameter | Range | Default | Purpose |
|---|---|---|---|
| `lucyEnabled` | bool | true | bypass |
| `lucyGlobal` | 0…1 | **0.0** | macro intensity, *not* a mix. Zero by default so adding LUCY changes no existing patch |
| `lucyLoss` | 0…1 | 0.55 | degradation depth **and** which frequencies it reaches |
| `lucySpeed` | 0…1 | 0.5 | the one decision rate: Loss, Packets, Freeze, Jitter |
| `lucyFilter` | 0…1 | **0.0** | filter width; zero is genuinely no filtering |
| `lucyFreq` | 0…1 | 0.5 | filter centre |
| `lucyVerb` | 0…1 | 0.0 | reverb amount |
| `lucyGateThreshold` | 0…1 | 0.25 | *alt of FILTER* — gate threshold |
| `lucyFreezer` | 0…1 | 1.0 | *alt of GLOBAL* — live ↔ frozen balance |
| `lucyDecay` | 0…1 | 0.45 | *alt of VERB* — reverb size / length |
| `lucyLimiterThreshold` | 0…1 | 0.8 | *alt of FREQ* — **limiter** threshold; lower means more limiting. Named in full because the coder has a masking threshold of its own, which is derived and never a parameter |
| `lucyAutoGain` | 0…1 | 0.75 | *alt of SPEED* — gain compensation for the Loss modes |
| `lucyLossGain` | −36…+36 dB | 0 dB | *alt of LOSS* — wet gain, and nothing else |
| `lucyMode` | STANDARD / INVERSE / JITTER | STANDARD | type of degradation |
| `lucyPackets` | CLEAN / LOSS / REPEAT | CLEAN | connection-style dropouts |
| `lucySlope` | 6 / 24 / 96 dB | 24 dB | filter slope. The section count behind it is internal |
| `lucyWeighting` | DARK / NEUTRAL / BRIGHT | NEUTRAL | which end of the spectrum the coder protects |
| `lucyFreeze` | OFF / SOLID / SLUSHY | OFF | one control, not two booleans — the pair could express "slushy while not frozen", which meant nothing |
| `lucyFilterInvert` | bool | false | band-pass ↔ band-reject |
| `lucyVerbPost` | bool | false | reverb after the chain instead of feeding it |
| `lucyGate` | bool | false | gate on |
| `lucySlow` | bool | false | bigger, darker, slower, more latency |
| `lucySpread` | 0…1 | 0.5 | stereo: packet alternation and reverb width |

**ALT is deliberately not a parameter.** It selects which function the six
paired knobs display, which is a property of the panel rather than of the sound.

There are **no backwards-compatibility shims**. `lucyFilterFreq`,
`lucyGateCutoff`, `lucyThreshold`, `lucyGain` and `lucyFreezeSlushy` are gone
rather than aliased, and `lucyWeighting` changed from a bipolar float to a
three-way choice. Sessions saved before this refresh will load LUCY at its
defaults.

## 8. CPU

Two real FFTs per hop per channel: at 512/128 and 48 kHz that is ~750 FFT pairs
per second per channel. The masking model is O(bins) per frame with a fixed
band count. Everything is allocated in `prepare()` — including the 1024-point
plan, so toggling SLOW allocates nothing. `processSampleFrame` has no
allocation, no locks and no unbounded loops.

## 9. Testing strategy

A dedicated `lucy` suite: construction and defaults; 44.1/48/88.2/96 kHz;
silence, impulse, and sines from 50 Hz to 10 kHz; complex material (saw, square,
chords); every Loss mode at five depths; every Packet mode with a seeded chain;
freeze solid and slushy, alone and combined with loss, packets and reverb;
jitter at five depths; the filter at its extremes and every slope; reverb pre
and post at high feedback; gate at four cutoffs; the limiter fed deliberate
overload; hostile combinations; per-parameter automation sweeps; state and
preset round-trip; and FX ordering against DOOM, Delay and Reverb.
