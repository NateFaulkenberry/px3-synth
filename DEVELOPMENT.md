# P(X3) Development Guide

Current release line: v0.1.0

This guide is for maintainers. It focuses on architecture, state flow, debugging, versioning, and release workflow.

## Architecture Map

High-level runtime flow:

1. MIDI input (host + virtual keyboard)
2. Synth voice rendering into OSCILLATOR BUS (voice-local oscillator sum -> filter -> amp)
3. DRY BUS boundary (post-voice reference)
4. FX send path (scaled by `fxSendGain`) into processor-owned FX chain order
5. FX BUS return (wet delta scaled by `fxReturnGain`)
6. MASTER BUS sum (DRY + FX), then output buffer write

State flow:

1. UI/host automation writes AudioParameters
2. Processor reads parameter base values in processBlock
3. Processor computes transient DSP-effective values (for modulation)
4. `createParameterStateTree()` captures persisted state (`PX3_STATE`)
5. DAW restore (`setStateInformation`) and preset load both apply back through processor state application

Key architectural rule:

- Parameter/base values are persisted and automatable.
- DSP-effective values may include modulation and are transient.
- Do not write transient modulation values back to host parameters.

Bus architecture rule:

- Keep bus boundaries explicit in `processBlock`: OSCILLATOR, DRY, FX, MASTER.
- Do not allocate bus storage in the audio callback.
- LFO remains modulation-only and is never mixed into audio buses.

## Source Layout

Main code lives in Source/

- `Source/DSP/`: processor lifecycle, parameter definitions, voice/sound DSP (`PluginProcessor.*`, `SynthVoice.*`, `SynthSound.*`)
- `Source/UI/`: editor surface + UI components (`PluginEditor.*`, `PerformanceControls.*`, `PianoKeyboard.*`)
- `Source/Preset/`: preset read/write/import/export and metadata (`PresetManager.*`)
- `Source/Core/`: shared lightweight data/types (`PX3Version.h`)

## Where Do I Look?

Want to change oscillator synthesis behavior?
- `Source/DSP/SynthVoice.cpp`

Want to change internal bus routing stages?
- `Source/DSP/PluginProcessor.cpp` (`prepareToPlay`, `processBlock`)

Want to change envelope/filter defaults/ranges?
- parameter definitions in `Source/DSP/PluginProcessor.cpp`
- envelope/filter usage in `Source/DSP/SynthVoice.cpp`

Want to change LFO behavior?
- `currentLfoSignalForBlock()` and `applyLfoToNormalizedValue()` in `Source/DSP/PluginProcessor.cpp`

Want to change ADSR graph UI?
- `EnvelopeGraphComponent` in `Source/UI/PluginEditor.cpp`

Want to change module ordering behavior?
- UI drag/commit in `Source/UI/PluginEditor.cpp`
- canonical order storage/serialization in `Source/DSP/PluginProcessor.cpp`

Want to tune FX send/return gain behavior?
- parameter definitions + registration in `Source/DSP/PluginProcessor.cpp`
- accessors in `Source/DSP/PluginProcessorParameters.cpp`
- mix math in `Source/DSP/PluginProcessor.cpp`

Want to change preset/state serialization?
- processor state tree in `Source/DSP/PluginProcessor.cpp`
- preset file format in `Source/Preset/PresetManager.cpp`
- format details in `docs/PRESETS.md`

Want to change debug console behavior?
- setup/layout/actions in `Source/UI/PluginEditorDebug.cpp`
- debug event/state helpers in `Source/DSP/PluginProcessorDebug.cpp`

Want to change bus RMS debug taps?
- bus meter writes in `Source/DSP/PluginProcessor.cpp`
- debug getters in `Source/DSP/PluginProcessorDebug.cpp`
- debug readout text in `Source/UI/PluginEditorDebug.cpp`

Want to change plugin version?
- edit `PX3_VERSION` in `CMakeLists.txt`

Want to cut a release?
- `scripts/build-release.sh`
- versioning/release section below

## Debug Console Overview

The debug console is a developer tool. It should not alter musical behavior by default.

Major sections:

- Instance info: runtime identity, format, version, and environment
- Module order diagnostics: processor/UI/state order consistency
- ValueTree/serialized state views: persistence inspection
- Parameter inspector + backend controls: direct parameter read/write validation
- LFO + ADSR diagnostics: modulation and envelope observability
- Internal bus diagnostics: OSC/DRY/FX/MASTER RMS readouts
- Preset/state tools: state snapshots, round-trip tests, and developer preset dump

Debug performance HUD metric semantics:

- CPU metric is per-processor-instance block load, computed from `processBlock` elapsed time divided by block audio duration.
- CPU metric is intentionally smoothed for readability; do not treat it as a peak detector.
- RAM metric is an estimate only: process RSS divided by active PX3 processor instances.
- RAM estimate is closest in standalone single-instance use; in DAWs it is shared-process apportionment.

## Threading Notes

- `processBlock` is audio thread: do not add blocking I/O, allocations, or UI calls.
- UI/debug actions run on message thread.
- Cross-thread data handoff uses atomics and minimal lock scopes.

## Versioning (SemVer)

Authoritative version source:

- `PX3_VERSION` in `CMakeLists.txt`

Format:

- `MAJOR.MINOR.PATCH` (validated by CMake and release script)

SemVer meaning:

- MAJOR: breaking compatibility changes
- MINOR: backward-compatible features
- PATCH: backward-compatible fixes/maintenance

Pre-1.0 note:

- P(X3) is currently `0.x.x`; maintainers can evolve quickly, but compatibility discipline is still recommended.

## Release Workflow

1. Update `PX3_VERSION` in `CMakeLists.txt`
2. Build/test locally
3. Commit
4. Create annotated tag:
   - `git tag -a v0.1.0 -m "P(X3) v0.1.0"`
5. Push commit + tag:
   - `git push`
   - `git push origin v0.1.0`
6. Create GitHub release from the tag

Release build command:

- `./scripts/build-release.sh`

Debug-panel-enabled release build (for internal diagnostics only):

- `./scripts/build-release.sh --debug true`

Artifacts are versioned with `v<version>` in their names under `dist/`.
