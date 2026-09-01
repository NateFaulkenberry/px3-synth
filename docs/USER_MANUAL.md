# P(X3) — User Manual

A manual for playing the instrument. Nothing here assumes you know how it was
built; where something works differently from what you might expect, the reason
is given rather than left as a surprise.

---

## Contents

1. [What P(X3) is](#1-what-px3-is)
2. [Getting around](#2-getting-around)
3. [OSC — making the raw sound](#3-osc--making-the-raw-sound)
4. [FLT — shaping it](#4-flt--shaping-it)
5. [AMP — the volume contour](#5-amp--the-volume-contour)
6. [MOD — LFOs and envelopes](#6-mod--lfos-and-envelopes)
7. [MIX — balance, sends and inserts](#7-mix--balance-sends-and-inserts)
8. [FX — the eight effects](#8-fx--the-eight-effects)
9. [Macros — four performance controls](#9-macros--four-performance-controls)
10. [MIDI Learn — hardware control](#10-midi-learn--hardware-control)
11. [Playing it](#11-playing-it)
12. [Presets](#12-presets)
13. [Automation, modulation and macros together](#13-automation-modulation-and-macros-together)
14. [When something is wrong](#14-when-something-is-wrong)

---

## 1. What P(X3) is

A polyphonic synthesiser. Each note you play gets its own **voice**, and every
voice contains:

- **three oscillators plus a sub oscillator** — four sound sources
- **two filters** in series
- **an amplitude envelope**
- **three modulation envelopes and three LFOs** to move things

Those voices are mixed together, and the mix goes through **eight effects** in
whatever order you choose.

It runs as a standalone application and as an AU or VST3 plugin.

---

## 2. Getting around

Six buttons across the top switch what fills the main area:

| | |
|---|---|
| **OSC** | the oscillators and the sub |
| **MOD** | LFOs and modulation envelopes |
| **AMP** | the amplitude envelope |
| **FLT** | the two filters |
| **FX** | the eight effects and their order |
| **MIX** | levels, sends, solo and the bus inserts |

Two things never go away:

- **The Macro strip** down the left edge — four knobs, the same four on
  every panel. See [section 9](#9-macros--four-performance-controls).
- **The keyboard** across the bottom, which also shows you messages when the
  synth needs to tell you something.

---

## 3. OSC — making the raw sound

Three oscillator cards, each with a power button, plus a sub oscillator.

### Choosing a sound

**MODE** picks one of twenty oscillator types:

| | | | |
|---|---|---|---|
| SINE | SAW | SQUARE | TRIANGLE |
| NOISE | PINK NOISE | SUPER SAW | PWM |
| WAVETABLE | ADDITIVE | FORMANT | FM |
| HARD SYNC | KARPLUS | ORGAN | DIGITAL |
| PHYSICAL | ROB | ISAAC | PX3 |

### PARAM A, B and C

Three knobs whose meaning changes with the mode — this is where most of the
character lives. The labels change with the mode, so you never have to remember
which is which:

| Mode | A | B | C |
|---|---|---|---|
| SINE / SAW / SQUARE / TRIANGLE | — | — | — |
| NOISE / PINK NOISE | COLOR | — | — |
| SUPER SAW | SPREAD | — | — |
| PWM | WIDTH | — | — |
| WAVETABLE | POSITION | — | — |
| ADDITIVE | TILT | ODD/EVEN | ROLL |
| FORMANT | MORPH | COLOR | — (plus VOWEL) |
| FM | RATIO | INDEX | — |
| HARD SYNC | SYNC | DRIVE | — |
| KARPLUS | DECAY | BRIGHT | — |
| ORGAN | TONE | CLICK | — |
| DIGITAL | BITS | RATE | — |
| PHYSICAL | DECAY | MATERIAL | — |
| ROB | TRANS | BODY | CHAOS |
| ISAAC | SPREAD | ODD/EVEN | ROLL |
| PX3 | MORPH | CHAR | MOVE |

**FORMANT** adds a **VOWEL** menu (A/E/I/O/U) that PARAM A morphs between.

### WAVETABLE mode

Choosing WAVETABLE turns the card's display into a rotating 3D view of the
table — each frame drawn as a line, the current position highlighted. It is a
readout, not a control: **POSITION** moves through it, and POSITION can be
modulated, so an LFO or an envelope can sweep the table while you hold a note.

You can load your own tables from audio files or images, alongside the eight
factory tables.

### The sub oscillator

A separate simple voice under the others, with its own waveform, octave and
fine pitch. Useful for weight under a thin lead.

### Per-oscillator controls

Each card also has coarse and fine tuning. Levels live in
[MIX](#7-mix--balance-sends-and-inserts), not here, so every source's balance is
set in one place.

Turning a card off with its power button removes it from the voice — including
its filter tail, so it stops immediately rather than ringing out.

---

## 4. FLT — shaping it

Two filters per voice, in series. Each has:

- **CUTOFF** — roughly 80 Hz to 18 kHz, spaced so the useful range is spread
  across the knob rather than crammed at one end
- **RESO** — resonance, about 0.25 to 2.2
- **TYPE** — LP12, LP24, HP12, HP24, BandPass, Notch, AllPass

The 24 dB types are two stages in series, so they are steeper and darker than
their 12 dB counterparts at the same setting.

Each filter has its own bypass. A bypassed filter is genuinely out of the path.

---

## 5. AMP — the volume contour

The amplitude envelope decides how a note swells and fades. It is drawn as a
curve you can edit directly, with four knobs under it.

### The three handles

- **ATTACK** — how long the note takes to reach full level. Moves left and
  right along the top.
- **DECAY / SUSTAIN** — one handle doing two things, because they are the two
  coordinates of one point: drag it **sideways** for the decay time, **up and
  down** for the level the note holds at.
- **RELEASE** — how long the note takes to fade after you let go. Moves left
  and right along the bottom.

ATTACK and RELEASE are durations, so they stay pinned to the top and the bottom
however far down you drag. The note always begins at silence and ends at
silence.

Handles may sit on top of one another. Drag DECAY onto ATTACK and the decay
stage has no length — which is what both the graph and the sound then give you.
The one underneath is one drag away: grabbing a shared spot takes the later of
the two, and moving it uncovers the other.

### Curves

Drag the line *between* two handles to bend that stage. Double-click the small
handle on a bent segment to straighten it again.

### Extra points

Double-click empty space to add a point, and double-click a point to remove it.
Up to 16. Adding a point takes the shape past what ATTACK/DECAY/SUSTAIN/RELEASE
can describe, and the knobs stop following it — the drawn shape is then the
whole truth.

### The knobs

ATTACK, DECAY, SUSTAIN and RELEASE knobs sit under the graph. They and the
curve are two views of one thing: dragging the graph moves the knobs, turning a
knob moves the graph, and turning a knob does not straighten a bend you drew.

### Watching it play

While a note sounds, the area under the part of the envelope it has already
been through fills in. It follows the shape exactly, stops at the sustain point
for as long as you hold the note, and resumes from there when you let go. It
shows the most recently triggered note.

---

## 6. MOD — LFOs and envelopes

Three LFOs and three modulation envelopes, each on its own card.

### Using one

1. **ASSIGN** — choose what it moves.
2. **AMOUNT** — how far, from −100% to +100%. Negative inverts it.
3. For an LFO, set its **rate**; for an envelope, draw its shape.

The ENV 1–3 editors work exactly like AMP ENV, including the knobs, the curves
and the progress fill.

### What modulation does to a knob

The destination knob **does not move**. It shows what you set and what your DAW
would automate; a ring around it shows where the sound actually is. That is
deliberate — if modulation moved the knob it would be writing itself back into
the value you set.

Each source has one destination at a time. Full amount reaches the end of the
parameter's range and turns around there, rather than pushing past it and
flattening out.

---

## 7. MIX — balance, sends and inserts

Five channels: **SUB**, **OSC 1**, **OSC 2**, **OSC 3** and the **FX return**.

Each has a fader, a pan, a mute, a solo and a send to the FX bus. The send is
taken before the pan, so panning a source does not move where it sits in the
effects.

### Levels

Every source starts 4 dB below unity, so there is room for modulation before
anything clips. A fader at unity means unity — the trim is on the source, not
hidden inside the fader — and the fader goes 4 dB above unity so you can push a
channel back to its full level.

### Solo

- No solos: everything unmuted is heard.
- Solo a source: only soloed sources are heard.
- With sources soloed, the FX path is heard only if FX is soloed too.
- Muting a channel also kills its send.

### Bus inserts

An **EQ** and a **compressor** can be inserted on the dry bus and the FX bus
independently.

- **EQ** — four bands, with a graph you can drag directly.
- **Compressor** — an 1176-style FET compressor, with a VU meter whose needle
  is driven by an averaging detector rather than swept by a number.

Both can be bypassed per bus.

---

## 8. FX — the eight effects

Eight blocks after the mix. Each has a bypass in its corner; **checked means
on**. Bypassing a block clears it, so switching it back on starts clean instead
of releasing whatever was caught inside it.

**Order matters and you choose it.** Drag the nodes in the signal-flow strip
above the grid. The cards themselves are editors, not ordering controls, so
they can scroll freely while the strip stays put.

### VIBE — analog imperfection

Not an effect on the mix: it runs *inside each voice*, before the sources are
summed, because saturating four signals separately is not the same as
saturating their sum. Every voice drifts at its own rate, so a held chord
thickens rather than wobbling in unison. **AMOUNT** and a **TYPE** menu — Warm,
Hot, Cool, Vintage, Clean, LoFi.

### DELAY

**AMOUNT** (a wire at zero), **TIME**, **FEEDBACK**, a **SYNC** menu for
tempo-locked times, and seven algorithms: Granular, Tape, Analog/BBD,
Ping-Pong, Stereo, Modulated, Diffusion.

### REVERB

**INTENSITY** and four algorithms:

- **ROOM** — nine early reflections per channel into a feedback delay network
- **PLATE** — a full Dattorro plate
- **HALL** — an eight-line feedback delay network
- **CLOUD** — the same network as HALL scaled much longer, for an expansive,
  modulated wash

### MOOD — micro-looper and space

An always-listening looper plus spatial effects. **MIX** balances input against
Mood. **ROUTING** decides what the wet channel is fed: the input, the loop, or
both. **FEEDBACK** recycles material back into the loop and piles it up at the
top. **CLOCK** is Mood's own sample rate — lowering it lengthens the loop, drops
its pitch and slows the wet channel at once, because those are the same thing.

### DOOM — the other ambient engine

A separate engine, not a variant of Mood. One half is a micro-looper that
records while bypassed, so switching it on captures what you *already* played:
**BURST** slices the loop at its onsets and sequences them, **RADIO** scans five
loopers that interfere with each other, **MASK** replaces the loud parts. The
other half is a wet channel: **SOUP** resynthesises what passes through,
**RELAY** repeats without fading, **FLIP** builds harmonies across time.

### LUCY — spectral degradation

Not a bitcrusher. It models what a low-bitrate encoder throws away. **LOSS**
sets how hard and how wide. **STANDARD** keeps the coded signal — darker,
chiming; **INVERSE** plays what STANDARD discarded — brighter, feathery.
**JITTER** models an unstable clock, **PACKETS** a bad connection where losses
cluster, and **FREEZE** is a real spectral freeze, solid or slushy.

### CHORUS

Modelled on the Dimension D: two delay lines modulated in anti-phase, so there
is no audible vibrato and the wet signal cancels exactly when summed to mono.
Nine modes. The dry path is never filtered, so a bass note keeps its weight
while its harmonics move.

### SPREAD

Widens the image using allpass decorrelation rather than delay or polarity
tricks — so it widens a *mono* source, which mid/side gain cannot. Lows stay
mono, mids are decorrelated by phase, highs by level. Four modes: CLASSIC,
WIDE, DEEP, MONO SAFE. The mono sum keeps its level at every setting.

---

## 9. Macros — four performance controls

Four knobs down the left of every panel. Each can move any number of parameters
anywhere in the synth at once — an oscillator detune, a filter cutoff and a
reverb mix together, from one knob.

They are the same four wherever you are. Switch panels and they keep their
values and their assignments.

### Assigning

1. **Cmd-click a Macro knob.** It and its label light teal, and the keyboard says *"Click
   knobs to assign them to MACRO 1"*.
2. **Click any knob** to assign it. Click it again to remove it. The knob does
   not move while you assign — the click assigns, it does not drag.
3. **Switch panels and keep going.** Assignment stays active.
4. **Click the Macro knob** again to finish, or press **Escape**. What you
   clicked is already assigned; Escape does not undo it.

### Reading a knob

| The knob shows | Meaning |
|---|---|
| `MACRO 1` on a pale plate, above the spindle | Macro 1 drives it |
| `M1+` on that plate | several Macros drive it |
| `CC21`, amber, below the spindle | a MIDI control is mapped to it |
| both | both, and they add together |
| solid teal ring | assignable right now |
| dashed amber ring | selected for MIDI Learn |

**Teal is always Macro. Amber is always MIDI.**

### What a Macro does

It does not take a parameter over — it adds to it, the way an LFO does. The
destination knob stays where you set it and its ring shows where the sound
actually is. Turn a Macro to zero and its destinations return to exactly what
their own knobs show.

Macros can be automated by your DAW and mapped to MIDI like any other control.
A Macro cannot drive another Macro.

---

## 10. MIDI Learn — hardware control

Any knob can be driven by a hardware controller. No CC numbers to type.

1. **Shift-click a knob.** A dashed amber ring appears and the keyboard says
   *"Select knobs, then move a MIDI control to assign"*.
2. **Shift-click more knobs** — anywhere, across any panels.
3. **Move the hardware control.** Everything selected is assigned to it, and
   each knob shows its CC.

The controller's full travel sweeps each destination through its own range, so
a cutoff in hertz and a resonance in 0–1 both get a full sweep in their own
units. The movement that *teaches* the mapping does not also jump the knobs.

**To remove:** shift-click a mapped knob. Its assignment is dropped and it joins
the selection, ready for a new one — or press Escape to leave it unmapped.

Mappings are saved in both DAW sessions and presets, and are unique to each
plugin instance. A preset that carries no mappings leaves yours alone.

Notes: any MIDI channel drives a mapping; a CC arriving with nothing selected
only drives existing assignments; note input, mod wheel and pitch bend are
unaffected.

---

## 11. Playing it

**The keyboard** spans the full 88 keys, A0 to C8. Click or drag to play.
Clicked notes use a fixed medium velocity.

**PITCH** springs back to centre when released; double-click to centre it.
**MOD** stays where you leave it; double-click to zero. Pitch bend range is 1
to 24 semitones, default 2, and your host may expose it.

If nothing is making a sound, the keyboard greys out and tells you to engage an
oscillator — that message means every source is switched off, not that anything
is broken.

---

## 12. Presets

Presets carry the sound: oscillators, filters, envelopes, effects, mixer
settings — and also your **Macro assignments and values** and your **MIDI
mappings**, so a patch arrives with its performance controls already wired.

A preset that carries no MIDI mappings leaves the ones you have alone, so
auditioning factory sounds never costs you your controller setup.

Presets are `.px3preset` files. Loading a preset uses exactly the same path
your DAW uses to restore a project, so what you hear is what was saved.

---

## 13. Automation, modulation and macros together

Several things can move one parameter at once, and they do not fight:

```
     what you set with the knob   ← also DAW automation, also a mapped MIDI CC
   + what the LFOs are doing
   + what the envelopes are doing
   + what the Macros are doing
   = what you hear
```

They **add**. Nothing overwrites anything else, and the order does not matter.

The rule that makes this work: **only you, your DAW and a directly mapped MIDI
CC write the parameter itself.** LFOs, envelopes and Macros are layered on top
at the moment the sound is made. That is why a modulated knob does not move
while its ring does — and why automation you record stays exactly what you
recorded.

---

## 14. When something is wrong

**No sound at all.** Check the keyboard: if it is greyed out with a message,
every oscillator is off. Otherwise check MIX for a solo left engaged on a
channel you are not playing, or a muted channel.

**A knob will not move.** You may be in an assignment mode. Press Escape.

**A knob moves on its own.** It is mapped to a MIDI CC (amber `CC` label) or
driven by a Macro (violet `MACRO` label). Shift-click clears a MIDI mapping;
Cmd-click the Macro and click the knob to remove it from that Macro.

**A knob's ring moves but the knob does not.** That is correct — see
[section 13](#13-automation-modulation-and-macros-together).

**An effect sounds like it is still on after bypassing.** It is not; bypass
clears the block. What you hear is a tail from an effect *later* in the chain.

**A preset changed my controller mappings.** A preset only replaces MIDI
mappings if it carries some of its own.

**The sound is thinner than the preset name suggests.** Check whether an
oscillator card was switched off — turning one off removes it from the voice
entirely.
