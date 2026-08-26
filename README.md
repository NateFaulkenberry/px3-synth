# P(X3) Synth

P(X3) is a polyphonic JUCE synthesizer with a multi-mode source engine, a channel-style mixer, and reorderable FX (VIBE, Delay, Reverb, Mood). This README is a full operating guide for new users and a function-level map for developers.

Current version: v0.1.0

For dedicated build/install/release workflow documentation, see `docs/BUILDING.md`.
For preset system format and storage details, see `docs/PRESETS.md`.
For developer architecture, maintenance map, and release workflow, see `DEVELOPMENT.md`.

## What You Get

- 16-voice poly synth engine.
- 20 oscillator modes (classic + experimental + PX3).
- Three macro knobs whose meaning changes by oscillator mode.
- Dedicated SUB + OSC1/OSC2/OSC3 source channels.
- Filter, amp envelope, and master gain section.
- Three LFO modulation sources with assignable destinations.
- Four FX blocks with bypass and drag/drop processing order.
- Mixer channel controls: level, pan, send, mute, solo, per-channel meter.
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
  - ROOM
  - PLATE
  - HALL
  - CLOUD

What it does:

- ROOM: compact early-reflection style space with tighter decay behavior.
- PLATE: denser plate-style diffusion with a brighter, smoother tail.
- HALL: larger multi-line hall network with wider, longer ambience.
- CLOUD: expansive modulated diffusion mode with cloud feedback/diffusion shaping.
- Shared post-processing across modes includes stereo width shaping, wet DC filtering, gentle peak control/saturation at high wet levels, and output compensation to keep loudness more stable as INTENSITY rises.

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
- `applyLfoToNormalizedValue`
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

- `processDelayAlgorithmSample`
  - Delay algorithm switch and per-sample processing.
- `processIsaacGranularSample` / `spawnIsaacGrain`
  - Granular-delay grain lifecycle.
- `processReverbSampleFrame`
  - Reverb frame processing.
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

- Standalone: `build/PX3Synth_artefacts/Standalone/PX3 Synth.app`
- VST3: `build/PX3Synth_artefacts/VST3/PX3 Synth.vst3`
- AU: `build/PX3Synth_artefacts/AU/PX3 Synth.component`

## Troubleshooting

- No sound:
  - Verify MIDI input, note activity label, and that at least one key is active.
  - Check Gain and section bypass states.
- FX order seems wrong:
  - Drag section headers only; order updates on drop and is persisted.
