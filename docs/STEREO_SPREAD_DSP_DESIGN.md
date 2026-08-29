# STEREO SPREAD — DSP Design

A mono-compatible stereo widener for the P(X3) synthesiser.

The requirement that shapes everything: **width must not come from destructive
phase cancellation.** A widener that sounds enormous in stereo and hollow in
mono has not widened anything — it has moved energy into the side channel and
arranged for it to disappear when summed.

---

## 1. Psychoacoustics

### Interaural time difference (ITD)

Below roughly 1.5 kHz the auditory system localises using the **phase**
difference between the ears. Above it, a wavelength is shorter than the head, so
phase is ambiguous and ITD ceases to be usable — the system switches to the
**envelope** of the signal. This is the single most important fact for a
widener: **a timing offset is a localisation cue at low frequencies and a comb
filter at high ones.**

### Interaural level difference (ILD)

Level differences dominate above ~1.5 kHz, where the head shadows the far ear.
ILD is perfectly mono-compatible: summing two differently-scaled copies changes
level, never spectrum.

### Precedence (Haas)

Two arrivals within ~1–35 ms fuse into one image located at the earlier one. A
delay in that range makes the image *move*, it does not make it *wide* — and it
combs on sum. Fixed short delays are therefore explicitly not the mechanism
here.

### Correlation

Correlation says how similar the channels are, and nothing about whether the
result is musical. Correlation of −1 is a signal and its own inverse: infinitely
"wide" and completely silent in mono. A widener whose normal range approaches −1
is broken, whatever its meter says.

### Mid/Side

```
M = (L + R)/2      S = (L − R)/2
L = M + S          R = M − S
```

Raising S widens. **On its own it is not a widener**: for a mono source S is
zero, and no gain applied to zero produces anything. M/S is how width is
*applied*; something has to *create* the side content first.

---

## 2. Why the obvious approaches fail

| Approach | Failure |
|---|---|
| `L = x, R = delay(x, 12 ms)` | Haas. The image shifts left rather than widening, and `L+R` combs at 42 Hz intervals. |
| `L = M + gS, R = M − gS`, g large | Nothing to amplify on mono input. On stereo input it exaggerates whatever side already exists and collapses to the same mono. |
| `L = x, R = −x` | Correlation −1, mono silence. |
| Micro-pitch ±5 cents | This is a chorus. The project already has one. |
| A fixed allpass on one channel | A static phase difference is a fixed comb on sum. |

---

## 3. Architecture

Three mechanisms, each doing the job it is actually suited to, split by
frequency because that is how hearing is split.

```
                       INPUT
                         │
                         ▼
              ┌─────────────────────┐
              │ Linkwitz-Riley       │
              │ crossovers: 3 bands  │
              └──┬────────┬───────┬──┘
                 │ LOW    │ MID   │ HIGH
                 ▼        ▼       ▼
            ┌────────┐ ┌──────┐ ┌──────────┐
            │ MONO   │ │ ALL  │ │ ALLPASS  │
            │ anchor │ │ PASS │ │ + ILD    │
            │        │ │ decor│ │ decor    │
            └───┬────┘ └──┬───┘ └────┬─────┘
                │         │          │
                └────┬────┴──────────┘
                     ▼
              M/S width shaping
              (per band, from AMOUNT)
                     │
                     ▼
              correlation guard
                     │
                     ▼
                  MIX / OUTPUT
```

### 3.1 Crossover — Linkwitz-Riley

Two cascaded Butterworth sections per split, giving LR4. LR4's two outputs sum
**flat in magnitude** — they are in phase at the crossover and each is −6 dB
there. That property is the whole reason to use it here: a widener that splits
bands must be able to put them back together without a hole, or the "widening"
includes an EQ curve.

Rejected alternatives: a plain 1-pole split (audible tilt), and linear phase
(latency, and pre-ringing on the transients a synth is full of).

### 3.2 LOW — mono anchor

Below the low crossover the band is summed to mono and scaled. There is no
psychoacoustic width available down there — the wavelength is longer than any
room's stereo geometry — and side energy at low frequencies is exactly what
destroys mono compatibility and headroom. A bass patch keeps its weight because
its fundamental is never widened at all.

### 3.3 MID — allpass decorrelation

The mechanism that produces genuine width without a comb.

A cascade of **first-order allpass sections with different coefficients on the
two channels** changes the *phase* relationship between them while leaving each
channel's *magnitude* response flat. The result is two channels that differ
without either being filtered.

Two properties make it safe where a delay is not:

1. **Each channel's magnitude response is unity**, so nothing is coloured.
2. **The phase difference varies smoothly with frequency** rather than being the
   linear ramp a delay produces. A delay's constant group delay is what puts
   comb nulls at regular intervals; a dispersive network spreads the
   cancellation out so that summing produces a gentle, non-periodic ripple
   instead of deep regular notches.

The coefficients are chosen so the two channels' phase responses diverge
gradually across the mid band. Four sections per channel: enough divergence to
decorrelate, few enough that the group delay stays small.

**The allpass network is where the width comes from, and it is applied to a
mono source as readily as a stereo one** — which is the requirement M/S alone
cannot meet.

### 3.4 HIGH — allpass plus level

Above the high crossover, ITD is no longer a usable cue, so the band is widened
with a **level** difference — complementary gains that trade energy between the
channels — modulated very slowly and correlated between bands, plus a lighter
allpass pass. ILD is mono-safe by construction: summing two differently-scaled
copies changes level, never spectrum.

### 3.5 Correlation guard

After the bands recombine, the interchannel correlation is measured
continuously. As it approaches the floor set by MONO SAFE, the side gain is
smoothly reduced. Not a limiter on the sound — a limiter on the *mechanism*, so
extreme settings become very wide rather than becoming a phase trick.

Its action is smoothed over hundreds of milliseconds, so it is a slow safety
rather than an audible pumping.

---

## 4. Modes

| Mode | Strategy |
|---|---|
| **CLASSIC** | balanced: mid allpass, high ILD, mono lows |
| **WIDE** | more side gain in mid and high, lower low crossover |
| **DEEP** | more allpass sections and slower modulation; depth rather than width |
| **MONO SAFE** | correlation floor raised near zero, side gain constrained, low crossover raised — deliberately conservative, and not simply a lower AMOUNT |

---

## 5. Parameters

| Parameter | Range | Default | Purpose |
|---|---|---|---|
| `spreadEnabled` | bool | true | bypass |
| `spreadAmount` | 0…1 | **0.0** | macro: side gain, decorrelation depth, band width |
| `spreadMode` | CLASSIC / WIDE / DEEP / MONO SAFE | CLASSIC | strategy |
| `spreadWidth` | 0…1 | 0.6 | overall stereo expansion |
| `spreadDepth` | 0…1 | 0.4 | decorrelation depth (allpass count and spread) |
| `spreadCenter` | 0…1 | 0.7 | how strongly mid is anchored |
| `spreadLowWidth` | 0…1 | 0.0 | width permitted below the low crossover |
| `spreadHighWidth` | 0…1 | 0.8 | width in the top band |
| `spreadLowFreq` | 0…1 | 0.55 | low crossover, ~60–400 Hz. Defaulted so a synth bass fundamental sits inside the mono band, not near its edge |
| `spreadHighFreq` | 0…1 | 0.5 | high crossover, ~1–8 kHz |
| `spreadTone` | −1…+1 | 0.0 | tilt on the side signal only |
| `spreadMix` | 0…1 | 1.0 | final dry/wet |

Latency: **none.** All filters are minimum-phase and there is no delay line.

## 6. CPU

Four LR4 sections and eight first-order allpasses per channel, plus a
correlation follower. Comparable to two biquad filters per voice — cheap.
All state is fixed-size; nothing is allocated after `prepare()`.

## 7. Testing strategy

Silence, impulse and sines from 30 Hz to 15 kHz; mono, stereo, left-only,
right-only and identical-stereo input; **mono collapse at five AMOUNT settings,
measuring RMS retention, low-frequency retention and spectral change**;
correlation across the width range; per-band behaviour; automation sweeps;
extreme settings; state and preset round-trip; FX ordering.

The measurements that matter, because they are the brief:

1. **Mono collapse retains level.** `L+R` must not lose more than ~1.5 dB
   against the dry sum at any normal setting. A phase-trick widener loses far
   more.
2. **Low frequencies stay centred.** Side energy below the low crossover must
   stay near zero.
3. **Correlation stays above the floor** across the whole AMOUNT range.
4. **A mono input actually widens** — side energy must be created where there
   was none, which is what M/S alone cannot do.
