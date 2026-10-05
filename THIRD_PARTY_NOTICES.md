# Third-Party Notices

P(X3) contains no third-party source code beyond JUCE. The notices below record
published research and open-source projects that were used as *references* while
implementing this project's DSP. Every algorithm here was written from scratch
against the described technique; no code was copied. The only reproduced
material is published filter data (section 7a), credited there.

---

## 1) JUCE

- Website: https://juce.com
- License: JUCE 8 End User Licence Agreement / GPLv3 (dual)
- Usage: the plugin framework, DSP utilities and GUI toolkit this project is
  built on. Vendored under `JUCE/`.

---

## 2) Reverb

All six types are original implementations from the published literature
below; no third-party source code is included. Design notes and measurements:
`docs/REVERB_DSP_DESIGN.md`.

### 2a) Dattorro plate topology (PLATE)

- Reference: Jon Dattorro, *"Effect Design, Part 1: Reverberator and Other
  Filters"*, Journal of the Audio Engineering Society, Vol. 45 No. 9,
  September 1997.
- Usage: the plate's tank is the topology described in that paper - the input
  diffusers, the figure-of-eight recirculating tank with modulated allpasses,
  and the seven-tap-per-channel output pickup. The delay lengths in
  `PlateEngine::kLen` are the paper's published values for a 29761 Hz
  reference rate, scaled to the running rate. PX3 adds four short dense
  diffusers ahead of them and derives the tank gain from a decay time.
- A public-domain C reference implementation of the same paper
  (https://github.com/el-visio/dattorro-verb, MIT, el-visio) was consulted to
  confirm coefficient placement. No source was copied; the MIT text is retained
  below for completeness.

### 2b) Feedback delay networks (ROOM, HALL, CLOUD)

- Reference: Jean-Marc Jot and Antoine Chaigne, *"Digital Delay Networks for
  Designing Artificial Reverberators"*, AES 90th Convention, 1991: the
  delay-compensated per-line gain `g = 10^(-3M / (RT60 * fs))`, applied per
  frequency band.
- Reference: Fons Adriaensen, **zita-rev1** (GPLv2+; documentation only): an
  allpass inside each loop, and a decay control expressed as low/mid reverb
  times with an HF "half-time" frequency. No zita source is included.
- Reference: Sebastian J. Schlecht and Emanuel A. P. Habets, *"Time-Varying
  Feedback Matrices in Feedback Delay Networks and Their Application in
  Artificial Reverberation"*, JASA 138(3), 2015: ROOM's slowly rotating
  feedback matrix.
- Reference: J. B. Allen and D. A. Berkley, *"Image method for efficiently
  simulating small-room acoustics"*, JASA 65(4), 1979: ROOM's and HALL's early
  reflection patterns.
- Reference: Manfred Schroeder, *"Natural Sounding Artificial Reverberation"*,
  JAES Vol. 10 No. 3, 1962, for the allpass diffusers and for the backward
  integration used to measure RT60 in the tests.

### 2b-ii) Spring (SPRING)

- Reference: Vesa Valimaki, Julian Parker and Jonathan S. Abel, *"Parametric
  Spring Reverberation Effect"*, JAES 58(7/8), 2010; Julian Parker,
  *"Efficient Dispersion Generation Structures for Spring Reverb Emulation"*,
  EURASIP JASP 2011: the stretched-allpass dispersion cascade in a modulated
  feedback loop.

### 2b-iii) Shimmer (CLOUD)

- Background: the pitch-shifted-feedback "shimmer" sound (Eno/Lanois) and
  Sean Costello's ValhallaShimmer notes (pitch shifter in the feedback path,
  randomised grain timing against comb artifacts). Architecture insight only;
  no code.

### 2c) Reverb quality metrics (test suite only)

- Reference: Jonathan Abel and Patty Huang, *"A Simple, Robust Measure of
  Reverberation Echo Density"*, AES 121st Convention, 2006. Implemented as
  `echoDensity` in `tests/Tests/ReverbMeasure.h`.
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

---

## 7) Chorus

### 7a) Holters & Parker BBD filter fit / jpcima rc-effect-playground

- Paper: Martin Holters and Julian D. Parker, *"A Combined Model for a Bucket
  Brigade Device and its Input and Output Filters"*, Proc. DAFx-18, Aveiro,
  2018. https://www.hsu-hh.de/ant/wp-content/uploads/sites/699/2018/09/Holters-Parker-2018-A-Combined-Model-for-a-Bucket-Brigade-Device-and-its-Input-and-Output-Filters.pdf
- Repository: https://github.com/jpcima/rc-effect-playground
  (`sources/bbd_filter.cpp`, namespace `j60`)
- Author: Jean Pierre Cimalando
- License: ISC (text below)
- Usage in this project: the pole/residue values of the Juno-60 BBD input and
  output filters (Holters & Parker's fit, as tabulated in jpcima's
  `bbd_filter.cpp`) are reproduced as constants in
  `shared/DSP/Chorus/Chorus.cpp` (and in the reference curve of
  `tests/Tests/TestsChorusAndSpread.cpp`). They are discretised by this
  project's own impulse-invariant parallel-section code. Two further JUNO-60
  choices follow jpcima's Faust model (`sources/chorus.dsp`): in I+II both lines
  are swept in phase, and the dry sits at 0.83 against the BBD path.
  No source code was copied: jpcima's clocked BBD simulation is not used.

#### ISC License (rc-effect-playground)

Copyright (C) 2019-2020 J.P. Cimalando <https://jpcima.sdf1.org/>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.

### 7b) pendragon-andyh Juno60 analysis

- Repository: https://github.com/pendragon-andyh/Juno60 (`Chorus/README.md`)
- Author: Andy Harman / Pendragon Software Limited
- License: MIT (text below)
- Usage in this project: the measured Juno-60 chorus figures - one triangle LFO,
  right line inverted, Chorus I 0.513 Hz and II 0.863 Hz over 1.66-5.35 ms,
  I+II 9.75 Hz and near mono - define the JUNO-60 modes. Measurements only; no
  code was used.

#### MIT License (pendragon-andyh/Juno60)

Copyright (c) 2015 Andy Harman and Pendragon Software Limited.

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

### 7c) Other chorus references (no code, no licence terms)

- Roland SDD-320 owner's manual (modes, block diagram COMP - DELAY - EXP).
- BOSS CE-2W owner's manual (CE-1 mode: output A chorus only / output B direct
  only in stereo).
- Jürgen Haible, "String Ensemble / Triple Chorus" (Solina modulation:
  two three-phase generators, slow and fast, summed per BBD).
- Panasonic MN3009 / MN3002 data (256 / 512 stages, t = N / 2 f_clk).
