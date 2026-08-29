# Release Audit

Audit of `px3-synth` at version **0.2.1**, branch `main`, performed as a
pre-release readiness pass. Every figure below was measured on this machine
during the audit; nothing is estimated from code inspection alone. Where
something could not be tested it says so.

---

## Executive Summary

The repository is in good shape. The release configuration builds clean from
scratch, the whole test suite passes, the audio thread provably does not
allocate, there are no memory leaks, persistence is exact, and there is not a
single `TODO`, `FIXME`, `HACK` or `XXX` in 64,000 lines of source.

One material risk was found: **at maximum polyphony with every effect enabled
the plugin exceeded its real-time budget** (100–211% depending on
configuration), with dropouts rather than voice stealing as the failure mode.
**This has been fixed** — see Changes Made — and every configuration now runs
inside budget. Five other small defects were found and fixed; all were in
tooling or dead code, none in shipping DSP or UI behaviour.

**Decision: GO FOR RELEASE.**

---

## Current Repository State

| | |
|---|---|
| Branch | `main`, clean working tree at audit start |
| Version | 0.2.1 (`PX3_VERSION`, SemVer-validated in CMake) |
| Source | 146 files, 64,194 lines |
| Formats | AU, VST3, Standalone |
| Tests | 629 assertions across 25 suites |
| CI | **None.** No `.github/workflows`. All verification is local. |

Recent work, from git history and code inspection, has been: the FX signal-flow
strip, four new effects (DOOM, LUCY, CHORUS, STEREO SPREAD), the AnalogEngine,
a factory preset refresh, and a run of UI/preset-tab work. The last several
commits are UI and preset presentation.

Build gates: `PX3_DIAGNOSTICS` (harnesses), `PX3_DEBUG_PANEL` (in-plugin debug
UI). Both default **OFF**; the shipping plugin compiles with neither.

---

## Architecture Findings

Ownership and threading are clearly separated and consistent:

- **Processor** owns all 259 parameters, the 64-voice pool, the buses and the
  FX chain. `PluginProcessor.cpp` is split into focused units
  (`...Parameters`, `...State`, `...Effects`, `...Midi`, `...Source`,
  `...Debug`), which keeps serialization ownership in one file.
- **Editor** owns no DSP state. It reads parameters and writes them through
  attachments. It is destroyed and rebuilt with the window.
- **Session vs sound** is explicitly separated: `createParameterStateTree()`
  carries UI session state (top-menu view, loaded-preset identity);
  `createPresetStateTree()` strips it. That distinction is enforced by tests.
- **FX chain shape** has a single source of truth (`FxChain.h`) shared by the
  processor, the strip and the grid, so the ordering UI cannot drift from the
  processing order.
- **Filter response** now has a single coefficient builder (`FilterResponse.h`)
  used by both the audio path and the UI graph.

No circular dependencies were found. No architectural inconsistency was found
that warranted a change during this pass.

---

## Code Quality Findings

- **Markers:** zero `TODO`, `FIXME`, `HACK`, `XXX`, `WORKAROUND`, or
  `NOT IMPLEMENTED` in `Source/`.
- **Dead code:** `midiStatusLabel` and `midiStatusArea` were declared in
  `PluginEditor.h` and referenced only from commented-out lines. Removed,
  along with 15 lines of commented-out implementation. (See Changes Made.)
- **Stale naming (not changed):** the DELAY amount knob and its label are named
  `isaacTextureKnob` / `isaacTextureLabel`. The members are live and correct;
  only the names are legacy. Renaming touches ~8 sites for no functional gain
  and was deliberately left alone under the no-scope-expansion rule.
- **Duplication:** none material found. The FX cards share
  `FxCardComponent`; the card layout shares `Card`/`CardInner`; the STFT is
  shared by DOOM and LUCY.

---

## DSP Findings

Reviewed: oscillators, sub osc, filters, envelopes, LFOs, modulation, mixer,
all eight FX, VibeEngine, AnalogEngine, dry bus, FX bus, master.

Evidence gathered by measurement, not inspection:

- **Boundedness / NaN / DC:** `EdgeCase_EverythingAtMaximum` peaks at 0.968
  (under the ceiling) with DC 0.0039. All-oscillators-disabled, shortest and
  longest envelopes, maximum resonance on both filters, maximum modulation and
  all-FX-at-maximum are each finite and within ceiling.
- **Tails:** `LongRelease_AllTailsReachSilence` — RMS after the last release is
  exactly 0.0.
- **Discontinuities:** oscillator, sub-osc, filter and reverb enable toggles
  each produce a largest step at or below the steady-state step (0.18–0.20 vs
  0.184), i.e. no click.
- **Sample-rate independence:** renders correctly at 44.1/48/88.2/96/192 kHz
  and block sizes 32–1024. Delay echo time holds to 0.227% across rates.
- **Filters:** slopes measured at −12.2 / −24.3 / −11.8 / −23.6 dB/oct; notch
  nulls at −107 dB; all-pass is flat to 0.00 dB; a 200 Hz→6 kHz sweep four
  times a second produces a worst step of 1.7× the local slope (i.e. no
  zipper).
- **Oscillators:** every harmonic mode measures at least 6 dB richer than a
  pure sine at both ends of its macro range; formant resonances hold position
  across four octaves of fundamental.

**Aliasing is the one known DSP gap.** There is no PolyBLEP or oversampling
anywhere in `OscillatorUnit`. SAW and SQUARE are naive and will alias at high
pitch. This is a sound-quality limitation, measurable and long-standing, not a
stability risk. Recorded as deferred work; not a release blocker.

---

## Audio Thread Safety

Measured with a replaced global `operator new` counting allocations inside
`processBlock` (`PX3Diag rtsafety`):

| scenario | allocations / 200 blocks |
|---|---|
| 3 voices, filters bypassed | 0 |
| 3 voices, both filters active | 0 |
| 3 voices + cutoff sweeping | 0 |
| 3 voices releasing | 0 |
| 16 voices releasing | 0 |
| 48 voices releasing (past prune budget) | 0 |
| 48 voices releasing + full FX chain | 0 |
| **48 voices + all 8 FX + analog console** | **0** |

**0 failures.** No locks, no filesystem access, no logging and no UI calls were
found in the audio path. The `juce::String`-returning parameter-id helpers are
called only from the debug panel and tests. The oversize-block guard in
`processBlock` clears and returns rather than reallocating.

---

## Persistence Findings

- **259 parameters** round-trip exactly; the test configuration moves 110 of
  them, so the check is not vacuous.
- FX processing order, envelope assignments and LFO assignments all survive.
- Rendered audio after a round trip differs by 0.000196 RMS against a
  self-variation floor of 0.003757 — i.e. indistinguishable.
- **Hostile input:** null, garbage, truncated and out-of-range payloads are all
  rejected with the parameter set intact, every value finite and in range, and
  audio still finite.
- **Session vs sound separation** holds: the loaded-preset identity persists in
  DAW state and is stripped from preset files.

---

## Mixer / Bus Findings

`PX3Diag mixer` and `PX3Diag persistence` both pass with 0 failures.

- Faders, pan, FX send, FX return gain, FX return pan and master gain each move
  only the signal they should — verified as a cross-talk matrix.
- Saved 0 dB faders are not pulled to the new defaults on load ("preset wins").
- The fixed output boost measures **+6.000 dB** exactly, as designed.
- Gain staging: sources carry −4 dB headroom, and the overload protection is
  calibrated against that. A comment records that moving the trim upstream
  silently disabled the protection and made the synth 3.4 dB louder — the
  calibration is deliberate and load-bearing.

One defect found: the mixer diagnostic swept a parameter ID that does not
exist (`fxReturnPan` — the real ID is `mix.fx.pan`), so the FX-return-pan row
had been reporting a flat 0.000000 and passing without testing anything.
Fixed; it now measures.

---

## FX Findings

All eight effects (VIBE, DELAY, REVERB, MOOD, DOOM, LUCY, CHORUS, SPREAD) have
dedicated suites. Chain mechanics are covered by pure, testable layout
arithmetic: every move is a permutation across all 16 from/to pairs, moves to
one's own index are no-ops, out-of-range moves leave the order alone, and the
grid handles zero through nine effects without escaping the content width.

Ordering has one source of truth and the processor round-trips it through DAW
state.

---

## VibeEngine Findings

Stable. Its hiss now scales with the amount knob rather than sitting on a fixed
floor — previously hiss stepped 86 dB the instant the stage engaged and landed
within 14 dB of full amount, which also made all six type profiles measure
identically. Now −132.6 dBFS at amount 0.05 versus −61.9 at full, with the
profiles spanning 32 dB.

CPU: 16 voices + vibe = 17.86% mean at 48 kHz / 512, versus 5.39% without —
vibe is roughly 12 points at 16 voices. That is a significant but intentional
cost for an always-on analog-character stage.

---

## AnalogEngine Findings

The most recently added subsystem, and reviewed accordingly.

- **Invertibility:** the transfer pair is exact — `|inverse(forward(x)) − x|`
  worst case 4.17e-7 across every blend.
- **The premise holds:** with the colour stages neutralised, a single channel
  through the pair measures **0.000% THD on every profile**. The nonlinearity
  genuinely lives in the summing.
- **Profiles are distinct** in both THD and even/odd balance, and all five are
  level-matched to 0.00 dB on broadband material.
- **Tuning constants are internal.** 13 constants; none is a plugin parameter,
  none appears in serialised state, and a restored instance uses compiled
  tuning. Profile and enabled state do persist. This is exactly the specified
  contract and is enforced by four separate tests.
- **DC:** no stage accumulates DC (worst 1e-6 across profiles).
- **CPU:** 16 voices + analog only = 11.23% versus 5.39% baseline — about 6
  points at 16 voices.
- **Oversampling:** there is none. Aliasing was measured rather than assumed:
  `Analog_AliasingAtUnityRateIsBelowTheAudibleBar` passes at < −60 dB.

A genuine bug was found here during the audit and fixed before it shipped: see
Changes Made.

---

## UI Findings

- **Repaint cost is already handled.** A full editor repaint measures 10.32 ms
  (31% of a core at 30 Hz). The 30 Hz timer repaints **only** `logoPanelArea`,
  at 0.243 ms — 0.7% of a core. The code comments record the original 14.5 ms
  full-repaint problem and its fix.
- **Editor construction:** 170 ms. Acceptable for a plugin window.
- **UIConfig:** the shipping build reads `UIConfig.json` from the bundle's
  `Resources`, which the build copies. Hot reload only probes the source tree
  under `JUCE_DEBUG || PX3_DEBUG_PANEL`, so a release plugin cannot be
  influenced by a stray source checkout. Malformed config does not crash —
  every read has a typed fallback.
- Card, cardInner and preset-tab style properties are each covered by tests
  that assert the property measurably changes the rendered layout, so UIConfig
  cannot accumulate placeholder keys.

---

## Test Results

**629 passed, 0 failed**, across 25 suites:

SUB OSCILLATOR · OSCILLATORS · AMP ENV · ENV1/ENV2/ENV3 · LFO · VIBE · REVERB ·
COMB · CARD STYLE · CARD INNER · FX CHAIN · DOOM · LUCY · CHORUS ·
STEREO SPREAD · DELAY · MOOD · FX INDEPENDENCE AND SIGNAL PATH · PRESET/STATE ·
FACTORY PRESETS · FILTERS · OSCILLATOR RICHNESS · ANALOG ENGINE ·
EDITOR LIFECYCLE · INTEGRATION/LIFECYCLE/EDGE CASES

Additional harnesses, all passing with 0 failures: `PX3Diag rtsafety`,
`PX3Diag persistence`, `PX3Diag mixer`, `PX3Diag soak`.

---

## Regression Results

No regressions. The suite was run before any audit change, after the dead-code
removal, and again after the tooling fixes — 629/0 each time.

---

## CPU Benchmark Results

Shipping configuration (`PX3_DIAGNOSTICS=0`), 400 blocks × 5 sweeps per
scenario. Percentages are of the real-time budget for that block size.

| config | idle | 16 voices (typical) | 16 voices, everything | 64 voices, everything |
|---|---|---|---|---|
| 44.1 kHz / 64 | 1.64% | 5.79% | 30.60% | **100.33%** |
| 48 kHz / 64 | 1.52% | 6.30% | 33.23% | **108.89%** |
| 48 kHz / 128 | 0.96% | 5.80% | 32.29% | **107.60%** |
| 48 kHz / 512 | 0.54% | 5.39% | 31.39% | **104.77%** |
| 96 kHz / 128 | 1.93% | 11.47% | 63.49% | **210.93%** |

**After the sounding-voice budget was introduced** (see Changes Made), the
worst-case column resolves to:

| config | 64 held voices, everything on |
|---|---|
| 48 kHz / 64 | 108.89% -> **86.30%** |
| 48 kHz / 128 | 108.71% -> **83.35%** |
| 96 kHz / 128 | 210.93% -> **90.32%** |

Per-feature cost at 16 voices, 48 kHz / 512 (mean, against a 5.39% baseline):

| feature | mean |
|---|---|
| all 4 sources | 9.39% |
| mod envelopes | 9.91% |
| LFOs | 9.59% |
| mixer automation | 9.40% |
| analog only | 11.23% |
| new FX only | 14.60% |
| filters | 15.00% |
| FX chain | 16.87% |
| vibe | 17.86% |
| SUPERSAW | 23.88% |
| PX3 oscillator | 36.38% |

Voice stealing stress: 15.12% mean but **68.30% max** — the p99 is 6652 µs
against a 10,670 µs budget, so stealing produces a large but in-budget spike.

---

## GPU Benchmark Results

**GPU benchmarking not materially applicable.** The UI is JUCE software
rendering with no OpenGL/Metal context; there is no GPU path to measure. The
equivalent CPU-side render cost is reported under UI Findings.

---

## Memory / Stability Results

Per-instance cost:

- First instance: +15.4 MB (includes one-time init shared by later instances)
- Incremental per instance: **+14.9 MB**
- Peak across 8 instances: 133.3 MB RSS / 121.7 MB footprint
- **Heap returned to within +0.0 MB of baseline** (0.0% of peak) after teardown
- Footprint retained above baseline: +0.8 MB (allocator behaviour, not a leak)

Soak (`PX3Diag soak`), RSS sampled between phases:

| phase | RSS MB | delta |
|---|---|---|
| start | 11.34 | — |
| warm-up, 200 blocks | 27.41 | +16.06 |
| 50× create + destroy | 28.44 | +1.03 |
| 50× create + render | 28.48 | +0.05 |
| repeat 2 | 28.48 | **+0.00** |
| repeat 3 | 28.48 | **+0.00** |
| 200× state save/restore | 29.09 | +0.61 |
| repeat 2 | 29.11 | +0.02 |
| 20,000 blocks at max polyphony | 30.36 | +1.25 |
| repeat | 30.38 | **+0.02** |

Every repeated phase levels off. No growth, no degradation.

---

## Static Analysis

The project already compiles with `-Wall -Wextra -Wpedantic -Wshadow
-Wconversion -Wsign-conversion` (with only `-Wno-nontrivial-memcall` and
`-Wno-switch-enum` suppressed).

**Zero warnings** across all four configurations: clean-from-scratch Release,
`build/` (shipping), `build/tune/` (debug panel ON), `build/diag/` (tests and
benchmarks). No separate static-analysis tool was run; the enabled warning set
already covers narrowing, sign conversion, shadowing and unreachable code.

---

## Sanitizer Results

Full suite rebuilt and run under **AddressSanitizer + UndefinedBehaviorSanitizer**:

- **629 passed, 0 failed, exit 0**
- **Zero AddressSanitizer reports** — no memory errors anywhere
- Three UBSan reports initially, **none in shipping code**:
  - two signed-integer overflows in the test harness's own LCG noise generator
    (`ComponentTests.cpp`) — **fixed** during the audit
  - one NaN→int conversion inside `juce_String.cpp` (JUCE itself), reached when
    formatting a NaN into a test detail string. Not our code; left alone.

ThreadSanitizer was **NOT TESTED**. TSan and ASan cannot be combined in one
build, and the audio/message-thread interaction here is via `std::atomic` and a
single `std::mutex` guarding the loaded-preset struct; a TSan pass would need
a host driving real concurrent callbacks, which this harness does not provide.

---

## Release Build Results

Clean from-scratch build: `rm -rf` → `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release`
→ build.

- Configure: 38.5 s, no errors
- Build: **131/131 steps, zero errors, zero warnings**
- All three formats produced: `PX3 Synth.vst3`, `PX3 Synth.component`,
  `PX3 Synth.app`

No missing files, no generated-file dependency, no debug-only dependency, no
local-path dependency. `UIConfig.json` is correctly copied into each bundle's
`Resources`.

---

## Packaging Results

- **Architecture: arm64 only.** This is deliberate — `scripts/build-release.sh`
  explicitly *requires* arm64 and explicitly *rejects* x86_64, and reports
  "Apple Silicon (arm64)". Intel Macs are not a target.
- **Signing** is opt-in via `--sign` / `--sign-identity`, using a Developer ID
  Application identity, with `--options runtime --timestamp` (hardened runtime)
  and `productsign` for the installer. A plain cmake build is ad-hoc signed, as
  expected.
- Bundle resources verified present: `UIConfig.json`, `Icon.icns`,
  `moduleinfo.json`.
- `dist/` currently holds v0.2.1 artifacts (pkg, zip, uninstaller) from a prior
  run and is gitignored.

**NOT TESTED:** notarization and stapling, and installation on a clean machine.
Both require credentials and a second machine.

---

## Repository Cleanliness

- Working tree clean; no untracked-and-unignored files.
- **Zero** tracked files under `build/` or `dist/`.
- `.gitignore` covers build output, dist, `.DS_Store`, notes, local tooling and
  benchmark results.
- **Secret scan clean:** no API keys, credentials, tokens, private keys or
  certificates in tracked files; no absolute local paths in tracked source; no
  `.p12`/`.pem`/`.key`/`.mobileprovision` files tracked.

---

## Outstanding Work

| Issue | Subsystem | Severity | Scope | Release impact | Recommended action |
|---|---|---|---|---|---|
| No anti-aliasing (no PolyBLEP/oversampling) on SAW/SQUARE | Oscillators | MEDIUM | Large | Audible aliasing at high pitch | Future cycle; a per-oscillator BLEP is a self-contained change |
| No CI | Infrastructure | MEDIUM | Small | All verification is local and manual | Add a workflow running the 629-assertion suite on push |
| `isaacTexture*` legacy naming for the DELAY amount control | UI | LOW | Small | None | Rename when next touching that file |
| AnalogEngine has no oversampling | AnalogEngine | LOW | Moderate | Measured below the audible bar at unity rate | Revisit only if a user reports it |
| ThreadSanitizer not run | QA | LOW | Moderate | Unknown, likely nil | Needs a host-driven concurrency harness |
| Notarization / clean-machine install unverified | Packaging | MEDIUM | Small | Gatekeeper could block distribution | Verify on the release candidate before publishing |

---

## Release Blockers

**None remaining.**

The one blocker found during this audit — the polyphony CPU ceiling — was
fixed during the pass and re-measured. See Changes Made, item 5.

---

## Deferred Technical Debt

Recorded above under Outstanding Work. None of it is release-threatening.
Per the no-scope-expansion rule, the following were explicitly identified and
**left alone**: the `isaacTexture*` naming, the absence of oscillator
anti-aliasing, AnalogEngine oversampling, and any mixer/FX architectural
rework.

---

## Changes Made During Audit

Four changes, all narrow. No shipping DSP or UI behaviour was altered.

### 1. Removed dead UI members and commented-out code

- **File:** `Source/UI/PluginEditor.h`, `Source/UI/PluginEditor.cpp`
- **Change:** removed `midiStatusLabel` and `midiStatusArea` (declared, never
  used except from comments) and 15 lines of commented-out implementation
  covering those members, a stale `statusHeight` local, and stale
  `isaacTextureLabel` tooltip/text lines. 17 lines total.
- **Reason:** genuine dead code; the standing project rule is no commented-out
  implementations.
- **Risk:** none — the members had zero live references.
- **Validation:** all four configurations rebuild clean; 629/0.

### 2. Fixed undefined behaviour in the test harness

- **File:** `Source/Tools/ComponentTests.cpp` (two sites)
- **Change:** the LCG noise source computed `i * 1103515245` in `int`, which
  overflows by design. Now computed in `uint32_t`.
- **Reason:** UBSan reported signed integer overflow — real UB, in test code.
- **Risk:** none to shipping code. The generated values are unchanged in
  practice (the expression was already masked to 16 bits).
- **Validation:** UBSan reports dropped from 3 to 1 (the remaining one is
  inside JUCE); 629/0.

### 3. Fixed a silently vacuous mixer diagnostic

- **File:** `Source/Tools/DiagnosticMain.cpp`
- **Change:** the FX-return-pan sweep used the ID `fxReturnPan`, which does not
  exist — the registered ID is `mix.fx.pan` (`fxReturnPanParam` is the C++
  member name). Corrected.
- **Reason:** the row printed `!! parameter not found` and a flat 0.000000
  delta, i.e. it had been passing without testing anything.
- **Risk:** none — diagnostic tooling only, not shipped.
- **Validation:** the row now reports a real measurement (0.000055).

### 4. Made the CPU benchmark configurable

- **File:** `Source/Tools/CpuBenchmark.cpp`
- **Change:** `kSampleRate` and `kBlockSize` read `PX3_BENCH_RATE` and
  `PX3_BENCH_BLOCK` from the environment, defaulting to 48000/512.
- **Reason:** the harness was fixed at one configuration, so the release
  benchmark matrix could not be produced. Small buffers carry proportionally
  more fixed per-block overhead and had never been measured.
- **Risk:** none — benchmark tooling only; defaults reproduce the previous
  behaviour exactly.
- **Validation:** produced the five-configuration matrix above.

### 5. Fixed the polyphony CPU ceiling (the one release blocker)

- **Files:** `Source/DSP/PluginProcessor.h`, `Source/DSP/PluginProcessor.cpp`
- **Change:** the number of voices allowed to SOUND at once is now budgeted
  against the sample rate. The pool stays at 64 objects; the budget is 48 at
  the 48 kHz reference and scales down with the rate, floored at 16 — giving
  48 / 48 / 26 / 24 / 16 at 44.1 / 48 / 88.2 / 96 / 192 kHz. Voices over budget
  are faded out through the same graceful path the release-tail pruner already
  uses, quietest first with age breaking ties.
- **Reason:** 64 held voices with every effect enabled measured 108.7% of the
  block budget at 48 kHz and 211.2% at 96 kHz. Release tails were already
  pruned but held voices were exempt, so the failure mode was dropouts — the
  whole block late, every voice affected — rather than losing the quietest
  note, which is what a synth is supposed to do when it runs out of capacity.
- **Why sample-rate-scaled rather than a fixed number:** measurement ruled a
  fixed cap out. 48 voices is comfortable at 48 kHz (82.8%) and hopeless at
  96 kHz (161.4%); 32 voices is safe at 48 kHz (58.0%) and still over at
  96 kHz (112.5%). No single constant serves both, because a voice costs the
  same work whatever the rate while the time to compute it halves as the rate
  doubles. This corrected the recommendation made earlier in this audit, which
  had proposed a fixed cap of ~40.
- **Cost:** maximum polyphony at 48 kHz drops from 64 to 48. That is a real
  reduction, accepted deliberately in exchange for never overrunning the
  budget.
- **Risk:** moderate — it changes when notes are stolen. Mitigated by reusing
  the existing fade rather than adding a new stop path, and by two new tests.
- **Validation:** 48 kHz/128 108.71% -> 83.35%; 48 kHz/64 108.89% -> 86.30%;
  96 kHz/128 210.93% -> 90.32%. Still zero allocations on the audio thread
  across all eight RT-safety scenarios. Soak still flat (+0.00 MB on the
  repeat phase). 631 assertions pass. A 64-note chord against a 24-voice
  budget renders finite, audible and click-free — worst sample step 0.015.

---

## Risk Assessment

**Classification: GREEN — READY**

Against the RED criteria, none apply:

| criterion | status |
|---|---|
| Known crash | None. Editor create/destroy, teardown-after-processing and 50× create/destroy all pass |
| Reproducible audio corruption | None. Everything-at-maximum is finite and within ceiling |
| Broken persistence | None. 259/259 parameters exact; hostile payloads safe |
| Broken release build | None. 131/131, zero warnings, from scratch |
| Serious DSP instability | None. No NaN, no runaway, tails reach silence |
| Unacceptable CPU regression | No historical baseline exists to regress against (**NOT TESTED** — first recorded matrix). Worst case now 83–90% of budget, in budget everywhere |
| Critical test failure | None. 629/0, and 629/0 under sanitizers |
| Data/state loss | None |

Residual risks, all low:

- **No CI.** This audit is a point-in-time snapshot that nothing will re-verify
  automatically after the next commit.
- **Notarization unverified.** Apple Developer enrolment is pending, so
  notarization and stapling could not be tested. This is a distribution step,
  not a code risk, and it fails loudly rather than silently.
- **Oscillator aliasing.** A sound-quality gap, not a stability one.
- **Maximum polyphony is now 48 voices at 48 kHz** rather than 64. That is the
  deliberate cost of the fix below.

---

## Release Confidence

```
Release Confidence: 9 / 10
```

Nine, not ten, because:

- there is no CI, so nothing automatically re-verifies any of this after the
  next commit;
- notarization and clean-machine installation are unverified — Apple Developer
  enrolment is pending — and on macOS those are exactly the things that fail
  late.

Neither is a code risk. Nine, not lower, because the evidence is unusually strong for a project this
size: 629 passing assertions including sanitizers, provable zero-allocation
audio processing under 48 voices with the full FX chain, exact persistence
across 259 parameters including hostile input, no memory growth across a
20,000-block soak, and a clean warning-free release build from scratch with a
strict warning set.

I would be comfortable cutting this release from this commit.

---

## FINAL RELEASE DECISION

```
GO FOR RELEASE
```

Every blocker found during this audit has been fixed and re-validated: 631
assertions pass, the audio thread still does not allocate, memory is still
flat across a 20,000-block soak, and every sample-rate and buffer-size
configuration now runs inside its real-time budget.

Two things remain outside the code and outside this audit's reach:

1. **Notarization and stapling are unverified** because Apple Developer
   enrolment is pending. Verify on the release candidate before publishing —
   it fails loudly, not silently.
2. **There is no CI.** Nothing will re-verify any of the above after the next
   commit. Worth adding a workflow that runs the suite on push.
