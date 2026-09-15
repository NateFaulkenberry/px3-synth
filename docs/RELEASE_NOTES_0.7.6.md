# PX3 v0.7.6

## Hear Your Effects Again

PX3 v0.7.6 is a fix release.

The headline: **in Logic, and in any host that enables the Synth's second output pair, the effects could not be heard.** The FX panel showed every effect switched on and routed, and turning their knobs changed nothing you could hear. This release puts the effects back on your track.

It also makes LUCY audible the moment you switch it on, stops keys from getting stuck lit and animating, and starts INIT with every effect off.

---

## 🔊 Effects Are Back on Outputs 1/2

### What was wrong

Since v0.7.1, the Synth has offered a second stereo output pair so the dry sound and the effects could be recorded as separate stems. As soon as a host enabled that pair, the Synth split its mix: **dry on outputs 1/2, every effect on outputs 3/4.**

Logic enables every output an instrument offers, on every instance. So every PX3 Synth in Logic split its mix, and a normal stereo instrument track — which only listens to outputs 1/2 — heard the dry sound alone. The effects were running; they were going somewhere the track never listens.

The Synth's standalone app was not affected.

### What it does now

* **Outputs 1/2 always carry the full mix** — dry and effects — in every host, however many outputs the host has enabled.
* **Separate FX Output** is a new switch in **SETTINGS**, off by default. Turn it on to get the old split back: dry on 1/2, effects on 3/4. In Logic, create the instrument as **Multi-Output (2xStereo)** and add the extra channel strip.
* Switching it while notes play **crossfades** rather than clicking.
* The setting is saved with your session.

Sessions saved with earlier versions load with Separate FX Output **off**, which is what brings their effects back.

---

## 🟣 LUCY Is Heard When You Switch It On

LUCY used to start switched on with **GLOBAL** at zero — an effect that looked active and did nothing until you found a second control.

* In the Synth, LUCY now starts **off**, with **GLOBAL halfway up**. Switch it on and you hear it straight away.
* The standalone **PX3 Lucy** still processes the moment you insert it, now with GLOBAL halfway up so that it actually does something.

Patches and sessions keep their saved LUCY settings. Because LUCY now starts off, a fresh instance and anything saved before LUCY existed sound exactly as they did.

---

## 🎹 No More Stuck Keys

A key on the on-screen keyboard could stay lit and animating — sometimes with its note still sounding — after you let go. Four separate causes are fixed:

* **Repeated notes.** A second note-on for a key that was already held — from MIDI thru, overlapping recorded notes, or a hardware key and a mouse click on the same note — needed two note-offs to clear, even though the sound had already stopped at the first. One note-off now clears the key.
* **A busy keyboard.** When the host was not running audio and you kept playing, the on-screen keyboard could drop the note-off that releases a key. Note-offs are never dropped now.
* **Bypassing every oscillator** while holding a key forgot the key without releasing its note.
* **A lost mouse release** — the window losing focus mid-press, or a host shortcut taking the click — left the key down. The keyboard now notices the button is no longer pressed and releases it, and closing the plugin window releases any held key.

---

## 🎛️ INIT Starts With Every Effect Off

INIT now switches all eight effects off: VIBE, DELAY, REVERB, MOOD, DOOM, LUCY, CHORUS and SPREAD.

Previously INIT switched three effects on and left the other five alone, so loading INIT kept whatever effects the previous patch had turned on. It is now a genuinely clean starting point.

---

## 🎚️ One Coarse and One Fine Tune per Oscillator

Each oscillator used to carry three overlapping tuning controls: COARSE in semitones, FINE in cents up to a semitone, and a separate FINE TUNE of a quarter semitone. The sub had its own octave menu and yet another fine offset. That made it hard to tell which control had set an oscillator's pitch.

* **Every oscillator, sub included, now has two tuning knobs side by side: COARSE and FINE.**
* **COARSE** moves in whole octaves, −2 to +2.
* **FINE** moves in cents, −24 to +24.
* Both show their value in their own units, such as `-1 oct` and `+7 ct`.
* The sub's OCTAVE menu is gone. Its COARSE knob starts at −1 octave.
* **Pitch Mod** is still in every ASSIGN menu for LFOs and envelopes. It now moves pitch only through modulation, never from a value stored in a patch.

---

## ✂️ KARPLUS Removed

The KARPLUS plucked-string mode has been removed from the Synth for now. The factory preset **Porcelain** now uses FM.

---

## 🌊 Rebuilt Oscillators

Every oscillator mode has been rebuilt on band-limited, sample-rate-independent DSP. The design and its measurements are in `docs/OSCILLATOR_DSP_DESIGN.md`.

### Cleaner where it should be

* **SAW, SQUARE, TRIANGLE, PWM and SUPER SAW** are band-limited. At C5 a saw's aliasing below 15 kHz drops from −19 dB to −62 dB against its fundamental.
* **HARD SYNC** restarts at the exact instant the master wraps rather than at the next sample, and the restart is band-limited. At C5 and a ratio of 2.4 its aliasing drops from −7 dB to −52 dB.
* **FM** runs at twice the sample rate internally, so its sidebands no longer fold back into the audible range until they reach extreme settings.
* **The soft clipping every oscillator passes through** is anti-aliased. It is 15 dB cleaner and costs less CPU than before.
* **DIGITAL keeps its grit.** Its aliasing is the sound of the mode and is left in on purpose; it now sounds the same at every sample rate.

### Fixed

* **The mod wheel was a hidden volume control.** It turned every oscillator down: 15% with the wheel at rest, 37% at full. It now adds vibrato, and moves the width in PWM, and nothing else.
* **Oscillators started 120° apart, and the sub at a fixed phase.** All four now start together. Stacking the same waveform at the same pitch now adds up instead of partly cancelling; use FINE to thicken a stack.
* **ORGAN's 16′ and 5⅓′ drawbars** were not a sub-octave and a fifth. They are now.
* **FORMANT's MORPH** jumped between vowels at every quarter of the knob. It now glides from the selected vowel through the others in turn.
* **ADDITIVE's ROLL did nothing.** It now rolls harmonics away from the top or the bottom of the series.
* **ISAAC and ROB** used pitch relationships that jumped once per cycle. Their stretched partials are now truly inharmonic and continuous.
* **ROB** added noise at CHAOS zero. At zero there is now none.
* **PHYSICAL** detuned the note as MATERIAL moved, and DECAY changed level rather than ring time. MATERIAL now spreads only the upper modes, and DECAY is how long the strike rings.
* **Hidden knobs** could change SUPER SAW's and WAVETABLE's level. A knob a mode doesn't show no longer affects it.
* **Every voice played identical noise.** Each oscillator of each note now has its own stream, and rendering the same notes twice gives the same result.
* **Several modes changed character with the sample rate:** noise colour, organ key click, DIGITAL's hold, and ROB's and PHYSICAL's envelopes. Pitch-bend and mod-wheel smoothing did too. All now behave the same at 44.1, 48, 88.2 and 96 kHz.
* **Changing oscillator mode while a note plays** crossfades instead of clicking.

### Renamed knobs

* FORMANT: COLOR is now **SHIFT**.
* ISAAC: SPREAD and ROLL are now **TILT** and **STRETCH**.

---

## ♿ Accessibility

The switch chips on the DOOM and LUCY cards always showed the right caption, but VoiceOver could read the opposite state — "WET OFF" on a switch that was on. What VoiceOver reads now always matches what the chip shows.

---

## 🧪 Quality & Reliability

PX3 now has **1,536 automated assertions**, up from 1,481 in v0.7.5.

New coverage includes:

* A regression test for every oscillator bug fixed in this release, plus measured limits on aliasing, pitch accuracy (within 0.1 cent from C2 to C9 at every sample rate), DC and sample-rate consistency.
* The Logic case itself: with every output enabled, effects are heard on outputs 1/2 and outputs 3/4 stay silent.
* Separate FX Output: off by default, saved with the session, off for older sessions, and click-free to switch.
* LUCY's new defaults, and switching LUCY on being audible straight away.
* A key never left held: repeated note-ons, a full keyboard queue, bypassing the oscillators, a lost mouse release, and releasing twice.
* INIT switching all eight effects off, even after a patch that had them all on.
* Switch chips reporting the same state they show.

---

## ⚠️ Known Limitations

* **MOOD's DELAY mode with MODIFY at full never fades.** At the top of that knob the repeats are held at full level on purpose, so a note can keep sounding after you release it. Switching MOOD off stops it immediately.
* On narrower windows, the switch rows on the **DOOM** and **LUCY** cards can be wider than the card, hiding some switches.
* A second updater helper can still be launched across separate sessions in a specific staged-update scenario.
* The Synth's artwork assets are relatively large, contributing to the overall application size.
* **Windows standalone packaging is not yet available.**

---

## Known Behavior Changes

* **Outputs 3/4 are silent unless Separate FX Output is on.** If you were recording dry and effects as separate stems, turn it on in SETTINGS.
* **LUCY starts switched off in new Synth patches**, with GLOBAL halfway up.
* **INIT has every effect off.**
* **Tuning is now COARSE in octaves and FINE in cents** on every oscillator and the sub. Patches and sessions from earlier versions do not carry their tuning over.
* **KARPLUS is no longer an oscillator mode.**
* **Oscillators sound different.** SAW, SQUARE, PWM, SYNC and FM are cleaner at the top of the keyboard. PHYSICAL and PX3 were redesigned around what their knobs say. Same-pitch stacks add up in phase. Existing patches will not sound exactly as before.
* **The standalone PX3 Lucy starts with GLOBAL halfway up**, so it changes the sound as soon as it is inserted.

---

# What's New at a Glance

### ✨ New

* Separate FX Output switch in SETTINGS
* COARSE and FINE tuning knobs on every oscillator and the sub
* Band-limited, sample-rate-independent oscillators

### 🔧 Improved

* LUCY is audible as soon as it is switched on
* INIT starts with every effect off
* VoiceOver reads the true state of every switch chip

### 🐛 Fixed

* The mod wheel lowering every oscillator's volume
* Oscillators starting out of phase; ORGAN's sub and fifth drawbars; FORMANT's vowel morph; ADDITIVE's ROLL
* Effects inaudible in Logic and other hosts that enable the second output pair
* Keys left lit, animating or sounding after release
* INIT keeping effects from the previous patch

---

**PX3 v0.7.6 puts the effects back where you can hear them.**
