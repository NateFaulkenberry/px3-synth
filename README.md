# P(X3) Synth

P(X3) is a polyphonic JUCE synthesizer with a multi-mode oscillator, source engines (image/audio), and reorderable FX (VIBE, Delay, Reverb). This README is a full operating guide for new users and a function-level map for developers.

Current version: v0.1.0

For dedicated build/install/release workflow documentation, see `docs/BUILDING.md`.
For preset system format and storage details, see `docs/PRESETS.md`.
For developer architecture, maintenance map, and release workflow, see `DEVELOPMENT.md`.

## What You Get

- 16-voice poly synth engine.
- 20 oscillator modes (classic + experimental + PX3).
- Three macro knobs whose meaning changes by oscillator mode.
- Filter, amp envelope, and master gain section.
- One global LFO modulation source with assignable destination.
- Source Engine system:
  - Image Engine (image->wavetable + animation + modulation routing).
  - Audio Engine (granular source playback + animation).
- Three FX blocks with bypass and drag/drop processing order.
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

JUCE is fetched automatically through CMake FetchContent.

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

```bash
./scripts/run-standalone.sh --debug true
./scripts/run-standalone.sh --debug false
```

- If building manually with CMake:

```bash
cmake -B build -G Ninja -DPX3_DEBUG_PANEL=ON
cmake --build build
```

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
  -> Synth Voices (oscillator mode + macros + envelope + filter)
  -> FX Chain (user-order: VIBE / Delay / Reverb)
  -> Master Output
```

Important routing rules:

- When OSC mode is WAVETABLE, Image Engine is reserved for oscillator generation.
- In WAVETABLE mode, Audio source selection is disabled.
- Image Target modulation does not drive FX in WAVETABLE mode.

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
  - Also mirrors Image Position for visible feedback in WAVETABLE mode.
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

- Freq: global LFO frequency (Hz).
- Assign: one destination selected from supported float parameters.

Behavior:

- ONE LFO means a single shared modulation signal is generated per processing block.
- The selected destination receives normalized modulation:
  - effective = clamp(base + (depth * lfoSignal))
- UI knobs/sliders still represent and automate base values only.
- DSP reads effective values for sound generation; host-visible parameter values are never written by LFO.

Current limits:

- One destination at a time (single assignment).
- One global depth setting in processor code (no per-target depth UI yet).
- Block-rate LFO evaluation (intentionally lightweight; no sample-rate modulation path yet).

## OUTPUT Section

Controls:

- Gain (0.0 to 1.0 master voice gain factor).

## Source Engine Section

Top toggles:

- IMAGE
- AUDIO

Only one source is active at a time.

WAVETABLE exception:

- If OSC MODE is WAVETABLE, AUDIO selection is disabled and source is forced to IMAGE.

### Image Engine Panel

File input:

- Drop image file or click preview area.
- Supported: .png, .jpg, .jpeg, .bmp, .gif.

Controls:

- POS: base frame position through generated wavetable.
- ANIM: animation depth around POS.
- RATE: animation rate (or rate basis when sync mode active).
- MODE: Forward / Reverse / PingPong.
- TARGET: where Image Engine control signal modulates:
  - VIBE
  - Delay
  - Reverb
- OFF: disables Image routing (switches source mode away from Image when allowed).
- RESET: restores default image engine params and default internal wavetable.

TARGET behavior details:

- Processor extracts a control signal from current image wavetable content.
- That signal is smoothed, converted to a scale, and applied to only one chosen FX amount.
- In WAVETABLE mode, TARGET is disabled because Image Engine is oscillator-reserved.

### Audio Engine Panel

File input:

- Drop audio file or click waveform area.
- Supported: .wav, .aiff, .aif, .flac, .mp3, .ogg.

Controls:

- POS: read position in loaded audio source.
- GRAIN: granular grain size behavior.
- TEXT: texture/random spread behavior.
- ANIM: animation depth around POS.
- RATE: animation rate.
- MODE: Forward / Reverse / PingPong.
- SYNC: Free, 1 Bar, 1/2, 1/4, 1/8, 1/16.
- OFF: disables Audio routing (switches source mode to Image).
- RESET: resets params and unloads active audio source.

## FX Sections

You have three FX blocks:

- VIBE
- DELAY
- REVERB

Each block has a corner bypass toggle and can be reordered by drag-and-drop.

Bypass semantics:

- Checked = enabled (not bypassed).
- Unchecked = bypassed.

Header behavior:

- Clicking section header toggles its bypass.
- Dragging header reorders blocks; order is committed to DSP and saved in state.

### VIBE

Controls:

- WARMTH knob: effect amount.

What it does:

- Global analog-imperfection amount control distributed through per-voice behavior.
- Adds correlated drift, subtle saturation, asymmetry, filter movement, PSU/temperature motion, and noise.
- VIBE is an original texture system, not a hardware emulation claim.

### Delay

Controls:

- Main amount knob (center knob in delay card): granular-delay amount/mix driver.
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

What it does:

- Granular algorithm spawns windowed grains from delay memory.
- Other algorithms use classic delay reads with per-mode coloration and feedback topology.

### Reverb

Controls:

- INTENSITY knob: reverb amount.
- ALGO dropdown:
  - Hall
  - Plate
  - Room
  - Cavern
  - Moon

What it does:

- Core reverb parameters adjust per algorithm.
- Moon adds extra modulated reflection layer for spacious/otherworldly tail.
- Output compensation helps keep perceived loudness stable across wetness.

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
- Last loaded image path.
- Last loaded audio path.

On restore, if those files still exist, the plugin asynchronously reloads them.

## Automation Vs Modulation

- Automation controls base parameter values (what host lanes and UI show).
- The AMP ENV graph writes those same base ADSR parameters; automation and graph edits stay in sync.
- Modulation is an internal DSP-time offset from the ONE LFO.
- Effective DSP value is computed from base + modulation and clamped to parameter range.
- The plugin does not push effective values back to host automation lanes.
- This keeps automation deterministic and prevents host writeback noise while still allowing animated sound.

## Internal Function Map (Developer Guide)

This section describes the major internal functions and what each one controls.

### Core Processor Lifecycle

- `prepareToPlay`
  - Sets sample rate, pushes current settings to voices, prepares delay/reverb engines.
- `processBlock`
  - Merges MIDI + virtual keyboard MIDI.
  - Updates note state for UI.
  - Computes ONE LFO signal for the block and applies effective-value modulation at read points.
  - Updates animation positions (image/audio).
  - Updates all synth voices with oscillator/filter/envelope/performance/source settings.
  - Renders synth voices.
  - Processes FX in current drag-ordered chain.

### Modulation Core

- `buildLfoAssignableTargets`
  - Builds the assignable destination list from supported float parameters.
- `currentLfoSignalForBlock`
  - Generates the current block LFO signal and tracks debug phase/value state.
- `applyLfoToNormalizedValue`
  - Applies normalized base + depth * signal and clamps to [0, 1].

### Voice + Synthesis

- `SynthVoice::renderNextBlock`
  - Per-sample voice render pipeline.
  - Applies pitch bend/mod wheel smoothing.
  - Samples image/audio source content.
  - Renders selected oscillator mode.
  - Applies envelope + filter.
- `SynthVoice::renderOscillatorSample`
  - Switches across all 20 oscillator modes.
  - Applies mode-specific macro behavior and loudness normalization.

Notable mode helpers:

- `renderSuperSaw`, `renderPwm`, `renderAdditive`, `renderFm`, `renderHardSync`, `renderKarplus`, `renderOrgan`, `renderDigital`, `renderPhysical`, `renderRobOsc`, `renderPx3`.

### Source Engine Internals

- `requestImageLoadAsync` / `requestAudioLoadAsync`
  - Asynchronous file loading via thread pools.
- `disableImageEngine` / `disableAudioEngine`
  - Turns off routing by switching source mode and animation as needed.
- `resetImageEngine` / `resetAudioEngine`
  - Restores defaults and clears loaded media state.
- `updateImageAnimationPosition` / `updateAudioAnimationPosition`
  - Computes animated POS each block (free-run or tempo-synced).
- `computeImageTargetControlSignal`
  - Scans image wavetable and creates modulation control signal for target FX.

### FX + Ordering

- `processDelayAlgorithmSample`
  - Delay algorithm switch and per-sample processing.
- `processIsaacGranularSample` / `spawnIsaacGrain`
  - Granular-delay grain lifecycle.
- `processReverbSampleFrame`
  - Reverb frame processing, including Moon reflections.
- `getFxProcessingOrder` / `setFxProcessingOrder`
  - Sanitized user order storage and retrieval.

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

- Standalone: `build/SynthProject_artefacts/Standalone/PX3 Synth.app`
- VST3: `build/SynthProject_artefacts/VST3/PX3 Synth.vst3`
- AU: `build/SynthProject_artefacts/AU/PX3 Synth.component`

## Troubleshooting

- No sound:
  - Verify MIDI input, note activity label, and that at least one key is active.
  - Check Gain and section bypass states.
  - If in WAVETABLE mode, remember Audio source is disabled by design.
- Dropped file does nothing:
  - Confirm extension is supported.
  - Try click-to-load from chooser.
- FX order seems wrong:
  - Drag section headers only; order updates on drop and is persisted.
