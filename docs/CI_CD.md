# CI/CD

Two workflows. `ci.yml` proves a change is sound; `release.yml` turns a release
you created into artifacts attached to that same release.

The division that matters: **CI never publishes, and the pipeline never decides
a release should exist.** You create the GitHub Release, you write the notes,
and GitHub Actions does the repetitive build/test/package/upload work.

---

## The three flows

### Development

```text
feature branch → push → CI
              → PR to main → CI
              → merge → main → CI (+ benchmarks)
```

### Beta

```text
main (green)
→ bump PX3_VERSION in CMakeLists.txt to 0.7.1, merge to main
→ GitHub → Releases → Draft a new release
→ tag v0.7.1-beta.1 (created on publish, targeting main)
→ write the release notes yourself
→ tick "Set as a pre-release"
→ Publish
→ Actions builds the tag, attaches assets to that release
```

### Stable

```text
main (green, PX3_VERSION already 0.7.1)
→ GitHub → Releases → Draft a new release
→ tag v0.7.1
→ write the release notes yourself
→ leave "Set as a pre-release" unticked
→ Publish
→ Actions builds the tag, attaches assets to that release
```

---

## What runs where

| Job | Runner | Feature branch | PR | main | Notes |
|---|---|---|---|---|---|
| `static` | ubuntu | ✅ | ✅ | ✅ | shell parse, shellcheck, version helper, manifest |
| `macos` | macos-14 | ✅ | ✅ | ✅ | build all 8 products, full test estate, bundle check |
| `benchmarks` | macos-14 | ❌ | ❌ | ✅ | reported only, never gating |
| `windows` | windows | disabled | disabled | disabled | see *Windows* below |
| `ci` | ubuntu | ✅ | ✅ | ✅ | the single required status check |

`macos-14`, not `macos-13`: PX3 is Apple Silicon only and the build scripts pin
`CMAKE_OSX_ARCHITECTURES=arm64`. `macos-13` is x86_64.

### What the macOS job actually asserts

- Every product builds in the shipping configuration (`Release`, arm64).
- **No warnings from PX3's own sources.** JUCE's vendored code is filtered out
  by path; everything else fails the build. The project is warning-clean today,
  so any line is new.

  The report separates two kinds, because they look alike and are not:

  - **source warnings** carry a `file:line:col:` prefix and mean what they say.
  - **command-line warnings** carry no prefix. They are clang complaining about
    the *flags*, not the code — almost always a `-Wno-…` suppression that this
    compiler is too old to recognise. `CMakeLists.txt` probes every suppression
    with `check_cxx_compiler_flag` and drops the ones the compiler does not
    know, so these should not occur; if one does, the probe was bypassed.

  A warning that appears only in CI is nearly always a compiler-version
  difference — the runner's Xcode clang is older than a current local one. The
  job logs `clang --version` immediately before configuring for exactly this
  reason. If the runner's compiler turns out to be noisier than yours on the
  same code, the choice is to fix the warnings or to pin the runner's Xcode;
  do not widen the filter, which would hide real ones.
- `PX3Tests` — 1342 assertions.
- `PX3Diag regress` — 29 audio-artifact cases.
- `PX3Diag rtsafety` — 0 allocations per audio block.
- `PX3SmokeTest` — factory defaults are audible at every rate and block size.
- All 17 plug-in bundles carry an arm64 executable. A bundle skeleton with no
  binary in it is a real failure mode, and this is what catches it.

### What CI deliberately does not do

**`auval`.** It requires the component installed in a system plug-in folder and
drives macOS's own `AudioComponentRegistrar`. On an ephemeral runner it would
validate a copy no user will ever have, while taking minutes. The bundle check
is what CI can honestly assert; `auval` belongs on a real machine before a
release. There is no `pluginval` in this project today — adding it would be a
reasonable future step and is not a CI change alone.

**Gating on benchmarks.** `PX3MemBench` only fails when compared against a saved
baseline, and no baseline is committed (`.benchmarks/` is gitignored). A GitHub
runner is also shared hardware, so its CPU numbers are not comparable with a
developer's Mac. The benchmarks run on `main` and upload their output; they are
`continue-on-error` because a failure would mean "the runner was busy", not "the
synth got slower". Committing a baseline measured *on a runner* would make
gating possible later.

---

## Versioning

**`PX3_VERSION` in `CMakeLists.txt` is the single source of truth.** The release
workflow does not override it — it *verifies the tag agrees with it*.

That direction is deliberate. `PX3_VERSION` is set with a plain `set()`, not a
cache entry, so **`-DPX3_VERSION=` on the command line is silently ignored**
(verified: passing `-DPX3_VERSION=9.9.9` still yields `0.7.0`). A workflow that
injected the tag version would produce binaries reporting the old version inside
files named after the new one, with nothing anywhere saying so.

So the rule is: **bump `PX3_VERSION` on `main` before you tag.** If you forget,
the release build stops in about twenty seconds and tells you exactly what to do.

CMake's SemVer regex also rejects prerelease suffixes, so a tag is split:

| Tag | Build stamps | Filenames carry | GitHub prerelease |
|---|---|---|---|
| `v0.7.1` | `0.7.1` | `0.7.1` | off |
| `v0.7.1-beta.1` | `0.7.1` | `0.7.1-beta.1` | on |
| `v0.7.1-rc.1` | `0.7.1` | `0.7.1-rc.1` | on |

`scripts/ci/release-version.sh <tag>` does this and can be run locally:

```bash
./scripts/ci/release-version.sh v0.7.1-beta.1
```

The workflow also **cross-checks the tag suffix against GitHub's prerelease
checkbox** and fails if they disagree — a `-beta.` tag published as a stable
release is a mistake worth catching before the assets exist.

---

## Release artifacts

Built by the project's own `scripts/build-release.sh`; CI does not reimplement
packaging. Names carry the full version including any prerelease identifier:

```text
PX3-v0.7.1-macOS.pkg                      the installer
PX3-v0.7.1-macOS-arm64-plugins.zip        the bundles
PX3-v0.7.1-macOS-arm64-Installer.zip      the installer, zipped
PX3-v0.7.1-macOS-SHA256SUMS.txt           checksums over the above
```

`build-release.sh` names its own output after the core version, so the workflow
renames on the way out — otherwise `v0.7.1-beta.1` and `v0.7.1` would produce
identically-named assets and the second would silently look like the first.

---

## Signing and notarisation

**Not yet enabled.** The build already supports it (`--sign`, `--notarize`, and
`APPLE_ID`/`APPLE_APP_PASSWORD`/`APPLE_TEAM_ID`), and the workflow wires it up,
but with no secrets configured a release builds **unsigned** and says so — in a
`::warning::`, in the job summary, and on the release run's summary page.

Unsigned artifacts install on the machine that built them and are refused by
Gatekeeper everywhere else. Do not distribute them.

### Secrets to configure

| Secret | What it is | How to get it |
|---|---|---|
| `MACOS_APPLICATION_P12` | Developer ID **Application** cert + key, base64 | `base64 -i application.p12 \| pbcopy` |
| `MACOS_APPLICATION_P12_PASSWORD` | password for that `.p12` | set when exporting from Keychain Access |
| `MACOS_INSTALLER_P12` | Developer ID **Installer** cert + key, base64 | `base64 -i installer.p12 \| pbcopy` |
| `MACOS_INSTALLER_P12_PASSWORD` | password for that `.p12` | set when exporting from Keychain Access |
| `MACOS_CODESIGN_IDENTITY` | e.g. `Developer ID Application: Name (TEAMID)` | `security find-identity -v -p codesigning` |
| `MACOS_INSTALLER_IDENTITY` | e.g. `Developer ID Installer: Name (TEAMID)` | `security find-identity -v` |
| `APPLE_ID` | Apple ID for notarisation | your developer account email |
| `APPLE_APP_PASSWORD` | app-specific password | appleid.apple.com → Sign-In and Security |
| `APPLE_TEAM_ID` | 10-character team id | developer.apple.com → Membership |

**Two P12 files, each exactly as Keychain Access exported it.** Do not combine
them into one.

Combining previously seemed reasonable — one import instead of two — but a
bundle rebuilt with OpenSSL is read back happily by `openssl pkcs12 -info` and
rejected by macOS with:

```text
SecKeychainItemImport: MAC verification failed during PKCS12 import
```

The cause is not the combining. It is that **OpenSSL 3 writes a PKCS#12 that
macOS will not read**: it defaults to a SHA-256 MAC with AES-256-CBC, while
Security.framework expects the SHA-1 MAC that Keychain Access produces.
Measured on the two files side by side — `MAC: sha256, Iteration 2048` from
OpenSSL's default against `MAC: sha1, Iteration 2048` from a legacy export;
only the second imports.

So the rule is: **export each certificate from Keychain Access and upload it
untouched.** Right-click the certificate (with its private key beneath it) →
Export → `.p12`. If you ever must go through OpenSSL, it needs
`-legacy -macalg sha1` to produce something macOS accepts.

The identity secrets carry **names, not keys** — the private keys live inside
the P12 files and nowhere else.

Once configured, verify locally first — the credentials fail faster there:

```bash
./scripts/test-signing.sh --preflight    # credentials only, submits nothing
```

### Variables

| Variable | Purpose |
|---|---|
| `PX3_ENABLE_WINDOWS` | set to `true` to un-skip the Windows jobs |

---

## GitHub environments

The release job runs in an environment chosen by the release type:

- `beta` — prereleases
- `production` — stable releases

Neither exists until you create it, and a missing environment does not block a
run. To require manual approval before a stable release is signed and published:

> Settings → Environments → New environment → `production`
> → Required reviewers → add yourself → Save

Do the same for `beta` if you want it, though approving your own betas gets old.
The rehearsal path (`workflow_dispatch`) uses **no** environment, so testing the
pipeline never waits on an approval.

---

## Branch protection

`main` should always be releasable, which means CI has to be able to stop a
merge. GitHub cannot be configured from the repository, so this is manual:

> Settings → Branches → Add branch ruleset (or Add rule) → branch name `main`
> - ✅ Require a pull request before merging
> - ✅ Require status checks to pass before merging
>   - add **`CI`** — the one check to add
> - ✅ Require branches to be up to date before merging

Add `CI` and nothing else. It is a job that exists only to aggregate the others
(`static` and `macos`), so jobs can be added, renamed or split later without
anyone having to remember to update the protection rule. A list of individual
job names goes stale silently, and a stale required check is one that stopped
being required without anybody noticing.

## Security

- Default `permissions: contents: read`. Only the `publish` job is granted
  `contents: write`, and only to attach assets.
- **PR builds never see signing secrets.** `pull_request` runs `ci.yml`, which
  has no secrets at all; signing lives only in `release.yml`.
- The signing keychain is created per-run with a random password, and deleted in
  an `always()` step.
- Nothing echoes a secret. `HAVE_SIGNING_CERT` carries only whether the two P12
  secrets are set, never their values. The decoded `.p12` files are removed by
  an `EXIT` trap, so a failed import does not leave key material in
  `RUNNER_TEMP` for the rest of the job.
- The identity check prints certificate common names and SHA-1 hashes, which is
  what `security find-identity` emits. No key material, and no passwords.
- JUCE is pinned to a tag, and all actions are pinned to major versions.

---

## Running CI locally

The workflow runs nothing you cannot run yourself:

Every build uses **Ninja**, deliberately: CI, the benchmarks, the diagnostic
harnesses and `build-release.sh` alike. Shipping binaries produced by a
generator nothing else exercises is a gap nobody would choose on purpose, and
Ninja's self-limiting job count is what keeps a small runner from exhausting its
process table.

```bash
# what the macOS job does
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DPX3_BUILD_DIAGNOSTIC=ON \
  -DPX3_COPY_PLUGIN_AFTER_BUILD=OFF
cmake --build build --parallel

./build/PX3Tests_artefacts/Release/PX3Tests
./build/PX3Diag_artefacts/Release/PX3Diag regress
./build/PX3Diag_artefacts/Release/PX3Diag rtsafety
./build/PX3SmokeTest_artefacts/Release/PX3SmokeTest

# what the static job does
bash -n scripts/*.sh scripts/installer/*.sh
./scripts/ci/release-version.sh v0.7.0

# what the release job does, unsigned
./scripts/build-release.sh
```

A clean configure takes ~50 s (JUCE fetch included) and a clean build ~6.5 min
on a 10-core Mac; expect roughly 20–25 min on a 3-core GitHub runner, less once
ccache warms.

---

## Testing the release pipeline without releasing anything

`release.yml` has a `workflow_dispatch` rehearsal mode. It builds any ref,
packages it, and uploads the result as a **workflow artifact** — it touches no
release and creates no tag.

> Actions → Release → Run workflow → ref: `main` (or `v0.7.0`) → Run

This proves the whole chain — checkout the exact ref → build → test → package →
named artifacts — with `dry_run=true`, so the `publish` job is skipped entirely.

**Prefer this over a throwaway `ci-test-0.0.0` tag.** A dispatch run leaves no
tag and no release behind, so there is nothing to clean up and no chance of a
fake version reaching the release list or an updater.

---

## Troubleshooting

**"tag vX.Y.Z means version X.Y.Z, but PX3_VERSION in CMakeLists.txt is …"**
You tagged before bumping the version. Bump `PX3_VERSION` on `main`, delete the
release *and* its tag, then re-create the release on the new commit.

**"Release is marked prerelease=… but the tag … says …"**
The prerelease checkbox disagrees with the tag suffix. Edit the release to match
the tag, then re-run the workflow from the Actions tab.

**New warnings failed the build.** Download the `macos-build-log` artifact from
the run; `our-warnings.log` in the test-reports artifact lists exactly the lines.

**A test failed.** The reports artifact carries `test-report.txt`,
`regress-report.txt`, `rtsafety-report.txt` and `smoke-report.txt` for 14 days,
so a failure is diagnosable without rebuilding locally.

**`posix_spawn failed: Resource temporarily unavailable` during compilation.**
Fixed at its source, but worth knowing about. `build-release.sh` used to pass no
`-G`, so CMake picked Unix Makefiles on macOS, where `--parallel` with no number
hands make a bare `-j` — which to GNU make means *no limit*, not *one job per
core*. Across eight products that is hundreds of concurrent clang processes, and
a 3-core runner runs out of them.

Every build in this project now asks for **Ninja**, which ignores a bare
`--parallel` and self-limits to cores+2. If this ever recurs, set
`CMAKE_BUILD_PARALLEL_LEVEL` on the build step; the scripts read it and pass it
as an explicit `-j`. The environment variable alone is not enough on its own —
an explicit `--parallel` on the command line overrides it, which is why the
scripts read the variable rather than leaving it to CMake.

**Assets are unsigned.** Expected until the secrets above are configured. The run
summary says so explicitly. Signing needs **both** P12 secrets: with only one,
the bundles would sign and `productsign` would fail at the very end of a
40-minute job, so the workflow treats one-of-two as unsigned from the start.

**"MAC verification failed during PKCS12 import".** The `.p12` was written by
OpenSSL 3, whose default SHA-256 MAC macOS cannot read. Re-export it from
Keychain Access, or pass `-legacy -macalg sha1`. See *Secrets to configure*.

**"identity is in the keychain but is not valid for signing".** The certificate
and its key imported, but the chain does not validate. Usually an expired
certificate, or the Apple *Developer ID Certification Authority* intermediate
not being reachable — a `.p12` exported without the issuing chain can do this on
a bare runner even though the same file works on your Mac, where the
intermediate is already installed. Re-export including the chain, or add a step
that installs the intermediate before signing. The error prints the exact reason
`security` gave, so it is worth reading before guessing.

### Retrying or rolling back a release

The build is **idempotent** — assets upload with `--clobber`, so re-running
replaces them rather than duplicating.

- **Build failed, source is fine** (flaky runner, timeout): Actions → the failed
  run → *Re-run failed jobs*. The tag is unchanged, so the same source rebuilds.
- **Build failed because the source is wrong:** delete the release and its tag,
  fix `main`, then create the release again. Do not force-move a tag that has
  already been published — anyone who fetched it keeps the old commit.
- **Bad assets already attached:** fix the cause, then re-run. `--clobber`
  overwrites same-named assets. To withdraw a release entirely, mark it a draft:
  the notes and tag survive, and it disappears from the release list.

Assets attach to the release that already exists. The workflow never creates a
release, so your release notes are never touched.

---

## Windows

**Not supported today, and the pipeline does not pretend otherwise.** Both
workflows carry a Windows job, skipped unless the repository variable
`PX3_ENABLE_WINDOWS` is `true`. It exists so enabling Windows later is a
configuration change plus a real port, rather than writing CI from scratch.

What a port would have to deal with, none of it CI work:

- Every product declares **AU**, which is macOS-only. Windows is VST3 only.
- `build-release.sh`, `cpu-benchmark.sh` and `memory-benchmark.sh` all pin
  `-DCMAKE_OSX_ARCHITECTURES=arm64`.
- Packaging is `pkgbuild`/`productbuild` plus an AppleScript uninstaller. There
  is no Windows installer.
- The updater installs a signed `.pkg` and includes `<unistd.h>`.
- README and the release notes both state Apple Silicon only.
