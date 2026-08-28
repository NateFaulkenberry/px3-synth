# Third-Party Notices

P(X3) contains no third-party source code beyond JUCE. The notices below record
published research and open-source projects that were used as *references* while
implementing this project's DSP. Every algorithm here was written from scratch
against the described technique; nothing was copied.

---

## 1) JUCE

- Website: https://juce.com
- License: JUCE 8 End User Licence Agreement / GPLv3 (dual)
- Usage: the plugin framework, DSP utilities and GUI toolkit this project is
  built on. Vendored under `JUCE/`.

---

## 2) Reverb

### 2a) Dattorro plate topology (`ReverbType::Plate`)

- Reference: Jon Dattorro, *"Effect Design, Part 1: Reverberator and Other
  Filters"*, Journal of the Audio Engineering Society, Vol. 45 No. 9,
  September 1997.
- Usage: the plate algorithm implements the topology described in that paper —
  the four cascaded input diffusers, the figure-of-eight recirculating tank with
  modulated allpasses, and the canonical seven-tap-per-channel output pickup.
  The delay lengths in `kPlateBaseDelays` are the paper's published values, which
  are specified for a 29761 Hz reference rate and are scaled to the running
  sample rate at prepare time.
- A public-domain C reference implementation of the same paper
  (https://github.com/el-visio/dattorro-verb, MIT, el-visio) was consulted to
  confirm coefficient placement. No source was copied; the MIT text is retained
  below for completeness.

### 2b) Feedback delay network (`ReverbType::Room`, `Hall`, `Cloud`)

- Reference: Jean-Marc Jot and Antoine Chaigne, *"Digital Delay Networks for
  Designing Artificial Reverberators"*, AES 90th Convention, 1991. The
  delay-compensated per-line gain rule `g = 10^(-3M / (RT60 * fs))` is from this
  paper and is what makes the decay rate uniform across lines of different
  lengths.
- Reference: Fons Adriaensen, **zita-rev1**
  (https://kokkinizita.linuxaudio.org/linuxaudio/, GPLv2+). Used as a reference
  for a well-behaved incommensurate eight-line delay set and for the
  allpass-inside-each-loop arrangement. The delay values in `kFdnDelaySeconds`
  and `kFdnAllpassSeconds` follow the proportions Adriaensen documents. No zita
  source is included in this project.
- Reference: Manfred Schroeder, *"Natural Sounding Artificial Reverberation"*,
  JAES Vol. 10 No. 3, 1962, for the input diffusion allpass chain and for the
  backward-integration method used to measure RT60 in the test suite.

### 2c) Reverb quality metrics (test suite only)

- Reference: Jonathan Abel and Patty Huang, *"A Simple, Robust Measure of
  Reverberation Echo Density"*, AES 121st Convention, 2006. Implemented as
  `normalisedEchoDensity` in `Source/Tools/ComponentTests.cpp`.
- Reference: ISO 3382-1:2009, *Acoustics — Measurement of room acoustic
  parameters*. The decay-curve nonlinearity measure used to check for flutter is
  the standard's linear-regression-residual method.

---

## 3) Delay

### 3a) Fractional delay interpolation

- Reference: Julius O. Smith III, *Physical Audio Signal Processing* — the
  "Delay-Line Interpolation" chapter (CCRMA, Stanford;
  https://ccrma.stanford.edu/~jos/pasp/). Two findings from it shape the
  implementation: linear interpolation suppresses its imaging products by only
  about 26 dB and its gain droops with the fractional part, so a moving read
  pointer gets a lowpass that wobbles in step with the motion; and allpass
  interpolation, though it has unity gain, is recursive and rings when the delay
  length changes, so it is the wrong choice for a modulated line. The delay
  therefore reads with a four-point Catmull-Rom (cubic Lagrange-family)
  interpolator, which is non-recursive and safe under modulation.

### 3b) Bucket-brigade device modelling

- Reference: the MN3007/MN3005 device descriptions at ElectroSmash
  (https://www.electrosmash.com/mn3007-bucket-brigade-devices) and the
  bucket-brigade device literature generally. Three properties are modelled from
  them: a BBD has a fixed number of stages and is clocked at whatever rate gives
  the wanted delay, so its usable bandwidth is set by the delay time (long
  settings are dark, short settings bright); it needs a steep anti-alias filter
  before the sampling stage and a reconstruction filter after it; and it needs
  companding (compress in, expand out, as in the NE570/571 companders these
  circuits were built around) to reach a usable noise floor, which is why a real
  BBD delay breathes on decays. No vendor source or netlist was used.

### 3c) Tape transport modelling

- Wow, flutter and scrape flutter are modelled as separate speed-error
  mechanisms at decades-apart rates (capstan eccentricity, roller and motor
  cogging, tape rubbing across the head), following the standard description of
  tape transport error in the audio engineering literature. Head bump and gap
  loss are modelled from the same source: the record and playback gap geometry
  produces a low-frequency resonance and a high-frequency roll-off that
  accumulates with each pass.

### 3d) Diffusion

- Reference: Manfred Schroeder, *"Natural Sounding Artificial Reverberation"*,
  JAES Vol. 10 No. 3, 1962. The diffusion algorithm's feedback path is a chain
  of Schroeder allpasses at mutually incommensurate lengths with alternating
  signs.

---

## 4) Mood

P(X3)'s Mood component is a two-channel micro-looper and spatial-effects module
inspired by the **MOOD** pedal by Chase Bliss Audio (in collaboration with Old
Blood Noise Endeavors and Drolo Effects). No Chase Bliss code, firmware, DSP or
artwork is used or reproduced - the pedal is not software this project could copy
from. What was taken is the *published specification of behaviour* from the
official MOOD MKII manual, used to give each control a defined meaning:

- **CLOCK** is the engine's sample rate, controlling "the quality and time of the
  effects" and "the length and resolution of the loops" together, and moving "in
  musical, harmonized steps" - lowering it an octave half-speeds the micro-loop
  and the wet channel alike. Implemented as a semitone-quantised clock divider.
- **Wet channel modes** — Reverb (TIME = decay and size at once, MODIFY = smear,
  from multi-tap at minimum to reverb at maximum), Delay (TIME "cleanly
  transitions between delay times without creating pitch-bends in existing
  echoes", MODIFY = feedback that holds at maximum), Slip (TIME = sampling size,
  MODIFY = playback speed and direction "in semi-tone steps").
- **Micro-looper modes** — Env (LENGTH = slice size, MODIFY = detector
  sensitivity), Tape (LENGTH shrinks the loop, MODIFY = speed and direction in
  the harmonised set 4x/2x/1x/.5x in each direction), Stretch (LENGTH = slice
  size, MODIFY = direction and stretch amount, frozen at noon).
- **SPREAD** — the per-mode stereo treatments are implemented from the manual's
  own list: Reverb places reflections differently per channel; Delay ping-pongs
  "mirroring your panning depth"; Slip pans smoothly; Env holds the incoming
  image until the detector fires and then pans; **Tape plays the loop forward on
  the right channel and in reverse on the left**; Stretch drifts slowly side to
  side.
- **ROUTING** — input only / input plus micro-looper / micro-looper only.

Chase Bliss Audio, Old Blood Noise Endeavors and Drolo Effects are not affiliated
with this project and do not endorse it. "MOOD" is used here only to identify the
hardware that inspired the module.

The delay-line interpolation, allpass diffusion and crossfaded-tap references in
section 3 apply to this module as well; it shares those techniques.

---

## 5) Vibe

### 5a) Airwindows

- Repository: https://github.com/airwindows/airwindows
- Author: Chris Johnson (Airwindows)
- License: MIT
- Usage in this project:
  - The saturation stage in `SynthVoice::applyVibeSourceStage` and in the vibe
    VCA uses the `sin()`-based soft-clip Chris Johnson uses throughout the
    Console series (notably `ConsoleBuss` / `ConsoleChannel` in
    `plugins/MacAU/`), rather than the more common `tanh()`. The distinction
    matters musically: `sin()` folds to a hard ceiling at ±pi/2 and produces a
    different, sweeter harmonic series than `tanh()`'s asymptotic curve, and it
    is what gives the stage its analog-console character.
  - The general principle — that a small, always-on, level-appropriate
    nonlinearity applied *per source before summing* sounds different from the
    same nonlinearity applied once to the mix — is Airwindows' Console concept,
    and is why the vibe saturation lives per-source in the voice rather than on
    the output bus.
  - No Airwindows source code was copied. The make-up gain normalisation
    (`saturationMakeupGain`, an inverse-sinc series about a nominal operating
    level) is this project's own, and exists to keep the stage level-neutral,
    which the Console plugins do not need to do.

#### MIT License (Airwindows)

MIT License

Copyright (c) 2018 Chris Johnson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

### 5b) Other vibe references

- Pink ("1/f") noise generation uses the three-pole filter approximation
  published by Paul Kellett on the music-dsp mailing list (public domain). Real
  analog hiss falls at roughly 3 dB/octave; flat white noise is the giveaway of
  a digital source.
- The chaotic component of the drift generator is a standard Lorenz attractor
  (Edward Lorenz, *"Deterministic Nonperiodic Flow"*, Journal of the Atmospheric
  Sciences, 1963), integrated at a fixed timestep so its rate is independent of
  the host's buffer size.

---

## 6) MIT License text (el-visio / dattorro-verb)

MIT License

Copyright (c) el-visio

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
