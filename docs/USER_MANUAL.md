# P(X3) — User Manual

For PX3 Synth v0.8.3.

---

## Contents

**Getting started**
[Welcome](#welcome) · [Quick start](#quick-start) · [The interface](#the-interface) · [Signal flow](#signal-flow)

**The pages**
[VOICE: oscillators](#voice--oscillators) · [VOICE: filters](#voice--filters) · [VOICE: AMP ENV](#voice--amp-env) · [VOICE: LFOs and envelopes](#voice--lfos-and-envelopes) · [MOD — the modulation matrix](#mod--the-modulation-matrix) · [FX](#fx--effects) · [MIX](#mix--mixer) · [SETTINGS](#settings)

**Performance**
[Macros](#macros) · [MIDI Learn](#midi-learn) · [MIDI and Macros together](#midi-and-macros-together) · [Playing](#playing) · [Presets](#presets)

**Reference**
[Sound design walkthroughs](#sound-design-walkthroughs) · [Interaction reference](#interaction-reference) · [Visual indicators](#visual-indicators) · [Standalone and plugin](#standalone-and-plugin) · [Troubleshooting](#troubleshooting) · [Glossary](#glossary)

---

# Welcome

P(X3) is a 64-voice polyphonic synthesiser. Every note you play is given its
own voice, and every voice contains four sound sources, two filters, an
amplitude envelope, and modulation of its own. Those voices go through VIBE,
are mixed, sent through six effects in an order you choose, then through LUCY
and SPREAD on the master, and delivered to the output.

Four things shape the way you work with it.

**Nineteen oscillator modes, not nineteen waveforms.** Alongside the familiar
sine, saw, square and triangle there are FM, hard sync, additive, formant,
a modal resonator, wavetable and several of our own. Each brings its own
controls, so the same three PARAM knobs mean something different in every mode.

**Envelopes you draw.** The amplitude and modulation envelopes are curves you
edit directly — bend a stage, add a point, watch the fill track a note as it
plays — while the familiar four knobs sit beneath the graph, showing and setting
the same shape.

**A patch bay.** Every LFO, envelope and Macro has a jack. Drag from a jack onto
any knob and that knob is modulated. Every connection is listed, and can be
edited, on the MOD page.

**A performance layer that reaches everywhere.** Six Macros sit down the left of
every page and can move any number of parameters at once, anywhere in the
instrument. A single hardware knob can drive a Macro, and that Macro can
transform the whole patch.

It runs as a standalone application and as an AU or VST3 plugin on Apple Silicon
Macs. Eight of its effects also ship as plug-ins of their own — PX3 Delay, Mood,
Chorus, Spread, Reverb, Doom, Lucy and Vibe — with the same controls as their
cards in the Synth.

---

# Quick start

This takes about two minutes and gets you from silence to a sound you have
shaped yourself.

### 1. Make a sound

Open **VOICE**. A new patch (INIT) starts with **OSC 1** switched on, playing a
sine. Play a note on your keyboard, or click the on-screen keyboard along the
bottom of the window.

> **If you hear nothing:** when every source is switched off the keyboard greys
> out and says *"Please engage an oscillator!"*. Switch one on with the power
> button in the corner of its card.

### 2. Choose a character

Set OSC 1's **MODE** menu to `SAW` for a bright, buzzy tone, or `FM` for
something metallic. In FM the **PARAM A** and **PARAM B** knobs become RATIO and
INDEX — turn INDEX up and listen to the harmonics build.

### 3. Shape the tone

Both filters are switched on in INIT but set to `AllPass`, which leaves the tone
alone. Set **FILTER 1**'s **TYPE** to `LP24` and pull **CUTOFF** down. The sound
darkens as the filter removes the upper harmonics. Add a little **RESONANCE** to
emphasise the frequencies right at the cutoff point.

### 4. Shape the swell

On **AMP ENV**, drag the **attack** handle to the right and the note fades in
instead of starting instantly. Drag the **release** handle to the right and it
rings on after you let go. The four knobs beneath the graph follow as you drag,
and turning them moves the graph.

### 5. Make it move

In the **LFO 1–4** panel below the oscillators, LFO 1 is showing. Drag the round
jack on its **LFO 1** tab onto FILTER 1's CUTOFF knob and let go. The cutoff now
sweeps with the LFO; a coloured ring on the knob shows how far.

### 6. Add space

Open **FX** and make sure **REVERB** is switched on. Set its **MODE** to `HALL`
and bring **MIX** up. Bring **DELAY**'s **AMOUNT** up too, then drag either card
by the ⋮⋮ handle on its tab to change which comes first.

### 7. Keep it

Use **MENU** in the top bar and choose **Save As**. The `<` and `>` buttons step
through the library; click the preset name to open the browser.

---

# The interface

Everything happens in one window. Its default size is 1488 × 884, and it can be
resized from 1100 × 700 up to 2400 × 1400. No feature opens a window of its own.

Four buttons across the top switch the main area between pages:

| Page | Contents |
| --- | --- |
| **VOICE** | SUB OSC and OSC 1–3, FILTER 1 and FILTER 2, and AMP ENV side by side; beneath them two tabbed panels, LFO 1–4 and ENV 1–4 |
| **MOD** | The modulation matrix: every source's jack, every route, and a searchable list of destinations |
| **FX** | The effect cards in three sections - INSTRUMENT, SEND FX, MASTER - laid out in the order the signal meets them; drag a send card to reorder the chain |
| **MIX** | Channel strips for SUB, OSC 1–3, the DRY bus and the FX return, with the EQ and COMP inserts |

The rest of the top bar, left to right after the page buttons:

- **`<` and `>`** step to the previous or next preset.
- **The preset name.** Click it to open the preset browser.
- **MENU** — Save, Save As, favourites, import and export, Settings, and the
  installed version.
- **The gear** opens [SETTINGS](#settings), a full-width page rather than one of
  the four.
- **The MASTER knob**, at the far right, sets the output level.

Two things stay on screen whatever page you are viewing.

**The Macro strip**, down the left edge — six knobs, M1 to M6, each with a
**Depth** button beneath it. They are the same six everywhere. See
[Macros](#macros).

**The keyboard**, across the bottom, with the PITCH and MOD wheels to its left.
It also carries messages when the instrument has something to tell you.

---

# Signal flow

```
            MIDI / on-screen keyboard
                       │
        ┌──────────────┴───────────────┐
        │       VOICE (per note)       │
        │   SUB   OSC1   OSC2   OSC3   │
        │   ANALOG (per-voice drift)   │
        │   FILTER 1 / FILTER 2        │
        │   (SERIES or PARALLEL)       │
        │   AMP ENV                    │
        └──────────────┬───────────────┘
                       │  one channel per source
          ┌────────────┴─────────────┐
          │                          │
     level, pan,                    SEND
     mute, solo                      │
          │                          │
     ┌────▼──────────────────────────▼────┐
     │  VIBE  (one pedal on dry and send) │
     └────┬──────────────────────────┬────┘
          │                          │
          │                ┌─────────▼─────────┐
       DRY BUS             │     FX CHAIN      │
     (EQ, COMP)            │   (your order)    │
          │                └─────────┬─────────┘
          │                      FX RETURN
          │                     (EQ, COMP)
          └────────────┬─────────────┘
                     MASTER
                       │
          CONSOLE ENGINE → LUCY → SPREAD → OUTPUT
```

Each note gets a voice of its own. Inside it, the four sources are coloured by
ANALOG, filtered, and shaped by the amplitude envelope. Each source then arrives
at its own mixer channel, where level, pan, mute and solo apply, and goes on to
the **dry bus**. VIBE works on the instrument at this point, ahead of the split:
the dry signal and everything sent to the effects go through the same pedal, so
the effects hear the vibe too.

Each source channel also has a **send** into the FX bus. The effects process only
what is sent to them, and their output returns on its own channel with its own
level and pan. The dry bus and the FX return meet at the master, which passes
through the console engine, LUCY and SPREAD on its way out. LUCY and SPREAD work
on the whole instrument, so they do not depend on the sends.

Because sends are independent, you can push one oscillator deep into the effects
while another stays completely dry.

> **Note:** LFOs, envelopes and Macros are modulation sources. They move other
> controls; they are never mixed into the audio.

---

# VOICE — Oscillators

The first row of the VOICE page holds SUB OSC and three oscillator cards. Each
card has a power button in its corner. Switching a card off removes it from the
voice entirely, including any filter tail it was ringing, so it stops
immediately rather than fading.

Each card shows its waveform in a display at the bottom. With
[animations](#enable-animations) on, the displays move.

## MODE

Selects the oscillator type. This is the most consequential choice on the card —
it changes not only the waveform but what the three PARAM knobs do.

| | | | |
|---|---|---|---|
| SINE | SAW | SQUARE | TRIANGLE |
| NOISE | PINK NOISE | SUPER SAW | PWM |
| WAVETABLE | ADDITIVE | FORMANT | FM |
| HARD SYNC | ORGAN | DIGITAL | PHYSICAL |
| ROB | ISAAC | PX3 | |

## PARAM A, B and C

**What they do:** Up to three knobs whose function depends on the selected mode.
Their labels change with the mode, so you can always see what you are holding,
and a mode with fewer than three hides the ones it does not use.

**Use them for:** The character of the raw tone, before any filtering. In most
modes these are the difference between a usable sound and an interesting one.

| Mode | A | B | C |
|---|---|---|---|
| SINE / SAW / SQUARE / TRIANGLE | — | — | — |
| NOISE / PINK NOISE | COLOR | — | — |
| SUPER SAW | DETUNE | — | — |
| PWM | WIDTH | — | — |
| WAVETABLE | — (see [WAVETABLE mode](#wavetable-mode)) | — | — |
| ADDITIVE | TILT | ODD/EVEN | ROLL |
| FORMANT | MORPH | SHIFT | — |
| FM | RATIO | INDEX | — |
| HARD SYNC | SYNC | DRIVE | — |
| ORGAN | TONE | CLICK | — |
| DIGITAL | BITS | RATE | FOLD |
| PHYSICAL | DECAY | MATERIAL | — |
| ROB | TRANS | BODY | CHAOS |
| ISAAC | TILT | ODD/EVEN | STRETCH |
| PX3 | MORPH | CHAR | MOVE |

Several are worth knowing in more detail.

**FM** — RATIO sets the relationship between carrier and modulator. Whole-number
ratios stay harmonic and musical; values between them turn metallic and
bell-like. INDEX sets how much modulation is applied, heard as brightness and
harmonic density.

**SUPER SAW** — DETUNE sets how far apart the stacked saws sit, along with their
drift. Low settings give one fat saw; high settings give the classic wide sound.

**FORMANT** — a **VOWEL** menu appears, selecting the A/E/I/O/U profile. MORPH
glides onward from that vowel through the others in turn: each quarter of the
knob is one more vowel along, and at the top you are one short of back where you
started. SHIFT moves every formant together, from a large voice to a small one.
Modulate MORPH for a talking sound.

**ADDITIVE** — TILT sets how fast the harmonics fall away, ODD/EVEN leans the
balance toward the odd or the even harmonics, and ROLL slides a window along the
series. At the middle every harmonic plays; turn it down and the upper harmonics
roll away, turn it up and the lower ones do, thinning the sound from underneath.
The level stays the same all the way round.

**ISAAC** — ADDITIVE's harmonics with a shimmer an octave below. TILT and
ODD/EVEN work as in ADDITIVE; STRETCH pulls the upper partials sharp of the
harmonic series, towards a bell, and deepens the shimmer.

**HARD SYNC** — SYNC sets how far the synced oscillator runs ahead of the note
before it is restarted; DRIVE saturates the result.

**ORGAN** — nine drawbars, including the 16' an octave below and the 5 1/3' a
fifth above. TONE moves from a mellow flute registration to full drawbars; CLICK
adds the key click at the start of each note.

**DIGITAL** — a bitcrushed, folded sine. BITS sets the resolution (2 to 16
bits), RATE divides the sample rate down (1x to 64x, smoothly), and FOLD
wavefolds the sine before it is crushed, adding harmonics. All three are
independent.

**PHYSICAL** — a struck, ringing tone built from four inharmonic modes. DECAY is
how long the strike rings before it settles to a held level. MATERIAL runs from
wood through glass to metal: darker with fast-dying overtones at the left, bright
and evenly ringing at the right. It changes the tone only - the pitch stays where
you play it.

**ROB** — TRANS sets the attack transient. BODY thickens the tone with more and
louder overtones and a harder drive, without moving its pitch. CHAOS makes the
overtones flicker in random, grainy steps - faster as you turn it up. At zero
CHAOS there is none at all.

**PX3** — three engines in one. MORPH balances an FM tone against ISAAC-style
partials, CHAR pushes the whole voice harder (a driven saw and more FM), and MOVE
sets a slow movement of both.

> **The mod wheel** adds vibrato to every mode, and in PWM it also moves the
> pulse width. It does not change any oscillator's level.

## WAVETABLE mode

Selecting WAVETABLE turns the card's display into a three-dimensional view of
the table, each frame drawn as a line with the current position picked out, and
brings up two controls:

- **TABLE** — which table plays: one of the eight factory tables, or one you
  imported. The menu's **Import WAV / AIFF / image...** item turns an audio file
  or an image into a table; imported tables are listed under IMPORTED and can be
  removed from the same menu.
- **POSITION** — moves through the table. It is a modulation destination, so an
  LFO or an envelope can sweep the table while a note is held — the most
  characteristic wavetable sound.

## Tuning

Every oscillator, the sub included, has the same three static tuning knobs:
**OCT**, **SEMI** and **CENT**. Anything that moves pitch while you play (pitch
bend, vibrato, SLOP, ANALOG drift, or modulation routed to Pitch Mod) is added on
top of them and never changes what they are set to.

### OCT

**What it does:** Transposes the oscillator in whole octaves, from −2 to +2
(shown as `-1 oct`, `+2 oct`).

**Use it for:** Weight and register, such as one oscillator an octave down under
two at pitch.

### SEMI

**What it does:** Transposes in semitones, from −12 to +12 (shown as `+7 st`).

**Use it for:** Intervals — a fifth (+7) or a fourth (+5) above the other
oscillators for organ-like or power-chord stacks.

### CENT

**What it does:** Detunes the oscillator in cents, from −24 to +24, one cent at
a time (shown as `+7 ct`).

**Sound:** A few cents between two oscillators gives a slow beating that
thickens the sound. Nearer the ends of the range it sounds deliberately out of
tune.

**Use it for:** Width and thickness. Try +7 ct on OSC 2 against OSC 1 left at
zero.

### SLOP

On OSC 1–3 only.

**What it does:** Lets the oscillator drift on its own, slowly and randomly —
up to ±12 cents at full, wandering to a new point every second or so. Every
note, and every oscillator within a note, drifts independently.

**Sound:** The gentle instability of analogue oscillators. A little (10–30%)
makes stacked oscillators and chords breathe; a lot sounds like an old,
warming-up synth.

**Use it for:** Taking the "too perfect" edge off a pad or a unison stack.

### Pitch Mod

**What it does:** A modulation destination worth up to two octaves either way.
It has no knob and holds no setting of its own. Patch a source to **Osc 1 Pitch
Mod** (or Osc 2, Osc 3, Sub Osc) from the destination list on the
[MOD page](#mod--the-modulation-matrix), and only that modulation moves the pitch.

**Sound:** At 100% an LFO swings the oscillator a full two octaves each way, so
musical vibrato lives at small amounts. About 2% is ±half a semitone. An
envelope at +50% gives a one-octave blip at the start of a note.

**Why a separate destination:** OCT and SEMI step, and CENT spans only a quarter
of a semitone each way, so none of them makes a useful pitch sweep. Pitch Mod is
continuous and wide.

> **Note:** Oscillator levels are not on these cards. Balance between sources is
> set in [MIX](#mix--mixer), so every level in the instrument lives in one place.

## SUB OSC

A simple, solid voice beneath the others. Off in a new patch.

| Control | Function |
| --- | --- |
| **WAVE** | SINE, SQUARE (the default) or SAW |
| **OCT** | −2 to +2 octaves; starts at −1 |
| **SEMI** | −12 to +12 semitones |
| **CENT** | −24 to +24 cents |

The tuning knobs work exactly as they do on the oscillators. Sub Osc Pitch Mod is
in the MOD page's destination list.

**Use it for:** Weight under a thin lead, or the fundamental beneath a bass patch
whose main oscillator is doing something more complicated. A sine sub two octaves
down adds body without adding harmonics that fight the filter.

---

# VOICE — Filters

Two filters per voice, FILTER 1 and FILTER 2, beside the oscillators. Each has
its own power button, and each card shows its response in a display at the
bottom. A filter that is switched off passes its input straight through.

In a new patch both filters are on and set to `AllPass`, so they do not colour
the sound until you choose a type.

### SERIES / PARALLEL and BALANCE

A strip beneath the two filters sets how they connect.

| Routing | What it does |
| --- | --- |
| **SERIES** (default) | Filter 1 feeds Filter 2 — the second processes the output of the first. |
| **PARALLEL** | Both filters receive the same signal, and their outputs are blended by **BALANCE**. |

**BALANCE** (the **F1 — F2** slider) works in PARALLEL only, and is greyed out in
SERIES. Fully left is Filter 1 alone, fully right is Filter 2 alone, and the
middle is an even blend. The blend is level-matched: two identical filters in
parallel are exactly as loud as one.

Switching routing, or moving BALANCE, while notes sound is smooth — no click.

> **Tip:** In PARALLEL, a low-pass and a high-pass with a gap between their
> cutoffs keep the lows and the highs and remove the middle — a shape neither
> filter can make alone, and one SERIES cannot make either.

### CUTOFF

**What it does:** Sets the frequency at which the filter starts to act, from
80 Hz to 18 kHz.

**Sound:** On a low-pass filter, lower settings remove more of the upper
harmonics and the sound darkens; higher settings let more through and the sound
opens up.

**Use it for:** The most important tone control in subtractive synthesis. Patch
an envelope to it for a filter sweep, or assign it to a Macro for a performance
control.

### RESONANCE

**What it does:** Emphasises the frequencies immediately around the cutoff point.

**Sound:** Low settings are neutral. As you raise it, a peak forms at the cutoff
frequency and the filter takes on a vocal, whistling character. Combined with a
moving cutoff it produces the classic sweep. The top of the knob whistles hard;
in the Ladder, Curtis and ARP types it goes into self-oscillation — the filter
sings a pure tone at the cutoff on its own — and stays under control.

### KEY TRK and KEY

**What they do:** Make the cutoff follow the keyboard. KEY TRK runs from −100% to
+200%. At 100% the cutoff moves up an octave for every octave you play, so the
filter keeps the same tone across the keyboard; at 0% it ignores the keyboard;
negative values close it as you play higher. KEY sets the note where tracking has
no effect (C4 by default).

**Use it for:** Basses and leads that should stay equally bright high and low,
and a self-oscillating filter you can play in tune (100% tracking).

### TYPE

| Type | What it does |
| --- | --- |
| **LP12** | Low pass, gentle. Removes highs above the cutoff. |
| **LP24** | Low pass, steep — two stages. Darker and more decisive. |
| **HP12** | High pass, gentle. Removes lows below the cutoff. |
| **HP24** | High pass, steep. |
| **BandPass** | Keeps a band around the cutoff, removing above and below. |
| **Notch** | Removes a band around the cutoff, keeping the rest. |
| **AllPass** | Passes everything, altering phase. In PARALLEL against another filter it makes phase-cancellation notches. |
| **Comb** | A tuned resonator: metallic, string- and pipe-like tones. See below. |
| **SVF12 / SVF24** | Clean, smooth state-variable low pass. |
| **Ladder12 / Ladder24** | A transistor-ladder low pass: round, bass-thinning as resonance rises, self-oscillates at the top. |
| **Curtis24** | A four-pole OTA cascade in the spirit of the CEM3320 chips: smooth, keeps its low end at high resonance. |
| **ARP12** | An aggressive two-pole low pass inspired by vintage ARP filters: bright, biting resonance that screams at the top. |

**Use them for:** LP24 for basses and anything that should sit low in a mix. HP12
to thin a pad so it leaves room for a bass. BandPass for a narrow,
telephone-like character.

**Comb** replaces CUTOFF, RESONANCE and key tracking with its own controls:
**TUNE** (50 Hz to 8 kHz), **DECAY** (how long it rings, up to 12 seconds),
**DAMP**, **DISPERSE** (pulls its overtones out of tune, towards a bell),
**DRIVE**, **MIX**, and a **PHASE + / PHASE −** switch that inverts its feedback.

> **Tip:** Two filters in series can do what one cannot. Set Filter 1 to HP12 and
> Filter 2 to LP12 for a band you control from both ends.

---

# VOICE — AMP ENV

The amplitude envelope shapes the volume of every note, from the moment the key
goes down to the moment the sound finally disappears. It is drawn as a curve you
edit directly, on the right of the VOICE page.

## Always an ADSR

The amplitude envelope is an ADSR: an attack, a decay, a sustain level and a
release. Three handles on the graph, four knobs beneath it (**ATK**, **DEC**,
**SUS**, **REL**), and a curve on every segment.

It has no TYPE menu, because it has no second type to choose. A **BREAKPOINT**
envelope is a one-shot — it plays its whole trajectory and the voice retires at
the end, whatever the key is doing — which is a modulation shape. As an
*amplitude* envelope it would mean a note whose length the keyboard does not
control, so AMP ENV does not offer it.

**ENV 1–4 do.** See [Envelope type](#envelope-type).

AMP ENV has no power button: every note needs one.

## The handles

There are three handles for four stages, because two of the stages share a point.

### ATTACK

**What it does:** Sets how long the note takes to reach full level, from
instant to 40 seconds.

**Sound:** Short values give a percussive, immediate start. Long values fade the
note in.

**How to use it:** Drag the handle left and right along the top of the graph. It
stays pinned to the top, because attack is a duration, not a level.

### DECAY / SUSTAIN

**What it does:** One handle controlling two things — drag it **sideways** for
the decay time, **up and down** for the sustain level.

**Sound:** Decay sets how long the note takes to fall from its peak to the level
it holds at. Sustain is that held level. A high sustain with a short decay is an
organ-like sound that stays put; a low sustain with a long decay is a plucked
sound that dies away while the key is still down.

**Why one handle:** They are the two coordinates of a single point on the curve —
the moment the fall ends and the hold begins.

### RELEASE

**What it does:** Sets how long the note takes to fade after you let go, up to
40 seconds.

**Sound:** Short values stop the note cleanly. Long values leave a tail that
overlaps the next note.

**How to use it:** Drag left and right along the bottom of the graph. Like
attack, it is a duration and stays pinned.

## The knobs

The four knobs and the curve are two views of the same thing: dragging the
graph moves the knobs, and turning a knob moves the graph. They cannot fall out
of step, because neither is a copy of the other.

Turning a knob does not straighten a curve you have drawn.

The time knobs span 0 to 40 seconds and are weighted towards the short end:
a quarter of the way round is about 25 ms, halfway is 1 second and three
quarters is about 8.6 seconds, so percussive settings keep their precision.

| Envelope | Attack | Decay | Sustain | Release |
| --- | --- | --- | --- | --- |
| AMP ENV default | 15 ms | 300 ms | 80% | 500 ms |
| ENV 1–4 default | 250 ms | 600 ms | 70% | 1 s |

The defaults are a starting point that sounds like an instrument: an attack fast
enough to be immediate but not clicky, and a release that tails off rather than
stops. The modulation envelopes default slower, because a modulation envelope
is usually a sweep.

**AMP ENV's four controls are modulation destinations.** Patch an LFO to
SUSTAIN for a tremolo that follows the envelope, or a Macro to RELEASE to
lengthen every tail from one knob. A change of shape under a sounding note is
smoothed, so it never clicks.

## Curves

Drag the line *between* two handles to bend that stage. A bent attack can rise
quickly then ease into its peak, or hang low and arrive suddenly. Double-click
the small handle on a bent segment to straighten it again.

## Overlapping handles

Handles may sit on top of one another. Drag DECAY onto ATTACK and the decay stage
has no length — the envelope steps straight from its peak to the sustain level.
That is what both the graph and the sound will give you.

The handle underneath is one drag away: grabbing a shared spot takes the later of
the two, and moving it uncovers the other.

## Watching a note play

While a note sounds, the area beneath the part of the envelope it has already
travelled fills in. It follows the shape exactly, bends included; it stops at the
sustain point for as long as you hold the note; and it resumes from there when
you let go.

The fill shows the most recently triggered note, and is shown only while
[animations](#enable-animations) are on.

---

# VOICE — LFOs and envelopes

Modulation is what makes a sound move on its own: a filter that opens as the note
sounds, a pitch that wavers, a wavetable that sweeps.

The second row of the VOICE page holds two tabbed panels: **LFO 1–4** on the
left and **ENV 1–4** on the right. Each panel shows one card at a time; click a
tab to see another. The tabs you chose are remembered while the session runs.

Every tab carries its source's **jack**, so you can patch a modulator without
switching to it. To modulate something:

1. **Drag from the jack** onto any knob — on this page or another. While you
   drag, hovering over a page button for a moment opens that page.
2. Let go. The route is made at +50% depth, and the knob wears a ring in the
   source's colour.
3. Set the route's depth, polarity and curve on the
   [MOD page](#mod--the-modulation-matrix).

A jack's centre fills in once it has at least one route. A source can drive many
destinations, and a destination can take many sources; up to 64 routes in all.

> **Changed in v0.8.2:** the ASSIGN menus are gone from the LFO and ENV cards.
> Routing is the patch bay's job: drag a jack, or use the MOD page. The cards'
> AMOUNT knobs, which only ever set the depth of an ASSIGN route, are gone too:
> every route's depth is set on the MOD page.

## LFOs

A low-frequency oscillator cycles continuously, whether or not a note is playing —
unless you ask it to start with each note. LFO cards are purple.

Each LFO card shows its waveform in a display at the top, with the controls
beneath:

| Control | Function |
| --- | --- |
| **WAVE** | SINE, TRIANGLE, SAW, SQUARE, RAMP UP, RAMP DOWN, S&H (a new random step every cycle), SMOOTH RND (random, gliding between values) |
| **KEY SYNC** | Restart the LFO on every new note |
| **CLOCK** | FREE (RATE in Hz), TEMPO (a musical division of the host's tempo), TRANSPORT (locked to the song position). With no host — in the standalone — an external MIDI clock drives TEMPO and TRANSPORT. With neither, the display shows NO HOST CLOCK |
| **DIVISION** | The note length for TEMPO and TRANSPORT: 4 BARS, 2 BARS, 1 BAR, 1/2, 1/4, 1/8, 1/16, 1/8T |
| **RATE** | 0.01 Hz to 20 Hz, for the cyclic shapes in FREE. Reads SYNC when the clock is tempo-locked |
| **TIME** | 0.05 s to 60 s. Takes RATE's place for RAMP UP and RAMP DOWN |

How far an LFO moves what it is patched to is not set on the card: every route
has its own depth, on the [MOD page](#mod--the-modulation-matrix).

**Sound:** A sine or triangle gives smooth movement — vibrato on pitch, a gentle
sweep on cutoff. A square jumps between two values, useful for trills and gated
effects. A saw ramps and resets.

**Use it for:** Vibrato — a sine at 5–6 Hz on Osc 1 Pitch Mod, at about 2%.
Slow evolution on a pad — a triangle at 0.1 Hz on cutoff. A wobble on a
wavetable position.

### Ramps

**RAMP UP** and **RAMP DOWN** are not cycles. A ramp travels once, from one end of
its swing to the other, over the **TIME** you set — then holds there. Use one for
a filter that opens over twenty seconds, or a wavetable that drifts from one
end to the other across a long pad.

The ramp's length is real time, so it is the same at every sample rate and
buffer size.

### KEY SYNC

| KEY SYNC | Cyclic shapes | Ramps |
| --- | --- | --- |
| **Off** (default) | Run freely, never restarted | Restart on the first note after every key has been released. Notes played legato ride the same ramp |
| **On** | Restart from the beginning of the cycle on every new note | Restart on every new note |

The LFOs are shared by every voice, so a restart is **global**: the newest note
restarts the LFO for everything that is sounding. A note-off never restarts
anything. The restart lands within one audio buffer of the note.

> **Note:** The envelopes have no KEY SYNC switch because they do not need one —
> every envelope already starts from the beginning with each note.

## Modulation envelopes

An envelope runs once per note, from the moment the key goes down. Where an LFO
repeats, an envelope describes a journey with a beginning and an end. ENV cards
are yellow.

ENV 1–4 use the same editor as AMP ENV — see [AMP ENV](#voice--amp-env) for the
handles, the knobs and the curves. Each card has the editor on top, and beneath
it:

| Control | Function |
| --- | --- |
| **TYPE** | ADSR or BREAKPOINT — see below |
| **LOOP** | While the key is held, the envelope restarts its attack/decay each time it reaches sustain — a repeating, rhythmic contour |
| **SYNC** | Snaps attack, decay and release to musical note lengths at the current tempo (host or MIDI clock) |
| **ATK / DEC / SUS / REL** | The four ADSR knobs |
| **KEY** | Keyboard tracking, 0 to 100%: higher notes run the whole envelope faster (an octave up is twice as fast at 100%) |

Clicking the card's background, away from its controls, switches it on or off,
as its power button does.

Each note has its own envelope. When an envelope modulates something inside the
voice — filter cutoff and resonance, oscillator tuning and Pitch Mod, the
oscillator PARAMs or wavetable position — every note follows *its own* contour,
so a chord's notes open and close independently.

### Envelope type

The **TYPE** menu chooses how the envelope is built. Both choices are drawn on
the same graph and both support curves; they differ in how many points the
envelope may have, and therefore in whether four knobs can describe it.

| Type | What it is | When to use it |
| --- | --- | --- |
| **ADSR** | The traditional four-stage envelope: attack, decay, sustain, release. Three handles and four knobs. | Almost always. It is quick to set, easy to read, and covers most sounds. |
| **BREAKPOINT** | A free-form envelope of up to 16 points, each with its own time, level and curve. It plays its whole trajectory once and does not hold: the key triggers it, it does not gate it. | Multi-stage swells, rhythmic shapes, anything the four stages cannot say. |

**Switching between them is safe.** Choosing BREAKPOINT starts from exactly the
ADSR shape you were looking at, curves included, and you can then add points.
Choosing ADSR again brings back the ADSR you had — your breakpoint drawing is
kept, and switching back to BREAKPOINT restores it exactly, even after saving
and reloading.

> **Note:** The four ADSR knobs are greyed out in BREAKPOINT mode. They stay on
> screen so you can see the mode is not using them; four numbers cannot describe
> a sixteen-point envelope.

### Extra points

In **BREAKPOINT** mode, double-click empty space in the graph to add a point,
and double-click a point to remove it. Up to sixteen.

The first and last points are structural and cannot be removed: the note begins
at silence and ends at silence. Nor can the last point between them — an
envelope with only its two ends has nothing you can move and nothing it can say,
so the editor always keeps one point in the middle for you to drag.

Each point has its own time and level, and each segment between points has its
own curve, so a breakpoint envelope can rise, fall, hold and rise again as many
times as sixteen points allow. The shortest it can be is 10 ms.

In **ADSR** mode, double-clicking does nothing. The envelope is the four stages,
and that is the whole point of choosing it.

**How they differ from AMP ENV:** The amplitude envelope always shapes the volume
of every note. ENV 1–4 do nothing until you patch them to something.

**Use them for:** A filter that opens quickly and settles back — ENV 1 to
Filter 1 Cutoff with a fast attack and a medium decay. Or a short pitch blip —
ENV 2 to Osc 1 Pitch Mod with a very short decay and a small depth.

---

# MOD — the modulation matrix

The MOD page is the whole picture of a patch's modulation, in three columns.

**SOURCES**, on the left, holds a jack for every source: LFO 1–4, ENV 1–4 and the
six Macros (M1–M6). Each jack has its own colour, used by its cables, its routes
and the rings it draws on knobs.

- **Drag a jack** onto a destination in the list on the right, or onto any knob
  anywhere in the window.
- **Click a jack to arm it.** While it is armed, every destination you click in
  the list is patched to it. Click the jack again, or press **Escape**, to disarm.

**ROUTES**, in the middle, lists every connection — one row each:

| Part of the row | What it does |
| --- | --- |
| **SOURCE** | Where the modulation comes from |
| **AMOUNT** | Depth, −100% to +100%. Drag for coarse changes, Cmd/Alt-drag for fine; the mouse wheel and arrow keys step by 1% (0.1% with Shift); double-click to type a value |
| **POLARITY** | NATIVE keeps the source's own range (LFOs swing both ways; envelopes and Macros push one way). UNIPOLAR and BIPOLAR force one or the other |
| **CURVE** | LINEAR, SQUARE or SQ ROOT — how the source's movement maps onto the destination |
| **DESTINATION** | The control being moved |
| **X** | Removes the route. Or select a row and press Delete or Backspace |

The header counts how many of the 64 route slots are in use. Each patched route's
depth is a host parameter (Route 01 Depth to Route 64 Depth), so it can be
automated.

Macro assignments made from the Macro strip appear in the list too, marked
MACRO; their depth here is the same depth as in the Macro's
[depth panel](#macro-depth). Polarity and curve apply to patched routes only.

**DESTINATIONS**, on the right, lists every modulatable control in the
instrument, grouped by module. Type in **Search destinations** to narrow it;
click a group heading to fold it. Clicking a destination with no source armed
selects the first route that reaches it.

## What modulation does to a knob

**A modulated control does not move.** The knob shows the value *you* set — the
one your DAW would automate — and its rings show how far the modulation can take
it and where the value actually is as it moves.

This is deliberate. If modulation drove the knob, it would be writing itself back
into your setting, and you would lose the value you dialled in.

> **Tip:** If a knob's ring is moving but the knob is not, that is modulation
> working correctly.

**How far modulation goes.** An LFO at 100% swings its destination across the
**whole range**, half of it each way from where your knob sits. Wherever the
swing would pass an end of the range it **folds back** — it turns around there
and keeps moving — rather than stopping flat against the limit. A filter cutoff
at 12 kHz with a 100% LFO therefore sweeps roughly 1.1 kHz to 18 kHz.

An envelope or a Macro at full depth reaches the end of the range in the
direction its depth points, from wherever the knob is.

Several routes on one control add together.

---

# FX — Effects

Six effects process the FX bus in a chain whose order you choose: **DRIVE**,
**CHORUS**, **DOOM**, **DELAY**, **MOOD** and **REVERB** (that is the default
order). Four more cards sit on the same page but outside the chain: **ANALOG**,
which works inside the voices, **VIBE**, which works on the whole instrument
ahead of the mixer's dry/send split, and **LUCY** and **SPREAD**, which work on
the master, in that order, after everything else.

**The page is laid out the way the signal flows.** It scrolls vertically and has
three sections, top to bottom:

* **INSTRUMENT** — *processes the complete instrument.* **ANALOG** (tagged
  PER VOICE: it runs inside every voice) then **VIBE** (tagged INSTRUMENT
  INSERT: one pedal on the summed instrument, before the dry/send split). These
  two are fixed: they have no drag handle and cannot join the send chain.
* **SEND FX** (the **FX BUS**) — *processes signal sent to the FX bus.* The six
  send effects, each tagged with its position (SEND 1, SEND 2, ...). Cards run
  left to right with a small arrow between them; when a row is full the chain
  continues on the next row from the left (a ↓ under the last card of the row
  marks where it continues), so wrapping never changes the order. After the last
  card, **↓ FX BUS EQ / COMP › FX RETURN** marks where the bus leaves the chain:
  through its fixed EQ and compressor (set on the MIX page) and the FX RETURN
  fader, to be mixed with the dry signal.
* **MASTER** — *processes the finished mix: dry signal + FX return.* **LUCY**
  then **SPREAD**, ending in the main output (**OUT**). Fixed, like INSTRUMENT.

**SEND → FX BUS**, at the right of the SEND FX header, is how much of the source
signal is routed into the FX bus: a master trim over every source's own FX send
(the per-source sends are on the MIX page). It does not touch the dry signal.
There is no per-effect send:
every send effect processes the same bus, one after another.

**Reordering.** Hover a send card and the ⋮⋮ handle on its tab lights up; drag
the tab. The card lifts, a dashed outline shows where it will land, and the other
cards slide aside; the chain only accepts send cards, and a lifted card stays
inside the SEND FX section. Let go and the new order is what the audio uses, and
it is saved with the preset and the session. A bypassed card stays in its place
(its tab says BYPASSED) and can still be moved.

Each card has a power button in its corner; clicking the card's background does
the same.

**Amounts add, they do not crossfade.** On DELAY, REVERB, MOOD, DOOM and CHORUS
the AMOUNT / MIX control sets how much of the effect is added to your sound. The
dry signal is never turned down, so raising an effect only ever adds to the
patch, and fully up means the whole effect on top of an intact dry. (The
standalone PX3 plug-ins keep the usual dry/wet crossfade, where fully up is the
effect alone.) DELAY's repeats follow the square root of AMOUNT — clearly there
from a quarter of the knob, and the whole echo at full — and its Granular type
always makes its full grain cloud, with AMOUNT setting only how much of it you
hear. DRIVE is different: distortion has no dry signal in it, so adding it on
top of the dry would make it parallel distortion. It keeps its crossfade for
now. (VIBE is not on the send at all - see below.)

**Order matters, and you choose it.** Drag a send card by its tab, as above. The
INSTRUMENT and MASTER cards always run where they are shown.

> **Note:** Bypassing an effect clears it out. Switching it back on starts clean
> rather than releasing whatever was caught inside when you switched it off.

## ANALOG — per-voice analogue drift

**What it is:** The instrument's own analogue imperfection. It runs *inside each
voice*, before the sources are summed, because saturating four signals
separately does not sound like saturating their sum. It is not an insert, so its
card is the first in the INSTRUMENT section (tagged PER VOICE), ahead of VIBE,
and cannot be reordered.

**Sound:** Every voice drifts in pitch and filter at its own rate, so a held
chord thickens rather than wobbling in unison. Supply sag follows how loud the
instrument is playing, and the saturation adds harmonics and softens transients,
with a little pink hiss.

**Controls:** **AMOUNT** (0 = off) and a **STYLE** menu — Warm, Hot, Cool,
Vintage, Clean, LoFi.

**Use it for:** Making a digital patch sit more comfortably. A little on a pad
takes off the sterile edge.

## VIBE — Uni-Vibe

**What it is:** A model of the Uni-Vibe pedal: four phase stages built around
very different capacitors, swept together by one lamp shining on four
photocells. The cells brighten quickly and darken slowly, so the sweep lunges
up and drifts back - the lopsided, throbbing swirl - and the stages are not
perfect all-passes, so the low end swells and dips with it. The transistor
stages add a little grit when driven hard. Off in a new patch.

**Where it sits:** On the whole instrument, ahead of the mixer's dry/send split
- like a pedal between the synth and the desk. The dry signal and what is sent to
the effects go through one pedal (one lamp, one sweep), so the reverb and delay
hear the vibe, VIBRATO is the phase-shifted signal alone, and switching VIBE on
does not make the patch quieter. Its card sits in the INSTRUMENT section after
ANALOG (tagged INSTRUMENT INSERT) and cannot be reordered. (Up to 0.8.2 it was in the send chain, where the dry signal
passed underneath it: VIBRATO was never pure, and engaging it cost up to 3 dB.)

**Controls:**

* **MODE** — **CHORUS** mixes the phase-shifted signal equally with the dry one,
  which gives the moving notches; **VIBRATO** passes the phase-shifted signal
  alone, heard as pitch wobble. These are the pedal's own switch positions.
* **STEREO** — **LINKED** runs both sides through the same circuit;
  **INVERTED** takes the right side from the opposite-phase output (a well-known
  modification), so its notches fall where the left side has peaks.
* **SPEED** — 0.5 to 8 Hz.
* **INTENSITY** — how hard the lamp is driven. At 0 the lamp idles at a dim
  glow: no sweep, but a fixed colour, just like the pedal. Use the power switch
  to take VIBE out completely.
* **LEVEL** — output level, ±12 dB.

**Use it for:** Organ, guitar-like plucks and electric piano. Slow and deep in
CHORUS for swirl; faster in VIBRATO for a rotary-like wobble.

## DRIVE

**What it is:** An overdrive and distortion stage in the spirit of classic pedals,
not a copy of any one circuit. It sits early in the default chain, before the
modulation and time effects, as it would on a pedalboard. Bypassed in a new
patch.

| Control | Function |
| --- | --- |
| **CLIP** | SOFT (smooth, compressed op-amp overdrive), HARD (buzzier diode clipping), ASYM (unmatched diodes: warmer, with even harmonics) |
| **DRIVE** | How hard the signal hits the clipper, up to +40 dB |
| **TIGHT** | Keeps low end out of the clipper so the grit sits in the mids and the bass stays solid |
| **TONE** | Dark to open, after the clipper |
| **LEVEL** | Output trim, ±12 dB, around an automatic level match — DRIVE changes the character, not the volume |
| **MIX** | Blend with the clean sound; 0 is off |

## CHORUS

**What it is:** Four families of bucket-brigade chorus, each built to the
topology of the hardware it is named after. At the default knob positions each
mode runs at its hardware's own settings.

| MODE | Based on | Character |
| --- | --- | --- |
| **DIM 1–4** | Dimension D | Two lines swept in opposite directions, with the unit's compander. DIM 1 is the softest, DIM 4 the strongest. The wet signal is pure stereo difference, so the mono sum is exactly the dry sound |
| **DIM 1+4, 2+4, 3+4** | Dimension D | Two buttons held together on the same pair: faster sweeps |
| **ENSEMBLE** | Solina-style string ensemble | Three lines on slow and fast sweeps — the lush string-machine shimmer |
| **CE-1** | BOSS CE-1 | One line: chorus on the left, direct on the right, as the pedal's stereo output |
| **JUNO-60 I / II** | Juno-60 | Two lines swept in opposition; II is faster than I |
| **JUNO-60 I+II** | Juno-60 | Fast, narrow and in phase — a near-mono warble |

| Control | Function |
| --- | --- |
| **INTENSITY** | Fades from bypass to the hardware's own wet/dry balance and stereo routing. 0 in a new patch |
| **RATE** | LFO speed; the default is the hardware's |
| **DEPTH** | Delay sweep; the default is the hardware's swing, full is twice it |
| **WIDTH** | Stereo spread of the wet. In CE-1, 0 is the pedal's mono output; in ENSEMBLE, 0 is the original mono |
| **PHASE** | LFO phase between the lines |
| **TONE** | Warm to clear, on the wet path only |
| **LOW CUT** | Extra high-pass on the wet only; off at zero |
| **FEEDBACK** | Extra resonant colour, capped short of flanging; off at zero |
| **VINTAGE** | Bucket-brigade drive: 0 is clean, the default is the hardware's, full is hot |
| **DRY/WET** | Final balance of dry against chorus |

The dry path is never filtered or delayed, so a bass note keeps its weight while
its harmonics move. Changing MODE while notes sound crossfades rather than
clicking.

## DOOM — the other ambient engine

A separate engine from MOOD. One half is a micro-looper that records *while
bypassed*, so switching it on captures what you already played. Its MIX starts at
zero, so a new patch hears none of it until you turn it up.

**MIX adds DOOM on top.** In P(X3), DOOM sits on the FX send, so MIX sets how
much of DOOM is added to the mix — your dry sound stays exactly as it is, and MIX
fully up is all of DOOM on top of it. (Up to 0.8.2 MIX crossfaded, as it does on
the pedal and in the standalone PX3 Doom; on a send that made the patch quieter
as you turned it up, and left DOOM well under the dry.)

### Six knobs, twelve functions

DOOM and LUCY both follow pedals where each knob has a **second function
printed underneath it**. Their cards do the same: each paired knob has a dimmed
caption below the bold one, and a **MAIN / SHIFT** switch in the top row chooses
which of the pair the knobs are driving.

Three things are worth knowing about it:

- **SHIFT is not a parameter.** It is a property of the panel, not of the sound.
  It is not saved in presets and does not appear in your DAW's automation list,
  so switching it can never change what you hear.
- **Both functions are always live.** Each half of a pair is a real parameter
  with its own automation lane. Automating an alternate works whether or not the
  panel happens to be showing it.
- **A preset always stores both.** Nothing depends on which way the switch was
  left.

Knobs with a single caption — OVERDUB and WIDTH on DOOM, WIDTH on LUCY — have no
alternate.

### DOOM's pairs

| Primary | Alternate |
| --- | --- |
| **WET TIME** — wet channel time | **INTERFERE** — how much one channel (or your input) disturbs the other |
| **WET CHAR** — wet character | **TILT** — output tilt: left removes highs, right removes lows |
| **LOOP LEN** — micro-looper length or pace | **DECAY** — how much of the loop survives each lap while overdubbing |
| **LOOP CHAR** — loop character | **DRY LOOP** — clean loop blended past the wet channel |
| **CLOCK** — the engine's sample rate | **DRIVE** — level-matched saturation that folds and crushes near the top |
| **MIX** — how much DOOM you hear | **LOOP/WET** — micro-looper against wet channel |

**CLOCK** is the important one: it is the engine's rate, so it sets the loop's
length *and* its pitch *and* the wet channel's time, all from one control. Its
eleven steps are harmonised ratios, so each lands on a musical interval; the
**SMOOTH / STEPPED** switch sweeps it continuously instead.

The switches: **LOOPER / LISTEN** (play the captured micro-loop, or keep
listening), **WET ON / OFF**, **FREEZE** (freeze the wet channel and repeat it),
**HALF / FULL** (halve the micro-loop), and **CROSS: INPUT / CHAN** (whether your
playing or the other channel drives INTERFERE). **ROUTE** chooses what the wet
channel is fed: INPUT, INPUT+LOOP or LOOP.

As with MOOD, the loop and wet knobs mean different things in different modes.

| LOOP mode | What it does |
| --- | --- |
| **BURST** | Slices the loop at its own onsets and sequences them |
| **RADIO** | Scans five loopers that interfere with each other |
| **MASK** | Replaces the loud parts of the loop with something else |

The other half is the wet channel (**WET**): **SOUP** resynthesises what passes
through it, **RELAY** repeats without fading, **FLIP** builds harmonies and
spreads them across time.

## LUCY — spectral degradation

**What it is:** Not a bitcrusher. LUCY models what a low-bitrate encoder throws
away.

**Where it sits:** On the master, after the dry and FX buses are summed and
before SPREAD — the whole instrument goes through it, whatever the sends are set
to, and above the bottom of AMOUNT what you hear is LUCY's output. Its card is
the first in the MASTER section (tagged MASTER INSERT) and cannot be reordered. (Up to 0.8.2 it was in the
send chain, where the dry signal passed underneath it untouched: switching it on
made the patch quieter and most of its controls hard to hear.)

**Latency:** LUCY's output is about 17 ms late at 48 kHz (about 28 ms with SLOW
on) — one analysis frame plus its timing line and limiter look-ahead. P(X3) does
not report this to the host, so it is not compensated: with LUCY on, the
instrument plays slightly behind the beat. LUCY is a degradation effect and
usually sounds fine like that; if timing matters, nudge the track earlier.

LUCY's card works like DOOM's: six knobs, five of them with a second function
under it, and a **MAIN / SHIFT** switch to choose between them.

| Primary | Alternate |
| --- | --- |
| **BAND** — band filter width; fully down is no filtering at all | **GATE** — the gate's threshold |
| **VERB** — reverb amount | **VERB SIZE** — its size and length |
| **FREQ** — band filter centre frequency | **LIMIT** — the output limiter's threshold |
| **SPEED** — how fast loss, packets and freeze evolve | **AUTO GAIN** — level compensation for the loss modes |
| **LOSS** — how degraded, and how much of the spectrum it reaches | **LOSS GAIN** — wet level, ±36 dB |
| **AMOUNT** — how strongly the whole effect is expressed | **FROZEN MIX** — live against frozen |

The menus and switches beside them:

| Control | Function |
| --- | --- |
| **MODE** | STANDARD keeps the coded signal; INVERSE plays what STANDARD discarded; JITTER adds an unstable clock |
| **PACKETS** | CLEAN, LOSS or REPEAT — a bad connection, where losses cluster the way they really do |
| **FREEZE** | OFF, SOLID or SLUSHY. Slushy keeps drifting toward what you play |
| **FILTER SLOPE** | The band filter's steepness: 6, 24 or 96 dB |
| **PROTECT** | DARK, NEUTRAL or BRIGHT — which end of the spectrum survives |
| **GATE ON / OFF** | Silences anything below the GATE threshold |
| **VERB FIRST / LAST** | The reverb after the codec, or in front of it so the reverb is degraded too |
| **PASS / REJECT** | Keep the band, or keep everything but the band |
| **SLOW ON / OFF** | Bigger, darker, slower, and with more latency |

**Extreme settings stay audible.** LOSS fully up keeps the strongest part of
each frequency band — sparse, coarse and chiming, never silent. INVERSE never
falls more than about 6 dB below its input: where STANDARD has thrown little away
(low LOSS, or a sound with nothing in LOSS's range), the gap is filled with the
coded signal. The band filter is level-matched (up to +30 dB), so a narrow band is
heard at a similar level; a band placed where your sound has nothing can still be
quiet. GATE at its highest threshold chops everything but the loudest moments
rather than muting the instrument.

**AMOUNT is not a wet/dry.** It scales how strongly the character you have
dialled in is expressed — the coder's depth and reach, the packet rate, the
filter, the freeze. Turn it up and the same setting gets *more* of itself,
rather than more of a fixed wet signal being faded in.

In a new patch LUCY starts switched off with AMOUNT halfway up, so switching it
on is heard straight away.

**Sound:** STANDARD is darker and full of chiming artefacts. INVERSE is brighter,
thinner and feathery — it is playing the difference.

## DELAY

**Controls:** **AMOUNT** (a wire at zero; how much delay is added on top),
**TIME**, **FEEDBACK**, a **SYNC** menu
for tempo-locked times (Free, 1 Bar, 1/2, 1/4, 1/8, 1/8T, 1/16, 1/16T), and a
**TYPE** menu.

| TYPE | Character |
| --- | --- |
| Granular | Repeats broken into grains |
| Tape | Wow, flutter and high-end loss on each pass |
| Analog/BBD | Dark, compressed bucket-brigade repeats |
| Ping-Pong | Alternating left and right |
| Stereo | Independent times per channel |
| Modulated | Pitch movement on the repeats |
| Diffusion | Smeared, closer to reverb |

**FEEDBACK** sets how much of the output is fed back in — how many repeats you
hear (in Granular, how dense the cloud of grains becomes). Each algorithm has its
own safe limit.

GRANULAR has a **MODE** menu — CLASSIC, CLOUD, SHIMMER and RHYTHMIC — and in the
last three the knobs take that mode's names (SIZE, DIFFUSE, INTERVAL, SWING/FB,
RATE). TAPE has three more controls: **QUALITY** (worn tape at the left — darker
and more saturated — new tape at the right), **WOBBLE** (how much the tape speed
wanders: wow and flutter, from a perfect transport to a badly worn machine) and
**SLIP** (how often the tape head slips backward for a moment, playing the echo
in reverse). MODULATED has **MOD DEPTH**, the strength of its chorus-like
movement.

## MOOD — micro-looper and space

**What it is:** An always-listening looper paired with spatial effects.

MOOD has two channels — a micro-looper (**LOOP** mode) and a wet channel (**WET**
mode) — and three of its knobs change meaning, and caption, with the modes. That
is the whole control scheme, not an inconsistency.

| Control | Function |
| --- | --- |
| **MIX** | Balance between the input and MOOD |
| **CLOCK** | MOOD's own sample rate |
| **SPREAD** | How much stereo treatment is applied |
| **ROUTE** | What the wet channel is fed: DRY->WET (the input), LOOP->WET (the loop), or PARALLEL (both) |
| **FEEDBACK** | How much of both channels is recycled back into the loop |
| **FREEZE** | Stops the looper recording, so what is captured stays |
| **DEGRADE** | Bit and rate reduction with a noise floor. A PX3 addition, not a pedal control |

**LOOP mode**, and what LOOP LEN and the third knob do in each:

| Mode | LOOP LEN | Third knob |
| --- | --- | --- |
| **ENV** | How much is captured when the detector fires (0.03 to 0.4 s) | **SENS** — turn it up and quieter playing triggers it |
| **TAPE** | Loop length, 0.05 to 2.2 s | **SPEED** — eight musical rates: ±½×, ±1×, ±2×, ±4× |
| **STRETCH** | Grain size — long carries phrases, short blurs into texture | **WALK** — direction and stretch. Noon is frozen; either side walks the playhead |

**WET mode**, and what its two knobs do in each:

| Mode | Time knob | Character knob |
| --- | --- | --- |
| **REVERB** | **DECAY** — how long the tail lasts | **SMEAR** — diffusion, or how smeared the tail is |
| **DELAY** | **TIME** — 0.03 to 1.6 s | **REPEATS** — feedback. The very top is unity — repeats pile up rather than fading |
| **SLIP** | **LAG** — how far behind the slipped voices trail | **PITCH** — ±24 semitones, in semitone steps |

**ENV mode** listens for you to play. Its detector is debounced, so a fast run up
and down the keyboard captures clean slices rather than chattering. When MOOD is
not frozen, loops play back at the pitch you played them (TAPE at its 1× speed).

**CLOCK** is worth understanding: lowering it lengthens the loop, drops its pitch,
slows the wet channel and narrows the band — all at once, because they are the
same thing. It steps in semitones, down to three octaves below full, and between
the engine's slower steps the output glides smoothly rather than holding each
sample, so a reduced CLOCK sounds darker, not gritty.

**Use it for:** Capturing a phrase and letting it decay underneath what you play
next. High FEEDBACK piles material up the way a looper does.

**DEGRADE and CLOCK are separate on purpose.** CLOCK transposes; DEGRADE only
roughens. Turning DEGRADE up will never change the pitch of your loop.

## REVERB

**Controls:** **MODE** (the type), **PRESET** (starting points for that type),
**MIX** (how much reverb is added; fully up is the whole reverb on top of the
dry), and:

| Control | Function |
| --- | --- |
| **SIZE** | Size of the space (the room's dimensions, the plate's area, the spring's length) |
| **DECAY** | Reverberation time, shown in seconds. The range follows the type: halfway is a medium room on ROOM and a medium hall on HALL. On GATED it reads **LENGTH**: how long the burst lasts |
| **PRE-DELAY** | Gap before the reverb begins, 0-250 ms |
| **DAMPING** | How much faster the top end dies away. 0 is flat; full is dark |
| **LOW** | How long the low end lasts against the mids, x0.5 to x2 |
| **DIFFUSION** | Grainy to smooth. On SPRING it reads **SPLASH** |
| **MOD** | Slow random movement in the tail; keeps long tails from ringing |
| **WIDTH** | Stereo width of the reverb. 0 is mono; full is the type's own image. It never changes the mono sum |
| fourth knob | The type's own control - see below. Empty on PLATE |

Controls a type does not use are dimmed (MOD and LOW on GATED, LOW on SPRING).

| MODE | Character | Its own control |
| --- | --- | --- |
| **ROOM** | Early reflections from a modelled room into a short, dense tail. Puts a sound in a space without an obvious tail | **EARLY**: reflections against the tail |
| **PLATE** | Dense, bright and immediate: a plate is diffuse within a few milliseconds. Leads, keys, percussion, vowel sounds | - |
| **HALL** | A large, smooth late field with the low end and the top end decaying at their own rates | **EARLY**: the hall's first reflections |
| **CLOUD** | An ambient wash: attacks swell into it, and it can hold for up to a minute | **SHIMMER**: the tail is pitched up an octave and fed back into itself, so it climbs as it fades. 0 is a plain cloud |
| **SPRING** | A spring tank: every echo arrives as a rising chirp | **DRIP**: how strongly each echo chirps |
| **GATED** | A shaped burst that stops dead - the 80s drum room | **SHAPE**: reverse swell (0), flat gate (middle), falling (full) |

**PRESET** lists starting points for the current type only, named by what they
are for (Tight Room, Wide Pad, Short Decay, Shimmer Wash, Surf Tank, 80s Gate
...). Choosing one sets the type's controls - never MIX or the bypass - and
every knob stays live: move one and the menu shows the name with a star
("Wide Pad*"). The choice is saved with the session and with patches.

Changing MODE while sound is playing fades the old tail out over 50 ms and
starts the new type fresh, without a click.

**Use them for:** ROOM to place a sound without obviously reverberating it.
PLATE for shine. HALL for size. CLOUD for atmosphere, with SHIMMER for the
octave-up halo. SPRING for retro and dub. GATED for percussion that needs to be
big and short.

## SPREAD

**What it is:** Stereo widening by allpass decorrelation rather than delay or
polarity tricks — which means it widens a *mono* source, something simple
mid/side gain cannot do.

**Sound:** Lows stay mono so the bass end stays solid, mids are decorrelated by
phase, highs by level.

**Controls:** **AMOUNT** (0 = off, as in a new patch), **WIDTH**, **LOW MONO**
(everything below it stays mono) and **MIX**. **ADVANCED** unfolds the rest: a
**MODE** menu (CLASSIC, WIDE, DEEP, MONO SAFE), **DECORR**, **CENTER**,
**SIDE TONE**, **LOW WIDTH**, **HIGH WIDTH** and **HIGH XO**.

SPREAD works on the whole instrument — dry sound and effects together — as the
last stage before the output, so it widens even a completely dry patch. The mono
sum keeps its level and its low end at every setting.

---

# MIX — Mixer

Six channel strips: **SUB**, **OSC 1**, **OSC 2**, **OSC 3**, the **DRY** bus and
the **FX** return. Each has a meter.

| Control | On | Function |
| --- | --- | --- |
| **Fader** | all | Channel level. Double-click returns it to 0 dB |
| **PAN** | all | Position in the stereo field (shown as L42, C, R08) |
| **SEND** | sources | How much of this channel is sent to the effects |
| **M** | all | Mute — silences the channel, and its send with it |
| **S** | all | Solo |
| **Ø** | all | Inverts the channel's polarity |
| **EQ / COMP** | DRY, FX | Open that bus's insert — see below |

## Levels

Every source starts 4 dB below unity, leaving room for modulation before anything
clips. A fader at unity means unity — the trim is on the source, not hidden
inside the fader — and the fader travels 4 dB above unity so you can push a
channel back to its full level.

**How send differs from an effect's mix:** The **send** decides how much of a
channel reaches the effects. Each effect's own amount or mix decides how much it
does to what it receives. A high send with a low reverb MIX gives a lot of signal
lightly reverberated; the reverse gives a little signal drenched.

The send is taken before the pan, so panning a source does not move where it sits
in the effects.

**DRY and FX pans are bus pans.** At centre they pass the bus at unity; turned to
one side they move the whole bus there (the far side goes silent, the near side
rises 3 dB). A source's PAN is an equal-power pan instead, which is why a
centred source and its send arrive at the same level. Up to 0.8.2 the FX return
used the source law, which left every effect 3 dB lower at centre than the dry
signal it was taken from; it now matches the DRY bus, so patches that use
effects are about 3 dB wetter at the same settings.

## Solo

| State | What you hear |
| --- | --- |
| No solos | Everything unmuted |
| A source soloed | Only soloed sources, dry |
| Only FX soloed | The effects alone |
| Sources and FX soloed | Soloed sources, dry and through the effects |

Muting a channel also kills its send.

## Bus inserts

An **EQ** and a **compressor** can be inserted on the dry bus and on the FX
return independently. The **EQ** and **COMP** buttons on those two strips open
the insert in a panel over the page; each insert's own **ON** control switches it
in, and the button lights while it is running.

**EQ** — four bands, with a graph you can drag directly.

**Compressor** — an 1176-style FET compressor: INPUT, OUTPUT, ATTACK, RELEASE,
RATIO (4, 8, 12, 20 or ALL — the all-buttons mode), MIX and stereo LINK, with a
VU meter that shows GR, IN or OUT.

The panels can be dragged by their title band.

**Use them for:** EQ on the dry bus to carve room for a bass; compression on the
FX bus to even out a reverb tail without touching the dry signal.

---

# SETTINGS

The gear at the right of the top bar, or **MENU › Settings**. A full-width page
rather than one of the four.

The gear is a toggle — press it again, or the **CLOSE** button at the bottom of
the page, and you go back to the page you were on when you opened it.

## Enable animations

On by default. Turns off the display movement that is decoration rather than
information: the oscillator, sub and LFO wave displays hold still, the envelope
graphs stop showing a playing note's progress, and the logo stops moving. With
animations off the displays still redraw when you change something, so they
always show the current setting.

A held key on the keyboard is pressed in and lit either way — that is feedback
telling you which note is sounding.

This is a preference for **your install**, not part of a sound. Three things
follow from that:

- It is **shared by every open instance.** Turn it off in one and every other
  P(X3) window in the session follows immediately — you do not have to visit
  them.
- It is **remembered between sessions**, in P(X3)'s own settings file rather
  than in the project.
- It is **not written into presets**, so loading somebody else's patch never
  changes it.

## Console Engine

The console colour applied to the whole output, after the master. Five
voicings, none of them an emulation of a specific piece of hardware:

| Profile | Character |
| --- | --- |
| **CLEAN** | Modern large-format VCA. Nearly transparent — the default. |
| **BRITISH** | Discrete and transformer-coupled: second-harmonic warmth, weight low down. |
| **AMERICAN** | Discrete op-amp: a harder knee, odd harmonics, fast slew. |
| **TRANSFORMER** | Transformer-heavy. The strongest even-harmonic content, and a narrower band. |
| **MODERN** | A later clean VCA: tight and fast, with the most headroom. |

Unlike the animation setting, this **is** part of the sound: it is saved with
your patch and travels with a preset. In a host its parameters are named
**Console Profile** and **Console Enabled** (on by default; it has no switch on
this page), so they are not confused with the [ANALOG](#analog--per-voice-analogue-drift)
card.

## Separate FX Output

Off by default. P(X3) has a second stereo output pair that a host can enable.

| Separate FX Output | Outputs 1/2 | Outputs 3/4 |
| --- | --- | --- |
| **Off** (default) | The full mix: dry and FX | Silent |
| **On**, second pair enabled in the host | Dry only | The FX return (the send chain) |

Turn it on when you want the dry and FX signals on separate tracks — in Logic,
for example, create the instrument as **Multi-Output (2xStereo)** and add the
extra channel strip. With it off, P(X3) sounds the same in every host however
many outputs the host has enabled.

VIBE runs on the instrument before the dry/send split, so both pairs carry it:
3/4 is the send chain processing a vibed FX bus. The two outputs carry the fixed
output boost but not the master stages — the
console engine, LUCY, SPREAD and the output ceiling — which act on the sum. LUCY
in particular cannot be split: it codes the mix as a whole, so with Separate FX
Output on it is not heard on either pair. Summing 1/2 and 3/4 in your DAW is close
to the stereo output but not identical to it.

The setting is saved with your session. Switching it while notes sound
crossfades rather than clicking.

## Updates

The **UPDATES** section shows the installed version and checks for, downloads and
installs new versions while your DAW stays open. The installer is downloaded and
verified in the background; it installs once you save your work and quit the
host (or the standalone).

When an update is available, P(X3) asks once per DAW session (or once per
standalone launch), in a box headed **UPDATE AVAILABLE**: **UPDATE** opens this
page, **CANCEL** dismisses the question until next time. It never appears on top
of another question or an open sheet; it waits for them to close.

---

# Macros

Six knobs, **M1** to **M6**, down the left of every page. Each can move any
number of parameters at once, anywhere in the instrument.

They are the same six wherever you are. Switch pages and they keep their values
and their assignments.

## Why use one

A Macro turns several related adjustments into a single gesture. Instead of
reaching for the cutoff, then the resonance, then the reverb, you build one
control that does all three in the proportions you chose:

```
MACRO 1
 ├── Filter 1 Cutoff
 ├── Filter 1 Resonance
 ├── Delay Amount
 └── Reverb Mix
```

Turn that knob up and the patch opens, sharpens and moves back in the room at
once. Turn it down and it closes to a dry, dark version of itself. One knob, and
the patch has two distinct characters with everything in between.

## Assigning parameters

1. **Command-click a Macro knob** — or **double-click** it, which does the same
   thing. It lights teal, every knob it could drive shows a teal ring, and the
   keyboard shows *"Click on knobs to assign them to MACRO 1. Hit Enter to
   confirm."*
2. **Click any knob** to assign it. Click it again to remove it.
3. **Switch pages and keep going.** Assignment stays active, so one Macro can
   collect an oscillator detune, a filter cutoff, a delay amount and a mixer send
   in a single pass.
4. **Finish** by clicking the Macro knob again, or pressing **Enter** or
   **Escape**.

While assigning, clicking a knob assigns it — it does not move it. Your settings
are safe while you work.

Everything you clicked is already assigned the moment you click it; finishing,
whichever way, keeps it.

## Macro depth

The **Depth** button under each Macro opens a panel beside it listing everything
that Macro drives, one row per parameter, each with its own amount (−100% to
+100%, full by default) and an **X** to remove it. A Macro with nothing assigned
says so, and tells you how to start. Press Escape or Enter, or click away, to
close it.

The same assignments, with the same depths, appear on the
[MOD page](#mod--the-modulation-matrix) marked MACRO.

## What a Macro does to a parameter

A Macro does not take a parameter over. It **adds** to it, the way an LFO does.

The destination knob stays where you set it, and its ring shows where the value
actually is. Turn the Macro to zero and every destination returns to exactly what
its own knob shows.

This is what allows a parameter to be moved by its own knob, your DAW's
automation, a MIDI controller, an LFO, an envelope and more than one Macro at the
same time, with all of them contributing rather than overwriting each other.

**How a Macro differs from an LFO or envelope:** A modulation source moves on
its own — it cycles, or it runs when a note starts. A Macro moves only when you
move it. Both add to the parameter in the same way.

> **Macros in the patch bay.** M1–M6 also have jacks on the MOD page. Patching
> from one makes an ordinary route, with a polarity and a curve as well as a
> depth — useful when a Macro should push a control along a curve rather than a
> straight line.

## Reading a knob

| The knob shows | Meaning |
| --- | --- |
| `MACRO 1` on a pale plate above the spindle | One Macro drives it |
| `M1+` on that plate | Several Macros drive it; the first is named |
| A teal ring | Assignable right now, in the active Macro mode (brighter if already assigned) |

## MIDI control of Macros

A Macro is itself a control, so it can be mapped to a hardware knob exactly like
any other — see [MIDI Learn](#midi-learn). Macros can also be automated by your
DAW.

## What is remembered

Macro assignments, their depths **and** the Macro values are saved in presets and
in DAW projects, so a patch arrives with the performance controls it was designed
around.

Loading a preset does not change which hardware knob drives a Macro. The preset
says what the Macro *does*; your instance says what *moves* it.

> **Note:** There are six Macros, and a Macro cannot drive another Macro.

---

# MIDI Learn

Any knob in the instrument can be driven by a hardware controller. There is no CC
number to type and no dialog to open.

## Assigning

1. **Shift-click a knob.** A dashed amber ring appears and the keyboard shows
   *"Select knobs, then move a MIDI control to assign"*.
2. **Shift-click more knobs** if you want several on one control — anywhere, on
   any page.
3. **Move the hardware control.** Everything selected is assigned to it, and each
   knob shows its CC number.

The controller's full travel sweeps each destination through its own range, so a
cutoff in hertz and a resonance both get a complete sweep in their own units.

The movement that *teaches* the mapping does not also jump the knobs — they stay
where you left them, and the next movement drives them.

## Removing and reassigning

**Shift-click a mapped knob.** Its assignment is dropped and it joins the
selection, ready for a new one. Move a control to give it one, or press **Escape**
to leave it unmapped.

You never need to click a knob to find out what it is mapped to — a mapped knob
always shows its CC.

## What is remembered

MIDI assignments are saved in DAW projects and in preset files, and are unique to
each instance of the plugin. Two copies of P(X3) in one project can map the same
CC to completely different things.

A preset that carries no mappings of its own leaves yours alone, so auditioning
factory sounds never costs you your controller setup.

## Notes and limits

- Any MIDI channel drives a mapping.
- A CC arriving when nothing is selected only drives existing assignments — it
  never learns by itself.
- Note input, the mod wheel and pitch bend are unaffected. Mapping CC 1 gives you
  both the mod wheel's usual behaviour and the mapped parameter.
- A control change reaches the sound in the same buffer it arrives in, so a
  sweep is a sweep rather than a staircase. The knob and your DAW's automation
  lane catch up on the next interface update, which is where a recorded
  automation move comes from.
- The instrument cannot tell your controllers apart — a plugin receives all MIDI
  devices merged into one stream — so a mapping is to a CC number, not to a
  particular device.

---

# MIDI and Macros together

These are two separate systems, and combining them gives the most useful workflow
in the instrument.

```
   Hardware knob
        │
      CC 21
        │
      MACRO 1
        │
   ┌────┼─────┬────────┐
   ▼    ▼     ▼        ▼
Cutoff Reso Delay   Reverb
```

**Direct MIDI mapping** connects one hardware control to one parameter. Simple,
and right when you want a knob for the cutoff.

**MIDI to a Macro** connects one hardware control to one Macro, which drives as
many parameters as you assigned it. One physical knob transforms the whole patch.

### Setting it up

1. Command-click **M1** and click the parameters you want it to move. Press
   Enter.
2. Shift-click the **M1** knob itself.
3. Move the hardware control you want to use.

That hardware knob now drives Macro 1, and Macro 1 drives everything you assigned
to it.

### Why prefer this to mapping everything directly

Mapping one CC to four parameters directly gives all four the same full sweep,
whether that suits them or not, and changing your mind means re-learning all
four. Through a Macro, the set of destinations and their depths are part of the
patch — they travel with the preset — while the hardware mapping stays with your
studio. Change preset and the same knob does whatever the new patch's Macro 1
was designed to do.

> **Note:** Direct mappings and Macro assignments coexist. A parameter can be
> mapped to CC 22 *and* be a destination of Macro 1. The CC moves where the
> parameter sits; the Macro moves it from there.

---

# Playing

## The keyboard

The on-screen keyboard spans the full 88 keys, A0 to C8. Click or drag across it
to play. Clicked notes use a fixed medium velocity; play from a MIDI keyboard for
velocity response. A held key — clicked or played over MIDI — is drawn pressed in
and lit.

## Pitch and mod wheels

To the left of the keyboard.

**PITCH** springs back to centre when released. Double-click to centre it. The
bend range is 1 to 24 semitones, 2 by default; it is a host parameter (Pitch Bend
Range) rather than a control on the panel.

**MOD** stays where you leave it. Double-click to return it to zero. It adds
vibrato to every oscillator.

## Messages

The keyboard shows a message when the instrument has something to tell you — that
every source is off, or that you are in an assignment mode. The keyboard stays
playable while you assign, so you can hear what you are building.

---

# Presets

A preset is a complete patch. The top bar moves through the library:

| Control | Function |
| --- | --- |
| `<` and `>` | Step to the previous or next preset |
| The preset name | Shows what is loaded (with `*` once you have edited it); click it to open the browser |
| **MENU** | Save, Save As, Add to / Remove from Favorites, Import, Export, Settings, and the installed version |

**The browser** is a sheet over the page, headed P(X3) PRESETS. Filter by source
(All, Factory, User, Favorites) and by category, or search by name, category,
author or description; pick a preset and press **LOAD PRESET**. The sheet can be
dragged by its title band.

**INIT** is the default state rather than a preset: a blank patch you start from.
It cannot be overwritten, favourited or exported, and appears only under All.

**Unsaved changes are protected.** If you have edited the loaded patch and step
to, or load, another preset, P(X3) asks first — *"Do you want to save your
changes?"* — with **SAVE**, **DON'T SAVE** and **CANCEL**.

Presets are `.px3preset` files, kept in the shared `~/Library/P(X3)/` library;
the factory presets are installed there automatically. Loading one uses the same
path your DAW uses to restore a project, so what you hear is what was saved.

## What travels where

| | Saved in a preset | Saved in a DAW project |
| --- | --- | --- |
| Oscillators, filters, envelopes (including drawn shapes), effects, mixer | ● | ● |
| Modulation routes | ● | ● |
| Effect order | ● | ● |
| Macro assignments, depths and values | ● | ● |
| MIDI mappings | ● | ● |
| The name of the loaded preset | | ● |
| Enable animations | | |

A preset that carries no MIDI mappings leaves your existing ones untouched. A DAW
project is the complete state of that instance and restores exactly what was
saved, including having no mappings at all.

> **Changed in v0.8.2:** LFO 4 and ENV 4 were added, and sessions and presets
> saved by any earlier version — including 0.8.0 and 0.8.1 — no longer load. Their
> saved state is rejected rather than half-loaded. The factory library is
> reinstalled for the current version.

---

# Sound design walkthroughs

## Your first patch

1. **VOICE, OSC 1** — set MODE to `SAW`.
2. **VOICE, FILTER 1** — TYPE `LP24`, CUTOFF about a third of the way up,
   RESONANCE low.
3. **VOICE, AMP ENV** — a short attack, a medium decay, sustain around
   three-quarters, a medium release.
4. **VOICE, ENV 1** — drag the ENV 1 tab's jack onto FILTER 1's CUTOFF. Give
   ENV 1 a fast attack and a medium decay, then on the **MOD** page set that
   route's AMOUNT to about +40%.

Each note now opens the filter and lets it settle. This is the foundation of most
subtractive sounds.

## A bass

1. **VOICE** — OSC 1 to `SAW`. Switch on **SUB OSC**, WAVE `SINE`, OCT −1.
2. **MIX** — bring the SUB fader up until you feel it without hearing it
   separately.
3. **VOICE, FILTER 1** — `LP24`, cutoff low. Basses live below the rest of the
   mix.
4. **VOICE, AMP ENV** — attack at minimum, short decay, sustain around half,
   short release. A bass should stop when you stop.
5. **VOICE, ENV 1** — patch it to Filter 1 Cutoff; fast attack, short decay, a
   route depth around +30%. That is the pluck.

> **Tip:** Keep RESONANCE modest on a bass. High resonance at a low cutoff can
> produce more level at the peak than the rest of the patch.

## A lead

1. **VOICE** — OSC 1 to `FM`, RATIO at a whole-number setting, INDEX moderate.
   Switch on OSC 2 as a `SAW` with CENT at +7.
2. **VOICE, FILTER 1** — `LP12`, cutoff fairly open. A lead should be bright.
3. **VOICE, AMP ENV** — short attack, high sustain, medium release.
4. **MOD** — set LFO 1 to SINE at around 5.5 Hz, then patch it to **Osc 1 Pitch
   Mod** (search "pitch" in the destination list) with a small depth, around 2%.
   That is vibrato.
5. **FX** — a little DELAY, tempo-synced.

## A pad

1. **VOICE** — all three oscillators. OSC 1 `SAW`, OSC 2 `SAW` with CENT at −8,
   OSC 3 `WAVETABLE`.
2. **VOICE, AMP ENV** — long attack, long release, high sustain. A pad arrives
   slowly and leaves slowly.
3. **VOICE, LFO 1** — TRIANGLE, slow, around 0.1 Hz; drag its jack onto OSC 3's
   **POSITION** knob. The pad now evolves while it is held.
4. **FX** — REVERB on `HALL`, generous MIX. CHORUS (try `JUNO-60 I` or
   `ENSEMBLE`) for width. SPREAD if you want it wider still.

> **Tip:** With long attacks, add a little ANALOG, or some SLOP on each
> oscillator. The per-voice drift keeps a held chord from sounding static.

## A performance Macro

Starting from the pad above:

1. Command-click **M1**.
2. Click FILTER 1's **CUTOFF**, then its **RESONANCE**.
3. Switch to **FX** and click REVERB's **MIX** and DELAY's **AMOUNT**.
4. Switch to **MIX** and click OSC 3's **SEND**.
5. Press Enter to finish.
6. Click **Depth** under M1 and trim any destination that moves too far.

Macro 1 now takes the patch from closed and dry to open and enormous. Shift-click
the M1 knob, move a hardware knob, and that transformation is under your hand.

---

# Interaction reference

| Action | Result |
| --- | --- |
| Click and drag a knob | Adjust its value |
| Drag in an envelope graph | Move a handle or bend a segment |
| **Shift + drag** in an envelope graph | Fine adjustment |
| Arrow keys, with an envelope point selected | Nudge it in time or level |
| **Shift** + arrow keys | Nudge it more finely |
| **Delete** or **Backspace**, with an envelope point selected | Remove it |
| Double-click empty envelope space | Add a point (ENV 1–4, BREAKPOINT mode) |
| Double-click an envelope point | Remove it (BREAKPOINT mode) |
| Double-click a curve handle | Straighten that segment |
| Click an LFO or ENV tab | Show that card |
| Drag a jack onto a knob | Make a modulation route |
| Hover over a page button while dragging a jack | Open that page |
| Click a jack on the MOD page | Arm it; clicking destinations then patches them |
| **Delete** or **Backspace**, with a route selected | Remove the route |
| Drag a route's AMOUNT; **Cmd/Alt**-drag | Set its depth; finely |
| Double-click a route's AMOUNT | Type an exact value |
| **Shift + click** a knob | Select it for MIDI Learn |
| **Command + click** or double-click a Macro knob | Enter Macro assignment for that Macro |
| Click a knob during Macro assignment | Assign or unassign it |
| Click the active Macro knob, or **Enter** | Finish Macro assignment |
| **Escape** | Leave any assignment mode, disarm a jack, or close a Macro depth panel |
| Click **Depth** under a Macro | Open its depth panel |
| Double-click the pitch wheel | Return it to centre |
| Double-click the mod wheel | Return it to zero |
| Double-click a mixer fader | Return it to 0 dB |
| Drag a send card's ⋮⋮ tab on the FX page | Reorder the send chain |
| Click an effect's corner button, or its card background | Bypass or enable it |

Only one assignment mode is active at a time. Starting a MIDI selection leaves
Macro assignment, and entering Macro assignment clears a MIDI selection.

---

# Visual indicators

| Indicator | Meaning |
| --- | --- |
| A coloured arc around a knob | A route modulates this parameter; the colour is the source's |
| A moving marker on that arc | Where the value is right now |
| A filled jack | That source has at least one route |
| `MACRO 1` on a pale plate | A Macro drives this parameter |
| `M1+` on a pale plate | Several Macros drive it |
| `CC21` in amber | A MIDI control is mapped to this parameter |
| Teal ring | Assignable in the active Macro mode |
| Dashed amber ring | Selected for MIDI Learn |
| Teal highlight on the Macro strip | This Macro is being assigned |
| A greyed card | Switched off (bypassed) |
| A greyed keyboard with a message | No source is switched on |
| A lit EQ or COMP button on the mixer | That insert is running |

**Teal is Macro assignment. Amber is always MIDI.** Modulation routes wear their
source's own colour. On the cards, LFOs are purple, envelopes yellow and AMP ENV
green.

---

# Standalone and plugin

The instrument is identical in both. The differences are in the surrounding
environment.

| | Standalone | Plugin |
| --- | --- | --- |
| MIDI input | Chosen in the application's audio settings | Routed by your DAW |
| Tempo for synced LFOs, envelopes and delays | An external MIDI clock, if one is running | The host's tempo |
| Audio output | Chosen in the audio settings | Your DAW's track |
| Session state | Kept by the application | Saved in the project |
| Presets | Identical | Identical |
| MIDI mappings | Identical | Identical, and separate per instance |

MIDI Learn, Macros and every mapping behave the same way in both.

---

# Troubleshooting

### No sound

- **The keyboard is greyed with a message.** Every source is switched off. Switch
  on an oscillator or the sub.
- **Check MIX.** A solo left engaged on a channel you are not playing will
  silence everything else. Check for a muted channel, and check the faders.
- **Check AMP ENV.** A sustain at zero with a short decay means the note is gone
  before you hear it.
- **Check the MASTER knob** at the right of the top bar.
- **In a DAW**, check the track is receiving MIDI and is not muted.

### An old session or preset will not load

Sessions and presets saved before v0.8.2 are not compatible and are rejected by
design. See [Presets](#what-travels-where).

### A knob will not move

You are probably in an assignment mode — the keyboard will say so. Press
**Escape**.

### A knob moves on its own

It is mapped or assigned, and its label tells you which. An amber `CC` label means
a MIDI control; shift-click to clear it. A pale `MACRO` plate means a Macro;
command-click that Macro and click the knob to remove it, or remove it in the
Macro's depth panel.

### A knob's ring moves but the knob does not

That is correct. Modulation, Macros and mapped controls move the *value*; the knob
keeps showing what you set. See
[What modulation does to a knob](#what-modulation-does-to-a-knob).

### A modulation route does nothing

- Check the source's card is switched on.
- Check the route's AMOUNT on the MOD page — a route at 0% does nothing.
- An LFO on a TEMPO or TRANSPORT clock needs a host tempo or MIDI clock; its
  display says NO HOST CLOCK when there is none.
- An envelope only moves while notes play.

### A Macro does not seem to do anything

- Check the destination is actually assigned — it will show a `MACRO` plate.
- Check its depth in the Macro's depth panel is not near zero.
- Check the Macro itself is moving. If a hardware knob drives it, confirm that
  mapping is still there.

### A MIDI controller does not respond

- Confirm the device is connected and selected — in the standalone's audio
  settings, or in your DAW's track input.
- Confirm the control sends CC rather than notes.
- Confirm you are not in an assignment mode, which changes what a movement does.
- Confirm the mapping survived — a mapped knob shows its CC.

### An effect does nothing

Check its power button, that its own amount or mix is up (CHORUS's INTENSITY,
DELAY's AMOUNT, REVERB's MIX, DOOM's MIX and SPREAD's AMOUNT all start at zero),
and that the source channel's SEND is up.

### An effect sounds like it is still on after bypassing

It is not; bypass clears the effect. What you hear is a tail from an effect
*later* in the chain, still processing what reached it.

### Loading a preset changed my controller mappings

A preset only replaces MIDI mappings if it carries some of its own. If it does,
those mappings were part of the patch as saved.

### The patch is thinner than expected

Check whether an oscillator card is switched off — switching one off removes it
from the voice entirely.

---

# Glossary

**ADSR** — Attack, Decay, Sustain, Release: the four stages of a standard
envelope.

**Amount / depth** — How far a modulation source or a Macro moves its
destination, and in which direction.

**Assignment** — Connecting a Macro to a parameter.

**Bypass** — Switching a section out of the signal path.

**CC** — Control Change: the MIDI message a hardware knob or slider sends.

**Cutoff** — The frequency at which a filter begins to act.

**Destination** — A parameter that a modulation source or a Macro moves.

**Envelope** — A contour that runs once per note. The amplitude envelope shapes
volume; the modulation envelopes shape whatever you patch them to.

**Jack** — The socket on an LFO, envelope or Macro that you drag from to make a
route.

**LFO** — Low Frequency Oscillator: a cycling modulation source, generally below
the range of hearing.

**Macro** — One of six performance controls, each able to move any number of
parameters at once.

**MIDI Learn** — Assigning a hardware control by moving it, rather than by
entering a number.

**Modulation** — Automatic movement of a parameter by an LFO, an envelope or a
Macro.

**Oscillator** — The source of the raw tone.

**Patch bay** — The system of jacks and routes that connects modulation sources
to destinations.

**Preset** — A saved patch.

**Resonance** — Emphasis of the frequencies around a filter's cutoff.

**Return** — The channel on which the effects come back into the mix.

**Route** — One connection from a modulation source to a destination, with its
own depth, polarity and curve.

**Send** — How much of a channel is fed to the effects.

**Solo** — Hearing one channel alone.

**Sub oscillator** — A simple additional source, generally an octave or two below
the played note, used for weight.

**Sustain** — The level a note holds at while the key is down.

**Voice** — Everything that produces one note: sources, filters and envelopes.

**Wavetable** — A collection of waveforms that can be swept through while a note
sounds.
