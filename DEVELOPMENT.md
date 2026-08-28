# P(X3) Development Guide

Current release line: v0.1.0

This guide is for maintainers. It focuses on architecture, state flow, debugging, versioning, and release workflow.

## Architecture Map

High-level runtime flow:

1. MIDI input (host + virtual keyboard)
2. Synth voice rendering into source stems (SUB, OSC1, OSC2, OSC3)
3. Per-source mixer stage (pan/send/mute/solo gating)
4. DRY BUS boundary (post-source-gate reference)
5. FX send path (per-source send * `fxSendGain`) into processor-owned FX chain order
6. FX BUS return (wet delta scaled by `fxReturnGain`, return pan/mute/solo)
7. MASTER BUS sum (DRY + FX), then output buffer write

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

- Keep bus boundaries explicit in `processBlock`: source stems, DRY, FX, MASTER.
- Do not allocate bus storage in the audio callback.
- LFO remains modulation-only and is never mixed into audio buses.

Gain structure rule:

- The -4 dB of per-channel modulation headroom lives on the **sources**, not
  inside the mixer faders. A fader at unity is unity, and the strip shows the
  channel's real gain.
- `kSourceHeadroomDb`, `sourceHeadroomGain()` and `channelFaderMaxGain()` in
  `Source/DSP/PluginProcessorInternals.h` are the single source of truth. The
  source-side trim and the fader's maximum are derived from the same constant so
  they cannot disagree.
- Defaults must never be re-applied over restored state. The trim is a parameter
  default, so a preset or DAW session that saved a different fader value gets that
  value back untouched.

## Source Layout

Main code lives in Source/

- `Source/DSP/`: processor lifecycle, parameter definitions, voice/sound DSP (`PluginProcessor.*`, `SynthVoice.*`, `SynthSound.*`)
  - FX components, each a self-contained class with the same
    `prepare` / `reset` / `updateForBlock` / `processSampleFrame` interface:
    `Vibe.*` + `VibeEngine.*`, `Delay.*`, `Reverb.*`, `Mood.*`
  - Voice-level building blocks: `OscillatorUnit.*`, `SubOscillator.*`,
    `VoiceFilter.*`, `AmpEnvelope.*`, `EnvelopeGenerator.*`, `LfoGenerator.*`
  - Shared internals: `PluginProcessorInternals.h` (gain-structure constants,
    choice lists), `SmoothedGain.h`, `OutputCeiling.h`
- `Source/Tools/`: developer executables (see Testing And Measurement below)
- `Source/UI/`: editor surface + UI components (`PluginEditor.*`, `PerformanceControls.*`, `PianoKeyboard.*`)
- `Source/Preset/`: preset read/write/import/export and metadata (`PresetManager.*`)
- `Source/Core/`: shared lightweight data/types (`PX3Version.h`)

## Where Do I Look?

Want to change oscillator synthesis behavior?
- `Source/DSP/SynthVoice.cpp`

Developer note: oscillator/sub pitch controls

- OSC/Sub pitch controls are real automatable parameters (not UI-only state).
- Parameter IDs:
   - `osc1Pitch`
   - `osc2Pitch`
   - `osc3Pitch`
   - `subOscPitch`
- Range/default: continuous `-12.0 .. +12.0` semitones, default `0.0`.
- Display format: signed semitone string (example: `+4.62 st`).
- DSP mapping:
   - Oscillators: applied in `Source/DSP/SynthVoice.cpp` as part of semitone offset before ratio conversion.
   - Sub oscillator: applied in `Source/DSP/SubOscillator.cpp` and summed with sub octave offset.
   - Frequency ratio relationship is `pow(2.0, semitones / 12.0)`.
- Modulation:
   - Pitch parameters are modulation destinations through the existing assignment system in `Source/DSP/PluginProcessorParameters.cpp` (`buildLfoAssignableTargets`).
   - LFO/Envelope modulation is transient DSP-effective value math and does not overwrite base parameter values.
- State/preset persistence:
   - Included automatically through parameter tree serialization.
   - Additional explicit sub-osc subtree persistence/backfill is handled in `Source/DSP/PluginProcessorState.cpp`.

Want to change an FX algorithm?
- VIBE: `Source/DSP/VibeEngine.cpp` for the shared per-block state,
  `Source/DSP/SynthVoice.cpp` (`applyVibeSourceStage`) for the per-sample stage.
  Vibe is a per-voice effect, not a bus effect - it runs per source before the
  four sources are summed.
- DELAY: `Source/DSP/Delay.cpp`
- REVERB: `Source/DSP/Reverb.cpp`
- MOOD: `Source/DSP/Mood.cpp`

Want to change internal bus routing stages?
- `Source/DSP/PluginProcessor.cpp` (`prepareToPlay`, `processBlock`)

Want to change mixer channel layout and control arrangement?
- `Source/UI/MixerChannelComponent.cpp`
- `Source/UI/MixPanel.cpp`

Want to change mixer control paint/style behavior?
- `Source/UI/MixerControls.cpp`
- `Source/UI/UIConfig.json` (`mix.fader`, `mix.mute`, `mix.solo`, `mix.meter`)

Current mixer layout note:
- Strip-internal geometry is defined in `Source/UI/MixerChannelComponent.cpp`.
- `mix.channel` in UIConfig currently controls spacing and text sizing, not full strip geometry.

Want to change envelope/filter defaults/ranges?
- parameter definitions in `Source/DSP/PluginProcessor.cpp`
- envelope/filter usage in `Source/DSP/SynthVoice.cpp`

Want to change LFO behavior?
- `currentLfoSignalForBlock()` in `Source/DSP/PluginProcessor.cpp`
- `applyModulationToNormalizedValue()` in `Source/DSP/PluginProcessorParameters.cpp`

Want to change ADSR graph UI?
- `EnvelopeGraphComponent` in `Source/UI/PluginEditor.cpp`

Want to change module ordering behavior?
- UI drag/commit in `Source/UI/PluginEditor.cpp`
- canonical order storage/serialization in `Source/DSP/PluginProcessor.cpp`

Want to tune FX send/return gain behavior?
- parameter definitions + registration in `Source/DSP/PluginProcessor.cpp`
- accessors in `Source/DSP/PluginProcessorParameters.cpp`
- mix math in `Source/DSP/PluginProcessor.cpp`

Want to tune solo/mute routing policy?
- routing policy helpers in `Source/DSP/PluginProcessorParameters.cpp`:
   - `sourceDryAudible`
   - `sourceSendAudible`
   - `fxReturnAudible`

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

## Testing And Measurement

Four developer executables are built alongside the plugin. All are console apps
under `Source/Tools/` and are configured by CMake automatically.

| Target | What it is for |
| --- | --- |
| `PX3Tests` | Component correctness suite - the main regression gate |
| `PX3Diag` | Signal-path isolation, real-time safety, memory and soak diagnostics |
| `PX3Bench` | CPU benchmark across representative scenarios |
| `PX3SmokeTest` | Minimal end-to-end render check |

Build and run:

```bash
cmake -B build/diag -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/diag --target PX3Tests
build/diag/PX3Tests_artefacts/RelWithDebInfo/PX3Tests
```

`PX3Tests` takes an optional suite filter: `subosc`, `osc`, `ampenv`, `modenv`,
`lfo`, `vibe`, `reverb`, `delay`, `mood`, `fx`, `preset`, `integration`.

It also has measurement modes that print characterisation tables rather than
pass/fail results. These exist so that "sounds better" can be argued from numbers:

- `probe` - general parameter sweeps
- `gainstage` - level through each stage of the chain
- `vibemetrics` - per-voice drift correlation, DC offset, level neutrality
- `reverbmetrics` - echo density, spectral flatness, decay nonlinearity, RT60
- `delaymetrics` - echo times, zero-amount transparency, stability, sample-rate consistency
- `delaystress` - control sweeps followed by silence, to catch tails that never decay
- `moodmetrics` - per-mode stereo behaviour, clock transposition, degrade response

Important: `PX3_DIAGNOSTICS` is 1 only for `PX3Diag`. `PX3Tests`, `PX3Bench` and
`PX3SmokeTest` build with it at 0 so they measure the shipping code path.

### Writing DSP Tests - Lessons Worth Keeping

Most of the time lost on this codebase has gone to bad measurements, not bad code.
Several "bugs" turned out to be faults in the instrument:

- **Match the instrument to the quantity.** A pitch or length control cannot be
  measured by RMS. A control that is deliberately level-neutral cannot be measured
  by level. Vibe's amount is measured by harmonic content added to a sine, because
  a sine has none of its own.
- **Ratios need a denominator with something in it.** A sub-30 Hz energy share
  measured in a near-silent window swung between 0.06 and 0.31 run to run purely
  from grain-spawn randomness. Measure on sustained input.
- **Some things can only be measured against state.** Mood's CLOCK preserves pitch
  when the clock is held constant, by definition - capture, freeze, *then* move it.
- **Static-parameter tests miss a whole class of fault.** Three delay bugs only
  appeared while a control was moving. `delaystress` exists for that.
- **Prefer standard measures to hand-rolled ones.** A hand-rolled decay-ripple
  metric gave a different answer depending on where its window started and made a
  working reverb algorithm look broken. ISO 3382 decay-curve nonlinearity did not.
- **`juce::Random::setSeed()` is a silent no-op on the system Random.** Anything
  using `getSystemRandom()` varies run to run; average over several renders.
- **`juce::Synthesiser` retargets an existing voice** when the same pitch is played
  twice, so multi-voice behaviour must be measured on the engine directly.

### DSP Invariants Worth Knowing

- **Any gain-adding stage inside a feedback loop must be normalised**, or the
  feedback coefficient is not the loop gain. Two separate delay runaways came from
  this: an un-normalised head bump, and a hysteresis bias term with a DC gain of
  1/(1-k).
- **A state-variable filter's bandpass output peaks at Q, not unity.** Normalising
  as though it were unity leaves Q times more gain in the loop than budgeted.
- **A compander's two halves must be inverses.** A 2:1 compressor produces `x^0.5`,
  so the expander gain must be linear in the envelope. Square roots on both sides
  give a round trip of `x^0.75`, and a sub-linear loop gain has a stable non-zero
  attractor - it converges on a permanent tone regardless of input.
- **Feedback should set a decay time, not a per-repeat coefficient**, or the same
  knob position means wildly different decays at different delay lengths. Use
  Jot's rule: `g = 10^(-3*delaySeconds/decaySeconds)`.
- **Noise must never be injected unconditionally into a feedback path.** It
  accumulates to `noise/(1-g)` and stays there. Gate it by signal envelope.
- **Bypass must clear buffers**, or re-enabling replays a stale tail. Clear after
  any fade-to-silence completes so the bypass itself does not click.
- **Panning does not decorrelate.** Two channels carrying the same mono signal at
  different levels still measure as correlated. Cross-feed must be anti-symmetric
  to produce genuinely different signals.
- **Mid/side widening must be level-compensated.** Scaling both channels equally
  leaves the side-to-mid ratio - the actual width - untouched, so compensation
  costs nothing but the extra peak.

## Threading Notes

- `processBlock` is audio thread: do not add blocking I/O, allocations, or UI calls.
- UI/debug actions run on message thread.
- Cross-thread data handoff uses atomics and minimal lock scopes.

## UIConfig Runtime Notes

Primary files:

- `Source/UI/UIConfig.json`
- `Source/UI/UIConfig.h`
- `Source/UI/UIConfig.cpp`
- `Source/UI/UIConfigManager.h`
- `Source/UI/UIConfigManager.cpp`

Editor integration points:

- `resolveUiConfigFile()` in `Source/UI/PluginEditor.cpp` decides active config path.
- `loadUiConfig(false)` runs in `timerCallback()` (30 Hz) and calls `reloadIfChanged()`.
- `loadUiConfig(true)` is used on initial load and path switches.

Resolution policy:

- Debug (`JUCE_DEBUG` or `PX3_DEBUG_PANEL`) prefers source-tree config for fast iteration.
- `PX3_UI_CONFIG_PATH` can force a specific file in debug-mode runtime.
- Non-debug runtime does not probe source tree and only loads from bundle locations.

Failure behavior:

- Missing/unreadable/invalid JSON does not crash UI config flow.
- Parse failures keep last-known-good config active.
- UI config path switches and hot reloads emit debug event-log entries with full file paths.

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
