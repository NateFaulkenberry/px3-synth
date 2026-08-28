# P(X3) Synth

P(X3) is a polyphonic JUCE synthesizer with a multi-mode source engine, a channel-style mixer, and reorderable FX (VIBE, Delay, Reverb, Mood). This README is a full operating guide for new users and a function-level map for developers.

Current version: v0.1.0

For dedicated build/install/release workflow documentation, see `docs/BUILDING.md`.
For preset system format and storage details, see `docs/PRESETS.md`.
For developer architecture, maintenance map, and release workflow, see `DEVELOPMENT.md`.

## What You Get

- 64-voice poly synth engine.
- 20 oscillator modes (classic + experimental + PX3).
- Three macro knobs whose meaning changes by oscillator mode.
- Dedicated SUB + OSC1/OSC2/OSC3 source channels.
- Filter, amp envelope, and master gain section.
- Three LFO modulation sources with assignable destinations.
- Four FX blocks with bypass and drag/drop processing order.
- Mixer channel controls: level, pan, send, mute, solo, per-channel meter.
- Mixer faders show the real gain of each channel; the -4 dB of modulation headroom lives on the sources themselves, not hidden inside the fader.
- Explicit internal audio bus routing: OSCILLATOR STEMS -> DRY BUS + FX SEND BUS -> FX CHAIN -> FX RETURN -> MASTER.
- FX return controls: level, pan, mute, solo.
- Clickable 88-key keyboard (A0-C8) with medium click velocity.
- Performance strip (pitch bend and mod wheel).

## Requirements

- macOS (Apple Silicon supported)
- Xcode Command Line Tools
- CMake 3.22+
- Ninja
- VS Code

## Build And Run

```bash
git clone <your-repo-url>
cd px3-synth
cmake -B build -G Ninja
cmake --build build
./scripts/run-standalone.sh
```

Optional standalone script flags:

```bash
# force clean rebuild before launching
./scripts/run-standalone.sh --build true

# equivalent shorthand (no value means true)
./scripts/run-standalone.sh --build

# explicitly disable forced rebuild (default)
./scripts/run-standalone.sh --build false
```

JUCE is fetched automatically through CMake FetchContent.

## Uninstall And Logic Rescan

Remove installed P(X3) plugin bundles and related app/plugin data:

```bash
./scripts/uninstall-local.sh
```

Trigger Logic Pro relaunch helper after cleanup:

```bash
./scripts/uninstall-local.sh --logic-rescan
```

Notes:

- The uninstall script targets both user and system plugin folders/cache paths.
- System-wide removals may require `sudo`.

## Building a Release Installer

Run:

```bash
./scripts/build-release.sh
```

This command builds the release plugin formats and also creates a native macOS installer package.

The installer currently includes:

- Audio Unit (AU)
- VST3

Example output artifact:

- dist/PX3-v0.1.0.pkg

Installer plugin destinations:

- /Library/Audio/Plug-Ins/Components/
- /Library/Audio/Plug-Ins/VST3/

The installer flow is intentionally simple in this phase. Developer ID installer signing and notarization can be added later in the release pipeline.

## Debug Mode

The in-plugin DEBUG panel is controlled at build time and is OFF by default.

- Default behavior: no debug button is rendered in the UI.
- Enable for release packaging:

```bash
./scripts/build-release.sh --debug true
```

- Disable explicitly for release packaging:

```bash
./scripts/build-release.sh --debug false
```

- Enable/disable when launching standalone (script reconfigures/rebuilds first):

- Optional forced rebuild when launching standalone:

```bash
./scripts/run-standalone.sh --build true
./scripts/run-standalone.sh --build
```

- Enable/disable when launching standalone debug mode:

```bash
./scripts/run-standalone.sh --debug true
./scripts/run-standalone.sh --debug false
```

- Combine debug + forced rebuild:

```bash
./scripts/run-standalone.sh --debug true --build true
```

- If building manually with CMake:

```bash
cmake -B build -G Ninja -DPX3_DEBUG_PANEL=ON
cmake --build build
```

## UIConfig JSON

Runtime UI styling and layout are loaded from `UIConfig.json`.

Source of truth in the repo:

- `Source/UI/UIConfig.json`

Hot reload behavior:

- The editor checks for file changes during `timerCallback()` and reloads when file modification time changes.
- If JSON parsing fails, the previous valid config remains active.
- UI config load/reload and path-switch events are logged in the debug event log.

Mixer config scope:

- `mix.fader`, `mix.mute`, `mix.solo`, and `mix.meter` are fully style-driven from UIConfig.
- `mix.channel` currently supports spacing and text sizing.
- Channel internal geometry (section spacing, button spacing, footer row heights) is currently code-defined.

Path resolution order:

- Debug builds (`JUCE_DEBUG` or `PX3_DEBUG_PANEL`):
  - `PX3_UI_CONFIG_PATH` (if set and file exists)
  - `./Source/UI/UIConfig.json` from current working directory
  - upward probe for `Source/UI/UIConfig.json`, then `UIConfig.json`
  - bundle fallback: `Contents/UIConfig.json`, then `Contents/Resources/UIConfig.json`
- Non-debug builds:
  - bundle only: `Contents/UIConfig.json`, then `Contents/Resources/UIConfig.json`
  - no source-tree probing

Production packaging:

- CMake copies `Source/UI/UIConfig.json` into `Contents/Resources/UIConfig.json` for Standalone, AU, and VST3 bundles.
- `scripts/build-release.sh` fails fast if AU/VST3 bundles or component pkg payloads are missing `Contents/Resources/UIConfig.json`.

## Developer Preset Dumping

When DEBUG mode is enabled, the detached P(X3) DEBUG CONSOLE includes a `PRESET / STATE TOOLS` block with:

- `Preset Name` (optional suggested preset/file name)
- `DUMP PRESET`

`DUMP PRESET` behavior:

- Opens a native OS save dialog (developer chooses folder + filename).
- Uses the same preset extension and format as normal presets: `.px3preset`.
- Appends `.px3preset` automatically if omitted.
- Uses the same underlying preset serialization path as normal user presets (same state tree and serializer).
- Performs validation of the serialized preset structure before writing.
- Reports success/failure in the debug console and logs detailed events in the debug event log.

This is intended for developer workflows such as:

- collecting presets from beta testers
- QA snapshot capture
- regression/state-restore testing
- promoting dumped presets into the factory preset library later

Important:

- Dumped presets are production-compatible P(X3) presets, not a debug-only format.
- Runtime/debug UI state is not embedded unless it already belongs to normal preset state.

## Signal Flow (High Level)

```text
MIDI/Virtual Keyboard
  -> Voice Stems (SUB, OSC1, OSC2, OSC3)
  -> DRY BUS (channel pan/mute/solo applied)
  -> FX SEND BUS (channel sends + send gates)
  -> FX Chain (user-order: VIBE / Delay / Reverb / Mood)
  -> FX RETURN (return gain/pan/mute/solo applied)
  -> MASTER BUS (DRY + FX RETURN)
  -> Master Output
```

Important routing rules:

- LFO remains a modulation source only and is not mixed into any audio bus.

Bus architecture notes:

- Voice rendering exports explicit stems for SUB/OSC1/OSC2/OSC3.
- DRY BUS is the post-channel-gate reference signal.
- FX send path is independent per source and can be gated by solo state.
- FX BUS stores return-only contribution relative to the sent signal.
- MASTER BUS is the final sum and the source for post-block reverb compensation.

## Mixer Gain Structure

- Every source (SUB, OSC1, OSC2, OSC3) and the FX return defaults to -4 dB, giving
  each channel headroom for modulation without the mix clipping.
- That trim lives **on the sources themselves**, not inside the fader. A mixer
  fader at unity means unity: what the strip shows is the channel's actual gain.
- Because the trim is a default rather than a hidden offset, presets and DAW
  sessions store and restore whatever the fader was actually set to. Loading a
  preset never re-applies the default over the top of a saved value.
- The fader range extends above unity by the same 4 dB, so a channel can still be
  pushed back to its full pre-trim level.
- The trim value and the fader's maximum come from one shared constant in
  `Source/DSP/PluginProcessorInternals.h`, so the source side and the fader range
  cannot drift apart.

## Mixer Solo Rules

Mixer solo behavior is intentionally explicit:

- If no solo buttons are engaged, all unmuted source channels feed DRY and FX normally.
- If one or more source solos (SUB/OSC1/OSC2/OSC3) are engaged, only soloed sources are audible in the DRY path.
- During source-solo mode, FX path only passes when FX solo is also engaged.
- During source-solo + FX-solo mode, only soloed sources feed FX sends.
- FX return mute always hard-mutes the FX return channel.

## Debug Bus Observability

When DEBUG mode is enabled, the detached debug console exposes live internal bus observability:

- Bus RMS readouts for OSCILLATOR, DRY, FX, and MASTER buses.
- FX send/return gain controls for quick wet-path gain-staging checks.

These are developer diagnostics and do not change the preset format.

## Debug Performance HUD Accuracy

When `PX3_DEBUG_PANEL` is enabled, the bottom-left CPU/RAM overlay reports:

- `CPU`: per-instance plugin load measured from this instance's `processBlock` execution time relative to block audio duration.
- `RAM`: per-instance estimate computed as process resident memory divided by active PX3 instance count.

Accuracy notes:

- CPU is instance-specific and is the closest real-time indicator of this plugin instance's audio-thread cost.
- CPU is reported as smoothed block load, so very short spikes may be visually damped.
- RAM is an estimate, not exact per-instance ownership, because process memory is shared and cannot be perfectly partitioned by plugin instance.
- In standalone with one instance, RAM estimate effectively matches app RSS.

## UI Guide: Every Section And Control

## OSC Section

Controls:

- MODE (dropdown): chooses one of 20 oscillator types.
- PARAM A/B/C (three top-left knobs): their labels and behavior depend on mode.
- VOWEL (visible only in FORMANT mode): chooses A/E/I/O/U profile.

Mode list:

- SINE
- SAW
- SQUARE
- TRIANGLE
- NOISE
- PINK NOISE
- SUPER SAW
- PWM
- WAVETABLE
- ADDITIVE
- FORMANT
- FM
- HARD SYNC
- KARPLUS
- ORGAN
- DIGITAL
- PHYSICAL
- ROB
- ISAAC
- PX3

Macro mapping by mode:

- SINE/SAW/SQUARE/TRIANGLE
  - No macro controls shown.
- NOISE/PINK NOISE
  - A = COLOR (dark->bright noise color).
- SUPER SAW
  - A = DETUNE.
  - B = SPREAD/width/drift feel.
- PWM
  - A = WIDTH.
- WAVETABLE
  - A = POSITION (wavetable frame/morph position).
- ADDITIVE
  - A = TILT (harmonic rolloff shape).
  - B = ODD/EVEN harmonic balance.
  - C = ROLL/inharmonic animation influence.
- FORMANT
  - A = MORPH (between vowel profiles).
  - B = COLOR (spectral brightness/drive).
  - VOWEL selects the base vowel family.
- FM
  - A = RATIO.
  - B = INDEX.
- HARD SYNC
  - A = SYNC ratio.
  - B = DRIVE.
- KARPLUS
  - A = DECAY.
  - B = BRIGHT.
- ORGAN
  - A = TONE.
  - B = CLICK.
- DIGITAL
  - A = BITS depth tendency.
  - B = RATE/sample-hold tendency.
- PHYSICAL
  - A = DECAY/damping.
  - B = MATERIAL resonance character.
- ROB
  - A = TRANS (transient/edge behavior).
  - B = BODY.
  - C = CHAOS.
- ISAAC
  - A = SPREAD.
  - B = ODD/EVEN.
  - C = ROLL.
- PX3
  - A = MORPH.
  - B = CHAR.
  - C = MOVE.

## FILTER Section

Controls:

- Cutoff: filter cutoff frequency (~80 Hz to 18 kHz, skewed for musical travel).
- Reso: filter resonance/Q (~0.25 to 2.2).
- Filter Type dropdown:
  - LP12
  - LP24
  - HP12
  - HP24
  - BandPass
  - Notch
  - AllPass

Behavior notes:

- LP24 and HP24 are effectively cascaded two-stage behavior.
- Notch is achieved by subtracting a resonant band-pass contribution.

## AMP ENV Section

UI:

- AMP ENV now uses a single interactive ADSR graph in place of four separate knobs.
- Drag handles to edit stages:
  - Attack handle sets attack time.
  - Decay/Sustain handle sets decay time and sustain level together.
  - Release handle sets release time.
- Double-click a handle to reset that stage to its default parameter value.

Parameter mapping (unchanged source of truth):

- Attack: 0.001s to 3.0s
- Decay: 0.005s to 4.0s
- Sustain: 0.0 to 1.0
- Release: 0.010s to 5.0s

Architecture guarantee:

- The graph is a view/controller for existing plugin parameters only.
- DSP, host automation, presets, and state serialization continue using the same ADSR parameters.
- No parallel ADSR state was introduced in the editor.

This envelope remains per voice and shapes oscillator loudness before FX.

## LFO Section

Controls:

- Three independent LFO lanes (LFO 1/2/3).
- Per-LFO frequency and destination assignment.

Behavior:

- Each LFO lane generates a modulation signal per processing block.
- Assigned destinations receive normalized modulation:
  - effective = clamp(base + (depth * lfoSignal))
- UI knobs/sliders still represent and automate base values only.
- DSP reads effective values for sound generation; host-visible parameter values are never written by LFO.

Current limits:

- Each LFO supports one assignment at a time.
- Depth is defined by target mapping in DSP (no per-target depth UI editor yet).
- Block-rate LFO evaluation (intentionally lightweight; no sample-rate modulation path yet).

## OUTPUT Section

Controls:

- Gain (0.0 to 1.0 master voice gain factor).

## FX Sections

You have four FX blocks:

- VIBE
- DELAY
- REVERB
- MOOD

Each block has a corner bypass toggle and can be reordered by drag-and-drop.

Bypass semantics:

- Checked = enabled (not bypassed).
- Unchecked = bypassed.

Bypassing an FX block clears its internal buffers, so re-enabling it starts silent
rather than releasing the tail of whatever was playing when it was switched off.
Delay clears immediately; Reverb and Mood clear once their fade to silence
completes, so the bypass itself never clicks.

Header behavior:

- Clicking section header toggles its bypass.
- Dragging header reorders blocks; order is committed to DSP and saved in state.

### VIBE

Controls:

- AMOUNT knob: effect amount.
- TYPE dropdown: Warm, Hot, Cool, Vintage, Clean, LoFi.

What it does:

VIBE is a per-voice analog-imperfection layer rather than an effect on the mix. It
runs inside each voice, per source, *before* the four sources are summed - N
saturated signals summed does not equal their sum saturated once, and that
difference is the point.

What it adds:

- **Independent per-voice drift.** Every voice has its own drift rate (0.020-0.095 Hz),
  its own random walk, and its own thermal phase. Voices do not drift together;
  that independence is what thickens a held chord rather than sounding like vibrato.
  Measured movement is about 3.2 cents with an inter-voice correlation near zero.
- **Console-style saturation** using a `sin()` fold rather than `tanh()`, which
  reaches a hard ceiling and produces a different harmonic series.
- **Program-dependent PSU sag.** The supply sags because the amplifier is drawing
  current, so the sag follows the actual oscillator-bus level.
- **Pink (1/f) noise**, not white - analog hiss falls at roughly 3 dB/octave.
- **A coupling capacitor per source**, because asymmetric distortion makes DC by
  definition and DC inside a signal path eats headroom.
- **A chaotic (Lorenz) component** integrated at a fixed timestep, so its rate does
  not change with the host's buffer size.

Level behaviour:

- The stage is deliberately level-neutral: the worst level change across the whole
  AMOUNT range is about 1.2 dB. Turning VIBE up adds harmonic content and movement,
  not volume.

VIBE is an original texture system, not a hardware emulation claim. The `sin()`
saturation is credited to Airwindows in `THIRD_PARTY_NOTICES.md`.

### Delay

Controls:

- AMOUNT knob (center knob in delay card): delay amount/mix driver. At zero the
  block is a wire.
- ALGO dropdown:
  - Granular
  - Tape
  - Analog/BBD
  - Ping-Pong
  - Stereo
  - Modulated
  - Diffusion
- TIME knob: base delay time or beat-synced time basis.
- FEEDBACK knob: feedback amount (algorithm-aware safety limits).
- SYNC dropdown:
  - Free
  - 1 Bar
  - 1/2
  - 1/4
  - 1/8
  - 1/8T
  - 1/16
  - 1/16T
- GRANULAR MODE dropdown (applies to the Granular algorithm):
  - CLASSIC
  - CLOUD
  - SHIMMER
  - RHYTHMIC

What it does:

Every algorithm reads its delay line with four-point cubic interpolation. Linear
interpolation suppresses its imaging products by only about 26 dB and its gain
droops with the fractional part, so a read pointer that is moving - which is every
algorithm here - would get a lowpass that wobbles in step with the motion.

- **Granular** spawns windowed grains from delay memory, with four sub-modes
  (CLASSIC, CLOUD, SHIMMER, RHYTHMIC) selected by the GRANULAR MODE dropdown.
  Grains read from a point in the stereo field rather than from a mono sum.
- **Tape** models the transport rather than imitating it: wow, flutter and scrape
  as separate speed-error mechanisms at decades-apart rates, a head bump from the
  record/playback gap geometry, cumulative high-frequency gap loss per pass, and
  magnetic hysteresis in the saturation.
- **Analog/BBD** models a 4096-stage bucket-brigade chip. The stage count is fixed
  and the clock is whatever produces the wanted delay, so **bandwidth is locked to
  delay time**: short settings are bright, long settings are dark and grainy. It
  also has the companding (compress in, expand out) those chips need, and the
  anti-alias and reconstruction filters that go with it. Its TIME control stops at
  620 ms because a real chip runs out of stages - that ceiling is the model, not a
  clamp.
- **Ping-Pong** sums the input to one side and hands each repeat to the other
  channel, so the echoes genuinely alternate in time.
- **Stereo** runs two lines at a musical two-thirds ratio with light cross-coupling.
- **Modulated** slides its read pointers under three modulators at incommensurate
  rates, opposed between the channels.
- **Diffusion** puts a Schroeder allpass chain in the feedback path, so each repeat
  smears a little more than the last and the echoes dissolve into a wash.

FEEDBACK behaviour:

- FEEDBACK sets a decay **time**, not a per-repeat coefficient. The coefficient is
  derived from the delay length so that the same knob position means the same decay
  whether the delay is 20 ms or 2 seconds. As a raw coefficient it did not: 0.98 per
  repeat is a 30-second decay at 100 ms and an eleven-minute one at 2 seconds.
- The top of the control is a long but finite decay, not a drone.

Other behaviour:

- At zero AMOUNT the delay is a wire - bit-identical to its input.
- Changing TIME crossfades between delay positions on the digital algorithms, so
  existing echoes do not pitch-bend. Tape and Modulated slide the pointer instead,
  because the pitch movement is the effect in those two.
- Bypassing the block clears the delay lines, so re-enabling it does not replay a
  tail from before it was switched off.

### Reverb

Controls:

- INTENSITY knob: reverb amount.
- ALGO dropdown:
  - ROOM
  - PLATE
  - HALL
  - CLOUD

What it does:

- **ROOM**: an explicit nine-tap early-reflection pattern per channel, with
  different times per ear, feeding a feedback delay network at roughly 30-175 ms.
- **PLATE**: a full Dattorro plate (JAES 45/9, 1997) - four input diffusers, a
  figure-of-eight recirculating tank with modulated allpasses, and the canonical
  seven-tap-per-channel output pickup.
- **HALL**: a Jot/zita-style eight-line feedback delay network with an allpass
  inside each loop, Hadamard mixing, and per-line damping, scaled to ~37-231 ms.
- **CLOUD**: the same network scaled much longer (~75-488 ms) for expansive,
  modulated wash.

Three properties are what make these sound like spaces rather than metal:

- Per-line feedback gain is **delay-compensated** (Jot's rule), so short and long
  lines decay at the same rate in time rather than the same rate per pass.
- The feedback matrix is **orthogonal** (Hadamard), so it is energy-preserving.
- Delay lengths are **mutually incommensurate** and there is input diffusion ahead
  of the network, without which an impulse arrives as a burst of discrete taps.

Also:

- Early reflections are same-sign on both channels. Anti-phase measures wider but
  cancels when the mix is summed to mono.
- Shared post-processing includes stereo width shaping, wet DC filtering, gentle
  peak control at high wet levels, and output compensation so loudness stays stable
  as INTENSITY rises.
- Bypassing the block clears the delay lines once the amount fade reaches zero, so
  re-enabling it does not release the tail of whatever was playing when it was
  switched off.

### Mood

Mood is a two-channel micro-looper and spatial-effects module: an always-listening
looper and a suite of real-time spatial effects that can process the input, the
loop, or both. It is inspired by the MOOD pedal by Chase Bliss Audio - behaviour
only, no code; see `THIRD_PARTY_NOTICES.md`.

Shared controls (UI labels in brackets where they differ):

- MIX: balance between input and Mood.
- CLOCK: Mood's internal sample rate (see below).
- ROUTING dropdown: what the wet channel is fed.
  - `DRY->WET` - the input only.
  - `LOOP->WET` - the micro-loop only.
  - `PARALLEL` - both.
- FEEDBACK: how much of the loop and wet channel is recycled back into the loop.
  At the top it piles material up the way a looper does.
- SPREAD: how much per-mode stereo treatment is applied (see below).
- DEGRADE: progressive lo-fi - bit reduction, sample-rate reduction, a rising
  noise floor and asymmetric drive. Applied to what is written back into the loop,
  so it compounds pass over pass rather than sitting as a fixed layer on the output.
- FREEZE: stops the looper recording and repeats what it has indefinitely.

Wet channel (MODE dropdown):

The wet channel's two knobs are labelled WET TIME and WET MOD in the UI; the
micro-looper's are LOOP LEN and LOOP MOD.

- `REVERB` - WET TIME sets decay and size together; WET MOD sets smear, from
  multi-tap delay at minimum to full reverb at maximum.
- `DELAY` - WET TIME sets the delay time and crossfades between times, so changing
  it does not pitch-bend echoes already in flight; WET MOD sets feedback, and at
  maximum the repeats hold rather than decay.
- `SLIP` - an auto-sampler. WET TIME sets the sampling window; WET MOD sets
  playback speed and direction in semitone steps, from an octave down through
  neutral to an octave up, in either direction.

Micro-looper channel (MODE dropdown):

- `ENV` - LOOP LEN sets slice size; LOOP MOD sets detector sensitivity. The loop
  runs until the input crosses the threshold, then the current slice repeats until
  the input falls back below it.
- `TAPE` - LOOP LEN shrinks the loop; LOOP MOD sets speed and direction in
  harmonised steps (quarter, half, unity and double speed, in each direction).
- `STRETCH` - LOOP LEN sets slice size; LOOP MOD sets direction and stretch
  amount, with the loop frozen at the centre of the knob.

CLOCK - the control that ties the two channels together:

CLOCK is Mood's internal sample rate. It sets the length and resolution of the
loops and the quality and time of the wet effects at the same time, and it moves in
semitone steps across three octaves. Because audio captured at one rate and played
back at another changes speed and pitch together, dropping the clock an octave
half-speeds the micro-loop *and* the wet channel. Low settings introduce the
aliasing and downsampling that go with a low sample rate - that grit is the control
working, not an artifact. A side effect worth knowing: Mood costs less CPU at lower
clock settings, because the whole engine runs clock-divided.

SPREAD - each mode makes stereo its own way:

With SPREAD down, the incoming stereo image is preserved: a source panned to one
side comes out on that side. With SPREAD up, each mode alters the image in its own
way rather than simply mixing the channels into each other.

- REVERB - reflections are placed differently per channel, with anti-symmetric
  cross-feed so the two sides carry genuinely different signals.
- DELAY - ping-pong; echoes alternate between the channels, mirroring the panning
  depth of what went in.
- SLIP - smooth panning plus widening.
- ENV - the incoming image is held until the detector fires, then the held slice
  traverses the field, alternating direction on each opening. Speed follows
  LOOP LEN.
- TAPE - the right channel plays the loop forward and the left plays the same loop
  in reverse.
- STRETCH - the grain cloud drifts slowly from side to side.

Other behaviour:

- Bypassing the block clears the loop and wet buffers, so re-enabling it does not
  replay an old loop.

## Performance + Keyboard

Performance strip:

- PITCH wheel (returns to center on release; double-click to center).
- MOD wheel (0..1; double-click to zero).

These feed voice pitch bend and vibrato depth behavior.

Pitch bend range:

- Internal parameter exists (1 to 24 semitones, default 2).
- If your host exposes it, it affects pitch wheel range.

Keyboard:

- Full 88-key range A0 to C8.
- Click/drag keys to play virtual MIDI notes.
- Click velocity is fixed medium (~0.65 normalized).
- Visual key response includes velocity-based effects.

## State Saving

Saved in plugin state:

- All parameter values.
- Includes ADSR (attack/decay/sustain/release) values used by the AMP ENV graph.
- LFO assignment target selection.
- FX processing order.

## Automation Vs Modulation

- Automation controls base parameter values (what host lanes and UI show).
- The AMP ENV graph writes those same base ADSR parameters; automation and graph edits stay in sync.
- Modulation is an internal DSP-time offset from the active LFO lanes.
- Effective DSP value is computed from base + modulation and clamped to parameter range.
- The plugin does not push effective values back to host automation lanes.
- This keeps automation deterministic and prevents host writeback noise while still allowing animated sound.

## Internal Function Map (Developer Guide)

This section describes the major internal functions and what each one controls.

## Source Code Organization

`PX3SynthAudioProcessor` remains the single central orchestrator class, but its
implementation is split across multiple files by responsibility:

- `Source/DSP/PluginProcessor.h`
  - Single authoritative class declaration for `PX3SynthAudioProcessor`.
- `Source/DSP/PluginProcessor.cpp`
  - Core processor orchestration: constructor/destructor, plugin identity,
    JUCE lifecycle entry points, and `processBlock`.
- `Source/DSP/PluginProcessorParameters.cpp`
  - Parameter getters, LFO destination assignment, modulation application
    helper, and FX order API (`get/setFxProcessingOrder`).
- `Source/DSP/PluginProcessorMidi.cpp`
  - MIDI + virtual keyboard handling, note activity tracking, pitch/mod wheel
    state bridges.
- `Source/DSP/PluginProcessorEffects.cpp`
  - Delay/granular/reverb DSP helper implementations and reverb engine setup.
- `Source/DSP/PluginProcessorState.cpp`
  - State serialization/restoration (`getStateInformation`,
    `setStateInformation`, ValueTree create/apply).
- `Source/DSP/PluginProcessorDebug.cpp`
  - Debug event logging, debug state inspection, and round-trip/restore
    diagnostics used by the debug console.

If you are looking for a specific behavior, start with the matching file above,
then follow calls back into `processBlock` in `PluginProcessor.cpp` for the
runtime orchestration path.

### Core Processor Lifecycle

- `prepareToPlay`
  - Sets sample rate, pushes current settings to voices, prepares delay/reverb engines.
- `processBlock`
  - Merges MIDI + virtual keyboard MIDI.
  - Updates note state for UI.
  - Computes LFO signals for the block and applies effective-value modulation at read points.
- Updates all synth voices with oscillator/filter/envelope/performance settings.
  - Renders synth voices.
  - Processes FX in current drag-ordered chain.

### Modulation Core

- `buildLfoAssignableTargets`
  - Builds the assignable destination list from supported float parameters.
- `currentLfoSignalForBlock`
  - Generates the current block LFO signal and tracks debug phase/value state.
- `applyModulationToNormalizedValue`
  - Applies normalized base + depth * signal and clamps to [0, 1].

### Voice + Synthesis

- `SynthVoice::renderNextBlock`
  - Per-sample voice render pipeline.
  - Applies pitch bend/mod wheel smoothing.
  - Renders selected oscillator mode.
  - Applies envelope + filter.
- `SynthVoice::renderOscillatorSample`
  - Switches across all 20 oscillator modes.
  - Applies mode-specific macro behavior and loudness normalization.

Notable mode helpers:

- `renderSuperSaw`, `renderPwm`, `renderAdditive`, `renderFm`, `renderHardSync`, `renderKarplus`, `renderOrgan`, `renderDigital`, `renderPhysical`, `renderRobOsc`, `renderPx3`.

### FX + Ordering

Each FX block is its own component class with the same four-call interface -
`prepare`, `reset`, `updateForBlock`, `processSampleFrame` - so the processor does
not need to know anything about their internals:

- `Source/DSP/Vibe.*` and `Source/DSP/VibeEngine.*`
  - Vibe's shared per-block state. The per-sample application lives inside
    `SynthVoice`, because Vibe is a per-voice stage rather than a bus effect.
- `Source/DSP/Delay.*`
  - `processDelayAlgorithmSample` switches between the seven algorithms;
    `processIsaacGranularSample` / `spawnIsaacGrain` handle the granular grain
    lifecycle.
- `Source/DSP/Reverb.*`
  - `processFdn8` is the shared feedback delay network behind ROOM, HALL and
    CLOUD; the Dattorro plate is separate.
- `Source/DSP/Mood.*`
  - `processInternalStep` runs the clock-divided engine; the loop and wet modes
    are `renderLoop*` and `renderWet*`.
- `getFxProcessingOrder` / `setFxProcessingOrder`
  - Sanitized user order storage and retrieval.

`Source/DSP/PluginProcessorEffects.cpp` is now an empty placeholder - the effect
implementations were extracted into the component classes above.

### Editor/UI Wiring

- `refreshOscillatorModeUI`
  - Changes visible macro knobs and labels by oscillator mode.
- `refreshFxBypassUI`
  - Syncs bypass toggle states and disabled/grayscale UI behavior.
- `mouseDown` / `mouseDrag` / `mouseUp` in editor
  - Handles drag-sort of FX sections and header click bypass toggles.
- `timerCallback` in editor
  - Periodic UI refresh, oscillator viz update, MIDI status, logo vibration/glitch animation.

## Plugin Outputs

Build artifacts:

- Standalone: `build/PX3Synth_artefacts/Standalone/PX3 Synth.app`
- VST3: `build/PX3Synth_artefacts/VST3/PX3 Synth.vst3`
- AU: `build/PX3Synth_artefacts/AU/PX3 Synth.component`

## Troubleshooting

- No sound:
  - Verify MIDI input, note activity label, and that at least one key is active.
  - Check Gain and section bypass states.
- FX order seems wrong:
  - Drag section headers only; order updates on drop and is persisted.
