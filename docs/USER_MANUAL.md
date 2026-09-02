# P(X3) — User Manual

---

## Contents

**Getting started**
[Welcome](#welcome) · [Quick start](#quick-start) · [The interface](#the-interface) · [Signal flow](#signal-flow)

**The panels**
[OSC](#osc--oscillators) · [MOD](#mod--modulation) · [AMP](#amp--amplitude) · [FLT](#flt--filters) · [FX](#fx--effects) · [MIX](#mix--mixer)

**Performance**
[Macros](#macros) · [MIDI Learn](#midi-learn) · [MIDI and Macros together](#midi-and-macros-together) · [Playing](#playing) · [Presets](#presets)

**Reference**
[Sound design walkthroughs](#sound-design-walkthroughs) · [Interaction reference](#interaction-reference) · [Visual indicators](#visual-indicators) · [Standalone and plugin](#standalone-and-plugin) · [Troubleshooting](#troubleshooting) · [Glossary](#glossary)

---

# Welcome

P(X3) is a polyphonic synthesiser. Every note you play is given its own voice,
and every voice contains four sound sources, two filters, an amplitude envelope,
and modulation of its own. Those voices are mixed, sent through eight effects in
an order you choose, and delivered to the output.

Three things shape the way you work with it.

**Twenty oscillator types, not twenty waveforms.** Alongside the familiar sine,
saw, square and triangle there are FM, hard sync, additive, formant, Karplus,
physical modelling, wavetable and several of our own. Each brings its own three
controls, so the same three knobs mean something different in every mode.

**Envelopes you draw.** The amplitude and modulation envelopes are curves you
edit directly — bend a stage, add a point, watch the fill track a note as it
plays — while the familiar four knobs sit beneath the graph, showing and setting
the same shape.

**A performance layer that reaches everywhere.** Five Macros sit on the left of
every panel and can move any number of parameters at once, anywhere in the
instrument. A single hardware knob can drive a Macro, and that Macro can
transform the whole patch.

It runs as a standalone application and as an AU or VST3 plugin.

---

# Quick start

This takes about two minutes and gets you from silence to a sound you have
shaped yourself.

### 1. Make a sound

Open **OSC** and switch on **Oscillator 1** with the power button in the corner
of its card. Play a note on your keyboard, or click the on-screen keyboard along
the bottom of the window.

> **If you hear nothing:** the keyboard greys out and shows a message when every
> source is switched off. That message means exactly what it says — switch on an
> oscillator.

### 2. Choose a character

Set Oscillator 1's **MODE** menu to `SAW` for a bright, buzzy tone, or `FM` for
something metallic. In FM the **PARAM A** and **PARAM B** knobs become RATIO and
INDEX — turn INDEX up and listen to the harmonics build.

### 3. Shape the tone

Open **FLT** and switch on Filter 1. Set its type to `LP24` and pull **CUTOFF**
down. The sound darkens as the filter removes the upper harmonics. Add a little
**RESO** to emphasise the frequencies right at the cutoff point.

### 4. Shape the swell

Open **AMP**. Drag the **ATTACK** handle to the right and the note fades in
instead of starting instantly. Drag **RELEASE** to the right and it rings on
after you let go. The four knobs beneath the graph follow as you drag, and
turning them moves the graph.

### 5. Add space

Open **FX** and switch on **REVERB**. Choose the `HALL` algorithm and bring
**INTENSITY** up. Switch on **DELAY** too, then drag the nodes in the strip above
the cards to change which comes first.

### 6. Keep it

Use the **MENU** button in the top bar to save your patch. The `<` and `>`
buttons step through the library.

---

# The interface

Six buttons across the top switch the main area between panels:

| Panel | Contents |
| --- | --- |
| **OSC** | The three oscillators and the sub oscillator |
| **MOD** | Three LFOs and three modulation envelopes |
| **AMP** | The amplitude envelope |
| **FLT** | The two filters |
| **FX** | Eight effects and their order |
| **MIX** | Levels, pan, sends, solo, and the bus inserts |

Two things stay on screen whatever panel you are viewing.

**The Macro strip**, down the left edge — five knobs that are the same five
everywhere. See [Macros](#macros).

**The keyboard**, across the bottom, with the pitch and mod wheels beside it. It
also carries messages when the instrument has something to tell you.

The top bar holds the preset controls: the current preset's name, `<` and `>` to
step through the library, and **MENU** for saving and browsing.

---

# Signal flow

```
        MIDI / on-screen keyboard
                   │
        ┌──────────┴───────────┐
        │   VOICE (per note)   │
        │  SUB  OSC1 OSC2 OSC3 │
        │           │          │
        │  FILTER 1 → FILTER 2 │
        │           │          │
        │     AMP ENVELOPE     │
        └──────────┬───────────┘
                   │
              DRY BUS ──────────► level, pan, mute, solo
                   │
              FX SEND ──► FX CHAIN ──► FX RETURN
                   │      (your order)     │
                   └──────────┬────────────┘
                          MASTER
                             │
                          OUTPUT
```

Each note gets a voice of its own. Inside it, the four sources are filtered and
shaped by the amplitude envelope, then the voice is summed into the **dry bus**,
where the mixer's level, pan, mute and solo apply.

Each channel also has a **send** into the FX bus. The effects process only what
is sent to them, and their output returns on its own channel with its own level
and pan. The dry bus and the FX return meet at the master.

Because sends are independent, you can push one oscillator deep into the effects
while another stays completely dry.

> **Note:** LFOs and envelopes are modulation sources. They shape other controls;
> they are never mixed into the audio.

---

# OSC — Oscillators

Three oscillator cards and a sub oscillator. Each card has a power button in its
corner. Switching a card off removes it from the voice entirely, including any
filter tail it was ringing, so it stops immediately rather than fading.

## MODE

Selects the oscillator type. This is the most consequential choice on the panel —
it changes not only the waveform but what the three PARAM knobs do.

| | | | |
|---|---|---|---|
| SINE | SAW | SQUARE | TRIANGLE |
| NOISE | PINK NOISE | SUPER SAW | PWM |
| WAVETABLE | ADDITIVE | FORMANT | FM |
| HARD SYNC | KARPLUS | ORGAN | DIGITAL |
| PHYSICAL | ROB | ISAAC | PX3 |

## PARAM A, B and C

**What they do:** Three knobs whose function depends on the selected mode. Their
labels change with the mode, so you can always see what you are holding.

**Use them for:** The character of the raw tone, before any filtering. In most
modes these are the difference between a usable sound and an interesting one.

| Mode | A | B | C |
|---|---|---|---|
| SINE / SAW / SQUARE / TRIANGLE | — | — | — |
| NOISE / PINK NOISE | COLOR | — | — |
| SUPER SAW | SPREAD | — | — |
| PWM | WIDTH | — | — |
| WAVETABLE | POSITION | — | — |
| ADDITIVE | TILT | ODD/EVEN | ROLL |
| FORMANT | MORPH | COLOR | — |
| FM | RATIO | INDEX | — |
| HARD SYNC | SYNC | DRIVE | — |
| KARPLUS | DECAY | BRIGHT | — |
| ORGAN | TONE | CLICK | — |
| DIGITAL | BITS | RATE | — |
| PHYSICAL | DECAY | MATERIAL | — |
| ROB | TRANS | BODY | CHAOS |
| ISAAC | SPREAD | ODD/EVEN | ROLL |
| PX3 | MORPH | CHAR | MOVE |

Several are worth knowing in more detail.

**FM** — RATIO sets the relationship between carrier and modulator. Whole-number
ratios stay harmonic and musical; values between them turn metallic and
bell-like. INDEX sets how much modulation is applied, heard as brightness and
harmonic density.

**SUPER SAW** — SPREAD sets how far apart the stacked saws sit, along with their
drift. Low settings give one fat saw; high settings give the classic wide sound.

**FORMANT** — a **VOWEL** menu appears, selecting the A/E/I/O/U profile. MORPH
moves between vowel shapes and COLOR sets the spectral brightness. Modulate
MORPH for a talking sound.

**KARPLUS** — a plucked-string model. DECAY sets how long the string rings,
BRIGHT how much high end survives each pass.

## WAVETABLE mode

Selecting WAVETABLE turns the card's display into a rotating three-dimensional
view of the table, each frame drawn as a line with the current position picked
out.

**POSITION** (PARAM A) moves through the table. It is a modulation destination,
so an LFO or an envelope can sweep the table while a note is held — the most
characteristic wavetable sound.

You can import your own tables from audio files or images. They appear alongside
the eight factory tables in the same menu.

## Tuning

### COARSE

**What it does:** Transposes the oscillator in semitones, up to two octaves
either way.

**Use it for:** Octaves and intervals — two oscillators a fifth apart, or one an
octave down for weight.

### FINE

**What it does:** Detunes the oscillator in cents, up to a semitone either way.

**Sound:** Small amounts of detune between two oscillators produce a slow beating
that thickens the sound. Larger amounts sound deliberately out of tune.

**Use it for:** Width and thickness. Try +7 cents on Oscillator 2 against
Oscillator 1 left at zero.

### PITCH

**What it does:** A very fine offset, well under a quarter of a semitone,
displayed in semitones.

**How it differs from FINE:** FINE is the tuning control you reach for by hand.
PITCH is a narrow, precise offset intended as a modulation destination — point an
LFO at it for vibrato, or an envelope for a pitch blip at the start of a note.

> **Note:** Oscillator levels are not on this panel. Balance between sources is
> set in [MIX](#mix--mixer), so every level in the instrument lives in one place.

## Sub oscillator

A simple, solid voice beneath the others.

| Control | Function |
| --- | --- |
| **WAVEFORM** | SINE or SQUARE |
| **OCTAVE** | 0, −1 or −2 octaves below the played note |
| **PITCH** | Fine offset, as above |

**Use it for:** Weight under a thin lead, or the fundamental beneath a bass patch
whose main oscillator is doing something more complicated. A sine sub two octaves
down adds body without adding harmonics that fight the filter.

---

# MOD — Modulation

Modulation is what makes a sound move on its own: a filter that opens as the note
sounds, a pitch that wavers, a wavetable that sweeps.

The MOD panel holds **three LFOs** and **three modulation envelopes**. Each is a
*source*, and each is pointed at one *destination*.

## Using a modulation source

1. Choose a **destination** from the source's ASSIGN menu.
2. Set the **AMOUNT**, from −100% to +100%.
3. For an LFO, set its rate and waveform. For an envelope, draw its shape.

**AMOUNT** sets how far the source moves its destination, and its sign sets the
direction. At −100% an LFO that would have opened the filter closes it instead.

> **Note:** Each source has one destination at a time. To move several parameters
> together from a single control, use a [Macro](#macros).

## LFOs

A low-frequency oscillator cycles continuously, whether or not a note is playing.

| Control | Function |
| --- | --- |
| **RATE** | 0.01 Hz to 20 Hz |
| **WAVEFORM** | SINE, TRIANGLE, SAW, SQUARE |
| **ASSIGN** | Destination |
| **AMOUNT** | Depth and direction, −100% to +100% |

**Sound:** A sine or triangle gives smooth movement — vibrato on pitch, a gentle
sweep on cutoff. A square jumps between two values, useful for trills and gated
effects. A saw ramps and resets.

**Use it for:** Vibrato — a sine at 5–6 Hz on PITCH, small amount. Slow evolution
on a pad — a triangle at 0.1 Hz on cutoff. A wobble on a wavetable position.

## Modulation envelopes

An envelope runs once per note, from the moment the key goes down. Where an LFO
repeats, an envelope describes a journey with a beginning and an end.

ENV 1–3 use the same editor as the amplitude envelope — see [AMP](#amp--amplitude)
for the handles, the knobs and the curves. Each has an ASSIGN menu and an AMOUNT
knob of its own.

### Envelope type

A **TYPE** menu beneath the graph chooses how the envelope is built. Both
choices are drawn on the same graph and both support curves; they differ in how
many points the envelope may have, and therefore in whether four knobs can
describe it.

This menu is on ENV 1–3 only. The amplitude envelope is always an ADSR.

| Type | What it is | When to use it |
| --- | --- | --- |
| **ADSR** | The traditional four-stage envelope: attack, decay, sustain, release. Three handles and four knobs. | Almost always. It is quick to set, easy to read, and covers most sounds. |
| **BREAKPOINT** | A free-form envelope of up to 16 points, each with its own time, level and curve. It plays its whole trajectory once and does not hold: the key triggers it, it does not gate it. | Multi-stage swells, rhythmic shapes, anything the four stages cannot say. |

**Switching between them is safe.** Choosing BREAKPOINT starts from exactly the
ADSR shape you were looking at, curves included, and you can then add points.
Choosing ADSR again brings back the ADSR you had — your breakpoint drawing is
kept, and switching back to BREAKPOINT restores it exactly, even after saving
and reloading.

> **Note:** The four knobs are greyed out in BREAKPOINT mode. They stay on
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

**How they differ from the amp envelope:** The amplitude envelope always shapes
the volume of every note; it is not assignable, because it always has the same
job. ENV 1–3 do nothing until you point them at something.

**Use them for:** A filter that opens quickly and settles back — ENV 1 at Filter 1
Cutoff with a fast attack and a medium decay. Or a short pitch blip — ENV 2 at
Oscillator 1 Pitch with a very short decay and a small amount.

## What modulation does to a knob

**A modulated control does not move.** The knob shows the value *you* set — the
one your DAW would automate — and a ring around it shows where the value actually
is as the modulation moves it.

This is deliberate. If modulation drove the knob, it would be writing itself back
into your setting, and you would lose the value you dialled in.

> **Tip:** If a knob's ring is moving but the knob is not, that is modulation
> working correctly.

Modulation is scaled to the room your setting leaves. A source at full amount
arrives exactly at the end of the parameter's range and turns around there,
rather than pushing past it and flattening out.

---

# AMP — Amplitude

The amplitude envelope shapes the volume of every note, from the moment the key
goes down to the moment the sound finally disappears. It is drawn as a curve you
edit directly.

## Always an ADSR

The amplitude envelope is an ADSR: an attack, a decay, a sustain level and a
release. Three handles on the graph, four knobs beneath it, and a curve on every
segment.

It has no TYPE menu, because it has no second type to choose. A **BREAKPOINT**
envelope is a one-shot — it plays its whole trajectory and the voice retires at
the end, whatever the key is doing — which is a modulation shape. As an
*amplitude* envelope it would mean a note whose length the keyboard does not
control, so AMP ENV does not offer it.

**ENV 1–3 do.** See [Modulation envelopes](#modulation-envelopes) for the TYPE
menu and how to draw a free-form shape.

## The handles

There are three handles for four stages, because two of the stages share a point.

### ATTACK

**What it does:** Sets how long the note takes to reach full level.

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

**What it does:** Sets how long the note takes to fade after you let go.

**Sound:** Short values stop the note cleanly. Long values leave a tail that
overlaps the next note.

**How to use it:** Drag left and right along the bottom of the graph. Like
attack, it is a duration and stays pinned.

## The knobs

**ATTACK**, **DECAY**, **SUSTAIN** and **RELEASE** knobs sit beneath the graph.
On AMP ENV they span its whole width, there being no TYPE menu to share the row
with. They and the curve are two views of the same thing: dragging the
graph moves the knobs, and turning a knob moves the graph. They cannot fall out
of step, because neither is a copy of the other.

Turning a knob does not straighten a curve you have drawn.

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

The fill shows the most recently triggered note.

---

# FLT — Filters

Two filters per voice, in series — the second processes the output of the first.
Each has its own power button.

### CUTOFF

**What it does:** Sets the frequency at which the filter starts to act, from
around 80 Hz to 18 kHz.

**Sound:** On a low-pass filter, lower settings remove more of the upper
harmonics and the sound darkens; higher settings let more through and the sound
opens up.

**Use it for:** The most important tone control in subtractive synthesis. Point
an envelope at it for a filter sweep, or a Macro for a performance control.

### RESO

**What it does:** Emphasises the frequencies immediately around the cutoff point.

**Sound:** Low settings are neutral. As you raise it, a peak forms at the cutoff
frequency and the filter takes on a vocal, whistling character. Combined with a
moving cutoff it produces the classic sweep.

### TYPE

| Type | What it does |
| --- | --- |
| **LP12** | Low pass, gentle. Removes highs above the cutoff. |
| **LP24** | Low pass, steep — two stages. Darker and more decisive. |
| **HP12** | High pass, gentle. Removes lows below the cutoff. |
| **HP24** | High pass, steep. |
| **BandPass** | Keeps a band around the cutoff, removing above and below. |
| **Notch** | Removes a band around the cutoff, keeping the rest. |
| **AllPass** | Passes everything, altering phase. Useful in series with another filter. |

**Use them for:** LP24 for basses and anything that should sit low in a mix. HP12
to thin a pad so it leaves room for a bass. BandPass for a narrow, telephone-like
character.

> **Tip:** Two filters in series can do what one cannot. Set Filter 1 to HP12 and
> Filter 2 to LP12 for a band you control from both ends.

---

# FX — Effects

Eight effects process the FX bus. Each has a bypass in its corner — **checked
means on**.

**Order matters, and you choose it.** Drag the nodes in the signal-flow strip
above the cards. The cards themselves are editors, not ordering controls, so they
can scroll freely while the strip stays in view.

> **Note:** Bypassing an effect clears it out. Switching it back on starts clean
> rather than releasing whatever was caught inside when you switched it off.

## VIBE — analogue imperfection

**What it is:** Not an effect on the mix. VIBE runs *inside each voice*, before
the sources are summed, because saturating four signals separately does not
sound like saturating their sum.

**Sound:** Every voice drifts at its own rate, so a held chord thickens rather
than wobbling in unison. The saturation adds harmonics and softens transients.

**Controls:** **AMOUNT**, and a **TYPE** menu — Warm, Hot, Cool, Vintage, Clean,
LoFi.

**Use it for:** Making a digital patch sit more comfortably. A little on a pad
takes off the sterile edge.

## DELAY

**Controls:** **AMOUNT** (a wire at zero), **TIME**, **FEEDBACK**, a **SYNC** menu
for tempo-locked times, and an **ALGO** menu.

| Algorithm | Character |
| --- | --- |
| Granular | Repeats broken into grains |
| Tape | Wow, flutter and high-end loss on each pass |
| Analog/BBD | Dark, compressed bucket-brigade repeats |
| Ping-Pong | Alternating left and right |
| Stereo | Independent times per channel |
| Modulated | Pitch movement on the repeats |
| Diffusion | Smeared, closer to reverb |

**FEEDBACK** sets how much of the output is fed back in — how many repeats you
hear. Each algorithm has its own safe limit.

## REVERB

**Controls:** **INTENSITY** and an **ALGO** menu.

| Algorithm | Character |
| --- | --- |
| **ROOM** | Nine distinct early reflections per channel into a short tail. Small, believable spaces. |
| **PLATE** | A dense, bright plate. Classic on vocals, snares and leads. |
| **HALL** | A long, diffuse tail with damping. Concert-hall scale. |
| **CLOUD** | The hall network stretched much longer, for an expansive modulated wash. |

**Use them for:** ROOM to place a sound without obviously reverberating it. PLATE
for shine. HALL and CLOUD for scale and atmosphere.

## MOOD — micro-looper and space

**What it is:** An always-listening looper paired with spatial effects.

| Control | Function |
| --- | --- |
| **MIX** | Balance between the input and Mood |
| **ROUTING** | What the wet channel is fed: the input, the loop, or both |
| **FEEDBACK** | How much is recycled back into the loop |
| **CLOCK** | Mood's own sample rate |
| **SPREAD** | How much stereo treatment is applied |

**CLOCK** is worth understanding: lowering it lengthens the loop, drops its pitch,
slows the wet channel and narrows the band — all at once, because they are the
same thing.

**Use it for:** Capturing a phrase and letting it decay underneath what you play
next. High FEEDBACK piles material up the way a looper does.

## DOOM — the other ambient engine

A separate engine from Mood. One half is a micro-looper that records *while
bypassed*, so switching it on captures what you already played.

| Mode | What it does |
| --- | --- |
| **BURST** | Slices the loop at its own onsets and sequences them |
| **RADIO** | Scans five loopers that interfere with each other |
| **MASK** | Replaces the loud parts of the loop with something else |

The other half is a wet channel: **SOUP** resynthesises what passes through it,
**RELAY** repeats without fading, **FLIP** builds harmonies and spreads them
across time.

## LUCY — spectral degradation

**What it is:** Not a bitcrusher. LUCY models what a low-bitrate encoder throws
away.

| Control | Function |
| --- | --- |
| **LOSS** | How hard it degrades, and which frequencies it reaches |
| **MODE** | STANDARD keeps the coded signal; INVERSE plays what STANDARD discarded |
| **JITTER** | An unstable clock, in phase and timing |
| **PACKETS** | A bad connection — losses cluster the way they really do |
| **FREEZE** | A spectral freeze, solid or slushy |
| **SPEED** | How fast the loss, packets and freeze evolve |

**Sound:** STANDARD is darker and full of chiming artefacts. INVERSE is brighter,
thinner and feathery — it is playing the difference.

## CHORUS

**What it is:** A stereo chorus modelled on the Dimension D — two delay lines
modulated in anti-phase and summed with opposite polarity.

**Sound:** Because one line goes sharp exactly as the other goes flat, there is no
audible vibrato, just width. The wet signal cancels when summed to mono, so it is
completely mono-safe.

**Controls:** Nine modes, from the softest Dimension setting through combinations
to an ensemble mode and a warmer single-path character.

The dry path is never filtered, so a bass note keeps its weight while its
harmonics move.

## SPREAD

**What it is:** Stereo widening by allpass decorrelation rather than delay or
polarity tricks — which means it widens a *mono* source, something simple
mid/side gain cannot do.

**Sound:** Lows stay mono so the bass end stays solid, mids are decorrelated by
phase, highs by level.

**Controls:** Four modes — CLASSIC, WIDE, DEEP, MONO SAFE.

The mono sum keeps its level and its low end at every setting.

---

# MIX — Mixer

Five channels: **SUB**, **OSC 1**, **OSC 2**, **OSC 3**, and the **FX return**.

| Control | Function |
| --- | --- |
| **Fader** | Channel level |
| **Pan** | Position in the stereo field |
| **Send** | How much of this channel is sent to the effects |
| **Mute** | Silences the channel, and its send with it |
| **Solo** | Hears this channel alone |

## Levels

Every source starts 4 dB below unity, leaving room for modulation before anything
clips. A fader at unity means unity — the trim is on the source, not hidden
inside the fader — and the fader travels 4 dB above unity so you can push a
channel back to its full level.

**How send differs from an effect's mix:** The **send** decides how much of a
channel reaches the effects. Each effect's own **amount** or **mix** decides how
much it does to what it receives. A high send with a low reverb intensity gives a
lot of signal lightly reverberated; the reverse gives a little signal drenched.

The send is taken before the pan, so panning a source does not move where it sits
in the effects.

## Solo

| State | What you hear |
| --- | --- |
| No solos | Everything unmuted |
| A source soloed | Only soloed sources |
| Sources soloed, FX not | The dry path only |
| Sources and FX soloed | Soloed sources through the effects |

Muting a channel also kills its send.

## Bus inserts

An **EQ** and a **compressor** can be inserted on the dry bus and the FX bus
independently, and bypassed per bus.

**EQ** — four bands, with a graph you can drag directly.

**Compressor** — an 1176-style FET compressor, with a VU meter whose needle
follows an averaging detector.

**Use them for:** EQ on the dry bus to carve room for a bass; compression on the
FX bus to even out a reverb tail without touching the dry signal.

---

# Macros

Five knobs down the left of every panel. Each can move any number of parameters
at once, anywhere in the instrument.

They are the same four wherever you are. Switch panels and they keep their values
and their assignments.

## Why use one

A Macro turns several related adjustments into a single gesture. Instead of
reaching for the cutoff, then the resonance, then the reverb, you build one
control that does all three in the proportions you chose:

```
MACRO 1
 ├── Filter 1 Cutoff
 ├── Filter 1 Reso
 ├── Delay Amount
 └── Reverb Intensity
```

Turn that knob up and the patch opens, sharpens and moves back in the room at
once. Turn it down and it closes to a dry, dark version of itself. One knob, and
the patch has two distinct characters with everything in between.

## Assigning parameters

1. **Command-click a Macro knob** — or **double-click** it, which does the same
   thing. It and its label light teal, and the keyboard
   shows *"Click knobs to assign them to MACRO 1"*.
2. **Click any knob** to assign it. Click it again to remove it.
3. **Switch panels and keep going.** Assignment stays active, so one Macro can
   collect an oscillator detune, a filter cutoff, a delay mix and a mixer send in
   a single pass.
4. **Click the Macro knob** again to finish, or press **Escape**.

While assigning, clicking a knob assigns it — it does not move it. Your settings
are safe while you work.

Everything you clicked is already assigned; Escape leaves the mode without
undoing it.

## What a Macro does to a parameter

A Macro does not take a parameter over. It **adds** to it, the way an LFO does.

The destination knob stays where you set it, and its ring shows where the value
actually is. Turn the Macro to zero and every destination returns to exactly what
its own knob shows.

This is what allows a parameter to be moved by its own knob, your DAW's
automation, a MIDI controller, an LFO, an envelope and more than one Macro at the
same time, with all of them contributing rather than overwriting each other.

**How Macro assignment differs from modulation:** A modulation source moves on
its own — it cycles, or it runs when a note starts. A Macro moves only when you
move it. Both add to the parameter in the same way.

## Reading a knob

| The knob shows | Meaning |
| --- | --- |
| `MACRO 1` on a pale plate above the spindle | One Macro drives it |
| `M1+` on that plate | Several Macros drive it; the first is named |
| A solid teal ring | Assignable right now, in the active Macro mode |

## MIDI control of Macros

A Macro is itself a control, so it can be mapped to a hardware knob exactly like
any other — see [MIDI Learn](#midi-learn). Macros can also be automated by your
DAW.

## What is remembered

Macro assignments **and** Macro values are saved in presets and in DAW projects,
so a patch arrives with the performance controls it was designed around.

Loading a preset does not change which hardware knob drives a Macro. The preset
says what the Macro *does*; your instance says what *moves* it.

> **Note:** There are five Macros, and a Macro cannot drive another Macro.

---

# MIDI Learn

Any knob in the instrument can be driven by a hardware controller. There is no CC
number to type and no dialog to open.

## Assigning

1. **Shift-click a knob.** A dashed amber ring appears and the keyboard shows
   *"Select knobs, then move a MIDI control to assign"*.
2. **Shift-click more knobs** if you want several on one control — anywhere, on
   any panel.
3. **Move the hardware control.** Everything selected is assigned to it, and each
   knob shows its CC number.

The controller's full travel sweeps each destination through its own range, so a
cutoff in hertz and a resonance in 0–1 both get a complete sweep in their own
units.

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
- A control change reaches its parameter on the next interface update. For a knob
  gesture this is imperceptible; it is not a sample-accurate modulation path, by
  design.
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

1. Command-click **Macro 1** and click the parameters you want it to move. Press
   Escape.
2. Shift-click the **Macro 1 knob** itself.
3. Move the hardware control you want to use.

That hardware knob now drives Macro 1, and Macro 1 drives everything you assigned
to it.

### Why prefer this to mapping everything directly

Mapping one CC to four parameters directly gives all four the same full sweep,
whether that suits them or not, and changing your mind means re-learning all
four. Through a Macro, the set of destinations is part of the patch — it travels
with the preset — while the hardware mapping stays with your studio. Change preset
and the same knob does whatever the new patch's Macro 1 was designed to do.

> **Note:** Direct mappings and Macro assignments coexist. A parameter can be
> mapped to CC 22 *and* be a destination of Macro 1. The CC moves where the
> parameter sits; the Macro moves it from there.

---

# Playing

## The keyboard

The on-screen keyboard spans the full 88 keys, A0 to C8. Click or drag across it
to play. Clicked notes use a fixed medium velocity; play from a MIDI keyboard for
velocity response.

## Pitch and mod wheels

**PITCH** springs back to centre when released. Double-click to centre it. The
bend range is 1 to 24 semitones, 2 by default, and your host may expose it as a
parameter.

**MOD** stays where you leave it. Double-click to return it to zero.

## Messages

The keyboard shows a message when the instrument has something to tell you — that
every oscillator is off, or that you are in an assignment mode. The keyboard stays
playable while you assign, so you can hear what you are building.

---

# Presets

A preset is a complete patch. Use the top bar to move through the library:

| Control | Function |
| --- | --- |
| `<` and `>` | Step to the previous or next preset |
| Preset name | Shows what is loaded |
| **MENU** | Save, browse and manage |

Presets are `.px3preset` files. Loading one uses the same path your DAW uses to
restore a project, so what you hear is what was saved.

## What travels where

| | Saved in a preset | Saved in a DAW project |
| --- | --- | --- |
| Oscillators, filters, envelopes, effects, mixer | ● | ● |
| Effect order | ● | ● |
| Macro assignments and values | ● | ● |
| MIDI mappings | ● | ● |
| Which panel you were viewing | | ● |
| The name of the loaded preset | | ● |

A preset that carries no MIDI mappings leaves your existing ones untouched. A DAW
project is the complete state of that instance and restores exactly what was
saved, including having no mappings at all.

Projects and presets saved before Macros existed load correctly, with four empty
Macros.

---

# Sound design walkthroughs

## Your first patch

1. **OSC** — switch on Oscillator 1, set MODE to `SAW`.
2. **FLT** — switch on Filter 1, set `LP24`, CUTOFF about a third of the way up,
   RESO low.
3. **AMP** — a short attack, a medium decay, sustain around three-quarters, a
   medium release.
4. **MOD** — set ENV 1's ASSIGN to Filter 1 Cutoff, AMOUNT around +40%, and give
   it a fast attack with a medium decay.

Each note now opens the filter and lets it settle. This is the foundation of most
subtractive sounds.

## A bass

1. **OSC** — Oscillator 1 to `SAW`. Switch on the **sub oscillator**, SINE, −1
   OCT.
2. **MIX** — bring the sub up until you feel it without hearing it separately.
3. **FLT** — `LP24`, cutoff low. Basses live below the rest of the mix.
4. **AMP** — attack at minimum, short decay, sustain around half, short release. A
   bass should stop when you stop.
5. **MOD** — ENV 1 at Filter 1 Cutoff, fast attack, short decay, around +30%. That
   is the pluck.

> **Tip:** Keep RESO modest on a bass. High resonance at a low cutoff can produce
> more level at the peak than the rest of the patch.

## A lead

1. **OSC** — Oscillator 1 to `FM`, RATIO at a whole-number setting, INDEX moderate.
   Switch on Oscillator 2 as a `SAW` with FINE at +7 cents.
2. **FLT** — `LP12`, cutoff fairly open. A lead should be bright.
3. **AMP** — short attack, high sustain, medium release.
4. **MOD** — LFO 1, SINE, around 5.5 Hz, ASSIGN to Oscillator 1 Pitch, AMOUNT
   small. That is vibrato.
5. **FX** — a little DELAY, tempo-synced.

## A pad

1. **OSC** — all three oscillators. Oscillator 1 `SAW`, Oscillator 2 `SAW` with
   FINE at −8 cents, Oscillator 3 `WAVETABLE`.
2. **AMP** — long attack, long release, high sustain. A pad arrives slowly and
   leaves slowly.
3. **MOD** — LFO 1 slow, around 0.1 Hz, TRIANGLE, assigned to Oscillator 3's
   wavetable POSITION. The pad now evolves while it is held.
4. **FX** — REVERB on `HALL`, generous. CHORUS for width. SPREAD if you want it
   wider still.

> **Tip:** With long attacks, add a little VIBE. The per-voice drift keeps a held
> chord from sounding static.

## A performance Macro

Starting from the pad above:

1. Command-click **Macro 1**.
2. Click **Filter 1 Cutoff**, then **Filter 1 Reso**.
3. Switch to **FX** and click **Reverb Intensity** and **Delay Amount**.
4. Switch to **MIX** and click Oscillator 3's **send**.
5. Click the Macro 1 knob to finish.

Macro 1 now takes the patch from closed and dry to open and enormous. Shift-click
the Macro 1 knob, move a hardware knob, and that transformation is under your
hand.

---

# Interaction reference

| Action | Result |
| --- | --- |
| Click and drag a knob | Adjust its value |
| Drag in an envelope graph | Move a handle or bend a segment |
| **Shift + drag** in an envelope graph | Fine adjustment |
| Arrow keys, with a point selected | Nudge it in time or level |
| **Shift** + arrow keys | Nudge it more finely |
| **Delete** or **Backspace**, with a point selected | Remove it |
| **Shift + click** a knob | Select it for MIDI Learn |
| **Command + click** a Macro knob | Enter Macro assignment for that Macro |
| Click a knob during Macro assignment | Assign or unassign it |
| Click the active Macro knob | Leave Macro assignment |
| **Escape** | Leave any assignment mode |
| Double-click empty envelope space | Add a point (ENV 1–3, BREAKPOINT mode) |
| Double-click an envelope point | Remove it (BREAKPOINT mode) |
| Double-click a curve handle | Straighten that segment |
| Double-click the pitch wheel | Return it to centre |
| Double-click the mod wheel | Return it to zero |
| Drag a node in the FX strip | Reorder the effects |
| Click an effect's corner button | Bypass or enable it |
| Click an effect card's background | Bypass or enable it |

Only one assignment mode is active at a time. Starting a MIDI selection leaves
Macro assignment, and entering Macro assignment clears a MIDI selection.

---

# Visual indicators

| Indicator | Meaning |
| --- | --- |
| A moving ring around a knob | Something is modulating this parameter |
| `MACRO 1` on a pale plate | A Macro drives this parameter |
| `M1+` on a pale plate | Several Macros drive it |
| `CC21` in amber | A MIDI control is mapped to this parameter |
| Solid teal ring | Assignable in the active Macro mode |
| Dashed amber ring | Selected for MIDI Learn |
| Teal highlight on the Macro strip | This Macro is being assigned |
| A greyed card | Bypassed |
| A greyed keyboard with a message | No oscillator is switched on |

**Teal is always Macro. Amber is always MIDI.** Purple belongs to the LFOs and
envelopes, and means modulation.

---

# Standalone and plugin

The instrument is identical in both. The differences are in the surrounding
environment.

| | Standalone | Plugin |
| --- | --- | --- |
| MIDI input | Chosen in the application's audio settings | Routed by your DAW |
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
- **Check AMP.** A sustain at zero with a short decay means the note is gone
  before you hear it.
- **In a DAW**, check the track is receiving MIDI and is not muted.

### A knob will not move

You are probably in an assignment mode — the keyboard will say so. Press
**Escape**.

### A knob moves on its own

It is mapped or assigned, and its label tells you which. An amber `CC` label means
a MIDI control; shift-click to clear it. A pale `MACRO` plate means a Macro;
command-click that Macro and click the knob to remove it.

### A knob's ring moves but the knob does not

That is correct. Modulation, Macros and mapped controls move the *value*; the knob
keeps showing what you set. See
[What modulation does to a knob](#what-modulation-does-to-a-knob).

### A Macro does not seem to do anything

- Check the destination is actually assigned — it will show a `MACRO` plate.
- Check the Macro itself is moving. If a hardware knob drives it, confirm that
  mapping is still there.
- Check the destination is not already at the end of its range, where there is
  nowhere left for the Macro to take it.

### A MIDI controller does not respond

- Confirm the device is connected and selected — in the standalone's audio
  settings, or in your DAW's track input.
- Confirm the control sends CC rather than notes.
- Confirm you are not in an assignment mode, which changes what a movement does.
- Confirm the mapping survived — a mapped knob shows its CC.

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

**Amount** — How far a modulation source moves its destination, and in which
direction.

**Assignment** — Connecting a Macro to a parameter, or a modulation source to a
destination.

**Bypass** — Switching a section out of the signal path.

**CC** — Control Change: the MIDI message a hardware knob or slider sends.

**Cutoff** — The frequency at which a filter begins to act.

**Destination** — A parameter that a modulation source or a Macro moves.

**Envelope** — A contour that runs once per note. The amplitude envelope shapes
volume; the modulation envelopes shape whatever you assign them to.

**LFO** — Low Frequency Oscillator: a cycling modulation source, generally below
the range of hearing.

**Macro** — One of four performance controls, each able to move any number of
parameters at once.

**MIDI Learn** — Assigning a hardware control by moving it, rather than by
entering a number.

**Modulation** — Automatic movement of a parameter by an LFO or an envelope.

**Oscillator** — The source of the raw tone.

**Preset** — A saved patch.

**Resonance** — Emphasis of the frequencies around a filter's cutoff.

**Return** — The channel on which the effects come back into the mix.

**Send** — How much of a channel is fed to the effects.

**Solo** — Hearing one channel alone.

**Sub oscillator** — A simple additional source, generally an octave or two below
the played note, used for weight.

**Sustain** — The level a note holds at while the key is down.

**Voice** — Everything that produces one note: sources, filters and envelopes.

**Wavetable** — A collection of waveforms that can be swept through while a note
sounds.
