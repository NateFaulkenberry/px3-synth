# Building P(X3) On macOS

P(X3) macOS builds target Apple Silicon (`arm64`) only.

Intel Macs (`x86_64`) are not supported.

Project version source of truth:

- `PX3_VERSION` in `CMakeLists.txt` (SemVer: `MAJOR.MINOR.PATCH`).

## Development Build

From repository root:

```bash
cmake -B build -G Ninja
cmake --build build
```

Run standalone:

```bash
./scripts/run-standalone.sh
```

With DEBUG panel enabled builds, you can inspect internal bus routing in the detached debug console:

- OSCILLATOR, DRY, FX, and MASTER bus RMS readouts
- FX send and FX return gain controls for wet-path gain staging

The debug performance HUD (bottom-left overlay) reports:

- Per-instance CPU load from this PX3 processor's `processBlock` timing.
- Per-instance RAM estimate from process RSS divided by active PX3 instance count.

RAM is approximate in host/plugin scenarios because memory is shared within the process.

Mixer note:

- Channel-strip meters in the main mixer UI are active in normal runtime builds (not only debug-panel builds).
- Internal debug panel still provides deeper bus-level RMS observability (OSC/DRY/FX/MASTER).

Force a clean rebuild before launching standalone:

```bash
./scripts/run-standalone.sh --build true
```

Shorthand (no value means true):

```bash
./scripts/run-standalone.sh --build
```

Explicitly disable forced rebuild (default):

```bash
./scripts/run-standalone.sh --build false
```

## Release Packaging

```bash
./scripts/build-release.sh
```

Produces, under `dist/`:

| Artifact | Notes |
| --- | --- |
| `PX3-v<version>.pkg` | Installer with a format-selection step (AU / VST3 / Standalone) |
| `PX3-Uninstaller.pkg` | Uninstaller, intentionally unversioned |
| `P(X3)-v<version>-macOS-arm64.zip` | Bundles plus a copy of the uninstaller |
| `PX3-v<version>-macOS/` | The unarchived staging directory |

Flags:

```bash
./scripts/build-release.sh --sign                # codesign the bundles
./scripts/build-release.sh --debug true          # enable the in-plugin debug panel
./scripts/build-release.sh --no-uninstaller      # skip the uninstaller package
```

The installer is built as three component packages joined by an explicit
`Distribution.xml` with `customize="always"`, which is what makes the
Installation Type pane appear. Each format is an independently selectable choice
with its own description and install location.

The uninstaller is a payload-free package whose postinstall script runs as root.
It walks every home directory under `/Users` rather than relying on `$HOME`,
because presets live per-user and the script's own `$HOME` is root's. It removes
plug-ins, the standalone app, the entire preset library (factory and user),
preferences, caches, logs, Audio Unit caches and installer receipts.

Its `safe_remove` helper refuses empty, short, malformed or top-level paths
before calling `rm -rf`, since every path it acts on is built from variables and
an empty one would otherwise be catastrophic.

The release script validates both packages after building: that the component
packages exist and carry their payloads, that the Distribution actually contains
the format choices and the customize pane, that the uninstaller contains its
postinstall script and its warning screen, and that the generated script parses
as valid bash.

## App Icon

The icon is generated from `Source/Assets/px3.gif` by `scripts/make-app-icon.mjs`
and committed, so an ordinary build needs neither node nor sharp. CMake passes it
to JUCE as `ICON_BIG` when it exists, which puts it in the standalone app and
both plug-in bundles.

The wordmark is roughly 3:1, so laid horizontally it wastes most of a square
canvas. It is rotated 45 degrees anticlockwise onto the diagonal - the longest
line a square has - which lets it sit about 6% larger and fill the frame far
better. It is scaled, never cropped, and the rest of the canvas is filled with
the logo's own background colour, sampled from the artwork rather than assumed so
the two meet without a seam.

To regenerate (for example after changing the artwork):

```bash
npm install --prefix .tools sharp     # once; .tools/ is gitignored
node scripts/make-app-icon.mjs
```

Options: `--source <gif>`, `--out-dir <dir>`, `--angle <degrees>` (negative is
anticlockwise). `scripts/build-release.sh` re-runs this automatically when the
tooling is present, and falls back to the committed PNG when it is not - a
release never fails for want of node.

## Blocking Installs While A Host Is Running

Both packages refuse to run while a DAW or the P(X3) standalone is open. A host
with the plug-in loaded holds the bundle open, so replacing it underneath leaves
the host running stale code, and the uninstaller has the same problem in reverse.

Around twenty hosts are detected (Logic Pro, GarageBand, MainStage, Ableton Live,
Pro Tools, Cubase, Nuendo, Studio One, REAPER, Bitwig, FL Studio, Digital
Performer, Reason, Ardour, Waveform, LUNA, Renoise, Maschine, plug-in validation
hosts, and the P(X3) standalone). The list lives in one place in
`scripts/build-release.sh` as two index-aligned arrays, which generate both the
shell checker and the Installer's JavaScript so the names and exit codes cannot
drift apart.

There are two gates, deliberately:

- `installation-check` in the Distribution runs before anything happens and names
  the offending application - "Quit Logic Pro first".
- A `preinstall` script in every component package, which runs regardless of what
  the Installer's JavaScript context supports.

One trap worth knowing: **`productbuild --resources` only copies the resources its
Distribution actually references.** A helper that only the JavaScript calls is
silently dropped, and the check then finds nothing and passes. The release script
puts it back by expanding the product, adding the file, and re-flattening - and
then *verifies* it is present, because a check that silently no-ops is worse than
no check. Flattening discards signatures, so signing moved to a `productsign`
pass afterwards.

## Developer Test Executables

Alongside the plugin, four console executables are configured by CMake. They are
the regression gate and the measurement tools; see the Testing And Measurement
section of `DEVELOPMENT.md` for what each mode reports.

```bash
cmake -B build/diag -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/diag --target PX3Tests PX3Diag PX3Bench

# component regression suite (optionally filtered to one suite)
build/diag/PX3Tests_artefacts/RelWithDebInfo/PX3Tests
build/diag/PX3Tests_artefacts/RelWithDebInfo/PX3Tests delay

# real-time safety: allocations on the audio thread
build/diag/PX3Diag_artefacts/RelWithDebInfo/PX3Diag rtsafety

# memory footprint breakdown
build/diag/PX3Diag_artefacts/RelWithDebInfo/PX3Diag memory

# CPU benchmark across representative scenarios
build/diag/PX3Bench_artefacts/RelWithDebInfo/PX3Bench
```

Note: `PX3_DIAGNOSTICS` is enabled only for `PX3Diag`. `PX3Tests`, `PX3Bench` and
`PX3SmokeTest` build with it off so they exercise the shipping code path.

Before proposing a DSP change, the useful sequence is: run the relevant
measurement mode to get a baseline, make the change, run it again, and keep or
revert on the numbers.

Sanitiser and Debug configurations are worth running for DSP work:

```bash
cmake -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"
cmake --build build/asan --target PX3Tests
build/asan/PX3Tests_artefacts/RelWithDebInfo/PX3Tests
```

## UIConfig JSON During Development

UI styling/layout is driven by `Source/UI/UIConfig.json`.

In debug-enabled runtime (`JUCE_DEBUG` or `PX3_DEBUG_PANEL`):

- The editor hot-reloads when the file modification time changes.
- You can override config location with `PX3_UI_CONFIG_PATH=/absolute/path/to/UIConfig.json`.
- Default debug behavior prefers source-tree `Source/UI/UIConfig.json` when available.

In non-debug runtime:

- The editor loads from the app/plugin bundle (`Contents/UIConfig.json` or `Contents/Resources/UIConfig.json`).
- Source-tree probing is intentionally disabled.

## Install Locally (Development)

Install latest Release AU and (by default) VST3 into user plugin folders:

```bash
./scripts/install-local.sh
```

AU only:

```bash
./scripts/install-local.sh --au-only
```

Install locations:

- `~/Library/Audio/Plug-Ins/Components/`
- `~/Library/Audio/Plug-Ins/VST3/`

## Uninstall Local Development Plugins

Remove P(X3) plugin bundles and app/plugin data from user and system locations:

```bash
./scripts/uninstall-local.sh
```

Run with Logic rescan helper:

```bash
./scripts/uninstall-local.sh --logic-rescan
```

Behavior notes:

- Removes AU/VST3 from both `~/Library/...` and `/Library/...` plugin folders when present.
- Clears Audio Unit and Logic cache entries used by plugin discovery.
- Restarts `AudioComponentRegistrar` when possible.
- `--logic-rescan` launches Logic Pro (if not already running) after cleanup so AU cache rebuild/rescan starts on launch.
- System path cleanup may require `sudo`.

## Release Build (Apple Silicon)

Build, validate, and package:

```bash
./scripts/build-release.sh
```

The release pipeline:

1. Configures CMake for `Release` and `CMAKE_OSX_ARCHITECTURES=arm64`.
2. Builds plugin artifacts.
3. Locates AU/VST3 and Standalone (if enabled).
4. Validates bundle metadata and Mach-O architecture.
5. Validates `Contents/Resources/UIConfig.json` is present in AU and VST3 bundles.
6. Packages a distributable release directory, ZIP, and installer PKG.
7. Validates AU/VST3 component pkg payloads include `Contents/Resources/UIConfig.json`.

## Optional Signing

Unsigned release (default):

```bash
./scripts/build-release.sh
```

Signed release:

```bash
./scripts/build-release.sh --sign
```

Configure signing identity with either:

- `CODESIGN_IDENTITY` environment variable, or
- `--sign-identity "Developer ID Application: ..."`

Example:

```bash
CODESIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" ./scripts/build-release.sh --sign
```

If `--sign` is used and no valid identity is available, the script fails with a clear message.

## Output Locations

Release build outputs are generated under:

- `build/release/` (intermediate build tree)
- `dist/PX3-v<version>-macOS/`
  - `AU/*.component`
  - `VST3/*.vst3`
  - `Standalone/*.app` (when Standalone format is enabled)
- `dist/P(X3)-v<version>-macOS-arm64.zip`

## Release Tag Workflow

Suggested release flow:

1. Update `PX3_VERSION` in `CMakeLists.txt`
2. Build/test locally
3. Commit
4. Tag the release:

```bash
git tag -a v0.1.0 -m "P(X3) v0.1.0"
git push
git push origin v0.1.0
```

5. Create a GitHub Release from tag `v0.1.0`

## Notes

- Release build intentionally disables copy-after-build plugin installation.
- Build and install are separate operations.
- Notarization and stapling are not performed by this script.
- Internal audio routing is explicitly staged as source stems -> DRY -> FX -> MASTER.
- FX chain modules are VIBE, Delay, Reverb, and Mood (user-reorderable).
