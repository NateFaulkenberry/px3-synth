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

Remove only P(X3) local plugin artifacts:

```bash
./scripts/uninstall-local.sh
```

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
5. Packages a distributable release directory and ZIP.

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
- Internal audio routing is explicitly staged as OSCILLATOR -> DRY -> FX -> MASTER.
