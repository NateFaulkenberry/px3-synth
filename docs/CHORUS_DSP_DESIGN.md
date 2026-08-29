# CHORUS — DSP Design

A Dimension D-inspired stereo chorus for the P(X3) synthesiser, with the BOSS
CE-1 and the Juno/Solina ensemble circuits as secondary references.

The design target is Roland's own claim for the SDD-320:

> "The Dimension D gives a new dimension **without the apparent movement of
> sound** produced by most other chorus devices."

and

> "Monaural inputs … from a single point sound source into a sound which fills
> the entire stereo field."

Those two sentences are the whole brief. Width without wobble is not a tuning
problem — it is an architecture, and a single modulated delay plus a sine LFO
cannot produce it at any setting.

---

## 1. Research

### Roland SDD-320 Dimension D

**Documented by Roland:** four modes, "MODE 1 produces the softest effect; MODE
4 produces the strongest effect"; combination buttons; mono input feeds both
dimension channels; >95 dB S/N (A-weighted); balanced and unbalanced I/O.
Roland does not publish the circuit's operating principle.

**From published circuit analysis and service documentation** (flagged as
secondary, not Roland's own words):

- **Two independent BBD delay lines**, with **companders** and
  **pre-emphasis / de-emphasis** for noise performance.
- A **trapezoidal LFO** driving the BBD clock VCOs, roughly ±3 V.
- Two modulation speeds (on the order of a 2-second and a 4-second cycle) and
  two depths, selected by the mode buttons.
- **Mode 1** lowers the modulation depth *and* runs the VCOs slower, so its
  delay is longer than modes 2–4 — reported in the region of 8–12 ms.
- **Cross-channel routing with polarity inversion** between the two delay
  paths.

That last point is the mechanism. Sources:
- Roland — SDD-320 owner's manual, <https://archive.org/stream/SDD-320_owners_manual/SDD-320_owners_manual_djvu.txt>
- Roland — SDD-320 service notes
- Community circuit analysis (Klark Teknik BBD-320 threads, GroupDIY SDD-320
  calibration)

### BOSS CE-1 Chorus Ensemble

A single BBD path with a preamp, a compander, and a sine-ish LFO; a chorus/
vibrato switch that removes the dry path for vibrato. Its character comes from
its **bandwidth limit and its compander**, not from noise. Used here as the
reference for warmth and for a single-path chorus, not cloned.

### Juno-60 / 106, JX-3P/8P, Solina

The Juno chorus is **two BBD lines modulated in opposition**, summed to L and R
oppositely — the same trick as the Dimension D at a larger depth and rate. The
Solina-family ensemble uses **three phase-offset LFOs** across multiple delay
paths, which is why a string machine sounds like an ensemble rather than like
one detuned copy.

### DSP references

- Fractional delay and interpolation — Välimäki & Laakso, *Principles of
  Fractional Delay Filters*; CCRMA on delay-line interpolation,
  <https://ccrma.stanford.edu/~jos/>.
- BBD behaviour: bandwidth limiting, companding, pre/de-emphasis, clock
  feedthrough — standard analogue delay literature.

---

## 2. Why a single modulated delay fails

```
dry + delay(t + d·sin(ωt))
```

The wet copy's pitch is `1 + d·ω·cos(ωt)` — it goes sharp and flat, audibly and
periodically. Panning it, or inverting the LFO for the right channel, does not
fix it: each ear still hears a copy whose pitch is moving.

The Dimension D's answer:

```
delay A = base + depth·L(t)
delay B = base − depth·L(t)        ← anti-phase

L = dry + wetA − wetB
R = dry − wetA + wetB
```

Two consequences fall straight out of the algebra:

1. **The average pitch is constant.** When A goes sharp, B goes flat by the same
   amount. The ear integrates the pair and hears no vibrato — but the two copies
   are decorrelated from each other, which is what width is.
2. **`L + R = 2·dry`.** The wet terms cancel exactly in mono. The effect
   disappears when summed, and nothing combs. That is the strongest mono
   compatibility any chorus can have, and it is a property of the structure
   rather than of a setting.

CHORUS is built on this pair, and every mode is a different way of driving it.

---

## 3. Architecture

```
                    INPUT (stereo)
                          │
        ┌─────────────────┼──────────────────┐
        │ dry (untouched) │                  │
        │                 ▼                  │
        │        ┌────────────────┐          │
        │        │ pre-emphasis   │          │
        │        │ + compressor   │  BBD in  │
        │        └────────┬───────┘          │
        │                 │                  │
        │   ┌─────────────┴─────────────┐    │
        │   ▼                           ▼    │
        │ ┌──────────┐            ┌──────────┐
        │ │ DELAY A  │            │ DELAY B  │
        │ │ base+d·L │            │ base−d·L │   anti-phase
        │ │ Lagrange │            │ Lagrange │
        │ └────┬─────┘            └────┬─────┘
        │      │                       │
        │      ▼                       ▼
        │ ┌──────────────────────────────────┐
        │ │ bandwidth limit + soft saturate  │
        │ │ expander + de-emphasis           │
        │ │ wet high-pass (low-end anchor)   │
        │ └────┬────────────────────┬────────┘
        │      │ wetA               │ wetB
        │      ▼                    ▼
        │   L = dry + wetA − wetB
        └─► R = dry − wetA + wetB
                          │
                          ▼
                     WIDTH · MIX
                          │
                          ▼
                       OUTPUT
```

`ENSEMBLE` adds a third path with its LFO at 120°, distributed across the pair;
`CE WARM` collapses to one path with heavier companding and a lower bandwidth.

---

## 4. Subsystems

### 4.1 Modulation trajectory — trapezoid, not sine

The SDD-320's LFO is reported as **trapezoidal**. That is not incidental: a
trapezoid moves linearly between its extremes and dwells at them, so the
*velocity* — which is what pitch shift actually is — is constant over most of
the cycle rather than sinusoidal. A sine's pitch deviation is itself sinusoidal
and reads as "wooo"; a trapezoid's is close to two steady detunings with short
transitions, which reads as two slightly detuned copies. The corners are
rounded so the pitch does not step.

Rate sits in the region the hardware occupies — cycles of seconds, not hertz —
with the useful range weighted there rather than spread to 20 Hz.

**Not perfectly periodic:** a very slow, bounded random walk modulates the rate
and the depth by a few percent. Enough that the cycle never repeats exactly,
small enough that it is never heard as modulation of its own. This is the only
randomness in the effect and it is smoothed, bounded and correlated.

### 4.2 Fractional delay

**Cubic Lagrange (Catmull-Rom)**, matching `Delay`, `CombResonator` and `Doom`
in this codebase. Linear interpolation of a delay line moving at chorus rates is
audible as a dull, gritty modulation — its error is a level-dependent low-pass
that moves with the delay. Allpass interpolation is rejected because it is
recursive and produces transients under exactly the continuous modulation this
effect applies.

### 4.3 BBD character

Not noise. In order:

1. **Pre-emphasis** — a high-shelf boost before the delay.
2. **Compressor** — a soft, slow-acting gain reduction.
3. **Bandwidth limit** — a low-pass standing in for the BBD's clock-limited
   response, which is most of what makes an analogue chorus sound softer than a
   digital one.
4. **Soft saturation** — gentle, and inside the delay path only.
5. **Expander** — the compressor's inverse.
6. **De-emphasis** — the complement of the pre-emphasis.

Pre/de-emphasis around a compander is the documented BBD noise-reduction
arrangement, and it also produces the tone: the boost-then-cut is not exactly
complementary once saturation and bandwidth limiting sit between them, and that
asymmetry is the warmth. `CHARACTER` scales the whole group. At zero the wet
path is clean and modern.

**No noise generator.** A hiss floor is not what makes a chorus sound analogue,
and adding one to a synthesiser is a defect.

### 4.4 Low-end anchoring

The dry path is never filtered — it is the pitch and transient anchor, and on a
bass patch it is most of what is heard. The **wet** path is high-passed, so the
fundamental is not among the copies being detuned. A bass note keeps its weight
and its pitch while its harmonics move.

`LOW CUT` exposes the corner. Its default is placed where a synth bass
fundamental sits below it and the second harmonic above.

### 4.5 Modes

Every mode is a real change of architecture or of the modulation driving it, not
a preset of the same numbers.

| Mode | Structure |
|---|---|
| **DIM 1** | anti-phase pair, longest base delay, slowest LFO, smallest depth — the documented "softest", and the documented longer delay of mode 1 |
| **DIM 2** | shorter base delay, faster LFO, moderate depth |
| **DIM 3** | as 2, larger depth |
| **DIM 4** | shortest base delay, fastest LFO, largest depth — the documented "strongest" |
| **DIM 1+4** | both pairs running together at their own rates, summed. The combination buttons stack modes on the hardware; two pairs at different rates is a denser, less periodic field than either alone |
| **DIM 2+4** | as above, from modes 2 and 4 |
| **DIM 3+4** | as above, from modes 3 and 4 |
| **ENSEMBLE** | three paths, LFOs at 120°, distributed across the anti-phase sum — the string-machine arrangement |
| **CE WARM** | one path, heavier companding, lower bandwidth, sine LFO — the single-BBD chorus |

### 4.6 AMOUNT

A macro, not a multiplier over everything. Measured relationships:

```
AMOUNT   depth      wet level   width    character
0        —          0           —        —          (dry)
0.25     35%        45%         60%      30%        subtle width, stable centre
0.5      65%        80%         85%      55%        classic lush synth chorus
0.75     85%        100%        100%     75%        unmistakably modulated
1.0      100%       100%        115%     100%       expansive, still not seasick
```

Wet level reaches full before depth does, so pushing AMOUNT past the middle
deepens the movement rather than simply getting louder. `MIX` remains a separate
final dry/wet for placing the effect in a chain; `AMOUNT` is what the effect
does, `MIX` is how much of it you hear.

### 4.7 Feedback

Present but deliberately small, and capped well below where comb resonance
dominates. Chorus is defined by the *relationship* between a clean dry and
decorrelated wet copies; feedback strengthens the comb and turns it into a
flanger. It is available for colour and cannot reach flanger territory.

---

## 5. Parameters

| Parameter | Range | Default | Purpose | With AMOUNT |
|---|---|---|---|---|
| `chorusEnabled` | bool | true | bypass | — |
| `chorusAmount` | 0…1 | **0.0** | macro intensity | — |
| `chorusMode` | 9 modes | DIM 2 | architecture | — |
| `chorusRate` | 0…1 | 0.35 | LFO rate, weighted to hardware territory | independent |
| `chorusDepth` | 0…1 | 0.5 | modulation excursion | scaled by AMOUNT |
| `chorusWidth` | 0…1 | 0.75 | stereo expansion of the wet pair | scaled by AMOUNT |
| `chorusSpread` | 0…1 | 0.5 | LFO phase offset between paths | independent |
| `chorusTone` | −1…+1 | 0.0 | warm ↔ clear on the wet path | independent |
| `chorusLowCut` | 0…1 | 0.3 | wet-path high-pass; the bass anchor | independent |
| `chorusFeedback` | 0…1 | 0.0 | colour; capped short of flanging | independent |
| `chorusCharacter` | 0…1 | 0.5 | BBD group depth | scaled by AMOUNT |
| `chorusMix` | 0…1 | 1.0 | final dry/wet | independent |

Latency: **none**. The dry path is unfiltered and undelayed.

## 6. CPU

Two to three delay lines with cubic interpolation, one shelf pair, one low-pass
and one high-pass per channel. Comparable to the existing `Delay`. All buffers
allocated in `prepare()`.

## 7. Testing strategy

Construction and defaults; 44.1/48/88.2/96 kHz and several block sizes; silence,
impulse and sines from 50 Hz to 10 kHz; every mode; depth zero producing no
pitch modulation; **mono collapse at five AMOUNT settings**; correlation across
the width range; left-only, right-only and identical-stereo input; low-end
stability on a bass patch; automation sweeps on every continuous control;
extreme settings; state and preset round-trip; FX ordering.

The two measurements that matter most, because they are the design brief:

1. **Pitch stability** — the wet pair's average pitch deviation must stay far
   below a single-delay chorus at the same depth. Measured as the deviation of
   the summed signal's zero-crossing period.
2. **Mono collapse** — `L + R` must retain its level and its spectrum, because
   the wet terms cancel by construction rather than partially.
