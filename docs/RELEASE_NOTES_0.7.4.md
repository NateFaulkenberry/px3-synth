# PX3 v0.7.4

## A More Polished FX Experience

PX3 v0.7.4 is a major visual, usability, and control-surface pass across the FX system.

Effects now feel like a cohesive part of PX3 rather than separate tools bolted onto the Synth. Every effect card has its own artwork, color palette, shadows, and visual identity, while standalone effects now match the cards you see inside the Synth.

LUCY, DOOM, and MOOD also receive redesigned control surfaces that put the musical controls back at the center of the experience. Their underlying sound engines remain intact, but the way you interact with them has been substantially refined.

This release also expands the Reverb controls, improves Macros and presets, fixes several FX visibility and audio-routing issues, and significantly reduces the size of standalone effect bundles.

---

## 🎨 Redesigned FX Cards

Every FX card now has its own visual identity.

* **Custom artwork** — Each effect has its own background artwork, integrated directly into the card design.

* **Unique color schemes** — Controls, labels, and buttons are styled to complement each effect rather than using the same generic appearance everywhere.

* **Improved depth** — Cards now have subtle shadows that better separate them from the interface.

* **Improved bypass state** — Bypassed effects are fully greyed out, including their artwork and controls, making it immediately clear when an effect is inactive.

* **Cleaner controls** — The animated rainbow borders around FX knobs have been removed in favor of a cleaner design that works better with the new artwork.

The result is a more cohesive and visually distinct FX section while keeping the overall PX3 design language intact.

---

# 🎛️ LUCY, DOOM & MOOD — Redesigned Control Surfaces

Three of PX3's most distinctive effects receive a major control-surface redesign in v0.7.4.

LUCY, DOOM, and MOOD were originally presented largely as collections of individual parameters. While those parameters exposed the full capabilities of their engines, they didn't always correspond to the way you would naturally think about or play the effects.

That changes in this release.

The underlying sound engines remain intact. The focus here is on making the controls **more musical, more predictable, and more closely aligned with the character of each effect.**

The result is a much more intentional relationship between the knobs you turn and the sound you get.

---

## 🟣 LUCY

LUCY receives a substantial overhaul to the way its controls behave and interact.

### GLOBAL now controls effect intensity

GLOBAL is no longer simply a conventional wet/dry mix.

Instead, it acts as a **master intensity control** for LUCY's processing, allowing the character of the effect to become progressively stronger as you turn it up.

The result is much more useful for performance: increasing GLOBAL actually increases the intensity of LUCY's character rather than simply mixing more of the same processed signal into the output.

At the very bottom of the control, LUCY can still return smoothly to a clean signal.

### LOSS has a much more musical response

LOSS now has a carefully shaped response across its entire range.

Previously, much of the audible change was concentrated toward the upper end of the knob. The lower half could feel relatively inactive before the effect suddenly became much more aggressive.

The new response distributes the effect more naturally across the control:

* The core character begins developing earlier.
* The more destructive processing remains progressively stronger toward the top.
* The full range of the knob now feels useful rather than concentrating most of its action at the extreme end.

### SPEED now behaves like a musical time control

SPEED now follows a more natural geometric response, making the middle of the control much more useful.

The various temporal elements of LUCY respond together, producing a more coherent relationship between SPEED and the effect's overall movement.

### Simplified controls

LUCY's control set has also been cleaned up and clarified.

* **FREEZE** is now a three-state control: **OFF / SOLID / SLUSHY**
* **WEIGHTING** is now expressed as **DARK / NEUTRAL / BRIGHT**
* Several controls have been renamed for clarity and consistency.

**Important:** LUCY's parameter layout has been replaced rather than maintained through compatibility shims.

**Sessions and presets saved before v0.7.4 will load LUCY using its new defaults.**

---

# 🔴 DOOM

DOOM's parameter structure remains compatible, but the way its controls are presented and interpreted has been significantly refined.

### More meaningful control behavior

DOOM's controls now have clearly defined musical relationships across its different modes.

TIME, MODIFY, and LENGTH behave according to what they actually represent in each mode rather than relying on a generic response.

Some highlights:

* **RELAY MODIFY** steps through meaningful repeat counts rather than behaving like an arbitrary continuous control.
* **RADIO MODIFY** smoothly scans between stations, allowing neighboring stations to blend together.
* **BURST LENGTH** correctly reflects the pace of the sequence — higher settings produce faster, shorter steps.
* **MASK** reaches a true zero point, providing a completely untouched loop at the bottom of its range.

These relationships make DOOM's controls considerably easier to understand by ear.

### More reliable modes

DOOM's operating modes are now treated as distinct choices rather than ambiguous numerical values.

This doesn't change your existing DOOM presets or sessions — the stored parameters remain compatible.

### RAMP remains intentionally absent

DOOM does not add a separate internal ramp system.

PX3 already provides host automation and a modulation matrix, giving you more flexible ways to create evolving parameter movements without introducing a second, more limited ramping system inside the effect.

---

# 🟢 MOOD

MOOD receives both a control-surface refinement and a significant reliability improvement.

### Repeatable random behavior

MOOD's randomized processing is now independently controlled for each instance.

This means identical settings can produce repeatable results when initialized with the same state, while separate MOOD instances no longer interfere with each other's random behavior.

The result is more predictable behavior when using multiple instances, saving projects, or working with automated processing.

### Cleaner routing controls

MOOD's routing choices are now treated as distinct modes rather than arbitrary numerical values.

This makes the three routing configurations explicit and prevents accidental overlap between different routing states.

### A deliberate choice remains open

MOOD's internal channel balance remains part of its underlying character rather than being exposed as another user control.

This keeps the surface focused on the parameters that meaningfully affect how you interact with the effect.

---

# 🎛️ Pedal-Inspired Alternate Controls

LUCY and DOOM now take greater inspiration from the physical pedals that inspired them.

Instead of presenting a large collection of flat controls, each primary knob can now expose a **second function**.

Six large knobs are presented on each card, with the alternate function displayed beneath the primary function.

A **MAIN / ALT** switch lets you choose which set of controls is currently displayed.

### DOOM

| MAIN   | ALT     |
| ------ | ------- |
| TIME   | CROSS   |
| MODIFY | EQ      |
| LENGTH | FADE    |
| MODIFY | BLEND   |
| CLOCK  | GLUE    |
| MIX    | BALANCE |

### LUCY

| MAIN   | ALT       |
| ------ | --------- |
| FILTER | GATE      |
| VERB   | DECAY     |
| FREQ   | THRESHOLD |
| SPEED  | AUTO GAIN |
| LOSS   | LOSS GAIN |
| GLOBAL | FREEZER   |

This replaces a much larger collection of individual knobs with a more focused control surface while retaining access to the underlying functionality.

### Both functions remain fully independent

Switching between MAIN and ALT does **not** change the underlying parameters.

Both functions remain available for automation and modulation, regardless of which set is currently displayed.

The MAIN / ALT selection is simply a way of navigating the control surface — it is not itself an audio parameter.

This means you can automate or modulate an alternate control without worrying about whether the card happens to be displaying it.

---

## 🟢 MOOD — A Cleaner Control Layout

MOOD's controls have also been reorganized to make the relationship between its different sections easier to understand.

Rather than presenting its controls as one large collection, the card now groups them more intentionally:

* Channel-related controls are grouped together.
* Machine and processing controls follow.
* **MIX** is given its own position at the bottom of the card.

The layout now adapts to the card itself, keeping the controls properly sized and spaced rather than allowing a small change in available space to produce awkward wrapping.

The visual styling remains unchanged — MOOD keeps its existing palette, artwork, chips, knobs, and overall card treatment.

---

## 🎛️ Standalone Effects

Standalone FX now look and behave like the effects inside the Synth.

* **The same cards** — Standalone effects use the same visual design, artwork, colors, and control layouts as their Synth counterparts.

* **Working bypass** — Bypass now actually removes the effect from the audio path, while also clearly showing the bypassed state visually.

* **Consistent sizing** — Standalone effects now open at the same size used by their cards inside the Synth.

* **Mood & Delay controls restored** — Both standalone effects now display their full control interfaces instead of opening with empty panels.

* **Smaller downloads** — Standalone effects now include only the artwork and resources they actually need, substantially reducing their overall size.

---

## 🌊 Reverb Gets a Complete Control Set

Reverb receives one of the biggest functional improvements in this release.

Previously, the Synth's Reverb card exposed only **Amount** and **Mode**, despite Reverb supporting a much larger set of parameters.

The Synth now exposes all **11 Reverb controls**:

**SIZE · DECAY · DAMPING · PRE · DEPTH · RATE · WIDTH · REGEN · SMEAR · MODE · AMOUNT**

The Reverb card has also been redesigned to fit the complete control set cleanly, with the same PX3 rotary controls used throughout the rest of the interface.

**Existing presets remain fully compatible.** Your existing Reverb settings will load exactly as before.

---

## 🎚️ Macros & Presets

Macros receive a small but important usability upgrade.

* Macro labels now sit **above their knobs** for easier reading.
* Every Macro now has a dedicated **DEPTH** button underneath it.
* **Cmd-click assignment is back** — Cmd-clicking a Macro once again opens its assignment mode.
* **Double-clicking a preset now loads it** immediately.
* The preset browser's **CLOSE** button has been renamed **CANCEL** to better communicate its function.

---

## 🐛 Fixes & Improvements

This release also addresses several issues that could otherwise be easy to miss during normal use.

### FX disappearing from the Synth

**Vibe, Delay, and Mood could disappear from the FX panel** even though the effects themselves were still present elsewhere in the system.

This has been fixed. All FX stages now reliably appear in the Synth's FX panel.

### Artwork updates

Replacing an effect's artwork now correctly updates the artwork used by the application.

### More reliable FX processing

Additional safeguards now verify that every FX stage is actually connected to the audio chain.

### More reliable controls

FX controls are now verified to be properly connected to their underlying parameters, preventing a knob from appearing and responding visually without actually controlling the effect.

---

## 🧪 Quality & Reliability

PX3 now has **1,433 automated assertions**, up from 1,409 at the start of the current development cycle.

The expanded test coverage focuses on things that directly affect the experience of using the plugin:

* Every FX stage is verified to actually affect audio.
* Every FX knob is verified to control a parameter.
* Every FX card is verified to be visible.
* Standalone effects are verified against their Synth counterparts.
* Bypass states are verified visually.
* Artwork configuration is verified automatically.
* LUCY's control curves are verified across their full ranges.
* DOOM's mode-specific controls are verified for consistent behavior.
* MOOD's timing and routing controls are verified.
* MOOD's randomized processing is verified to be reproducible and independent between instances.
* The preset browser now has automated coverage.

These checks are designed to catch the kinds of regressions that can otherwise look fine while still leaving part of the plugin broken.

---

## ⚠️ Known Limitations

A few items remain unchanged from previous releases:

* A second updater helper can still be launched across separate sessions in a specific staged-update scenario.

* The Synth's artwork assets are relatively large, contributing to the overall application size.

* **Windows standalone packaging is not yet available.**

---

## Known Behavior Changes

* **LUCY sessions and presets saved before v0.7.4 load using the new default LUCY settings.** LUCY's parameter structure was intentionally replaced without a compatibility layer.

* **DOOM and MOOD are unaffected.** Their stored parameter IDs and values remain compatible.

* LUCY's rainbow ring has been removed from the FX amount controls; its new artwork and visual treatment now provide the effect's character.

* MOOD's routing menu uses shortened labels where necessary to keep the choices readable within the available space.

---

# What's New at a Glance

### ✨ New

* Custom artwork for every FX card
* Individual FX color schemes
* Card shadows and improved visual depth
* Redesigned LUCY control surface
* Redesigned DOOM control surface
* Redesigned MOOD control layout
* MAIN / ALT control system for LUCY and DOOM
* Full Reverb control set
* Macro DEPTH buttons
* Restored Cmd-click Macro assignment

### 🔧 Improved

* LUCY's LOSS response
* LUCY's GLOBAL intensity control
* LUCY's SPEED response
* DOOM's mode-specific control behavior
* MOOD's routing and randomization
* Standalone FX now match Synth FX cards
* Standalone effect sizing
* Bypass visual feedback and audio behavior
* Preset loading
* FX control reliability
* Standalone bundle sizes

### 🐛 Fixed

* Missing Vibe, Delay, and Mood cards
* Reverb layout issues
* Standalone bypass not actually bypassing
* Standalone Mood and Delay appearing empty
* Artwork changes not updating correctly
* FX controls that could appear functional without being connected
* FX stages that could appear in the interface without processing audio

---

**PX3 v0.7.4 is a substantial evolution of the FX system — making the effects more consistent, more expressive, and much more enjoyable to interact with.**
