# P(X3) v0.3.0

Released from the `main` branch, 64 commits after v0.2.1. macOS (Apple Silicon)
— AU, VST3 and Standalone.

This is the largest release so far. Four new effects, a new analog console
stage that runs across the whole mixer, a rebuilt FX panel, a refreshed preset
library, and a long list of DSP corrections found by measuring rather than by
listening and guessing.

---

## New

### Four new effects

The FX chain goes from four effects to eight. All eight can be reordered, and
every one of them is bypassable independently.

- **DOOM** — a two-channel ambient processor. Three "listening" modes (BURST,
  RADIO, MASK) and three "wet" modes (SOUP, RELAY, FLIP), a micro-looper that
  is always listening so you can capture a phrase you have already played, and
  a CLOCK that retunes the whole engine.
- **LUCY** — a spectral degradation instrument. A perceptual masking coder,
  packet loss modelled as bursts rather than as dice rolls, spectral freeze
  with a slushy mode that keeps updating, jitter, a filter, a small reverb, a
  gate and a lookahead limiter.
- **CHORUS** — nine modes built around an anti-phase modulated delay pair, so
  it widens without shifting the average pitch and collapses exactly to mono.
- **STEREO SPREAD** — four modes of mono-compatible widening, using allpass
  decorrelation rather than gain tricks, over a Linkwitz-Riley crossover.

Each has its own design document under `docs/`.

### AnalogEngine

A console-character stage distributed across the channel strips, the dry bus,
the FX bus and the master — not a saturator bolted on the end.

The channel runs a transfer function and the bus runs its exact inverse, so a
single channel through the pair is mathematically transparent (measured at
0.000% THD) and the nonlinearity exists only in the **summing**. That is what
"glue" actually is, and it means the effect grows with how much is playing
rather than being applied identically to everything.

Five profiles — CLEAN, BRITISH, AMERICAN, TRANSFORMER, MODERN — each with its
own harmonic balance, bandwidth, coupling and level dependence. All five are
level-matched to 0.00 dB against broadband material, so switching them is a
comparison of character and not of loudness.

Off by default. It exposes exactly two controls: on/off and the profile. The
fourteen tuning constants behind it are internal and are deliberately absent
from presets, DAW state and UIConfig.

### FX panel

A drag-to-reorder signal-flow strip sits above the cards and is the only place
the chain order can be changed. Below it, the cards wrap in a four-column grid
that scrolls. Every card carries its effect's own colour, from the strip node
down to the knob rings.

### Warning when nothing can sound

With the sub oscillator and all three oscillators bypassed the instrument
cannot make a sound. The keyboard now says so: it greys out, stops animating,
stops responding to the mouse, and shows a message. It clears the moment any
source is engaged.

---

## Improved

### Oscillators

Every mode was measured against a pure sine, and four were within 10 dB of one.

- **FORMANT was not formant synthesis.** It weighted the harmonics of the note,
  which pins the spectral peak to the pitch — so an "ah" stopped being an "ah"
  as you played up the keyboard. It is now three parallel resonators at the
  measured F1/F2/F3 of the five cardinal vowels, excited by a band-limited
  impulse train. The formants now hold position across four octaves.
  Overtones: **−12.8 dB → +25 dB**.
- **ORGAN sounded like a sine**, because it used plain integer harmonics 1–8
  and then crushed them. It now has the nine Hammond drawbar footages,
  including the 16′ sub and the 5⅓′ quint — neither of which is a harmonic of
  the note, and both of which are most of why an organ sounds like one.
  Overtones: **−10.8 dB → +0.3 dB**, and it now gets richer as the macro opens
  instead of thinner.
- **ADDITIVE and ISAAC** ran a rolloff that put the 8th harmonic 45 dB down at
  full macro, turning them into sines. Range compressed so the knob still runs
  bright to mellow without running to nothing.
- Harmonic modes are normalised by root-sum-square rather than by the sum of
  the partial magnitudes, which was making richer registrations quieter and
  flatter than sparse ones.

### Filters

- **The 24 dB modes squared their resonance.** Two biquads in series were both
  built at the user's Q, so at Q 10 they peaked at **+40 dB** where the 12 dB
  modes reached +20 — the resonance control meant something different depending
  on a menu. The pair is split the way a 4th-order design should be. Both
  slopes now peak identically at every Q.
- **NOTCH never nulled.** It was a band-pass subtracted from the input with a
  0.92 factor, reaching −21.9 dB at best. It is a real notch now: **−107 dB**.
- **The response graph was a drawing, not a measurement.** It used invented
  curve shapes and a gaussian bump for resonance, and drew a flat line for
  notch and all-pass. The audio path and the display now build coefficients
  through one shared function, so the curve on screen is the filter you hear —
  verified to 0.00 dB across every mode.
- The graph gained a logarithmic 20 Hz–20 kHz axis with labelled decades, a
  0 dB reference line, a filled skirt, and a cutoff marker with its frequency.

### Delay

- **TAPE and MODULATED clicked when the delay time moved.** Those two slide the
  read pointer instead of crossfading, and an end-to-end time change asked the
  pointer to move ~224 samples per sample — so it overtook the write head and
  read samples that had not been written yet. The read length is slew-limited
  now, which cannot overtake and still glides like a tape machine changing
  speed.
- **The mix knob was doubling as a character control.** TAPE scaled its wow and
  flutter depth, and MODULATED its modulation depth, by the wet amount — so the
  modulation sidebands grew deepest exactly where the wet signal was loudest.
  Non-harmonic content at full amount: TAPE −18.6 → −25.0 dB, MODULATED
  −14.0 → −33.8 dB.
- **The tape head bump was a broadband cut**, not a bump. It attenuated
  everything except 92 Hz, on every pass through the feedback loop, so after a
  few repeats the tail was nothing but a low resonance. Halved, so the tail
  keeps its material.

### VibeEngine

Hiss now scales with the amount knob instead of sitting on a fixed floor. It
used to jump 86 dB the instant the stage engaged, landing within 14 dB of full
amount — which also made all six type profiles measure identically. The
profiles now span 32 dB and Clean is genuinely clean.

### Presets

The twenty factory presets were rebuilt. Every effect defaults to enabled and a
preset only overrides what it lists, so all twenty had been shipping with all
eight effects running behind them. Each now names its effects explicitly:
seventeen distinct combinations across twenty presets, one to three effects
each, and every effect is the point of at least one preset.

**INIT is no longer a preset.** It was written to disk as a file in a factory
category of its own, which put the default state in the browser alongside
sounds somebody had designed. It is built in memory now, appears as `- INIT -`
at the top of the browser, and any INIT file left by an earlier version is
removed on startup.

The preset tab shows the loaded preset's category and author, and that identity
survives closing and reopening the plugin window.

---

## Fixed

- **A crash on quit.** The editor tore down panels before releasing the
  parameter attachments pointing into them.
- **A fade to silence.** BRITISH and AMERICAN AnalogEngine profiles faded to
  nothing after about six seconds of dense material. The slew stage
  reconstructed its output by integrating differences, which is a random walk
  on noise-like signals; it reached its rail, the transfer clamp flattened the
  signal, and the DC blocker removed what was left. Every test had used sines,
  where the drift cancels each cycle.
- **All eight FX strip nodes drew the same blue.** They read their colour when
  built, which happens before any config exists, and nothing rebuilt them when
  it arrived — so they stayed grey-blue until an unrelated action refreshed
  them, at which point they all snapped to the right colours at once.
- **The oscillator mode visual jumped** every couple of seconds on SUPER SAW,
  WAVETABLE, FORMANT, FM, KARPLUS, DIGITAL and PHYSICAL. Its phase was wrapped
  at 2π, which is only invisible for shapes built from whole multiples of it.
- **`Resonance` read `Resonan…`.** Chip labels were ellipsised rather than
  fitted; thirteen labels across the plugin were affected, RESONANCE by exactly
  one pixel and AUTO GAIN by twelve.
- The preset browser is now genuinely modal — it used to dim the interface
  while every knob underneath stayed live, so a click that missed the sheet
  edited the patch you were browsing away from.

---

## Performance

Maximum polyphony is now **budgeted against the sample rate** rather than fixed
at 64 voices: 48 at 44.1 and 48 kHz, 26 at 88.2, 24 at 96, 16 at 192.

A voice costs the same work whatever the rate while the time available to
compute it halves as the rate doubles, and 64 held voices with every effect
enabled took 108.7% of the real-time budget at 48 kHz and 211.2% at 96 kHz —
the whole block late, rather than the quietest note being dropped. Voices over
budget now fade out the way release tails already did.

Measured at 128-sample blocks, all effects plus the analog console and vibe:

| | 48 kHz | 96 kHz |
|---|---|---|
| before | 108.7% | 211.2% |
| after | **83.4%** | **90.3%** |

Idle is 0.5–1.9% of the budget; sixteen voices in a typical patch is 5–11%.
Memory is 14.9 MB per instance and does not grow across a 20,000-block soak.

---

## Under the hood

- The audio thread performs **zero heap allocations**, verified with a replaced
  global `operator new` across eight scenarios up to 48 voices with the full FX
  chain and the analog console.
- **639 automated assertions** across 25 suites, passing under
  AddressSanitizer and UndefinedBehaviorSanitizer with no AddressSanitizer
  reports.
- Builds clean from scratch with `-Wall -Wextra -Wpedantic -Wshadow
  -Wconversion -Wsign-conversion` and **zero warnings**.
- 259 parameters round-trip exactly; malformed, truncated and hostile state
  payloads are rejected with the parameter set intact.
- A full pre-release audit is recorded in `docs/RELEASE_AUDIT.md`.

---

## Known limitations

- **Apple Silicon only.** There is no Intel build; the release script requires
  arm64 and rejects x86_64.
- **Notarization is unverified** for this release. Gatekeeper may need to be
  cleared manually on first run.
- **SAW and SQUARE alias** at high pitch. There is no PolyBLEP or oversampling
  in the oscillators yet.
- **GRANULAR delay is grainy by design.** Its amount is a macro over grain
  size, spread, jitter and gain, and grain windowing spreads the spectrum — it
  is the algorithm, not a defect.
- There is no continuous-integration pipeline; all verification is local.
