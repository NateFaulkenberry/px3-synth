# P(X3) Development Guide

Current release line: v0.1.0

This guide is for maintainers. It focuses on architecture, state flow, debugging, versioning, and release workflow.

## Architecture Map

High-level runtime flow:

1. MIDI input (host + virtual keyboard)
2. Synth voice rendering (oscillator mode + macros + envelope + filter)
3. FX chain (Harmonic Drive, Delay, Reverb) in processor-owned module order
4. Output buffer

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

## Source Layout

Main code lives in Source/

- `PluginProcessor.*`: processor lifecycle, parameters, DSP orchestration, state serialization
- `SynthVoice.*` / `SynthSound.*`: per-voice synthesis and sound model
- `PluginEditor.*`: main UI, attachments, debug console, ADSR graph
- `PresetManager.*`: `.px3preset` read/write/import/export and metadata
- `ImageEnginePanel.*` / `AudioEnginePanel.*` / `SourceEnginePanel.*`: source engine UI and control surfaces
- `PerformanceControls.*`, `PianoKeyboard.*`: virtual performance and key input widgets
- `PX3Version.h`: centralized runtime version helpers

## Where Do I Look?

Want to change oscillator synthesis behavior?
- `Source/SynthVoice.cpp`

Want to change envelope/filter defaults/ranges?
- parameter definitions in `Source/PluginProcessor.cpp`
- envelope/filter usage in `Source/SynthVoice.cpp`

Want to change LFO behavior?
- `currentLfoSignalForBlock()` and `applyLfoToNormalizedValue()` in `Source/PluginProcessor.cpp`

Want to change ADSR graph UI?
- `EnvelopeGraphComponent` in `Source/PluginEditor.cpp`

Want to change module ordering behavior?
- UI drag/commit in `Source/PluginEditor.cpp`
- canonical order storage/serialization in `Source/PluginProcessor.cpp`

Want to change preset/state serialization?
- processor state tree in `Source/PluginProcessor.cpp`
- preset file format in `Source/PresetManager.cpp`
- format details in `docs/PRESETS.md`

Want to change debug console behavior?
- setup/layout/actions in `Source/PluginEditor.cpp`
- debug event/state helpers in `Source/PluginProcessor.cpp`

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
- Preset/state tools: state snapshots, round-trip tests, and developer preset dump

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
