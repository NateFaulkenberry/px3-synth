# PX3 v0.7.1

In progress.

The release that turns one plug-in into eight. Seven of the synth's effects now
install as plug-ins of their own, driving the same DSP objects rather than
copies of them, and the tree, the build, the installer and the uninstaller all
read one product table so that adding the ninth is a single declaration. On the
synth itself: separate Dry and FX outputs, per-route Macro depth, in-plugin
update checking, and MIDI CC that reaches the DSP in the block it arrives in.
Behind all of it, the project now has continuous integration and a release
pipeline, so a version is still something a person decides on and no longer
something a person has to build.

---

## Fixed

- **A mapped MIDI CC no longer waits for the interface.** It reached its
  parameter on the next UI tick — up to ~33 ms at the 30 Hz timer this synth
  runs — so a controller sweep arrived as a staircase rather than a sweep. It
  now drives the DSP from the block it arrives in.

  `setValueNotifyingHost` is two things bolted together: an atomic store into
  the parameter, and the call that tells the host. Only the second is unsafe
  on a real-time thread, so the audio thread now does the first directly when
  the CC arrives, and the message-thread pump still does both on its own
  schedule. Both write the same normalised number, so applying it twice is
  applying it once.

  Writing the parameter itself, rather than intercepting each DSP read, is
  what makes this reach every reader — the modulation seam, the direct reads
  in the effects, and anything added later. There is no per-parameter list to
  forget a parameter from.

  **Automation recording is unaffected**, which was the hard requirement:
  Logic's Touch, Latch and Write read the host notification, which still comes
  from the message thread exactly as before. JUCE's `setValueNotifyingHost`
  has no early-out on an unchanged value, so the audio thread's earlier store
  cannot swallow it — verified in JUCE's source rather than assumed, because
  the whole design rests on it.

  A CC remains **authoritative, not additive**: it sets the parameter's base
  value and contributes no delta, so the modulation accumulator is untouched
  and an LFO, envelope or Macro still sums on top of whatever the controller
  last asked for.

- **The uninstaller now shows its own icon.** It was being generated — the
  app's icon with a red X struck through it — and copied into the bundle, and
  then ignored. `osacompile` also writes an `Assets.car` holding the stock
  AppleScript scroll and sets `CFBundleIconName`, which macOS resolves through
  that catalog in preference to the `applet.icns` the build had just written.
  Measured rather than assumed: before the fix the shipped uninstaller's icon
  matched the stock one to four decimal places. The build now removes both the
  key and the catalog, and refuses to finish if either comes back.
- **The EQ analyser's grid is drawn once instead of sixty times a second.** It
  was redrawing its gridlines, labels and zero line every frame behind a trace
  that actually changes — measured at 49% of the component's entire paint cost,
  201 us a frame. The VU meter's face was already cached; this now is too.

- **The last failing `PX3Diag regress` case was a stale diagnostic mark, not a
  click.** `N sine key-release, sustain=0` had failed at a note-off transient
  ratio of 8.7 against a threshold of 6.0 since before 0.5.0.

  The note-off mark is deferred — `stopNote` does not know the sample index
  within the block, so it is placed at the voice's next render. A voice that is
  already silent when the key is released is retired without rendering again,
  so the flag survived into whatever note next reused that voice, and the
  metric scored that note's *attack* as a release transient. Measured: the
  stale mark landed one sample after the new note's own start mark, while every
  passing case had its note-off 5,000–9,700 samples clear of any onset.

  `startNote` clears it now. The suite reports **0 failures**, and every passing
  case's ratio is unchanged — the fix removed a bogus measurement rather than
  moving a threshold.

  One consequence worth stating: with sustain at exactly zero the voice is
  silent at note-off, so that case cannot detect a note-off click at all. A
  `sustain=0.02` case was added to cover what it was meant to — a release
  starting from about −34 dBFS, which scores 5.5 and passes.

## New: separate Dry and FX outputs

- **The mixer's two buses, offered to the host as stereo pairs.** A plain
  instance is unchanged — one stereo output carrying the full mix. Enable the
  second pair in the host and **1/2 carries Dry, 3/4 carries FX**, as
  independent stems rather than one four-channel output.

  The synth already kept the two apart all the way to the final copy, so this
  exposes buffers that existed rather than adding a parallel signal path:
  measured at **0.7% worst-case CPU change**, most cases under 0.3%. The stems
  carry the fixed output boost but not the analog master stage or the output
  ceiling — those act on the sum and cannot be divided between two stems, so
  summing 1/2 and 3/4 in the host is close to the stereo output rather than
  identical to it.

  Bus configuration is the host's, not the sound's: it is not written into
  presets or plugin state, so auditioning a patch never changes your routing.

## New: Macro depth

- **Every Macro → parameter assignment has its own depth.** Cmd-click (Ctrl on
  Windows) a Macro knob to open a panel listing everything it drives, each with
  its own slider and a signed percentage. Depth is per *route*: the same
  parameter driven by two Macros has two depths, and one Macro's several
  destinations each have their own.

  New assignments are full depth, so existing patches sound exactly as they did.
  The stored preset format already carried a per-destination depth — this
  exposes it rather than changing the format.

- **Assignment mode is easier to leave.** Double-click still arms it. It now
  finishes on the Macro knob, on a click on background, or on **Enter** — all
  three through one commit path, so they cannot mean three different things.
  The keyboard notice names the Macro and says how to finish.

## New: update checking

- **SETTINGS now has an Updates section.** It shows the installed version — read
  from the build, not a string kept in the UI — and checks the project's GitHub
  Releases for a newer one.

  **Prepare Update** downloads and verifies the signed installer while your DAW
  stays open. Nothing is installed from inside the plugin: a helper shipped
  inside the standalone waits for the host to quit and then runs the same
  notarised `.pkg` you would have run by hand, and checks the version actually
  changed. You never have to close your DAW and go looking for an installer.

  Built around a product registry and a provider interface rather than around
  this synth: PX3 Synth was the first product registered — all eight are now —
  and GitHub is one provider behind an abstraction that can be replaced without
  touching the UI or the installation path.

## New: seven of the synth's effects, as plug-ins of their own

- **PX3 Delay, Mood, Chorus, Spread, Reverb, Doom and Lucy** now install as
  standalone AU and VST3 effects, so the effect you liked inside the synth can
  go on a drum bus without the synth. They are selectable in the installer and
  ticked by default; untick what you do not want.

  **They are not ports.** Each product drives the same `shared/DSP/...` object
  the Synth drives, through the same `prepare` / `updateForBlock` /
  `processSampleFrame` contract — nothing is copied or reimplemented, so a fix
  to the Delay is a fix in both places by construction rather than by
  discipline. There is exactly one difference between the two consumers, and it
  is a consequence of what a synth is: the Synth builds its settings through
  the modulation accumulator so an LFO or Macro can move a parameter, while a
  standalone effect has no modulation matrix and reads its parameters directly.

  Two shared halves carry a product: `FxPluginProcessor` (buses, prepare, the
  block loop, host tempo, parameter state) and `FxCardEditor` (the card, its
  UIConfig style, the PX3 knob, the attachments). The only virtual the audio
  thread crosses is `processFxBlock`, called **once per block** — the
  per-sample loop stays in the product where `processSampleFrame` inlines. What
  is left of a card-shaped product is the rows it declares: PX3 Chorus's editor
  is 37 lines.

  Effects ship as AU and VST3 only. An effect has no reason to have a
  standalone application, and `hasStandalone` in the product registry says so
  rather than a comment saying it.

- **The tree is now shared infrastructure plus products.** `Source/` became
  `shared/` (DSP, UI components, infrastructure) and `products/PX3Synth/`
  alongside the seven effects. The Synth is declared exactly the way any other
  product is, through one `px3_add_product` call, so "how do I add a product"
  has the same answer as "how was the Synth added".

  That table in `CMakeLists.txt` is the single list: the per-product build
  script, the installer's component list, the update registry and the
  uninstaller's manifest all read from it. A product added there is added
  everywhere, and nobody has to remember a second list beside it. A product
  declared but not built is skipped rather than fatal, so a partial build still
  produces an installer for what it did build.

- **Building one product no longer means building eight.**
  `scripts/build-product.sh lucy --vst3` is the quick loop; each product gets
  its own build directory, so switching between one product and the full build
  reconfigures neither. See section 6a of `docs/ECOSYSTEM_ARCHITECTURE.md`.

## New: the uninstaller removes what you choose

- **It asks which products, and what happens to your presets.** It used to
  remove PX3 Synth and every preset on the machine, unconditionally. With eight
  products that is wrong twice: removing PX3 Mood should leave the Synth
  working, and nobody's preset library should go without being asked.

  Everything it finds is ticked to begin with. **Keep My Presets is the
  default**, and it keeps what you made — your own presets and any wavetables
  you imported — while removing what a reinstall puts back: factory presets,
  settings, staged updates. The shared `~/Library/P(X3)/` directory is only
  touched once no PX3 product is left installed at all.

- **It offers what is on the machine, not what it was built knowing about.**
  The scanner reports the union of the shipped manifest and what is actually on
  disk, so a product released after this uninstaller shipped is still listed —
  marked `unknown` — and still removable. One uninstaller serves the ecosystem
  across versions instead of one per release.

- Two bugs fixed on the way through. `exec >>` on a log file the process cannot
  open is a *fatal* redirection error, so a stale root-owned
  `/tmp/px3-uninstall.log` from an earlier run killed the whole uninstall
  before it removed anything; a log nobody can write is now a reason to carry
  on without one. And the receipt cleanup ran `pkgutil --pkgs` once per
  candidate — five per product, forty full package enumerations to remove
  eight — which is slow enough to look like a hang.

## New: continuous integration and a release pipeline

- **Every push and pull request is now built and tested by GitHub Actions**,
  and a published GitHub Release is built from its own tag and has the
  artifacts attached to it. Neither workflow ever creates a release: deciding
  that a version should exist stays a deliberate act, and the release notes are
  written by hand — this one included.

  CI runs static checks on Linux in about thirty seconds (every shell script
  parsed, shellcheck, the release version helper exercised, the product
  manifest generated), then the real work on `macos-14`. `macos-13` would be
  wrong: it is x86_64, and PX3 is Apple Silicon only. That job builds all eight
  products in the shipping configuration, fails on any warning from PX3's own
  sources, and runs the whole existing estate — `PX3Tests`, `PX3Diag regress`,
  `PX3Diag rtsafety`, `PX3SmokeTest` — before checking that all 17 plug-in
  bundles carry an arm64 executable. A bundle skeleton with no binary in it is
  a failure mode this project has actually hit.

- **The release build verifies the version rather than injecting it.** That
  direction is the whole point. `PX3_VERSION` is a plain `set()`, not a cache
  entry, so **`-DPX3_VERSION=` is silently ignored** — confirmed in an isolated
  project, where `-DPX3_VERSION=9.9.9` still yielded the value written in the
  file. A pipeline that injected the tag's version would ship binaries reporting
  the old one inside files named after the new one, and nothing anywhere would
  say so.

  So `CMakeLists.txt` stays the single source of truth and the release stops in
  about twenty seconds if the tag disagrees with it. A tag is also split, because
  CMake's SemVer regex rejects prerelease suffixes: `v0.7.1-beta.1` builds as
  `0.7.1` and is named `0.7.1-beta.1`. The tag's suffix is cross-checked against
  GitHub's prerelease checkbox, so a beta published as a stable release is
  caught before any asset exists.

- **The release pipeline can be rehearsed without releasing anything.** A
  `workflow_dispatch` mode builds any ref, packages it, and hands back workflow
  artifacts while touching no release — so the whole chain can be proven before
  a real version depends on it, with no throwaway tag to clean up afterwards
  and no chance of a fake version reaching the release list or the updater.

- **Signing is wired but not yet enabled.** `build-release.sh` already supported
  it, so CI passes credentials through rather than reimplementing anything. With
  no secrets configured a release builds **unsigned and says so** — in a
  warning, in the job summary, and on the release run's summary. Nothing
  pretends otherwise. `docs/CI_CD.md` lists the secrets required to turn it on.

- Windows is scaffolded and skipped. There is no Windows support to build:
  every product declares AU, the build scripts pin `CMAKE_OSX_ARCHITECTURES`,
  packaging is `pkgbuild` plus an AppleScript uninstaller, and the updater
  installs a `.pkg`. The job exists so that enabling it later is configuration
  plus a real port, rather than writing CI from scratch.

## Fixed: the build's own tooling

- **The warning policy reached one target in nine.** It was a loop over four
  hand-written target names sitting *below* the product declarations, so the
  seven products and the update helper added above it inherited nothing:
  `-Wno-nontrivial-memcall` reached 1 of the 9 targets that compile JUCE's
  vendored harfbuzz, and the other 8 each emitted ~22 warnings about
  third-party code on every clean build. It now lives inside
  `px3_add_product`, so a product added tomorrow cannot opt out of the policy
  by being added.

  Turning it on everywhere found three real things: a dangling
  `for (auto* child : editor->getChildren())` whose body was the next
  statement, calling `refreshFromParameters()` once per child; a vibe survey
  nested inside another test's block by accident, shadowing its processor; and
  a test that compared a trace with itself. `AmpEnvDsp_IgnoresHoldEntirely`
  passed a "hold" its helper silently discarded — `EnvelopeSettings` has no
  such field — so both renders came from identical settings and it would have
  passed just as well if a hold stage existed and were broken. The absence of a
  hold stage is now asserted against the type, which fails the moment
  `EnvelopeSettings` grows one.

- **The warning policy was not portable, and had been checked wrong.** Two of
  its suppressions — `-Wno-nontrivial-memcall` and
  `-Wno-unnecessary-virtual-specifier` — postdate the clang that ships with the
  CI runner's Xcode. An unknown `-Wno-…` is not ignored: clang answers with
  `-Wunknown-warning-option` **once per translation unit**, and that warning
  carries no file path, so it survives any filter written in terms of paths and
  reads exactly like a warning in our own code. A policy meant to keep the
  build quiet became the thing making the noise. Each suppression is now probed
  with `check_cxx_compiler_flag` and added only where the compiler knows it,
  which costs nothing: a compiler that has never heard of
  `-Wnontrivial-memcall` will not emit it either.

  And the claim that this project built warning-clean was **wrong when it was
  made.** It had been checked with an incremental build against an
  already-built tree, so nothing recompiled and nothing warned. A genuine build
  from scratch had **23 warnings** — 16 in tests, 4 in tools, 3 in products —
  which CI, having no stale tree to hide behind, found on its first run.

  All 23 are fixed, each site read rather than blind-cast. One was not a style
  nit: `noteOffWorstSample` was an `int` assigned `globalSampleBase + n`, where
  `globalSampleBase` is a running `long long` sample counter — so it truncated
  after about 12 hours of continuous rendering at 48 kHz. The sibling field
  recording the same quantity, `worstTransientSample`, was already `long long`.
  Widened to match. Casting would have silenced the warning and kept the
  truncation. The rest are explicit casts at narrowing conversions that were
  already happening implicitly.

- **`build-product.sh` with no format built nothing loadable.** `juce_add_plugin`
  makes the bare target the shared-code static library, so it compiled every
  source, linked the `.a`, and produced a bundle skeleton with no executable
  that macOS refuses to open. It builds `${TARGET}_All` now. `--debug` also
  meant `CMAKE_BUILD_TYPE=Debug` here while meaning the in-plugin debug panel
  in `build-release.sh`; it means the panel in both now, with `--config Debug`
  for an unoptimised build.

- **`run-standalone.sh`'s staleness check had been passing on every stale
  build.** It globbed `Source/`, which the restructure moved. `find` failed
  into `2>/dev/null`, the timestamp came back empty, and the comparison it
  exists to make silently succeeded.

- **`scripts/uninstall-local.sh` removed one product in eight.** The
  developer-machine reset took `head -n1` of the product table, which was right
  when there was one product and quietly wrong from the moment there were
  eight: it left seven effects installed and a host that still listed them. It
  reads the whole table now, so a product added there is cleaned up here
  without editing the script.

- **The release build now checks the uninstaller carries its scanner.** The
  bundle's completeness check listed the removal script but not
  `px3-list-products.sh` or the product manifest — and without the scanner the
  uninstaller finds nothing, tells the user there is no PX3 on the machine, and
  exits believing it succeeded. A total failure that reports success is exactly
  what that check exists for. Both are verified now, and both bundled scripts
  are parsed rather than only one.

## Measured

- **`PX3Diag eqspectrum`** — the two measurements the EQ spectrum brief asked
  for and never got. FFT size against CPU, window length and resolution
  (1024–16384, at 48 and 96 kHz), and the component's paint cost. It settles
  one open question: CPU is not the constraint at any FFT size — the largest
  costs half a percent of a frame — so the case for making the size
  configurable is about window length alone. See `docs/EQ_SPECTRUM_VISUALIZER.md`
  section 7.

---

## Testing

Nine tests were added for the MIDI CC work, every one verified to fail against
the fault it describes. Six fail with the fix removed; the three that are
invariant guards were each mutated toward the bug they name — a learn gesture
that jumps the knob, a route left behind by a cleared mapping, and dropping the
host notification on the grounds that the audio thread already wrote the value.

That found one test of mine that proved nothing:
`TheTeachingMoveDoesNotDriveTheDspEither` could not reach the learn path at
all, and its mutant was caught by a test that already existed. It was replaced
with a check that clearing one knob's mapping stops that knob and leaves the
one sharing its CC still moving — the shift-click-to-unmap path, which is the
only place a route can outlive the mapping it came from.

The uninstaller is shell, and it is the one piece of this project that deletes
things, so it is tested by being **run** — against a fixture tree standing in
for a real machine, with `PX3_SCAN_ROOT` pointing every system path at it. A
test that greps a script for `rm -rf` proves nothing about which paths reach
it. Eight cases, and the one that matters most is `RefusesWhenNothingIsSelected`:
every path the script deletes is built from a variable, and the failure mode of
an uninstaller whose selection came out empty is that it removes everything.
Anything reaching outside the fixture — `pkgutil --forget`, restarting the
component registrar — is gated on the run being a live one, or the tests would
forget the receipts for the copy installed on the developer's own machine.

The ecosystem has its own guards. `TestsEcosystem` checks that no two products
claim the same bundle identifier or plug-in code — a collision there is a
plug-in that silently replaces another in a host's list, a class of bug that
cannot exist until there is a second product to collide with. It also checks
the dependency direction the whole arrangement rests on: **shared code must not
depend on a product.** Nothing enforces that at compile time, because a
`#include` reaching the wrong way still builds; the test is what makes it a
rule rather than an intention.

The release pipeline is tested by the parts of it that can be tested without a
release. `scripts/ci/release-version.sh` is exercised against matching,
mismatched and malformed tags on every CI run, because it is the one piece of
release logic that decides whether binaries and filenames agree. Four bugs in
the workflows were found by running them rather than reading them — among them
an empty-array expansion that aborts under `set -u` in the bash 3.2 macOS
runners use, which would have broken the *unsigned* path, the first release
anyone runs.

**1342 component tests pass, 0 fail.** `PX3Diag regress` reports 0 failures and
`PX3Diag rtsafety` 0 allocations per block, including blocks carrying MIDI,
which is where the new audio-thread work sits. The factory-default smoke test
is audible at every sample rate and block size it tries.

**All measured against a build from scratch**, which is the correction this
release had to make: the equivalent claim earlier in the cycle came from an
incremental build over an already-compiled tree, where nothing recompiles and
so nothing warns. A clean 1,489-target build now reports no warnings from PX3's
own sources — worth something only because the number was verified the way CI
verifies it.

---

## Known limitations

- **Apple Silicon only.** arm64; no Intel or universal build.
- **Releases are not signed yet.** The pipeline supports it and the build always
  did; the credentials are not configured. Until they are, an artifact installs
  on the machine that built it and Gatekeeper refuses it everywhere else, so a
  release built today is not one to hand to anyone. The workflow says so rather
  than implying otherwise. `docs/CI_CD.md` lists what turning it on needs.
- **Benchmarks run in CI but do not gate it.** `PX3MemBench` can only fail
  against a saved baseline and none is committed, and a shared runner's CPU
  numbers are not comparable with a developer's Mac — a failure there would
  mean "the runner was busy", not "the synth got slower". The numbers are
  published on every `main` build; committing a baseline measured *on a runner*
  is what would make gating possible.
- **CI validates the plug-in bundles, not the plug-ins.** It checks that every
  bundle carries an arm64 executable, which catches the skeleton-with-no-binary
  failure. It does not run `auval`, which needs the component installed in a
  system folder and drives macOS's own registrar — on an ephemeral runner that
  validates a copy no user will ever have. There is no `pluginval` here yet;
  adding one is the obvious next step and is not a CI change alone.
- **Windows remains unsupported.** See above: the job is scaffolded and skipped.
- **MIDI CC is block-accurate, not sample-accurate.** `processBlock` samples
  its parameters once, before rendering, so one block is the floor. True
  sample accuracy would mean splitting the render at CC timestamps — a rewrite
  of the mixer and FX chain, not a change to this path. The CC is read before
  that sampling, so it is early within its block rather than late.
- **Mappings are per CC number, not per device.**
- **The effect products have no presets and no update checking.** They keep
  their state through the host, like any other plug-in, and the Updates panel
  lives in the Synth's SETTINGS — the registry knows all eight, but only the
  Synth has UI in front of it. Both are additions rather than redesigns:
  `FxCardEditor` is where either would go.
- **The effect products have no modulation.** No LFO, envelope or Macro can
  move their parameters; that machinery is the Synth's. Host automation works
  normally.
- **The uninstaller finds products by name.** Its scan matches bundles called
  `PX3 *` or `P(X3)*` in the standard plug-in folders. A future product named
  outside that convention, or installed somewhere non-standard, would not be
  offered — the manifest names it, but the scan is what builds the list.
- **A zero-duration ADSR is still reachable** by setting all four times to zero.
  Unlike the Breakpoint case this is not a trap — an ADSR holds at its sustain
  rather than retiring — so no floor was added.
- ~~One `PX3Diag regress` case still fails.~~ **Fixed** — see Fixed, above. The
  suite now reports 0 failures.
- **The debug panel's preset dump cannot be fully tested**: the file chooser is a
  modal async dialog nothing headless can drive. The mapping from fields to
  metadata is tested; the chooser itself is not.
- **EQ spectrum visualiser** items from its design brief remain open.
