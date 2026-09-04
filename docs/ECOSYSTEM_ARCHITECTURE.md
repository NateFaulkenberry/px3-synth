# PX3 Ecosystem Architecture

How this repository is organised now that PX3 Synth is a product inside it
rather than the whole of it.

---

## 1. The shape

```
shared/                      code any PX3 product may use
  DSP/                       Mood Delay Reverb Doom Lucy Chorus
                             StereoSpread Vibe Filter Analog Core
  UI/
    Components/              Card ChipLabel BypassButton PianoKeyboard
                             VuMeter RoundedRect ModalBackdrop ...
    Style/                   UIConfig, UIConfigManager, UIConfig.json
    Fx/                      the FX editors, which are already product-neutral
  Infrastructure/
    Update/                  provider, registry, service, semantic version
    Settings/                the global preference file
  Assets/                    ecosystem assets (the uninstaller icon)

products/
  PX3Synth/                  everything only the Synth needs
    DSP/ UI/ Preset/ Assets/

updater/                     the helper application
tools/                       diagnostics and benchmarks
tests/Tests/                 the component suite
cmake/PX3Product.cmake       px3_add_product()
```

Nothing under `shared/` includes anything from `products/`. That is the rule the
boundary rests on, and it is checkable: a grep for `products/` inside `shared/`
should return nothing.

---

## 2. Why the DSP was already shared

Every FX class - Mood, Delay, Reverb, Doom, Lucy, Chorus, StereoSpread, Vibe,
AnalogEngine, VoiceFilter, StftEngine, OutputCeiling, BusAnalyser - includes
nothing but its own headers. None has ever known about `PluginProcessor`.

So the migration moved them; it did not extract them. That is why no audio
behaviour changed, and it is the reason a standalone FX product is a wrapper
rather than a reimplementation.

The same turned out to be true of more UI than expected: only 11 of 104 UI
files include `PluginProcessor.h`, and the FX **editors** are not among them.
`MoodComponent`, `DelayComponent`, `ReverbComponent`, `VibeComponent` and
`FilterComponent` sit in `shared/UI/Fx` beside the DSP they drive.

---

## 3. Why shared code is not a static library

`JuceHeader.h` is generated **per target** and carries that target's
`JucePlugin_*` identity. A single static library built against one product's
header would bake that product's identity into every other product linking it.

So `shared/` is a source list compiled into each product, not a library. The
boundary comes from the two lists and the include paths:

```cmake
set(PX3_SHARED_SOURCES ...)   # shared/
set(PX3_SYNTH_SOURCES  ...)   # products/PX3Synth/
set(PX3_ALL_SOURCES ${PX3_SHARED_SOURCES} ${PX3_SYNTH_SOURCES})
```

A product target compiles `PX3_SHARED_SOURCES` plus its own. The tools and the
test suite compile `PX3_ALL_SOURCES`, because they build the Synth in full.

---

## 4. Product identity

Identity exists in two halves that must agree.

**Build side** - `cmake/PX3Product.cmake`:

```cmake
px3_add_product(
    TARGET       PX3Synth
    PRODUCT_NAME "PX3 Synth"
    BUNDLE_ID    "com.px3.px3synth"
    PLUGIN_CODE  SyP1
    IS_SYNTH     TRUE
    FORMATS      AU VST3 Standalone
    SOURCES      ${PX3_SYNTH_SOURCES})
```

**Runtime side** - `ProductRegistry::Registration`: product id, display name,
version provider, bundle id, which formats, and the installer component id.

| | convention | note |
|---|---|---|
| product id | `px3-<name>` | `px3-synth` |
| bundle id | `com.px3.<name>` | the Synth is **grandfathered** as `com.px3.px3synth` |
| manufacturer code | `SyPr` | one manufacturer, many products |
| plugin code | unique, four characters | `SyP1` for the Synth |

`PLUGIN_CODE` must be unique across the ecosystem. Two products sharing one is
how a DAW ends up loading the wrong plug-in.

The Synth's bundle id and plugin code are **not** changed to match the newer
convention, and must not be: they are what is already installed on people's
machines, and changing them orphans every existing install and session.

---

## 5. Version

`PX3_VERSION` in the top-level `CMakeLists.txt` is the single source of truth.
It reaches JUCE as `ProjectInfo::versionString`, and everything else -
`px3::version::string()`, the Settings panel, the updater's comparison, the
installer's filenames - reads it from there. Nothing keeps a second copy.

---

## 5a. The products

| Product | Bundle | Code | Formats |
|---|---|---|---|
| PX3 Synth | `com.px3.px3synth` | `SyP1` | AU · VST3 · Standalone |
| PX3 Delay | `com.px3.delay` | `DlP1` | AU · VST3 |
| PX3 Mood | `com.px3.mood` | `MdP1` | AU · VST3 |
| PX3 Chorus | `com.px3.chorus` | `ChP1` | AU · VST3 |
| PX3 Spread | `com.px3.spread` | `SpP1` | AU · VST3 |
| PX3 Reverb | `com.px3.reverb` | `RvP1` | AU · VST3 |
| PX3 Doom | `com.px3.doom` | `DmP1` | AU · VST3 |
| PX3 Lucy | `com.px3.lucy` | `LcP1` | AU · VST3 |

Effects ship as AU and VST3 only. An effect has no reason to have a standalone
application, and `hasStandalone` in the registry says so rather than a comment.

**One implementation, several consumers.** Each effect product drives the same
`shared/DSP/...` object the Synth drives, through the same
`prepare` / `updateForBlock` / `processSampleFrame` contract. Nothing is
copied or reimplemented. The Synth's version and the standalone differ in
exactly one way, and it is worth knowing which:

> The Synth builds its settings through the **modulation accumulator**, so an
> LFO, envelope or macro can move a parameter. A standalone has no modulation
> matrix, so it reads its parameters directly.

That is the line between *the FX parameter* and *the Synth's modulation
destination*, and it is the only difference between the two consumers.

### The two shared halves of a product

`shared/Infrastructure/Fx/FxPluginProcessor` — buses, prepare, the block loop,
host tempo, parameter state. The only virtual the audio thread crosses is
`processFxBlock`, called **once per block**; the per-sample loop lives in the
product where `processSampleFrame` inlines.

`shared/Infrastructure/Fx/FxCardEditor` — the card, its UIConfig style, the PX3
knob, the attachments. A card-shaped product's editor is then just the rows it
declares and the parameters it attaches. PX3 Chorus's is 35 lines.

### Parameter ranges are the trap

Every effect has at least one parameter that is **not** a 0..1 range, sitting
among neighbours that are:

| | |
|---|---|
| Mood `routing` | a 3-way choice the DSP reads as 0..1, mapped `index / 2` |
| Chorus `tone`, Spread `tone`, Doom `eq` | bipolar tilts, −1..+1 |
| Lucy `gain` | **decibels**, −36..+36 |

Declaring any of them 0..1 moves its centre and silently reinterprets every
value a user has stored. Read each parameter from the Synth's own declaration,
and pin it with a test on the **range**, not the default.

---

## 5b. Vibe: why it is not a product

Vibe is in `shared/DSP/Vibe`, and it is **not** a standalone effect. This was
assessed rather than assumed, and the evidence is one line:

**Vibe has no audio interface at all.** No `processSampleFrame`, no
`processBlock`, no `processSample` — where every other effect has one. It has
no input and no output.

What it does is hand each *voice* a `VibeVoiceVariation`:

```cpp
struct VibeVoiceVariation {
    float pitchCents, cutoffOffset, resonanceOffset,
          gainOffset, asymmetryBias, saturationBias;
};
```

which the voice applies at **six separate points inside itself** — oscillator
pitch, filter cutoff and resonance, waveform shaping, and voice gain.

An insert sees a summed stereo mix: no voices to detune, no per-voice filters
to offset, no oscillators to bias. A "standalone Vibe" would necessarily be a
*different effect that sounds vaguely similar*, so it does not exist. Its
absence from the registry is asserted by a test, not left to be true by
accident.

**The general rule this illustrates:** ask what an effect needs. One that reads
only its input buffer is a candidate. One that reaches into per-voice state,
the modulation matrix, or the Synth's internal signal path is not a
conventional insert, and forcing it into one changes what it does.

---

## 6. Adding a product: PX3 Mood, concretely

1. **Sources.** `products/PX3Mood/` with a `PluginProcessor` and `PluginEditor`
   that wrap `shared/DSP/Mood` and `shared/UI/Fx/MoodComponent`. Do not copy
   either - the shared implementation is the canonical one, and the wrapper is
   thin by design.

2. **Source list**, beside the Synth's:
   ```cmake
   set(PX3_MOOD_SOURCES products/PX3Mood/PluginProcessor.cpp ...)
   ```

3. **Target**, one call:
   ```cmake
   px3_add_product(
       TARGET       PX3Mood
       PRODUCT_NAME "PX3 Mood"
       BUNDLE_ID    com.px3.mood
       PLUGIN_CODE  MdP1
       FORMATS      AU VST3        # no Standalone: an effect does not need one
       SOURCES      ${PX3_MOOD_SOURCES})
   ```

4. **Include path**: add `products/PX3Mood` to `PX3_INCLUDE_DIRS`.

5. **Register it** in `registerDefaultProducts()` so the updater and the
   installer can see it. A product that builds but is not registered ships
   invisible to both, and a test checks the registry against the build:
   ```cpp
   ProductRegistry::Registration mood;
   mood.productId = "px3-mood";
   mood.displayName = "PX3 Mood";
   mood.versionProvider = [] { return px3::version::string(); };
   mood.bundleId = "com.px3.mood";
   mood.hasStandalone = false;
   mood.installerComponentId = "px3.mood";
   ```

6. **Tests** under `tests/Tests/`, covering the wrapper. The DSP is already
   covered by the Synth's suite, and that coverage now protects both consumers.

Nothing in that list touches PX3 Synth.

For a card-shaped effect the editor is shorter still — derive from
`px3::fx::FxCardEditor`, declare the rows, attach the parameters:

```cpp
PX3MoodEditor::PX3MoodEditor(PX3MoodProcessor& p)
    : px3::fx::FxCardEditor(p, "mood", "MOOD")
{
    rows().addKnobRow({ { "mix", "MIX", "Dry against wet" }, ... });
    attachKnob("mix", p.mix());
    attachBypass(p.enabled());
    finishSetup(720, 320);
}
```

Then check the parameter ranges against the Synth's own declarations — see
§5a, where five of seven effects had one that is not a unit range — and run
`scripts/build-product.sh mood --vst3` to try it.

### Is an effect worth extracting?

Ask what it needs. An effect that reads only its input buffer is a candidate.
One that reaches into per-voice state, the modulation matrix or the Synth's
internal signal path is not a conventional insert, and forcing it into one
would change what it does. Assess before extracting, and it is a perfectly
good answer that something stays Synth-only.

---

## 6a. Building one product, for development

The full build compiles eight products. Working on one should not.

**The product name comes first**, then the options.

```
scripts/build-product.sh --list                what there is to build
scripts/build-product.sh lucy --vst3           the quickest loop
scripts/build-product.sh delay                 AU and VST3
scripts/build-product.sh synth                 AU, VST3 and Standalone
scripts/build-product.sh synth --run           build, then launch the standalone
scripts/build-product.sh synth --debug --run   with the in-plugin debug panel
scripts/build-product.sh synth --config Debug  unoptimised, for a debugger
scripts/build-product.sh doom --no-install     leave installed plug-ins alone
```

`--debug` means the same thing here as in `build-release.sh`: the in-plugin
debug panel (`PX3_DEBUG_PANEL=ON`). An unoptimised build to step through in a
debugger is `--config Debug`, which is a separate axis - the two combine.

Each product gets its own build directory, so switching between one product and
the full build does not reconfigure either. Without `--no-install` a successful
build lands in `~/Library/Audio/Plug-Ins` and the host sees it on its next scan.

Asking for no format builds `${TARGET}_All`, not the bare target: JUCE makes the
bare target the shared-code static library, so building it compiles every source,
links the `.a`, and produces no loadable plug-in - and for the Synth, a bundle
skeleton with no executable in it.

The script reads the product list from `CMakeLists.txt`, so a product added with
`px3_add_product` is buildable by it immediately.

---

## 7. Installer

`scripts/build-release.sh` builds per-product component packages and combines
them with `productbuild`. Products install to the locations hosts expect:

```
/Library/Audio/Plug-Ins/Components     AU
/Library/Audio/Plug-Ins/VST3           VST3
/Applications                          standalone, where a product has one
```

No product depends on another product's install location. The only deliberately
shared installed component is the update helper, which ships inside the
standalone.

### Component selection

The Installation Type pane offers the Synth's three formats, then the effects
under a heading of their own:

```
[x] Audio Unit (AU)                        [x] Additional PX3 Effects
[x] VST3                                     [x] PX3 Delay  [x] PX3 Spread  [x] PX3 Doom
[X] Standalone Application (required)        [x] PX3 Mood   [x] PX3 Reverb  [x] PX3 Lucy
     ^ ticked and greyed                     [x] PX3 Chorus
```

One component package per **effect**, not per format — that is the choice a user
makes — so each carries that effect's AU and VST3 together. Every effect is
`start_selected="true"`: the user opts *out* of what they do not want.

**The standalone is the exception: `start_selected="true" start_enabled="false"`,
which shows the row with its checkbox on and greyed.** The updater helper lives
inside `PX3 Synth.app`, and a plug-in updates itself by staging an installer and
handing it to that helper — which is what waits for the host to quit. Without the
standalone there is nothing to hand it to, so Prepare Update succeeds and Install
cannot work.

It is shown rather than installed silently for the same reason the effects are
listed: what lands on the machine should be visible, even when there is no choice
about it. The title says "(required)" so the greyed checkbox reads as deliberate
rather than broken.

`build-release.sh` reads the product list from `CMakeLists.txt`, so a new
product is packaged without editing the installer. A product declared but not
built is skipped rather than fatal, so a partial build still produces an
installer for what it did build.

The build then **expands the finished product** and checks that each effect's
package is both present and referenced by the Distribution — `productbuild`
silently drops a package nothing selects, which is how the branding resources
were lost once before.

### Uninstaller

The uninstaller is an AppleScript **application**, not a `.pkg`. The macOS
Installer always presents install-style UI — a Destination Select pane, an
Installation Type pane and an "Install" button — and none of it can be
relabelled from a Distribution file, so an uninstaller delivered that way reads
as an installer whatever the panes say.

It is product-scoped, and it discovers what to offer at run time:

| File | Role |
| --- | --- |
| `px3-list-products.sh` | Scans disk for installed PX3 products |
| `generate-product-manifest.sh` | Writes `px3-products.tsv` from the `px3_add_product` table |
| `px3-uninstall.sh` | Removes exactly the products it is given |

The scanner reports the **union** of the manifest and what is on disk. A product
this uninstaller has never heard of — one released after it shipped — is still
listed, marked `unknown`, and still removable. That is what allows one
uninstaller to serve the ecosystem across versions instead of one per release.

Three rules do the load-bearing work:

1. **No "everything" default.** `px3-uninstall.sh` removes only what
   `PX3_PRODUCTS` names, and exits 2 having removed nothing if that is empty.
   Every path it deletes is built from a variable, and the failure mode of an
   uninstaller whose selection came out empty is that it removes everything.
2. **Shared data is only touched when nothing is left.** Presets, imported
   wavetables and settings live in one `~/Library/P(X3)/` directory shared by
   every product, so removing PX3 Mood must not take the Synth's preset library
   with it.
3. **Keeping presets is the default.** `PX3_KEEP_PRESETS=1` keeps what the user
   made — their own presets and any wavetables they imported — and removes what a
   reinstall puts back: factory presets, settings, staged updates. Unticking it
   removes the directory entire, and the confirmation dialog says so in those
   words.

`scripts/uninstall-local.sh` is the developer-machine equivalent and reads the
same table, so it too removes every product rather than the first one. It is a
reset, not the shipped uninstaller: it takes the preset library without asking.

`PX3_SCAN_ROOT` prefixes every system path so the whole thing can be tested
against a fixture tree. Anything that reaches outside that tree — `pkgutil
--forget`, restarting the component registrar — is gated on the run being a live
one, or a test would forget the receipts for the copy actually installed on the
developer's machine. `tests/Tests/TestsUninstaller.cpp` runs the real scripts
against that fixture rather than grepping them for `rm -rf`.

---

## 8. Updater

Product-agnostic by construction: it works in terms of product id, installed
version, available version, release and installer, and knows nothing about the
Synth. See `docs/midi-mapping-design.md`'s sibling sections and the update
tests for the detail. Adding a product to the updater is a registry entry.
