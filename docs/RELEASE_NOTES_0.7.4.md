# PX3 v0.7.4

## A more polished FX experience

PX3 v0.7.4 is a major visual and usability pass across the FX system.

Effects now feel like a cohesive part of PX3 rather than separate tools bolted onto the Synth. Every effect card has its own artwork, color palette, shadows, and visual identity, while standalone effects now match the cards you see inside the Synth.

This release also expands the Reverb controls, improves Macros and presets, fixes several FX visibility and audio-routing issues, and significantly reduces the size of standalone effect bundles.

---

## 🎨 Redesigned FX Cards

Every FX card now has its own visual identity.

* **Custom artwork** — Each effect has its own background artwork, integrated into the card design.
* **Unique color schemes** — Controls, labels, and buttons are styled to complement each effect rather than using the same generic appearance everywhere.
* **Improved depth** — Cards now have subtle shadows that better separate them from the interface.
* **Improved bypass state** — Bypassed effects are fully greyed out, including their artwork and controls, making it immediately clear when an effect is inactive.
* **Cleaner controls** — The animated rainbow borders around FX knobs have been removed in favor of a cleaner design that works better with the new artwork.

The result is a more cohesive and visually distinct FX section while keeping the overall PX3 design language intact.

---

## 🎛️ Standalone Effects

Standalone FX now look and behave like the effects inside the Synth.

* **The same cards** — Standalone effects use the same visual design, artwork, colors, and control layouts as their Synth counterparts.
* **Working bypass** — Bypass now actually removes the effect from the audio path, while also clearly showing the bypassed state visually.
* **Consistent sizing** — Standalone effects now open at the same size used by their cards inside the Synth.
* **Mood & Delay controls restored** — Both standalone effects now display their full control interfaces instead of opening with empty panels.

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

This release also addresses several issues that could make their way through without being immediately obvious.

### FX disappearing from the Synth

**Vibe, Delay, and Mood could disappear from the FX panel** even though the effects themselves were still present elsewhere in the system.

This has been fixed. All FX stages now reliably appear in the Synth's FX panel.

### Artwork updates

Replacing an effect's artwork now correctly updates the artwork used by the application.

### Smaller standalone bundles

Standalone effects no longer carry artwork and resources belonging to other effects.

This dramatically reduces bundle sizes — from roughly **432 MB total to about 186 MB** across the standalone products.

### Audio processing reliability

Additional safeguards now verify that every FX stage is actually connected to the audio chain. This helps prevent an effect from appearing correctly in the interface while silently doing nothing to the audio.

### Control reliability

All FX controls are now verified to be properly connected to their underlying parameters, preventing a knob from appearing and responding visually without actually changing the effect.

---

## 🧪 Quality & Reliability

PX3 now has **1,399 automated assertions**, up from 1,366.

More importantly, the new testing focuses on things that directly affect the experience of using the plugin:

* Every FX stage is verified to actually affect audio.
* Every FX knob is verified to control a parameter.
* Every FX card is verified to be visible.
* Standalone effects are verified against their Synth counterparts.
* Bypass states are verified visually.
* Artwork configuration is verified automatically.
* The preset browser now has automated coverage.

These checks are designed to catch the kinds of regressions that can otherwise look fine during normal development while still leaving part of the plugin broken.

---

## ⚠️ Known Limitations

A few items remain unchanged from previous releases:

* A second updater helper can still be launched across separate sessions in a specific staged-update scenario.
* The Synth's artwork assets are relatively large, contributing to the overall application size.
* **Windows standalone packaging is not yet available.**

---

## What's New at a Glance

**✨ New**

* Custom artwork for every FX card
* Individual FX color schemes
* Card shadows and improved visual depth
* Full Reverb control set
* Macro DEPTH buttons
* Restored Cmd-click Macro assignment

**🔧 Improved**

* Standalone FX now match Synth FX cards
* Standalone effect sizing
* Bypass visual feedback and audio behavior
* Preset loading
* FX control reliability
* Standalone bundle sizes

**🐛 Fixed**

* Missing Vibe, Delay, and Mood cards
* Reverb layout issues
* Standalone bypass not actually bypassing
* Standalone Mood and Delay appearing empty
* Artwork changes not updating correctly
* FX controls that could appear functional without being connected

**PX3 v0.7.4 is a substantial polish pass across the FX system — making the effects more consistent, more expressive visually, and more reliable to use.**
