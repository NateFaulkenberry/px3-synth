# PX3 v0.8.0

## One Window, a Patch Bay, and a Lot More Synth

PX3 v0.8.0 rebuilds the Synth's interface into a single, dense modular window and adds a large set of new sound-design features: new filter models, keyboard tracking, tempo-synced LFOs and envelopes, a sixth macro, new effects, and a modulation patch bay that can route any source to any destination.

> **Presets and sessions from v0.7 do not load in v0.8.** The parameter set was rebuilt from the ground up for this release. The factory library is reinstalled automatically.

---

## 🧩 The New Interface

* **Everything in one window.** VOICE shows the oscillators, filters and AMP envelope side by side, with the LFOs and envelopes in a row beneath them. MOD, FX and MIX each have a page of their own. No feature opens a separate window.
* **A dense, modular look.** Square modules with a coloured identity stripe, uniform 1-pixel seams, and the same title band, power button and controls on every card. LFOs are purple, envelopes yellow, the AMP envelope green.
* **The keyboard and wheels are back**, as one panel along the bottom, finished with a keyboard-case end at each side. A played key sits pressed in and lit. The PITCH and MOD wheels are drawn as hardware wheels: ribbed cylinders in a recessed slot that roll as you move them, with a stripe that marks the position.
* **Flat, modern controls.** Mixer switches (M / S / Ø, EQ / COMP) are flat keys with the name on the key. Level meters are clean bar meters coloured green, amber and red. The preset browser, the EQ and COMP sheets and SETTINGS all share the same panel style, and the preset, EQ and COMP sheets can be dragged by their title bar.
* **A refreshed COMP.** Theme switch keys for RATIO, the meter mode and LINK, consistent labels, and a dark display-style VU meter.
* **Knob readouts** show at most two decimal places.
* **Resizable** from 1100 × 700 up to 2400 × 1400, and laid out from a data-driven scene, so every module scales cleanly.

## 🔌 Modulation Patch Bay

* **Any source to any destination.** Drag from a source jack (LFO 1–3, ENV 1–3, M1–M6) onto any knob to create a route. Up to 64 routes, each with its own depth, polarity (native, unipolar, bipolar) and response curve.
* **Coloured rings** on every modulated knob show where its value is going.
* **The MOD page** lists every route with its depth control, polarity, curve and a delete X, next to a searchable destination browser.
* **Envelopes now modulate each voice on its own.** An envelope routed to the filter or the oscillators follows each note's own contour instead of one average across the chord.

## 🎛️ Six Macros

* **MACRO 6** joins the macro strip.
* **Depth panel**: each macro's assignments are listed with an amount slider and an X to remove them.

## 🔊 New Sound

**Filters**
* **New models:** a zero-delay-feedback **ladder**, a **Curtis**-style OTA cascade and an aggressive **ARP**-style 2-pole, plus a COMB filter with tuning, decay, damping, dispersion, drive and mix.
* **Real resonance:** up to Q 20, whistling or self-oscillating depending on the model, always bounded.
* **Keyboard tracking** from −100% to +200%, around a reference key.
* **Series or parallel** routing with a balance control.

**Oscillators**
* **SLOP**: per-voice analog drift, up to ±12 cents, independent for every voice and oscillator.
* **Sub oscillator SAW**, band-limited like the main oscillators, plus semitone tuning.
* **DIGITAL** has independent BITS, RATE and a real sine wavefolder on FOLD.
* **PHYSICAL MATERIAL** runs wood → glass → metal as timbre, without moving pitch.
* **Supersaw DETUNE** no longer loses level; **hard sync** drive goes to 10×; the **ORGAN** key click is clearly audible.
* **Custom wavetables**: import your own from each oscillator's wavetable menu.

**Modulation sources**
* **LFOs**: sample-and-hold and smooth-random shapes, each LFO with its own random series; FREE, TEMPO and TRANSPORT clocks; an external **MIDI clock** is followed when the host gives no tempo.
* **Envelopes**: LOOP, keyboard tracking (KEY) and tempo SYNC.

**Effects**
* **DRIVE**: a new distortion stage with SOFT, HARD and ASYM clippers, TIGHT, TONE and automatic level matching. It runs 8× oversampled with anti-derivative anti-aliasing (4× at 96 kHz), with aliasing below −66 dB even at full HARD drive on a 5 kHz tone, and its dry signal is phase-aligned, so MIX never combs.
* **Uni-Vibe**: a real four-stage photocell phaser at VIBE's place in the chain.
* **Chorus**: Juno-style modes I, II and I+II.
* **Delay**: tape WOBBLE, QUALITY and SLIP, and a MOD DEPTH control.
* **Reverb**: SHIMMER on CLOUD, and an **IR** mode that loads your own impulse responses.
* **Stereo Spread** now works on the whole instrument (it is on the master bus).
* **Doom** is clean by default, and its GLUE no longer changes the level.

## 🧠 Presets and State

* **INIT** is the default state rather than a preset. It cannot be overwritten, favourited or exported, and appears only under All. Both filters start on AllPass and DRIVE starts bypassed.
* **Unsaved changes are protected.** Switching presets with unsaved edits asks first: SAVE, DON'T SAVE or CANCEL.
* **A freshly loaded preset is no longer marked as edited** (*) straight away.
* **Preset browser**: one LOAD PRESET button, a search field and category and source filters.
* **Saving an unedited project reproduces it exactly**, so hosts no longer mark a reopened project as changed.

## ⚡ Performance

* **Audio CPU is on par with v0.7.6** (within ±3% in every measured scenario at 48 kHz / 512) **with zero real-time allocations**, despite the new features.
* **The editor is far lighter**: about 4% of one CPU core while idle, where v0.7.6 redrew the whole window 30 times a second. With animations on, the animated displays repaint only themselves.
* **Sheets open instantly.** The blurred backdrop behind the preset, EQ and COMP sheets used to take seconds on a Retina display.
* **Envelope modulation inside each voice is updated every 32 samples**, so a fast filter pluck sounds the same at any host buffer size.

## 🐞 Fixes Worth Knowing

* Dropdowns no longer ignore a click when the screen refreshes at the same moment.
* A bypassed module disables all of its controls, including filter key tracking, comb, the ENV card's mode, loop, sync, key and curve, and oscillator SLOP.
* Modulating DRY LEVEL, DRY PAN or filter KEY now changes the sound (the route showed on the knob but did nothing).
* An envelope modulating an oscillator's macros no longer makes them wobble every block.
* Smooth-random LFOs no longer jump once a cycle.
* Modulating delay WOBBLE / MOD DEPTH or the compressor's INPUT, OUTPUT and MIX no longer clicks.
* Shimmer and DRIVE sound the same at 44.1, 48 and 96 kHz.
* MENU › Settings works in every build, and the version number is shown in the menu.
* The master output can no longer exceed full scale: the reverb's level matching used to act after the output ceiling.
* Host automation of the AMP envelope's enable is no longer overridden while the editor is open.

## Known Limits

* The CLOUD reverb's level is about 1.7 dB higher at 96 kHz than at 48 kHz.
