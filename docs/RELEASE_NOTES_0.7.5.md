# PX3 v0.7.5

## Modulation That Moves

PX3 v0.7.5 is a modulation and filter release.

The headline fix: **an LFO at 100% now does what 100% says.** Until now, a full-strength LFO could barely move a control that sat near either end of its range, which made modulation feel timid exactly where it matters most — on a filter cutoff at its default, for example.

Around that fix, this release adds one-shot **timed ramps**, **KEY SYNC**, continuous **Pitch Mod** destinations, envelope times up to **40 seconds**, and a choice of **SERIES or PARALLEL** routing for the two filters.

Your existing sessions and presets are carried forward: saved envelope times and LFO shapes load exactly as they were saved.

---

## 🌀 Full-Range Modulation

### What was wrong

An LFO's swing used to be limited to the room on the *nearer* side of the knob. A control in the middle of its range could swing freely, but one near either end hardly moved at all — even at 100%.

Filter cutoff was the clearest casualty. At its 12 kHz default, a 100% LFO swept only **7.5 kHz to 18 kHz — about two-thirds of an octave**, all of it above where a low-pass filter is really audible.

Nothing in the signal path was turning the modulation down. The rule itself was.

### What it does now

* **100% covers the whole range.** An LFO swings half the range each way from where the knob sits.
* **It folds back instead of flattening.** Where a swing would pass the end of a range, it turns around and keeps moving rather than sitting flat against the limit.
* **The same rule everywhere.** Every destination behaves the same way; none is quietly scaled down.

That same LFO on the default cutoff now sweeps **1.1 kHz to 17.8 kHz — four full octaves.**

Envelopes and Macros are unchanged: they already reached the end of the range in the direction their amount points.

---

## 📈 Timed Ramps

Two new LFO shapes: **RAMP UP** and **RAMP DOWN**.

A ramp is not a cycle. It travels once from one end of its swing to the other over a time you set, then holds there.

* **TIME** runs from **0.05 s to 60 s**, and takes the place of RATE on the card while a ramp is selected.
* The wave graph draws the ramp as the one-shot it is: travel, then hold.
* A ramp's length is real time — **identical at every sample rate and buffer size.**

Use one for a filter that opens across twenty seconds, or a wavetable that drifts from one end to the other over a long pad.

---

## 🎹 KEY SYNC

Every LFO now has a **KEY SYNC** switch.

| KEY SYNC | Cyclic shapes | Ramps |
| --- | --- | --- |
| **Off** (default) | Run freely, exactly as before | Restart on the first note after all keys are released — legato notes ride the same ramp |
| **On** | Restart from the top of the cycle on every new note | Restart on every new note |

PX3's LFOs are shared by every voice, so a restart is **global**: the newest note restarts the LFO for everything that is sounding. A note-off never restarts anything, and a restart lands within one audio buffer of the note.

The envelopes don't get a KEY SYNC switch, because they don't need one — every envelope already starts from the beginning with each note.

---

## 🎵 Pitch Mod & Fine Tune

### New: Pitch Mod destinations

**Osc 1 Pitch Mod**, **Osc 2 Pitch Mod**, **Osc 3 Pitch Mod** and **Sub Osc Pitch Mod** are new modulation destinations in every ASSIGN menu.

* Continuous, so an LFO on them is smooth vibrato rather than a staircase of semitones.
* ±24 semitones at 100%, so envelopes can make real pitch sweeps.
* Musical vibrato lives at small amounts — around **2% is ±half a semitone.**

### Renamed: PITCH is now FINE TUNE

The oscillator and sub knob labelled **PITCH** is now **FINE TUNE**, which is what its ±0.24-semitone range always was. It was never wide enough to hear as pitch modulation.

This is a name change only. Automation and sessions that use it are unaffected.

---

## ⏳ Longer Envelopes

Attack, decay and release now reach **40 seconds** on the amplitude envelope and all three modulation envelopes — up from 3, 4 and 5 seconds.

The knobs are weighted towards the short end, so fast, percussive settings are as precise as before:

| Knob position | Time |
| --- | --- |
| 25% | ≈ 25 ms |
| 50% | 1 s |
| 75% | ≈ 8.6 s |
| 100% | 40 s |

The envelope editor's handles can now be dragged out to the full 40 seconds too.

### New defaults

| Envelope | Attack | Decay | Sustain | Release |
| --- | --- | --- | --- | --- |
| AMP ENV | 15 ms | 300 ms | 0.8 | 500 ms |
| ENV 1–3 | 250 ms | 600 ms | 0.7 | 1 s |

Immediate without clicking, and a release that tails off rather than stops. The modulation envelopes default slower, because a modulation envelope is usually a sweep.

Presets and sessions keep the times they were saved with.

---

## 🎚️ Series or Parallel Filters

A new strip above the two filter cards sets how they connect.

* **SERIES** (default) — Filter 1 feeds Filter 2, exactly as before.
* **PARALLEL** — both filters receive the same signal, and **BALANCE** blends their outputs, from Filter 1 alone to Filter 2 alone.

Details that matter:

* **Level-matched.** Two identical filters in parallel are exactly as loud as one — no hidden +3 dB.
* **Click-free.** Switching routing or moving BALANCE while notes sound is smooth.
* **BALANCE greys out in SERIES**, where it does nothing.
* BALANCE is a modulation destination, so an LFO can sweep between the two filters.

Try a low-pass and a high-pass in PARALLEL with a gap between their cutoffs: the lows and highs stay, the middle goes — a shape neither filter, nor SERIES, can make.

---

## 💾 Compatibility

v0.7.5 changes the range of several stored controls, so it carries a migration for anything saved earlier.

* **Envelope times** saved before v0.7.5 load as the same number of seconds.
* **LFO waveforms** saved before v0.7.5 load as the same shape.
* **New controls** load at their defaults from older sessions — SERIES, key sync off, pitch mod at zero — whatever the instance was set to before.
* **Automation is preserved.** No parameter ID changed, and new parameters were added after the existing ones, so hosts that address parameters by position see nothing move.
* **Factory presets** are unaffected, and none of them uses an LFO, so no shipped sound changes.

---

## 🧪 Quality & Reliability

PX3 now has **1,481 automated assertions**, up from 1,433 in v0.7.4.

New coverage includes:

* Every stage time reaches at least 30 seconds, and a 30-second attack and release really take 30 seconds.
* Old envelope times and LFO shapes survive loading, and current ones are never migrated twice.
* Ramps travel end to end, hold, and run identically across sample rates and buffer sizes.
* KEY SYNC restarts on every note, ignores note-offs, and ramps ride legato phrases.
* **A destination audit:** all 194 modulation destinations are checked to swing fully at 100%.
* Modulation past an end of a range folds back rather than stalling.
* Pitch Mod is heard in exact semitones on the oscillators and the sub.
* SERIES still chains the filters; PARALLEL extremes equal each filter alone; parallel adds no gain; routing and balance changes don't click; routing survives save and reload.

---

## ⚠️ Known Limitations

* **KEY SYNC is global**, because the LFOs are shared by every voice. A new note restarts the LFO for notes that are already sounding.
* Ramps travel in a straight line; curve shapes are not yet available.
* A second updater helper can still be launched across separate sessions in a specific staged-update scenario.
* The Synth's artwork assets are relatively large, contributing to the overall application size.
* **Windows standalone packaging is not yet available.**

---

## Known Behavior Changes

* **LFOs move further.** A patch with an LFO on a control set away from the middle of its range will now swing that control more — up to its whole range at 100%. If an older patch now modulates too strongly, lower the LFO's AMOUNT.
* **New envelope defaults** apply wherever an envelope time was never set. Saved presets and sessions keep their own times.
* **PITCH is labelled FINE TUNE** on the oscillator and sub cards and in your DAW's parameter list.
* The FLT panel has a routing strip above the filter cards, so the cards sit slightly lower.

---

# What's New at a Glance

### ✨ New

* RAMP UP and RAMP DOWN LFO shapes, up to 60 seconds
* KEY SYNC on all three LFOs
* Pitch Mod destinations for Osc 1–3 and the sub oscillator
* SERIES / PARALLEL filter routing with BALANCE
* Envelope times up to 40 seconds

### 🔧 Improved

* 100% LFO modulation covers the whole range of every destination
* Modulation folds back at range ends instead of flattening
* Envelope defaults
* Envelope editor drag range
* FINE TUNE naming

### 🐛 Fixed

* LFOs at 100% barely moving controls near either end of their range
* Cutoff modulation at the default setting covering less than an octave
* The PITCH control being too narrow to work as a pitch modulation destination

---

**PX3 v0.7.5 makes modulation do what its knobs say — and gives it longer, more deliberate shapes to do it with.**
