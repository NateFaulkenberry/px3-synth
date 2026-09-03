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
  `products/PX3Synth/DSP/PluginProcessorInternals.h` are the single source of truth. The
  source-side trim and the fader's maximum are derived from the same constant so
  they cannot disagree.
- Defaults must never be re-applied over restored state. The trim is a parameter
  default, so a preset or DAW session that saved a different fader value gets that
  value back untouched.

## Source Layout

The tree is **shared infrastructure plus products**. Anything more than one
product can use lives in `shared/`; anything only PX3 Synth has lives in
`products/PX3Synth/`. `docs/ECOSYSTEM_ARCHITECTURE.md` is the full account —
this is the map.

```
shared/       DSP, UI components and infrastructure, used by 2+ products
products/     one directory per product; PX3Synth plus seven effects
tools/        developer executables (see Testing And Measurement below)
tests/        PX3Tests, one module per area
updater/      the helper the standalone carries
```

### `shared/`

- `shared/DSP/<Effect>/`: one directory per effect, each a self-contained class
  with the same `prepare` / `reset` / `updateForBlock` / `processSampleFrame`
  interface: `Vibe/` (`Vibe.*` + `VibeEngine.*`), `Delay/`, `Reverb/`, `Mood/`,
  `Doom/`, `Lucy/`, `Chorus/`, `StereoSpread/`.

  **That directory boundary is why each of these could become a product.** The
  Synth and the standalone effect drive the same object through the same
  contract — nothing is copied.

  Each also has a plain-struct settings header (`DoomTypes.h`, `LucyTypes.h`,
  `ChorusTypes.h`, `StereoSpreadTypes.h`) holding one block's worth of
  controls. The Synth builds those through `applyModulationToNormalizedValue`,
  which is what makes every continuous control a modulation destination with no
  per-effect plumbing; a standalone effect has no modulation matrix and reads
  its parameters directly. That is the *only* difference between the two
  consumers.
- `shared/DSP/Core/`: pieces with no effect of their own — `StftEngine.*`
  (short-time Fourier analysis/synthesis with overlap-add, used by DOOM's SOUP
  and by LUCY), `BusInsertTypes.h`, `BusInsertChain.h`, `FetCompressor.*`,
  `SmoothedGain.h`, `OutputCeiling.h`
- `shared/DSP/Analog/`: `AnalogEngine.*` — read
  `docs/ANALOG_ENGINE_ARCHITECTURE.md` first
- `shared/UI/Components/`: the shared widgets — `Card.*`, `CardInner.*`,
  `ModalBackdrop.*`, the PX3 knob
- `shared/UI/Style/UIConfig.json`: external, hot-reloadable styling and layout
- `shared/Infrastructure/Fx/`: the two halves every effect product is built
  from — `FxPluginProcessor` (buses, prepare, the block loop, host tempo,
  parameter state) and `FxCardEditor` (the card, its style, the attachments)
- `shared/Infrastructure/Update/`: product registry, provider interface,
  `UpdateService`, `SemanticVersion`

### `products/PX3Synth/`

- `DSP/`: processor lifecycle, parameter definitions, voice/sound DSP
  (`PluginProcessor.*` split by responsibility, `SynthVoice.*`, `SynthSound.*`)
  - Voice-level building blocks: `OscillatorUnit.*`, `SubOscillator.*`,
    `VoiceFilter.*`, `AmpEnvelope.*`, `EnvelopeGenerator.*`, `LfoGenerator.*`
  - `FxChain.h`: the chain's shape in one place — stage ids, `FxOrder`, and the
    default order. Stage ids are permanent; `MODULE_ORDER` stores them by name
    and the FX panel addresses its cards by id, so append, never renumber
  - `PluginProcessorInternals.h`: gain-structure constants and choice lists
  - `ParametricEQ.*`, and the bus-insert UI's other half
- `UI/`: editor surface and the Synth's own components (`PluginEditor.*`,
  `PerformanceControls.*`, `PianoKeyboard.*`, `SettingsPanel.*`)
- `Preset/`: preset read/write/import/export and metadata (`PresetManager.*`)
- `Assets/`: the icon and wordmark sources

### The seven effect products

`products/PX3Delay`, `PX3Mood`, `PX3Chorus`, `PX3Spread`, `PX3Reverb`,
`PX3Doom`, `PX3Lucy` — five files each (`PluginProcessor.*`,
`PluginEditor.*`, `PluginEntry.cpp`), because everything else is the two shared
halves above. PX3 Chorus's editor is 35 lines. Build one with
`scripts/build-product.sh chorus --vst3`.

## Where Do I Look?

Want to change oscillator synthesis behavior?
- `products/PX3Synth/DSP/SynthVoice.cpp`

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
   - Oscillators: applied in `products/PX3Synth/DSP/SynthVoice.cpp` as part of semitone offset before ratio conversion.
   - Sub oscillator: applied in `products/PX3Synth/DSP/SubOscillator.cpp` and summed with sub octave offset.
   - Frequency ratio relationship is `pow(2.0, semitones / 12.0)`.
- Modulation:
   - Pitch parameters are modulation destinations through the existing assignment system in `products/PX3Synth/DSP/PluginProcessorParameters.cpp` (`buildLfoAssignableTargets`).
   - LFO/Envelope modulation is transient DSP-effective value math and does not overwrite base parameter values.
- State/preset persistence:
   - Included automatically through parameter tree serialization.
   - Additional explicit sub-osc subtree persistence/backfill is handled in `products/PX3Synth/DSP/PluginProcessorState.cpp`.

Want to change the factory presets?
- The library is `products/PX3Synth/Preset/FactoryPresets.cpp` - one entry per preset, with
  named enums for every choice index. That is the only file a sound designer
  needs to touch.
- Values are written in each parameter's OWN units: seconds, hertz, semitones,
  decibels, a 0..1 amount, or a choice index. `PresetManager` converts through
  the parameter itself, so a skewed range is handled correctly and a value in
  the wrong units fails the `factorypresets` suite rather than clamping quietly.
- **Bump `kFactoryLibraryVersion` in `FactoryPresets.h` whenever you change the
  list.** The writer skips files that already exist, so without a bump a
  refreshed preset never reaches anyone who has already run the plugin. On a
  bump the factory tree is swept and rewritten; INIT and the whole user tree are
  left alone.
- `PX3Tests factorypresets` checks that every id exists, every value is in its
  parameter's range, every preset renders audio without clipping or NaNs, every
  effect is showcased somewhere, and that the library is not a volume ride.

Want to change AnalogEngine?
- `shared/DSP/Analog/AnalogEngine.*`. Read `docs/ANALOG_ENGINE_ARCHITECTURE.md` first -
  the stage ordering is not stylistic, and three separate bugs came from getting
  it wrong.
- The premise: the channel runs a forward transfer, the buses run its exact
  inverse, so ONE channel is transparent and the character comes from summing.
  Two rules keep that true, and both were violated at some point:
  **the channel and bus drives must be equal**, and **nothing may filter between
  the forward and inverse transfer** - colour goes before the transfer on the
  forward side and after it on the inverse side.
- Its tuning constants are internal. They live in `AnalogEngine::Tuning`, reach
  the runtime only through the debug console, and are never serialised. A test
  asserts they do not appear in saved state.
- `PX3Tests analog` measures the architecture rather than asserting it: transfer
  invertibility, single-channel THD, accumulation across 1/2/4/8 channels,
  profile separation, THD versus level and frequency, aliasing at 1x against 4x,
  DC, and the Vibe interaction. Current numbers are in
  `docs/ANALOG_ENGINE_TUNING.md`.

Want to change an FX algorithm?
- VIBE: `shared/DSP/Vibe/VibeEngine.cpp` for the shared per-block state,
  `products/PX3Synth/DSP/SynthVoice.cpp` (`applyVibeSourceStage`) for the per-sample stage.
  Vibe is a per-voice effect, not a bus effect - it runs per source before the
  four sources are summed.
- DELAY: `shared/DSP/Delay/Delay.cpp`
- REVERB: `shared/DSP/Reverb/Reverb.cpp`
- MOOD: `shared/DSP/Mood/Mood.cpp`
- DOOM: `shared/DSP/Doom/Doom.cpp` - design notes in `docs/DOOM_DSP_DESIGN.md`
- LUCY: `shared/DSP/Lucy/Lucy.cpp` - design notes in `docs/LUCY_DSP_DESIGN.md`
- CHORUS: `shared/DSP/Chorus/Chorus.cpp` - design notes in `docs/CHORUS_DSP_DESIGN.md`
- SPREAD: `shared/DSP/StereoSpread/StereoSpread.cpp` - design notes in
  `docs/STEREO_SPREAD_DSP_DESIGN.md`

Each of those four has a design document recording what the source hardware
publicly documents, what is inferred from it, and which DSP reproduces each
behaviour. Read the document before changing the algorithm: several choices that
look arbitrary are the point (RELAY's parallel taps rather than feedback,
CHORUS's anti-phase pair, LUCY's masking model, SPREAD's allpass network).

Want to add a new FX?
1. `products/PX3Synth/DSP/FxChain.h`: append a stage id and a module id string, bump
   `kFxStageCount`, and place it in `kDefaultFxOrder`.
2. `products/PX3Synth/DSP/PluginProcessorInternals.h`: append the matching entry to
   `kFxModuleIds`, in the same order.
3. Write the engine with the shared `prepare` / `reset` / `updateForBlock` /
   `processSampleFrame` interface, plus a settings struct.
4. Register parameters in `PluginProcessor.cpp`, accessors in
   `PluginProcessorParameters.cpp`, and a `current<Name>Settings()` in
   `PluginProcessorSource.cpp`.
5. Add the stage to the `switch` in `processBlock`.
6. Build the card with `px3::ui::FxCardComponent` in a `build<Name>Card()` in
   `PluginEditor.cpp`, and hand it to `FxPanel::addCard`.
7. Add a `cards.<key>` block to `UIConfig.json`.
8. Default its amount/mix to **zero**, matching `reverbAmount`: adding an effect
   must not change what existing patches sound like.
9. Give it an inaudible-path early-out (see below).

Every step after 1 and 2 is additive - nothing else in the chain needs to know
the stage count changed, because packing, sanitising and `MODULE_ORDER` restore
are all driven by `kFxStageCount`. An older saved order that predates the new
stage still loads, with the new stage appended in its default slot.

Why every FX has an idle early-out:
- An effect at zero mix that still runs its whole engine costs as much as one
  you can hear. With eight in the chain that is the difference between a
  full-voice patch fitting the real-time budget and not.
- The pattern: when the amount is zero AND its smoother is not still ramping,
  clear the state once and return the input. The smoothing check is what keeps
  it from clicking on the way in or out.
- `reset()` must therefore be allocation-free - fills only - because the idle
  transition calls it from the audio thread.

Want to change internal bus routing stages?
- `products/PX3Synth/DSP/PluginProcessor.cpp` (`prepareToPlay`, `processBlock`)

Want to change mixer channel layout and control arrangement?
- `products/PX3Synth/UI/MixerChannelComponent.cpp`
- `products/PX3Synth/UI/MixPanel.cpp`

Want to change mixer control paint/style behavior?
- `products/PX3Synth/UI/MixerControls.cpp`
- `shared/UI/Style/UIConfig.json` (`mix.fader`, `mix.mute`, `mix.solo`, `mix.meter`)

Current mixer layout note:
- Strip-internal geometry is defined in `products/PX3Synth/UI/MixerChannelComponent.cpp`.
- `mix.channel` in UIConfig currently controls spacing and text sizing, not full strip geometry.

Want to change envelope/filter defaults/ranges?
- parameter definitions in `products/PX3Synth/DSP/PluginProcessor.cpp`
- envelope/filter usage in `products/PX3Synth/DSP/SynthVoice.cpp`

Want to change LFO behavior?
- `currentLfoSignalForBlock()` in `products/PX3Synth/DSP/PluginProcessor.cpp`
- `applyModulationToNormalizedValue()` in `products/PX3Synth/DSP/PluginProcessorParameters.cpp`

Want to change ADSR graph UI?
- `EnvelopeGraphComponent` in `products/PX3Synth/UI/PluginEditor.cpp`

Want to change module ordering behavior?
- The order is edited in ONE place: the signal-flow strip,
  `shared/UI/Fx/FxSignalFlow.cpp`. The FX cards are editors, not ordering controls.
- The strip reports; it does not apply. `FxPanel::onChainOrderChanged` ->
  `PX3SynthAudioProcessorEditor::applyFxChainOrder` -> the processor, which then
  hands the order back through `FxPanel::setChainOrder`. One authority, no UI
  copy that can drift.
- Ordering and grid arithmetic: `shared/UI/Fx/FxChainLayout.cpp`, shared by the
  strip and the grid so there is one implementation and it can be tested without
  a window.
- Canonical storage, packing and sanitising: `products/PX3Synth/DSP/PluginProcessor.cpp`
  and `PluginProcessorInternals.h` (`packFxOrder`, `unpackFxOrder`,
  `sanitizeFxOrder`).
- Serialisation: `MODULE_ORDER` in `products/PX3Synth/DSP/PluginProcessorState.cpp`.

Want to change an FX card's controls or styling?
- `shared/UI/Fx/FxCardComponent.*` is the shared card. It OWNS its controls and is
  asked for them by id; the editor attaches parameters to what it gets back.
  Layout is declared as rows, so a different control set is a different
  declaration rather than a different `resized()`.
- Sizes, colours and fonts come from `cards.<key>.controls` in `UIConfig.json`.
- Looking a control up by an id the card does not have returns null, so a wiring
  typo fails at startup rather than shipping as a knob that does nothing.

Want to tune FX send/return gain behavior?
- parameter definitions + registration in `products/PX3Synth/DSP/PluginProcessor.cpp`
- accessors in `products/PX3Synth/DSP/PluginProcessorParameters.cpp`
- mix math in `products/PX3Synth/DSP/PluginProcessor.cpp`

Want to tune solo/mute routing policy?
- routing policy helpers in `products/PX3Synth/DSP/PluginProcessorParameters.cpp`:
   - `sourceDryAudible`
   - `sourceSendAudible`
   - `fxReturnAudible`

Want to change preset/state serialization?
- processor state tree in `products/PX3Synth/DSP/PluginProcessor.cpp`
- preset file format in `products/PX3Synth/Preset/PresetManager.cpp`
- format details in `docs/PRESETS.md`

Want to change debug console behavior?
- setup/layout/actions in `products/PX3Synth/UI/PluginEditorDebug.cpp`
- debug event/state helpers in `products/PX3Synth/DSP/PluginProcessorDebug.cpp`

Want to change bus RMS debug taps?
- bus meter writes in `products/PX3Synth/DSP/PluginProcessor.cpp`
- debug getters in `products/PX3Synth/DSP/PluginProcessorDebug.cpp`
- debug readout text in `products/PX3Synth/UI/PluginEditorDebug.cpp`

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
under `tools/` (with the test suite in `tests/Tests/`) and are configured by
CMake automatically.

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

`PX3Tests` also has two developer modes that are not pass/fail:

- `params` prints every parameter, its type, its default as the NORMALISED value
  a preset file stores, and the normalised value of each choice option. Preset
  definitions are written against real units and converted, but this is how you
  check what a parameter's range and options actually are.
- `artifacts` scans DOOM and LUCY for discontinuities across their mode space
  and prints the worst jump per configuration, measured against the LOCAL slope
  rather than an absolute threshold - a loud passage legitimately has large
  deltas and a click in a quiet one does not. Anything above about 8 is worth
  listening to.
- `installpresets` runs the real factory-library install and lists what landed
  on disk. It is a developer action rather than a test because it writes into
  the user's application-support directory.

`PX3Tests` takes an optional suite filter: `subosc`, `osc`, `ampenv`, `modenv`,
`lfo`, `vibe`, `reverb`, `comb`, `delay`, `mood`, `doom`, `lucy`, `chorus`,
`spread`, `analog`, `cardstyle`, `cardinner`, `fxchain`, `fx`, `preset`,
`factorypresets`, `editor`, `integration`.

The four newest FX suites each carry the measurement that IS their design brief,
so a refactor that quietly breaks the premise fails rather than passing quietly:

| Suite | The measurement that matters |
| --- | --- |
| `doom` | the looper is always listening (engage it, feed silence, still get audio); RELAY's repeats do not fade |
| `lucy` | INVERSE is brighter and thinner than STANDARD; PACKET LOSS produces drop-out runs that CLEAN does not |
| `chorus` | pitch stability against a naive single-delay chorus built inside the test; mono collapse at five AMOUNT settings |
| `spread` | side energy CREATED from a mono source; mono-sum level retained at five AMOUNT settings |

Stochastic behaviour in DOOM, LUCY and CHORUS runs off a seeded generator
(`setSeed`), so every one of those tests is reproducible.

A note on measuring these, learned the hard way several times: **check the
instrument before changing the DSP.** A warm-up that ends on a silent bar makes a
correct looper look broken; an impulse fired during a parameter ramp makes the
dry path louder than the effect; comparing a processed signal against an
undelayed reference measures latency rather than processing. Every one of those
looked like a DSP bug first.

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

- `shared/UI/Style/UIConfig.json`
- `shared/UI/Style/UIConfig.h`
- `shared/UI/Style/UIConfig.cpp`
- `shared/UI/Style/UIConfigManager.h`
- `shared/UI/Style/UIConfigManager.cpp`

Editor integration points:

- `resolveUiConfigFile()` in `products/PX3Synth/UI/PluginEditor.cpp` decides active config path.
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
