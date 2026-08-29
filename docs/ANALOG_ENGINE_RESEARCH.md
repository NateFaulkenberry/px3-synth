# AnalogEngine — Research

Research and architectural debate for a distributed analog-console character
engine. Written before implementation, per the project brief.

---

## 1. The question

"Analog console sound" is usually sold as saturation. The research says it is
mostly **something that happens in the summing**, not something that happens to
a channel.

From the summing literature: when multiple signals are summed on a physical mix
bus, the components "react nonlinearly, generating subtle harmonic distortion and
**nonlinear inter-channel crosstalk**", and "a little harmonic distortion,
especially if it is **frequency-dependent**, can help tremendously in glueing
signals together". Transformers contribute "predominantly 2nd- and 3rd-order
harmonics"; even-order reads as warmth, odd-order as density.

The operative words are *inter-channel* and *glueing*. A saturator on each
channel does not glue anything — it distorts each channel identically and
independently, and the sum is just the sum of distorted things. Whatever "glue"
is, it is a property of channels **interacting** in the bus.

Sources:
- <https://science-of-sound.net/2016/02/analog-summing-demystified-part-1-an-introduction/>
- <https://vintagemaker.net/analogue-warmth-analog-summing-harmonics/>
- <https://www.sonarworks.com/blog/learn/transformers>

---

## 2. Airwindows Console — the central finding

Studied across three generations from the MacAU sources
(<https://github.com/airwindows/airwindows/tree/master/plugins/MacAU>, MIT).
53 Console-family plugins exist; the informative comparison is between
generations, not between siblings.

### PurestConsole2

```
Channel:  clamp(x, ±π/2) → sin(x)
Buss:     clamp(x, ±1)   → asin(x)
```

### Console5

Same `sin`/`asin` core, plus, on the channel:

```
difference = lastSample − input,  clamped ±1
difference = lastFX + asin(difference)      // arcsine ENHANCES slew
iirCorrect += input − difference             // DC servo accumulator
lastFX     += iirCorrect * 5e-7              // servo pulls DC back
nearZero    = (|lastFX| − 1)²
lastFX     *= 1 − nearZero * bassTrim        // level-dependent LF trim
```

The buss mirrors it with `sin()` on the slew — arcsine enhances, sine cuts back.

### Console7

```
Channel:  biquad HP → x *= gain³ → clamp(±1.097)
          → 0.8·sin(x·|x|)/|x| + 0.2·sin(x)   → x /= gain
Buss:     biquad HP → x *= √gain → clamp(±1)
          → 0.618·asin(x·|x|)/|x| + 0.382·asin(x) → biquad HP
```

Three things are new and all three matter:

1. **The transfer is a blend of two curves** with different harmonic structure
   (80/20 on the channel, golden-ratio weighted on the buss). The character is
   the *blend*, not one function.
2. **The fader is applied before the nonlinearity and undone after it**
   (`gain³` then `/gain`). The fader position therefore selects *where on the
   distortion curve the signal sits* — which is level-dependent character, not a
   volume control.
3. **Filtering is part of the model**, before and after.

### The invariant that makes this an architecture

For a single channel, `asin(sin(x)) = x`. **One channel through the console is
mathematically transparent.**

But `asin(sin(a) + sin(b)) ≠ a + b`. The nonlinearity exists *only in the sum*,
and it grows with the number of channels being summed.

That is the entire idea, and it is exactly what the brief asks for:

- Channel-only processing: transparent, as it should be.
- Bus-only processing: a plain saturator, which is what we are trying not to
  build.
- Channel **and** bus: character that emerges from accumulation and scales with
  how much is playing.

It also answers the brief's own test — "if Channel + Bus simply sounds like twice
as much distortion, rethink the architecture". Under this construction it
provably cannot, because for one channel it is exactly unity.

---

## 3. Independent perspectives

Airwindows is not treated as the sole authority.

### Wave Digital Filters

WDFs are the physically grounded alternative: port-Hamiltonian formalism
guarantees power balance and passivity, and published WDF models "produce richer
static harmonic response, introducing comparable or less aliasing and requiring
approximately 50% less CPU time than previous models".

The cost is real: "as far as circuits with multiple nonlinearities is concerned,
much research effort is still needed in order to develop systematic strategies
for solving the corresponding multivariate systems of implicit equations with low
computational requirements". Implicit solving per sample, per channel, on an
instrument that already runs 64 voices, is the wrong trade here.

- <https://pure.qub.ac.uk/en/publications/virtual-analog-modeling-of-audio-circuitry-using-wave-digital-fil/>
- <https://link.springer.com/article/10.1007/s00034-019-01331-7>

One WDF-adjacent finding is directly useful regardless of architecture:
**Antiderivative Antialiasing (ADAA)** achieves "significant aliasing reduction
even with low oversampling factors". That is a cheaper lever than 8× oversampling
and is considered in the aliasing section below.

### JUCE

`juce::dsp::Oversampling` provides 2×/4×/8×/16× with explicit latency reporting.
The project currently has **no** oversampling infrastructure, so any use of it is
new latency in a synth that presently reports none — a real cost, not a free win.

### Independent Airwindows integrations

`airwin2rack` and `rackwindows` both demonstrate the same thing: Console is
integrated as a *pair of stages around a sum*, not as a single insert. Neither
treats the channel plugin as usable alone. That corroborates the reading above.

---

## 4. What a console profile actually consists of

Reduced from the research to the axes that can be independently varied:

| Axis | What it controls | Where it comes from |
|---|---|---|
| Transfer pair | odd-harmonic structure, saturation onset | the `sin`/`asin`-class function chosen |
| Curve blend | harmonic distribution between two curves | Console7's 80/20 idea |
| Headroom | where on the curve nominal level sits | pre-gain into the nonlinearity |
| Even-harmonic bias | 2nd harmonic — "warmth" | deliberate small asymmetry |
| HF behaviour | bandwidth, air | one-pole rolloff, level-dependent |
| LF behaviour | coupling-capacitor HP, transformer LF | HP corner + level-dependent trim |
| Slew | transient personality | arcsine-enhance / sine-cut on the difference |
| Bus drive | how hard the sum hits the curve | bus-side pre-gain |

**Noise is deliberately zero.** The brief is explicit, and the research does not
support it: console noise is a defect that engineers spent decades reducing, not
a character people are reaching for. A synthesiser that generates its own signal
has no reason to inherit a mic-preamp's noise floor.

---

## 5. Console archetypes

Generic archetypes informed by research into broad design families. **No claim of
exact hardware emulation is made anywhere in the code or UI.**

| Archetype | Informed by | Character target |
|---|---|---|
| `CLEAN` | modern large-format VCA desks | nearly transparent; the accumulation effect only |
| `BRITISH` | British discrete transformer-coupled designs | 2nd-harmonic bias, LF weight, softened top |
| `AMERICAN` | American discrete op-amp designs | harder knee, odd harmonics, forward mids, fast slew |
| `TRANSFORMER` | transformer-heavy designs | strongest even-harmonic content, level-dependent LF, restricted bandwidth |
| `MODERN` | later clean VCA large-format | tight, fast, minimal colour, most headroom |

---

## 6. The architectural debate

### Approach A — simple algorithmic model

`filter → nonlinear transfer → slew → filter`, one instance per stage.

- **For:** cheap, stable, trivially tunable.
- **Against:** it is a saturator per stage. Channel + bus is then genuinely twice
  the distortion, which the brief explicitly rejects. There is no mechanism by
  which character could accumulate *musically* — it accumulates additively.

### Approach B — hybrid, matched-pair distributed model

An analytically **invertible** transfer pair split across channel and bus, plus
non-inverted supporting stages (HF/LF behaviour, slew, even-harmonic bias) that
carry the profile's colour.

- **For:** single channel is provably transparent, so the character comes from
  summing — which is what the research says a console actually does. Cheap
  (two transcendentals per sample per stage). Every axis in §4 is independently
  tunable. Context differences fall out naturally: the channel runs the forward
  function, the bus runs the inverse.
- **Against:** the invertible core constrains the function choice to analytic
  pairs, and any asymmetry added for even harmonics breaks exact invertibility —
  so the transparency guarantee is exact only for the core and approximate once
  colour is added.

### Approach C — Wave Digital Filter model

Model a real channel-strip topology as a WDF with nonlinear elements.

- **For:** physically grounded, excellent nonlinear behaviour, good aliasing
  characteristics.
- **Against:** implicit solving per sample. Multiple nonlinearities remain an
  open research problem. Four channels plus three buses of implicit solving on
  top of 64 voices is not affordable, and the brief's own warning against
  overmodelling applies squarely.

### Decision

**Approach B**, judged on the brief's stated priority order:

1. **Musicality** — it is the only one of the three whose character emerges from
   accumulation, which is what the summing research identifies as the actual
   effect. A and C both produce per-stage distortion.
2. **Stability** — closed form, no implicit solving, bounded by construction.
3. **CPU** — two transcendentals per stage per sample against C's iterative
   solve.
4. **Alias suppression** — measured, not assumed; see the architecture document.
5. **Architecture** — channel/bus asymmetry is intrinsic rather than bolted on.
6. **Tunability** — every axis in §4 is a separate constant.

Approach C is the more sophisticated engineering and is rejected deliberately.
The brief asks for the best solution for *this instrument*, and a physically
exhaustive model of a circuit nobody will A/B against the real hardware is not
it.

### Recorded disagreement

Points where the candidates genuinely conflict, for the record:

- **A measures better in isolation.** A single channel through Approach A shows
  real THD; Approach B's single channel shows essentially none. By the usual
  "does it distort" test A wins and B looks broken. That test is the wrong one:
  B's THD appears when channels sum, which is the case that matters.
- **C is more rigorous and would probably sound better on a single loud
  channel.** It would also cost several times the CPU for a difference that
  arrives mostly at levels this instrument's gain structure never reaches, since
  the sources are already trimmed to −4 dB of headroom.
- **Even harmonics fight the invariant.** Getting 2nd-harmonic warmth requires
  asymmetry, and asymmetry means channel ∘ bus is no longer exactly identity.
  Resolution: keep the core pair exactly invertible and treat the asymmetry as
  colour applied outside it, scaled small. The transparency property is then
  exact for the mechanism that produces glue and approximate for the mechanism
  that produces warmth — which is the correct way round.

---

## 7. Relationship to VibeEngine

They model different things at different points and must not be merged.

`VibeEngine` is a **per-voice instrument-analog** model: oscillator drift, PSU
sag under load, per-voice component variation, waveform asymmetry. It runs
*inside* the voice, before the mixer, and its whole purpose is that voices differ
from each other.

`AnalogEngine` is a **signal-path console** model. It runs after the voices are
summed into source channels, and its whole purpose is that channels interact.

Signal order is therefore Vibe → AnalogEngine, and it is not a choice: Vibe is
upstream by construction. They are complementary — Vibe makes the *instrument*
imperfect, AnalogEngine makes the *desk* imperfect.

What is reused: VibeEngine's **conventions**, not its DSP. Specifically the
string-keyed `Tuning` struct, the `setTuningValue`/`getTuningValue` accessors,
the debug-console slider registry, and the discipline that tuning constants are
never serialised. What is deliberately not reused: any of its DSP, its per-voice
variation model, or its RNG — none of which describe a mixing desk.

---

## 8. Licensing

The Airwindows repository is MIT licensed. Its Console algorithms were read as an
engineering reference; the finding taken from them is the **architectural idea**
that a console is an invertible pair split across channel and bus.

No Airwindows source is copied into this project. The transfer functions,
profile set, filtering, slew model, context handling and tuning surface are
independently written. `docs/ANALOG_ENGINE_ARCHITECTURE.md` records which ideas
came from where, and `THIRD_PARTY_NOTICES.md` records the reference.
