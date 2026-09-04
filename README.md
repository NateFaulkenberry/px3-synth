# P(X3)

P(X3) is eight macOS plug-ins built from one codebase.

**PX3 Synth** is a polyphonic JUCE synthesizer with a multi-mode source engine,
a channel-style mixer, and eight reorderable FX (VIBE, CHORUS, DOOM, LUCY,
Delay, Mood, Reverb, SPREAD).

**Seven of those effects also ship on their own** — PX3 Delay, Mood, Chorus,
Spread, Reverb, Doom and Lucy — as AU and VST3. They are not ports: each drives
the same shared DSP object the Synth drives, through the same interface. See
[docs/ECOSYSTEM_ARCHITECTURE.md](docs/ECOSYSTEM_ARCHITECTURE.md).

This README is a full operating guide for new users and a function-level map for
developers.

Current version: v0.7.0

For dedicated build/install/release workflow documentation, see `docs/BUILDING.md`.
For preset system format and storage details, see `docs/PRESETS.md`.
The shipped factory presets are defined in `products/PX3Synth/Preset/FactoryPresets.cpp`.
For developer architecture, maintenance map, and release workflow, see `DEVELOPMENT.md`.
For CI, releases and signing, see [docs/CI_CD.md](docs/CI_CD.md).

## Start Here

**Playing it?** [docs/USER_MANUAL.md](docs/USER_MANUAL.md) — the full user
manual: quick start, every panel and control, the Macro and MIDI systems, sound
design walkthroughs, troubleshooting and a glossary.

**Building or changing it?** Carry on below.

## What You Get

- 64-voice poly synth engine.
- 20 oscillator modes (classic + experimental + PX3), including a band-limited
  WAVETABLE mode with eight factory tables, audio and image import, and a user
  library.
- A GPU-rendered 3D wavetable display: real perspective, a lit floor, a
  procedural environment, and an orbiting camera.
- Three per-oscillator macro knobs whose meaning changes by oscillator mode, and
  five global Macros that reach any parameter in the synth.
- Dedicated SUB + OSC1/OSC2/OSC3 source channels.
- Filter, amp envelope, and master gain section.
- Two envelope types on ENV 1-3, chosen from a TYPE menu: a four-stage ADSR, or
  a graphical breakpoint envelope of up to 16 points with a curve on every
  segment. AMP ENV is always an ADSR - a breakpoint envelope is a one-shot the
  key does not gate, which is a modulation shape rather than an amplitude one.
- A SETTINGS page behind the gear in the top bar: animation preferences, and the
  analog console's profile.
- Bus inserts on the dry and FX buses: a four-band EQ with a playable graph, and
  an 1176-style FET compressor with a physically derived VU meter.
- Three LFO modulation sources with assignable destinations.
- Eight FX blocks with bypass, and a signal-flow strip for setting the processing order.
- DOOM: a two-channel ambient processor - an always-listening micro-looper and a
  set of spatial effects, tied together by one musical clock.
- LUCY: a spectral degradation engine built on a masking coder - low-bitrate
  artifacts, packet loss, spectral freeze and timing jitter.
- CHORUS: a Dimension D-inspired stereo chorus that widens without wobbling and
  collapses cleanly to mono.
- SPREAD: a mono-compatible stereo widener using allpass decorrelation rather
  than delay or phase inversion.
- Mixer channel controls: level, pan, send, mute, solo, per-channel meter.
- Mixer faders show the real gain of each channel; the -4 dB of modulation headroom lives on the sources themselves, not hidden inside the fader.
- Explicit internal audio bus routing: OSCILLATOR STEMS -> DRY BUS + FX SEND BUS -> FX CHAIN -> FX RETURN -> MASTER.
- FX return controls: level, pan, mute, solo.
- Clickable 88-key keyboard (A0-C8) with medium click velocity.
- Performance strip (pitch bend and mod wheel).
- Separate Dry and FX outputs, offered to the host as two stereo pairs.
- In-plugin update checking, with the installer downloaded and verified while
  your DAW stays open.
- **Seven standalone effect plug-ins** — PX3 Delay, Mood, Chorus, Spread,
  Reverb, Doom and Lucy — AU and VST3, selectable in the installer.

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

- It removes **every** product in the `px3_add_product` table — the Synth and
  all seven effects — reading that table rather than carrying its own list.
- It targets both user and system plugin folders and cache paths. System-wide
  removals may require `sudo`.
- This is a **developer-machine reset**: unlike the shipped uninstaller it takes
  the whole `~/Library/P(X3)/` preset library without asking. Use the shipped
  `PX3 Uninstaller.app` if you want to be asked.

## Building a Release Installer

Run:

```bash
./scripts/build-release.sh
```

This builds the release plugin formats and creates two native macOS packages: an
installer and an uninstaller.

### Installer

The installer presents an **Installation Type** step offering the Synth's three
formats and then the seven effects under a heading of their own. Everything is
selected by default; you opt *out* of what you do not want.

| Component | Destination | |
| --- | --- | --- |
| Audio Unit (AU) | `/Library/Audio/Plug-Ins/Components/` | optional |
| VST3 | `/Library/Audio/Plug-Ins/VST3/` | optional |
| Standalone application | `/Applications/` | **required** |
| PX3 Delay, Mood, Chorus, Spread, Reverb, Doom, Lucy | both plug-in folders above | optional |

The standalone is listed but its checkbox is ticked and disabled, because the
updater helper lives inside `PX3 Synth.app`. A plug-in stages an installer and
hands it to that helper, which is what waits for the host to quit — so without
the standalone, **Prepare Update succeeds and Install cannot work**. It is shown
rather than installed silently so that what lands on the machine is still
visible.

One component package per **effect** rather than per format — that is the choice
a user actually makes — so each carries that effect's AU and VST3 together.
`build-release.sh` reads the product list from `CMakeLists.txt`, so a new
product is packaged without editing the installer, and the finished package is
expanded and checked to confirm each effect's package is both present and
referenced by the Distribution. `productbuild` silently drops a package nothing
selects, which is how the branding resources were lost once before.

Example output artifact:

- `dist/PX3-v<version>.pkg`

### Uninstaller

An uninstaller **application** is generated on every release build:

- `dist/PX3 Uninstaller.app`

It is deliberately an application rather than a `.pkg`. The macOS Installer
always shows install-style UI — a Destination Select pane, an Installation Type
pane and an "Install" button — and none of that can be relabelled from a
Distribution file, so an uninstaller shipped as a package reads as an installer
whatever the panes say. Owning the app means owning every word the user sees,
and it gets the system's own authorisation prompt instead of an installer's.

It is also not versioned. It scans the machine for what is actually installed
rather than working from a list baked in when it was built, so it removes
products released after it shipped and installs left over from releases before
it. A product it has not heard of is listed all the same.

It asks two questions, then acts on the answers:

1. **Which products.** Everything found is ticked to begin with; untick what
   should stay. Removing PX3 Mood leaves PX3 Synth working.
2. **What happens to the presets.** *Keep My Presets* — the default — keeps
   your own saved presets and any wavetables you imported, and removes what a
   reinstall puts back: factory presets, settings and staged updates. *Remove
   Everything* deletes the shared `~/Library/P(X3)/` directory entire, and the
   confirmation says so in those words. Either way the shared directory is only
   touched once no PX3 product is left installed.

For each selected product, **across every user account on the machine**:

- AU and VST3 plug-ins, from both system and per-user plug-in folders
- The standalone application, for products that have one
- Preferences, caches, logs and crash reports
- Installer receipts, so a later install is not skipped as already-present

Audio Unit caches are cleared at the end, so removed plug-ins disappear from
host plug-in lists rather than lingering as broken entries. A log of everything
removed is written to `/tmp/px3-uninstall.log`.

The removal is shell (`scripts/installer/px3-uninstall.sh`) and is tested by
being run against a fixture tree standing in for a real machine — see
`tests/Tests/TestsUninstaller.cpp` and section 7 of
[docs/ECOSYSTEM_ARCHITECTURE.md](docs/ECOSYSTEM_ARCHITECTURE.md).

To skip building it:

```bash
./scripts/build-release.sh --no-uninstaller
```

For a developer-machine uninstall that does not involve a package, use
`./scripts/uninstall-local.sh` instead.

### App icon

The application and plug-in icon is generated from `products/PX3Synth/Assets/px3.gif`. The
wordmark is rotated 45 degrees onto the diagonal so it fills the square without
being cropped, and the rest of the tile uses the logo's own background colour.
See `docs/BUILDING.md` to regenerate it.

### Supported DAWs and hosts

P(X3) ships as an Audio Unit, VST3 and a standalone application, so it loads in
any host supporting those formats. The installer and uninstaller additionally
recognise the following Audio Unit hosts by bundle identifier, and will ask you
to close them before installing or removing the plug-in:

| Host | Host |
| --- | --- |
| Logic Pro | Reason |
| GarageBand | LUNA |
| MainStage | Ardour |
| Ableton Live | Waveform |
| Pro Tools | Renoise |
| Cubase | Maschine |
| Nuendo | AU Lab |
| Studio One | Gig Performer |
| REAPER | Vienna Ensemble Pro |
| Bitwig Studio | Plogue Bidule |
| FL Studio | Digital Performer |

The list lives in `scripts/installer/au-hosts.tsv` and is the only place host
identifiers are defined. Adding a host is a one-line edit there.

Samplitude and Sequoia are Windows-only products and so have no macOS bundle
identifier to match.

### Running-host check

Both the installer and the uninstaller refuse to run while one of the hosts
above, or the P(X3) standalone, is open. A host with the plug-in loaded holds
the bundle open, so replacing or removing it underneath leaves that host running
stale code.

Applications are identified by **bundle identifier**, read from each running
application's own `Info.plist` - not by process name. Ordinary audio software
(Spotify, browsers, QuickTime, conferencing apps) and macOS audio services
(`coreaudiod`, `AudioComponentRegistrar`, `auval`) do not trigger it.

The check runs twice: once when the installer opens, and again immediately
before the plug-in is written or removed, so a DAW opened while the installer
sits waiting is still caught.

### Installer branding

Both packages show the P(X3) logo in the bottom-left corner of the Installer
window. The image is taken from `products/PX3Synth/Assets/px3-installer.png` if present,
otherwise generated at build time from `products/PX3Synth/Assets/px3.gif` (converted to PNG
and scaled, since the Installer renders a still image). If neither exists the
packages simply build without a background.

Signing: Developer ID installer signing is applied when `DEVELOPER_ID_INSTALLER`
is set. Notarization can be added later in the release pipeline.

## Settings

The gear at the right of the top bar opens SETTINGS: a full-width page with no
Macro strip, because nothing on it is a Macro destination. It is a toggle -
pressing it again, or CLOSE at the bottom of the page, returns to the panel you
came from.

- **Enable animations** (on by default) gates the keyboard sparks, the wheel
  sparkles and the logo movement. It is a global install preference rather than
  per-instance state: changing it in one open plugin window changes it in all of
  them, it is kept in `~/Library/Application Support/P(X3)/settings.xml`, and it
  is never written into a preset or a project.
- **Analog Engine** selects the console profile - CLEAN, BRITISH, AMERICAN,
  TRANSFORMER or MODERN. This one IS part of the sound: it is an automatable
  parameter and travels in sessions and presets. The console is enabled by
  default as of 0.6.0.

## Diagnostics

Beyond the test suite, `PX3Tests` and `PX3Diag` carry diagnostic modes that
measure things a pass/fail assertion cannot:

| command | what it measures |
|---|---|
| `PX3Tests glcheck` | whether the GPU renderer draws, by reading pixels back |
| `PX3Tests envcheck` | the wavetable environment, off against on |
| `PX3Tests sharpcheck` | waveform line sharpness, by edge profile in physical pixels |
| `PX3Tests attackpop` | the note onset - first samples, largest step, signal against the envelope |
| `PX3Tests onsethunt` | onset discontinuity swept over MIDI offset, velocity, attack, voices |
| `PX3Diag rtsafety` | allocations inside `processBlock`, with a backtrace at the first |
| `PX3Diag memory` | per-object and per-voice memory |

There is also an in-process onset capture for faults that only appear in a real
host. Set `PX3_ONSET_CAPTURE` to a file path and the first note-on records 8192
samples of the final output, the amp envelope of the voice that took the note,
its attack setting, how far into the note its envelope believes it is, and the
sounding voice count:

```bash
PX3_ONSET_CAPTURE=/tmp/onset.tsv "…/PX3 Synth.app/Contents/MacOS/PX3 Synth"
```

It allocates nothing unless the variable is set, and the file is written on the
message thread. It is how the note-on click was found: the capture showed a
voice holding a 12 ms attack while the drawn envelope had a 4 second one.

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

- `shared/UI/Style/UIConfig.json`

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
  - `./shared/UI/Style/UIConfig.json` from current working directory
  - upward probe for `shared/UI/Style/UIConfig.json`, then `UIConfig.json`
  - bundle fallback: `Contents/UIConfig.json`, then `Contents/Resources/UIConfig.json`
- Non-debug builds:
  - bundle only: `Contents/UIConfig.json`, then `Contents/Resources/UIConfig.json`
  - no source-tree probing

Production packaging:

- CMake copies `shared/UI/Style/UIConfig.json` into `Contents/Resources/UIConfig.json` for Standalone, AU, and VST3 bundles.
- `scripts/build-release.sh` fails fast if AU/VST3 bundles or component pkg payloads are missing `Contents/Resources/UIConfig.json`.

## Developer Preset Dumping

When DEBUG mode is enabled, the detached P(X3) DEBUG CONSOLE includes a `PRESET / STATE TOOLS` block with:

- `Preset Name` (required)
- `Author` (required)
- `Category` (a dropdown of the categories the library actually has)
- `DUMP PRESET`, disabled until the name and the author are both filled in

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
  -> FX Chain (user-order: VIBE / CHORUS / DOOM / LUCY / DELAY / MOOD / REVERB / SPREAD)
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

## Mixer Gain Structure

- Every source (SUB, OSC1, OSC2, OSC3) and the FX return defaults to -4 dB, giving
  each channel headroom for modulation without the mix clipping.
- That trim lives **on the sources themselves**, not inside the fader. A mixer
  fader at unity means unity: what the strip shows is the channel's actual gain.
- Because the trim is a default rather than a hidden offset, presets and DAW
  sessions store and restore whatever the fader was actually set to. Loading a
  preset never re-applies the default over the top of a saved value.
- The fader range extends above unity by the same 4 dB, so a channel can still be
  pushed back to its full pre-trim level.
- The trim value and the fader's maximum come from one shared constant in
  `products/PX3Synth/DSP/PluginProcessorInternals.h`, so the source side and the fader range
  cannot drift apart.

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

## Macros: Five Performance Controls

Five knobs sit on the left of every panel — OSC, MOD, FLT, FX, AMP and MIX. Not
on SETTINGS, which is full width and has no Macro destinations on it.
They are the same four wherever you are: switch panels and they keep their
values and their assignments, because there is only one set of them.

Each Macro can move any number of parameters anywhere in the synth at once.

### Assigning

1. **Cmd-click a Macro knob**, or double-click it. It and its label light teal,
   and the keyboard says
   *"Click knobs to assign them to MACRO 1"*.
2. **Click any knob** to assign it. Click it again to remove it. The knob does
   not move while you are assigning — the click assigns, it does not drag.
3. **Switch panels and keep going.** Assignment stays active, so one Macro can
   collect an oscillator detune, a filter cutoff, a delay mix and a mixer send
   without leaving the mode.
4. **Click the Macro knob** again to finish, or press **Escape**. Everything
   you clicked is already assigned; Escape does not undo it.

### Reading a knob

| The knob shows | It means |
|---|---|
| `MACRO 1` on a pale plate above the spindle | one Macro drives it |
| `M1+` on that plate | several Macros drive it; the first is named |
| `CC21` below the spindle, amber | a MIDI control is mapped to it directly |
| both labels | both, and they add together |
| a solid teal ring | assignable right now, in the active Macro mode |
| a dashed amber ring | selected for MIDI Learn |

Teal is always Macro, amber is always MIDI. Purple is the LFOs and
envelopes, and means something else.

### What a Macro does to a parameter

A Macro does not take a parameter over. It adds to it, the way an LFO does, so
the knob, a DAW automation lane, a mapped MIDI CC, the LFOs, the envelopes and
all five Macros can reach the same parameter and the result is the sum of
what each is asking for. The destination knob stays where you set it and its
ring shows where the sound actually is — the same convention modulation already
uses.

Turning a Macro to zero returns its destinations to exactly the values their
own knobs show.

### Macros and MIDI

Macros are parameters like any other, so they are MIDI-mappable with the same
gesture: **Shift-click a Macro knob, move a hardware control**. That gives you
one physical knob moving one Macro moving a dozen parameters.

A Macro can also be automated by your DAW.

### What gets saved

- **Presets** carry Macro assignments *and* Macro values, so a patch ships with
  the performance controls it was designed around.
- **DAW sessions** carry both as well.
- **MIDI mappings** of the Macros belong to the instance and are not replaced
  by loading a preset. `CC 21 → Macro 1` survives while the preset decides what
  Macro 1 does — which is the point of a Macro being its own control source.

Projects and presets saved before Macros existed load with five empty Macros.

### Limits

- Five Macros, no more; a Macro cannot drive another Macro.
- Assignments are at full positive depth. The stored format carries a per
  destination depth so an editor for it can be added without breaking presets.
- Macro knobs are not themselves assignable to Macros.

The design and the reasoning behind it are in
[docs/macro-system-design.md](docs/macro-system-design.md).

## MIDI Learn: Mapping Hardware Controls

Any knob in the synth can be driven by a hardware controller. There is no CC
number to type and no dialog to open.

### Assigning

1. **Shift-click a knob.** It gets a dashed amber ring, and the keyboard shows
   *"Select knobs, then move a MIDI control to assign"*.
2. **Shift-click more knobs** if you want several on one control. They can be
   anywhere - a filter cutoff, a reverb mix and an oscillator macro at once.
   The selection follows you between panels.
3. **Move the hardware control.** Every selected knob is assigned to it, the
   selection clears, and each knob shows `CC21` (or whichever it was) under
   its pointer.

The controller's full travel sweeps each destination through its own range, so
a cutoff in hertz and a resonance in 0-1 both get the whole sweep in their own
units.

The movement that *teaches* the mapping does not also jump the knobs - they
stay where you left them, and the next movement drives them.

### Removing and reassigning

**Shift-click a mapped knob.** Its assignment is dropped there and then, and it
joins the selection ready for a new one. Move a control to give it one, or
press **Escape** to leave it unmapped.

That gesture is deliberately destructive - it is the only way to end up with a
parameter unmapped - but never silent: the `CC` label leaves the knob in the
same click. You never need to click a knob to find out what it is on, because a
mapped knob always shows it.

### While selecting

- Shift-clicking a knob that is already selected takes it back out.
- Emptying the selection leaves Select Mode.
- Escape leaves Select Mode without assigning anything.
- Ordinary clicks and drags are untouched: without Shift, a knob behaves
  exactly as it always did.
- The keyboard still plays while you are selecting.

### What gets saved

MIDI assignments are saved in **both** places:

- **DAW sessions.** Close the project, reopen it, the assignments are back.
- **Preset files.** Saving a patch saves the hardware layout it was designed
  around, and loading that patch on another machine brings the assignments
  with it.

A preset that carries no assignments leaves yours alone, so auditioning factory
sounds never costs you your controller setup. A DAW session is the whole truth
for that instance: one saved with no assignments restores none.

Assignments are **per plugin instance**. Two copies of PX3 in one project can
map the same CC to completely different parameters without affecting each
other.

### Notes and limits

- Any MIDI channel drives a mapping; the channel it was learned on is recorded
  for future use but not matched against.
- A CC arriving when nothing is selected only drives existing assignments - it
  never learns by itself.
- Note input, the mod wheel and pitch bend are unaffected. Mapping CC 1 gives
  you both the mod wheel's usual modulation and the mapped parameter.
- MIDI mapping is separate from the LFO and envelope modulation matrix. A
  mapped control moves the parameter itself, the way your hand or a DAW
  automation lane would; modulation is layered on top of that, unchanged.
- A control change reaches the parameter on the next UI tick rather than
  sample-accurately. For a knob gesture this is imperceptible; it is not a
  sample-accurate modulation path, by design.
- The synth cannot tell your controllers apart - a plugin receives all MIDI
  devices merged into one stream - so a mapping is to a CC number, not to a
  particular box.

The design and the reasoning behind it are in
[docs/midi-mapping-design.md](docs/midi-mapping-design.md).

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
  - A = SPREAD (how far apart the stacked saws sit, plus their drift and edge feel).
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

- AMP ENV is a graphical breakpoint editor, as are ENV 1-3. The default shape is
  ADSR, because ADSR is a special case of the model rather than a separate one.
- Stages run ATTACK | DECAY | SUSTAIN | RELEASE, and every handle names what
  it changes when the mouse is over it.
- Three handles. ATTACK moves in time along the top and RELEASE in time along
  the bottom - both are durations, and neither carries a level. DECAY / SUSTAIN
  is one handle moving in both: sideways is the decay time, upwards the sustain
  level, because they are the two coordinates of one breakpoint.
- ATTACK, DECAY, SUSTAIN and RELEASE knobs sit under the graph. They and the
  curve are two views of one thing: a drag moves the knobs, a knob moves the
  curve, and turning a knob does not straighten a bend you drew.
- Drag a point to move it in time and level. Drag the curve between two points
  to bend it; the bend is symmetric, so equal numbers are equal bends either way.
- Double-click empty space to add a point, double-click a point to remove it.
  Structural points - the first, the sustain point and the last - cannot be
  removed.
- Up to 16 points. The time axis is labelled in seconds, to a maximum of 8.
- Handles may sit on top of one another: drag the decay onto the attack and the
  stage has no length, which is what both the graph and the DSP then give you.
  The handle underneath is one drag away - grabbing a shared spot takes the
  later of the two, and moving it uncovers the other.
- While a note is sounding, the area under the part of the envelope it has
  already been through fills in behind the curve. All four envelopes do this -
  AMP ENV and ENV 1-3 alike, each following its own envelope. It follows the shape exactly,
  bends included, because the fill and the curve come from one sampler. The
  fill stops dead at the sustain point for as long as the note is held - the
  sustain bar's drawn width is not a duration, so creeping across it would be
  inventing time the envelope is not spending - and the bar fills in at
  note-off as the release runs on from its far edge. Retriggering starts it
  over; when nothing is sounding there is no fill. It shows the most recently
  triggered voice, which is the one a player has just pressed.

Parameter mapping (unchanged source of truth):

- Attack: 0.001s to 3.0s
- Decay: 0.005s to 4.0s
- Sustain: 0.0 to 1.0
- Release: 0.010s to 5.0s

Architecture guarantee:

- The four ADSR parameters remain the authoritative representation of the four
  points they describe, so host automation and existing presets are unaffected.
- Extra points and curve amounts live in a versioned state node. An old preset
  has no such node, which is a valid state meaning "plain ADSR" - not an error.
- The curve the editor draws is the same function the DSP evaluates, sampled
  into a path, so there is no drawn shape and played shape to drift apart.
- The progress fill reads the playing voice's own envelope position - four
  slots, AMP ENV and ENV 1-3 - published once per block into atomics; it is not
  a UI clock counting alongside the DSP,
  so it cannot drift, and it is read-only - exposing it changed no audio
  behaviour and added no work to the audio thread beyond five atomic stores.

Release starts from wherever the envelope actually is, not from the sustain
level, so releasing during a slow attack does not jump first.

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

Each block has a corner bypass toggle. The processing order is set by dragging
the nodes in the signal-flow strip above the grid - the cards themselves are
editors, not ordering controls, so they can wrap and scroll freely. The strip is
the only place the order can be changed, and it stays visible while the grid
scrolls.

Bypass semantics:

- Checked = enabled (not bypassed).
- Unchecked = bypassed.

Bypassing an FX block clears its internal buffers, so re-enabling it starts silent
rather than releasing the tail of whatever was playing when it was switched off.
Delay clears immediately; Reverb and Mood clear once their fade to silence
completes, so the bypass itself never clicks.

Header behavior:

- Clicking section header toggles its bypass.
- Dragging header reorders blocks; order is committed to DSP and saved in state.

### VIBE

Controls:

- AMOUNT knob: effect amount.
- TYPE dropdown: Warm, Hot, Cool, Vintage, Clean, LoFi.

What it does:

VIBE is a per-voice analog-imperfection layer rather than an effect on the mix. It
runs inside each voice, per source, *before* the four sources are summed - N
saturated signals summed does not equal their sum saturated once, and that
difference is the point.

What it adds:

- **Independent per-voice drift.** Every voice has its own drift rate (0.020-0.095 Hz),
  its own random walk, and its own thermal phase. Voices do not drift together;
  that independence is what thickens a held chord rather than sounding like vibrato.
  Measured movement is about 3.2 cents with an inter-voice correlation near zero.
- **Console-style saturation** using a `sin()` fold rather than `tanh()`, which
  reaches a hard ceiling and produces a different harmonic series.
- **Program-dependent PSU sag.** The supply sags because the amplifier is drawing
  current, so the sag follows the actual oscillator-bus level.
- **Pink (1/f) noise**, not white - analog hiss falls at roughly 3 dB/octave.
- **A coupling capacitor per source**, because asymmetric distortion makes DC by
  definition and DC inside a signal path eats headroom.
- **A chaotic (Lorenz) component** integrated at a fixed timestep, so its rate does
  not change with the host's buffer size.

Level behaviour:

- The stage is deliberately level-neutral: the worst level change across the whole
  AMOUNT range is about 1.2 dB. Turning VIBE up adds harmonic content and movement,
  not volume.

VIBE is an original texture system, not a hardware emulation claim. The `sin()`
saturation is credited to Airwindows in `THIRD_PARTY_NOTICES.md`.

### Delay

Controls:

- AMOUNT knob (center knob in delay card): delay amount/mix driver. At zero the
  block is a wire.
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
- GRANULAR MODE dropdown (applies to the Granular algorithm):
  - CLASSIC
  - CLOUD
  - SHIMMER
  - RHYTHMIC

What it does:

Every algorithm reads its delay line with four-point cubic interpolation. Linear
interpolation suppresses its imaging products by only about 26 dB and its gain
droops with the fractional part, so a read pointer that is moving - which is every
algorithm here - would get a lowpass that wobbles in step with the motion.

- **Granular** spawns windowed grains from delay memory, with four sub-modes
  (CLASSIC, CLOUD, SHIMMER, RHYTHMIC) selected by the GRANULAR MODE dropdown.
  Grains read from a point in the stereo field rather than from a mono sum.
- **Tape** models the transport rather than imitating it: wow, flutter and scrape
  as separate speed-error mechanisms at decades-apart rates, a head bump from the
  record/playback gap geometry, cumulative high-frequency gap loss per pass, and
  magnetic hysteresis in the saturation.
- **Analog/BBD** models a 4096-stage bucket-brigade chip. The stage count is fixed
  and the clock is whatever produces the wanted delay, so **bandwidth is locked to
  delay time**: short settings are bright, long settings are dark and grainy. It
  also has the companding (compress in, expand out) those chips need, and the
  anti-alias and reconstruction filters that go with it. Its TIME control stops at
  620 ms because a real chip runs out of stages - that ceiling is the model, not a
  clamp.
- **Ping-Pong** sums the input to one side and hands each repeat to the other
  channel, so the echoes genuinely alternate in time.
- **Stereo** runs two lines at a musical two-thirds ratio with light cross-coupling.
- **Modulated** slides its read pointers under three modulators at incommensurate
  rates, opposed between the channels.
- **Diffusion** puts a Schroeder allpass chain in the feedback path, so each repeat
  smears a little more than the last and the echoes dissolve into a wash.

FEEDBACK behaviour:

- FEEDBACK sets a decay **time**, not a per-repeat coefficient. The coefficient is
  derived from the delay length so that the same knob position means the same decay
  whether the delay is 20 ms or 2 seconds. As a raw coefficient it did not: 0.98 per
  repeat is a 30-second decay at 100 ms and an eleven-minute one at 2 seconds.
- The top of the control is a long but finite decay, not a drone.

Other behaviour:

- At zero AMOUNT the delay is a wire - bit-identical to its input.
- Changing TIME crossfades between delay positions on the digital algorithms, so
  existing echoes do not pitch-bend. Tape and Modulated slide the pointer instead,
  because the pitch movement is the effect in those two.
- Bypassing the block clears the delay lines, so re-enabling it does not replay a
  tail from before it was switched off.

### Reverb

Controls:

- INTENSITY knob: reverb amount.
- ALGO dropdown:
  - ROOM
  - PLATE
  - HALL
  - CLOUD

What it does:

- **ROOM**: an explicit nine-tap early-reflection pattern per channel, with
  different times per ear, feeding a feedback delay network at roughly 30-175 ms.
- **PLATE**: a full Dattorro plate (JAES 45/9, 1997) - four input diffusers, a
  figure-of-eight recirculating tank with modulated allpasses, and the canonical
  seven-tap-per-channel output pickup.
- **HALL**: a Jot/zita-style eight-line feedback delay network with an allpass
  inside each loop, Hadamard mixing, and per-line damping, scaled to ~37-231 ms.
- **CLOUD**: the same network scaled much longer (~75-488 ms) for expansive,
  modulated wash.

Three properties are what make these sound like spaces rather than metal:

- Per-line feedback gain is **delay-compensated** (Jot's rule), so short and long
  lines decay at the same rate in time rather than the same rate per pass.
- The feedback matrix is **orthogonal** (Hadamard), so it is energy-preserving.
- Delay lengths are **mutually incommensurate** and there is input diffusion ahead
  of the network, without which an impulse arrives as a burst of discrete taps.

Also:

- Early reflections are same-sign on both channels. Anti-phase measures wider but
  cancels when the mix is summed to mono.
- Shared post-processing includes stereo width shaping, wet DC filtering, gentle
  peak control at high wet levels, and output compensation so loudness stays stable
  as INTENSITY rises.
- Bypassing the block clears the delay lines once the amount fade reaches zero, so
  re-enabling it does not release the tail of whatever was playing when it was
  switched off.


### DOOM

DOOM is a two-channel ambient processor inspired by the Chase Bliss BAD MOOD. It
is a separate engine from Mood, not a variant of it.

One half is an **always-listening micro-looper**: it records continuously while
bypassed, so engaging it captures what you already played rather than starting a
recording. Three modes - BURST slices the loop at its own onsets and sequences
them, RADIO scans five loopers with interference between them, MASK replaces the
loud parts of the loop with something else.

The other half is a **wet channel** - SOUP is a spectral reverb that resynthesises
what passes through it, RELAY is a delay whose repeats do not fade (you choose how
many, and they all share one volume), FLIP builds harmonies and spreads them
across time.

**CLOCK** is the engine's sample rate, and it moves in musical steps. Lowering it
lengthens the loop, drops its pitch, slows the wet channel and narrows the band -
all at once, because they are all the same thing.

**CROSS** modulates pitch and loudness from the music itself, either from what
you play or from one channel to the other. **GLUE** is an end-of-chain saturator
that goes from warming things up to destroying them.

DSP design notes: `docs/DOOM_DSP_DESIGN.md`.

### LUCY

LUCY is a spectral degradation engine inspired by the Chase Bliss and Goodhertz
Lossy. It is not a bitcrusher: the heart of it is a **masking coder** that models
what a low-bitrate encoder throws away.

**LOSS** sets both how hard it degrades and which frequencies it reaches.
**STANDARD** keeps the coded signal - darker, full of chiming artifacts.
**INVERSE** plays what STANDARD discarded - brighter, thinner, feathery.
**JITTER** models an unstable clock in both phase and timing.

**PACKETS** simulates a bad connection using a two-state burst model, so losses
cluster the way they do on a real link. LOSS drops frames; REPEAT conceals them
with the previous frame, smeared.

**FREEZE** is a real spectral freeze - solid, or *slushy*, where it keeps
updating from what you play. **SPEED** sets how fast the loss, the packets and
the freeze all evolve.

The reverb sits in **front** by default, so the degradation codes its tail too.
A filter, a gate and a limiter finish the chain.

DSP design notes: `docs/LUCY_DSP_DESIGN.md`.

### CHORUS

CHORUS is a stereo chorus modelled on the Roland SDD-320 Dimension D's actual
mechanism: two delay lines modulated in **anti-phase** and summed to the outputs
with opposite polarity. When one goes sharp the other goes flat by the same
amount, so there is no audible vibrato - and the wet signal cancels exactly when
the two outputs are summed, so it is perfectly mono compatible.

Nine modes: four Dimension modes from softest to strongest, three combinations,
an ENSEMBLE mode after the string machines, and a CE-style single-path warmth.

The dry path is never filtered and the wet path is high-passed, so a bass note
keeps its weight and its pitch while its harmonics move.

DSP design notes: `docs/CHORUS_DSP_DESIGN.md`.

### SPREAD

SPREAD widens the stereo image using **allpass decorrelation** rather than a
delay or a phase inversion. It splits into three bands and treats each the way
hearing does: lows stay mono, mids are decorrelated by phase, highs by level.

Because the decorrelation *creates* side content rather than amplifying content
that is already there, it widens a mono source - which plain mid/side gain
cannot do. Mono compatibility is a first-class requirement: the mono sum keeps
its level and its low end at every setting.

Four modes: CLASSIC, WIDE, DEEP and MONO SAFE.

DSP design notes: `docs/STEREO_SPREAD_DSP_DESIGN.md`.

### Mood

Mood is a two-channel micro-looper and spatial-effects module: an always-listening
looper and a suite of real-time spatial effects that can process the input, the
loop, or both. It is inspired by the MOOD pedal by Chase Bliss Audio - behaviour
only, no code; see `THIRD_PARTY_NOTICES.md`.

Shared controls (UI labels in brackets where they differ):

- MIX: balance between input and Mood.
- CLOCK: Mood's internal sample rate (see below).
- ROUTING dropdown: what the wet channel is fed.
  - `DRY->WET` - the input only.
  - `LOOP->WET` - the micro-loop only.
  - `PARALLEL` - both.
- FEEDBACK: how much of the loop and wet channel is recycled back into the loop.
  At the top it piles material up the way a looper does.
- SPREAD: how much per-mode stereo treatment is applied (see below).
- DEGRADE: progressive lo-fi - bit reduction, sample-rate reduction, a rising
  noise floor and asymmetric drive. Applied to what is written back into the loop,
  so it compounds pass over pass rather than sitting as a fixed layer on the output.
- FREEZE: stops the looper recording and repeats what it has indefinitely.

Wet channel (MODE dropdown):

The wet channel's two knobs are labelled WET TIME and WET MOD in the UI; the
micro-looper's are LOOP LEN and LOOP MOD.

- `REVERB` - WET TIME sets decay and size together; WET MOD sets smear, from
  multi-tap delay at minimum to full reverb at maximum.
- `DELAY` - WET TIME sets the delay time and crossfades between times, so changing
  it does not pitch-bend echoes already in flight; WET MOD sets feedback, and at
  maximum the repeats hold rather than decay.
- `SLIP` - an auto-sampler. WET TIME sets the sampling window; WET MOD sets
  playback speed and direction in semitone steps, from an octave down through
  neutral to an octave up, in either direction.

Micro-looper channel (MODE dropdown):

- `ENV` - LOOP LEN sets slice size; LOOP MOD sets detector sensitivity. The loop
  runs until the input crosses the threshold, then the current slice repeats until
  the input falls back below it.
- `TAPE` - LOOP LEN shrinks the loop; LOOP MOD sets speed and direction in
  harmonised steps (quarter, half, unity and double speed, in each direction).
- `STRETCH` - LOOP LEN sets slice size; LOOP MOD sets direction and stretch
  amount, with the loop frozen at the centre of the knob.

CLOCK - the control that ties the two channels together:

CLOCK is Mood's internal sample rate. It sets the length and resolution of the
loops and the quality and time of the wet effects at the same time, and it moves in
semitone steps across three octaves. Because audio captured at one rate and played
back at another changes speed and pitch together, dropping the clock an octave
half-speeds the micro-loop *and* the wet channel. Low settings introduce the
aliasing and downsampling that go with a low sample rate - that grit is the control
working, not an artifact. A side effect worth knowing: Mood costs less CPU at lower
clock settings, because the whole engine runs clock-divided.

SPREAD - each mode makes stereo its own way:

With SPREAD down, the incoming stereo image is preserved: a source panned to one
side comes out on that side. With SPREAD up, each mode alters the image in its own
way rather than simply mixing the channels into each other.

- REVERB - reflections are placed differently per channel, with anti-symmetric
  cross-feed so the two sides carry genuinely different signals.
- DELAY - ping-pong; echoes alternate between the channels, mirroring the panning
  depth of what went in.
- SLIP - smooth panning plus widening.
- ENV - the incoming image is held until the detector fires, then the held slice
  traverses the field, alternating direction on each opening. Speed follows
  LOOP LEN.
- TAPE - the right channel plays the loop forward and the left plays the same loop
  in reverse.
- STRETCH - the grain cloud drifts slowly from side to side.

Other behaviour:

- Bypassing the block clears the loop and wet buffers, so re-enabling it does not
  replay an old loop.

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

- `products/PX3Synth/DSP/PluginProcessor.h`
  - Single authoritative class declaration for `PX3SynthAudioProcessor`.
- `products/PX3Synth/DSP/PluginProcessor.cpp`
  - Core processor orchestration: constructor/destructor, plugin identity,
    JUCE lifecycle entry points, and `processBlock`.
- `products/PX3Synth/DSP/PluginProcessorParameters.cpp`
  - Parameter getters, LFO destination assignment, modulation application
    helper, and FX order API (`get/setFxProcessingOrder`).
- `products/PX3Synth/DSP/PluginProcessorMidi.cpp`
  - MIDI + virtual keyboard handling, note activity tracking, pitch/mod wheel
    state bridges.
- `products/PX3Synth/DSP/PluginProcessorEffects.cpp`
  - Delay/granular/reverb DSP helper implementations and reverb engine setup.
- `products/PX3Synth/DSP/PluginProcessorState.cpp`
  - State serialization/restoration (`getStateInformation`,
    `setStateInformation`, ValueTree create/apply).
- `products/PX3Synth/DSP/PluginProcessorDebug.cpp`
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
- `applyModulationToNormalizedValue`
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

Each FX block is its own component class with the same four-call interface -
`prepare`, `reset`, `updateForBlock`, `processSampleFrame` - so the processor does
not need to know anything about their internals:

- `shared/DSP/Vibe/Vibe.*` and `shared/DSP/Vibe/VibeEngine.*`
  - Vibe's shared per-block state. The per-sample application lives inside
    `SynthVoice`, because Vibe is a per-voice stage rather than a bus effect.
- `shared/DSP/Delay/Delay.*`
  - `processDelayAlgorithmSample` switches between the seven algorithms;
    `processIsaacGranularSample` / `spawnIsaacGrain` handle the granular grain
    lifecycle.
- `shared/DSP/Reverb/Reverb.*`
  - `processFdn8` is the shared feedback delay network behind ROOM, HALL and
    CLOUD; the Dattorro plate is separate.
- `shared/DSP/Mood/Mood.*`
- `shared/DSP/Doom/Doom.*`, `shared/DSP/Doom/DoomTypes.h`
- `shared/DSP/Lucy/Lucy.*`, `shared/DSP/Lucy/LucyTypes.h`
- `shared/DSP/Chorus/Chorus.*`, `shared/DSP/Chorus/ChorusTypes.h`
- `shared/DSP/StereoSpread/StereoSpread.*`, `shared/DSP/StereoSpread/StereoSpreadTypes.h`
- `shared/DSP/Core/StftEngine.*` (shared spectral analysis/synthesis)
- `products/PX3Synth/DSP/FxChain.h` (stage ids, chain order, default order)
  - `processInternalStep` runs the clock-divided engine; the loop and wet modes
    are `renderLoop*` and `renderWet*`.
- `getFxProcessingOrder` / `setFxProcessingOrder`
  - Sanitized user order storage and retrieval.

`products/PX3Synth/DSP/PluginProcessorEffects.cpp` is now an empty placeholder - the effect
implementations were extracted into the component classes above.

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
